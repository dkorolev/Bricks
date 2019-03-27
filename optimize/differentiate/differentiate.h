/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2019 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

#ifndef OPTIMIZE_DIFFERENTIATE_DIFFERENTIATE_H
#define OPTIMIZE_DIFFERENTIATE_DIFFERENTIATE_H

#include "../base.h"
#include "../expression/expression.h"
#include "../vars/vars.h"

#include <cmath>

namespace current {
namespace expression {

struct DifferentiatorForThisNodeTypeNotImplementedException final : OptimizeException {};

struct DifferentiationDeliberatelyNotImplemented : OptimizeException {};
struct DoNotDifferentiateUnitStepException final : DifferentiationDeliberatelyNotImplemented {};
struct DoNotDifferentiateSigmoidException final : DifferentiationDeliberatelyNotImplemented {};

struct SeeingLambdaWhileNotDifferentiatingByLambdaException final : OptimizeException {};
struct DirectionalDerivativeGradientDimMismatchException final : OptimizeException {};

// The purpose of this class for manual stack is to make sure no frequent allocations/deallocations take place
// while the gradient is being computed. Simply put, when multiple derivatives are to be computed, this class:
// 1. Does not shrink the allocated memory on `pop_back`-s. At all.
//    Because its lifetime is strictly bounded, and it will be used extensively while it's being used.
// 2. Allocates enough memory when reallocations do happen. Maybe even more than enough, but even a few MB
//    is still a small investment of RAM compared to what we have saved by getting rid of per-node "cache".
class DifferentiatorManualStack {
 public:
  struct ManualStackEntry {
    // The manual stack stores the "indexes" of the nodes to work with, with the special bit used to tell whether
    // during the "recursive" call the node being considered does already or does not yet have its dependencies ready.
    // Effectively, it's the flag to tell whether the "recursive" call is visiting this node on the way "down" or "up".
    ExpressionNodeIndex index_with_special_bit;

    // The results of the execution of the "recursive" calls down the stack. For `lhs` and `rhs` respectively.
    ExpressionNodeIndex return_value[2];

    // Stack element index to return the value. It is multiplied by two, and the LSB is the LHS/RHS bit.
    size_t return_value_index_times2;
  };
  static_assert(sizeof(ManualStackEntry) == 32, "`ManualStackEntry` should be 32 bytes.");

 private:
  // This variable is `mutable`, because all the `push` / `pop` calls are made on `DifferentiatorManualStack const&`
  // object, because this allows passing it in as the value of the default parameter in the function signature.
  mutable std::vector<ManualStackEntry> call_stack_;

  mutable size_t actual_size_;
  mutable size_t allocated_size_;

  void GrowIfNecessary() const {
    if (actual_size_ == allocated_size_) {
      allocated_size_ = std::max(static_cast<size_t>(256u), allocated_size_ * 2u);
      size_t const nodes_count = VarsManager::TLS().Active().NumberOfNodes();
      if (nodes_count > allocated_size_) {
        allocated_size_ = allocated_size_ + (nodes_count - allocated_size_) / 4u;
      }
      call_stack_.resize(allocated_size_);
    }
  }

 public:
  // The default size of the stack is one, and it never gets to zero.
  // This is to not make an exception for the generic case of the "ultimate return value". This ultimate return value
  // of the derivative will just be placed into `call_stack_[0].return_value[0]`, where it is up for grabs.
  DifferentiatorManualStack() : call_stack_(1u), actual_size_(1u), allocated_size_(1u) {}

  bool NotEmpty() const { return actual_size_ > 1u; }

  size_t DoPush(ExpressionNodeIndex index, size_t return_value_index_times2) const {
    GrowIfNecessary();
    call_stack_[actual_size_].index_with_special_bit = index;
    call_stack_[actual_size_].return_value_index_times2 = return_value_index_times2;
    return actual_size_++;
  }

  ManualStackEntry const& DoPop() const { return call_stack_[--actual_size_]; }

  void DoReturnValue(value_t value, size_t return_value_index_times2) const {
    call_stack_[return_value_index_times2 >> 1].return_value[return_value_index_times2 & 1] = value;
  }

  ExpressionNodeIndex ExtractReturnValue() const { return call_stack_[0].return_value[0]; }
};

// The generic differentiator, contains all the logic on how to differentiate expression nodes of all types.
class Differentiator final {
 private:
  VarsContext const& vars_context_;
  bool const differentiate_by_lambda_;
  size_t const derivative_per_finalized_var_index_;
  DifferentiatorManualStack const& stack_;

  value_t DerivativeOfVar(size_t var_index) const {
    if (!differentiate_by_lambda_) {
      // So, the derivative is zero or one.
      // TODO(dkorolev): Do not, of course, allocate independent expression nodes for every single zero and one.
      return vars_context_.LeafDerivativeZeroOrOne(var_index, derivative_per_finalized_var_index_);
    } else {
      // When differentiating by lambda, the derivative by each variable is zero.
      return 0.0;
    }
  }

  void PushToStack(ExpressionNodeIndex index, size_t return_value_index_times2) const {
    index.Dispatch(
        [&](size_t) { stack_.DoPush(index, return_value_index_times2); },
        [&](size_t var_index) { stack_.DoReturnValue(DerivativeOfVar(var_index), return_value_index_times2); },
        [&](double) { stack_.DoReturnValue(0.0, return_value_index_times2); },
        [&]() {
          if (!differentiate_by_lambda_) {
            CURRENT_THROW(SeeingLambdaWhileNotDifferentiatingByLambdaException());
          } else {
            stack_.DoReturnValue(1.0, return_value_index_times2);
          }
        });
  }

 public:
  Differentiator(VarsContext const& vars_context,
                 size_t derivative_per_finalized_var_index,
                 DifferentiatorManualStack const& stack)
      : vars_context_(vars_context),
        differentiate_by_lambda_(false),
        derivative_per_finalized_var_index_(derivative_per_finalized_var_index),
        stack_(stack) {}

  struct ByLambda final {};
  Differentiator(VarsContext const& vars_context, ByLambda, DifferentiatorManualStack const& stack)
      : vars_context_(vars_context),
        differentiate_by_lambda_(true),
        derivative_per_finalized_var_index_(static_cast<size_t>(-1)),
        stack_(stack) {}

  value_t Differentiate(value_t value_to_differentiate) const {
    ExpressionNodeIndex const index_to_differentiate = value_to_differentiate;

    PushToStack(index_to_differentiate, 0u);

    while (stack_.NotEmpty()) {
      DifferentiatorManualStack::ManualStackEntry const element = stack_.DoPop();

      ExpressionNodeIndex index = element.index_with_special_bit;
      bool const ready_to_differentiate = index.ClearSpecialBitAndReturnWhatItWas();

      // The node is `short-lived`, as the const reference to it can and will be invalidated as more nodes are added
      // to the tree. Thus, all the relevant pieces of data must be extracted from this node before adding the new ones.
      ExpressionNodeImpl const& short_lived_node = index.template Dispatch<ExpressionNodeImpl const&>(
          [this](size_t node_index) -> ExpressionNodeImpl const& { return vars_context_[node_index]; },
          [](size_t) -> ExpressionNodeImpl const& { CURRENT_THROW(OptimizeException("Internal error.")); },
          [](double) -> ExpressionNodeImpl const& { CURRENT_THROW(OptimizeException("Internal error.")); },
          []() -> ExpressionNodeImpl const& { CURRENT_THROW(OptimizeException("Internal error.")); });
      ExpressionNodeType const node_type = short_lived_node.Type();

      if (IsOperationNode(node_type)) {
        value_t const a = short_lived_node.LHSIndex();
        value_t const b = short_lived_node.RHSIndex();
        if (!ready_to_differentiate) {
          // Going down. Need to differentiate the dependencies of this node first. Use the special bit. Push this node
          // before its arguments for it to be evaluated after. Then push { rhs, lhs }, so their order is { lhs, rhs }.
          index.SetSpecialBit();
          size_t const dfs_call_return_value_index = stack_.DoPush(index, element.return_value_index_times2);
          PushToStack(b, dfs_call_return_value_index * 2u + 1u);
          PushToStack(a, dfs_call_return_value_index * 2u);
        } else {
          // Going up, the { lhs, rhs } are already differentiated.
          value_t const da = element.return_value[0];
          value_t const db = element.return_value[1];
          value_t df;
          if (node_type == ExpressionNodeType::Operation_add) {
            df = da + db;
          } else if (node_type == ExpressionNodeType::Operation_sub) {
            df = da - db;
          } else if (node_type == ExpressionNodeType::Operation_mul) {
            df = a * db + b * da;
          } else if (node_type == ExpressionNodeType::Operation_div) {
            df = (b * da - a * db) / (b * b);
          } else {
            CURRENT_THROW(OptimizeException("Internal error."));
          }
          stack_.DoReturnValue(df, element.return_value_index_times2);
        }
      } else if (IsFunctionNode(node_type)) {
        value_t const x = short_lived_node.ArgumentIndex();
        if (!ready_to_differentiate) {
          // Going down. Need to differentiate the argument of this call first. Use the special bit.
          index.SetSpecialBit();
          size_t const dfs_call_return_value_index = stack_.DoPush(index, element.return_value_index_times2);
          PushToStack(x, dfs_call_return_value_index * 2u);
        } else {
          // Going up, the argument is already differentiated.
          value_t const f = index;
          value_t const dx = element.return_value[0];
          value_t df;  //  = ExpressionNode::ConstructDefaultExpressionNode();
          if (node_type == ExpressionNodeType::Function_exp) {
            df = dx * f;
          } else if (node_type == ExpressionNodeType::Function_log) {
            df = dx / x;
          } else if (node_type == ExpressionNodeType::Function_sin) {
            df = dx * cos(x);
          } else if (node_type == ExpressionNodeType::Function_cos) {
            df = -dx * sin(x);
          } else if (node_type == ExpressionNodeType::Function_tan) {
            df = dx / sqr(cos(x));
          } else if (node_type == ExpressionNodeType::Function_sqr) {
            df = dx * 2.0 * x;
          } else if (node_type == ExpressionNodeType::Function_sqrt) {
            df = dx / (2.0 * f);
          } else if (node_type == ExpressionNodeType::Function_asin) {
            df = dx / sqrt(1.0 - sqr(x));
          } else if (node_type == ExpressionNodeType::Function_acos) {
            df = -dx / sqrt(1.0 - sqr(x));
          } else if (node_type == ExpressionNodeType::Function_atan) {
            df = dx / (1.0 + sqr(x));
          } else if (node_type == ExpressionNodeType::Function_unit_step) {
            CURRENT_THROW(DoNotDifferentiateUnitStepException());
          } else if (node_type == ExpressionNodeType::Function_ramp) {
            df = dx * unit_step(x);
          } else if (node_type == ExpressionNodeType::Function_sigmoid) {
            CURRENT_THROW(DoNotDifferentiateSigmoidException());
          } else if (node_type == ExpressionNodeType::Function_log_sigmoid) {
            df = dx * sigmoid(-x);
          } else {
            CURRENT_THROW(OptimizeException("Internal error."));
          }
          stack_.DoReturnValue(df, element.return_value_index_times2);
        }
      } else {
        CURRENT_THROW(DifferentiatorForThisNodeTypeNotImplementedException());
      }
    }
    return stack_.ExtractReturnValue();
  }
};

// The per-variable differentiator.
inline value_t Differentiate(value_t f,
                             size_t derivative_per_finalized_var_index,
                             DifferentiatorManualStack const& stack = DifferentiatorManualStack()) {
  return Differentiator(VarsManager::TLS().Active(), derivative_per_finalized_var_index, stack).Differentiate(f);
}

// Gradient computer, well, generator. "Simply" differentiates the provided function by each variable.
inline std::vector<value_t> ComputeGradient(value_t f,
                                            DifferentiatorManualStack const& stack = DifferentiatorManualStack()) {
  VarsContext const& vars_context = VarsManager::TLS().Active();
  std::vector<value_t> result(vars_context.NumberOfVars());
  for (size_t i = 0u; i < result.size(); ++i) {
    result[i] = Differentiator(vars_context, i, stack).Differentiate(f);
  }
  return result;
}

// Given a function and the formulas for its gradient (actually, node indexes for them only),
// generates a one-dimensional function `f(lambda)`, which is `f(x0 + lambda * gradient)`.
inline value_t GenerateLineSearchFunction(VarsMapperConfig const& config, value_t f, std::vector<value_t> const& g) {
  value_t const lambda = value_t::lambda();
  std::vector<value_t> substitute(config.name.size());
  for (size_t i = 0u; i < substitute.size(); ++i) {
    substitute[i] = ExpressionNodeIndex::FromVarIndex(i) + lambda * g[i];
  }
  return Build1DFunction(f, config, substitute);
}

// The caller for the differentiator by the lambda, not by a specific variable.
inline value_t DifferentiateByLambda(value_t f, DifferentiatorManualStack const& stack = DifferentiatorManualStack()) {
  return Differentiator(VarsManager::TLS().Active(), Differentiator::ByLambda(), stack).Differentiate(f);
}

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_DIFFERENTIATE_DIFFERENTIATE_H
