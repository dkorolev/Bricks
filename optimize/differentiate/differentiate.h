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
#include "../tree_balancer/tree_balancer.h"
#include "../vars/vars.h"

#include <cmath>

namespace current {
namespace expression {

constexpr static const size_t kNodeHeightCutoffIndicatingUnbalancedExpression = 1000;
struct DifferentiatorRequiresBalancedTreeException final : OptimizeException {};

struct DifferentiatorForThisNodeTypeNotImplementedException final : OptimizeException {};

struct DifferentiationDeliberatelyNotImplemented : OptimizeException {};
struct DoNotDifferentiateUnitStepException final : DifferentiationDeliberatelyNotImplemented {};
struct DoNotDifferentiateSigmoidException final : DifferentiationDeliberatelyNotImplemented {};

struct SeeingLambdaWhileNotDifferentiatingByLambdaException final : OptimizeException {};
struct DirectionalDerivativeGradientDimMismatchException final : OptimizeException {};

inline value_t DifferentiateOperation(ExpressionNodeType node_type, value_t a, value_t b, value_t da, value_t db) {
  if (node_type == ExpressionNodeType::Operation_add) {
    return da + db;
  } else if (node_type == ExpressionNodeType::Operation_sub) {
    return da - db;
  } else if (node_type == ExpressionNodeType::Operation_mul) {
    return a * db + b * da;
  } else if (node_type == ExpressionNodeType::Operation_div) {
    return (b * da - a * db) / (b * b);
  } else {
    CURRENT_THROW(OptimizeException("Internal error."));
  }
}

inline value_t DifferentiateFunction(ExpressionNodeType node_type, value_t f, value_t x, value_t dx) {
  if (node_type == ExpressionNodeType::Function_exp) {
    return dx * f;
  } else if (node_type == ExpressionNodeType::Function_log) {
    return dx / x;
  } else if (node_type == ExpressionNodeType::Function_sin) {
    return dx * cos(x);
  } else if (node_type == ExpressionNodeType::Function_cos) {
    return -dx * sin(x);
  } else if (node_type == ExpressionNodeType::Function_tan) {
    return dx / sqr(cos(x));
  } else if (node_type == ExpressionNodeType::Function_sqr) {
    return dx * 2.0 * x;
  } else if (node_type == ExpressionNodeType::Function_sqrt) {
    return dx / (2.0 * f);
  } else if (node_type == ExpressionNodeType::Function_asin) {
    return dx / sqrt(1.0 - sqr(x));
  } else if (node_type == ExpressionNodeType::Function_acos) {
    return -dx / sqrt(1.0 - sqr(x));
  } else if (node_type == ExpressionNodeType::Function_atan) {
    return dx / (1.0 + sqr(x));
  } else if (node_type == ExpressionNodeType::Function_unit_step) {
    CURRENT_THROW(DoNotDifferentiateUnitStepException());
  } else if (node_type == ExpressionNodeType::Function_ramp) {
    return dx * unit_step(x);
  } else if (node_type == ExpressionNodeType::Function_sigmoid) {
    CURRENT_THROW(DoNotDifferentiateSigmoidException());
  } else if (node_type == ExpressionNodeType::Function_log_sigmoid) {
    return dx * sigmoid(-x);
  } else {
    CURRENT_THROW(OptimizeException("Internal error."));
  }
}

// The generic differentiator, contains all the logic on how to differentiate expression nodes of all types.
template <typename IMPL>
class Differentiator final {
 private:
  // The stack for differentiation is manual to avoid overflowing the "native" stack during recursion.
  // NOTE(dkorolev): I'm still undecided whether balancing the expression tree, or constructing it in a balanced way,
  // might well be a better way.
  class ManualStack final {
   public:
    struct Entry {
      // The manual stack stores the "indexes" of the nodes to work with, with the special bits used to tell whether
      // during the "recursive" call the node being considered does already or does not yet have its dependencies ready.
      // Effectively, it's the flag to tell whether the "recursive" call is seeing this node on the way "down" or "up".
      ExpressionNodeIndex index_with_special_bit;

      // The results of the execution of the "recursive" calls down the stack. For `lhs` and `rhs` respectively.
      typename IMPL::retval_t return_value[2];

      // Stack index to return the value. stack[i].return_value[0], if the MSB is 0, stack[~i].return_value[1], if 1.
      size_t return_value_index;
    };
    // TODO(dkorolev): This `static_assert` is obsolete, but the logic must be preserved.
    // static_assert(sizeof(Entry) == 32, "`Entry` should be 32 bytes.");

   private:
    std::vector<Entry> call_stack_;

    size_t actual_size_;
    size_t allocated_size_;

    void GrowIfNecessary() {
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
    ManualStack() : call_stack_(1u), actual_size_(1u), allocated_size_(1u) {}

    bool NotEmpty() const { return actual_size_ > 1u; }

    size_t DoPush(ExpressionNodeIndex index, size_t return_value_index) {
      GrowIfNecessary();
      call_stack_[actual_size_].index_with_special_bit = index;
      call_stack_[actual_size_].return_value_index = return_value_index;
      return actual_size_++;
    }
    size_t CurrentStackIndex() const { return actual_size_ - 1u; }
    Entry& MutableTop() { return call_stack_[actual_size_ - 1u]; }
    void DoPop() { --actual_size_; }

    typename IMPL::retval_t& Ref(size_t return_value_index_lhs) {
      size_t const return_value_index_rhs = ~return_value_index_lhs;
      return return_value_index_lhs < return_value_index_rhs ? call_stack_[return_value_index_lhs].return_value[0]
                                                             : call_stack_[return_value_index_rhs].return_value[1];
    }

    typename IMPL::retval_t const& ExtractReturnValue() const { return call_stack_[0].return_value[0]; }
  };

  VarsContext const& vars_context_;
  IMPL const impl_;
  mutable ManualStack stack_;

  void PushToStack(ExpressionNodeIndex index, size_t return_value_index) const {
    index.Dispatch([&](size_t) { stack_.DoPush(index, return_value_index); },
                   [&](size_t var_index) { impl_.DoReturnDerivativeOfVar(var_index, stack_.Ref(return_value_index)); },
                   [&](double) { impl_.DoAssignZero(stack_.Ref(return_value_index)); },
                   [&]() { impl_.DoReturnDerivativeOfLambda(stack_.Ref(return_value_index)); });
  }

 public:
  template <typename... ARGS>
  Differentiator(VarsContext const& vars_context, ARGS&&... args)
      : vars_context_(vars_context), impl_(vars_context, std::forward<ARGS>(args)...) {}

  typename IMPL::retval_t const& DoDifferentiate(value_t value_to_differentiate) const {
    size_t const node_height = ExpressionTreeHeight(value_to_differentiate);
    if (node_height > kNodeHeightCutoffIndicatingUnbalancedExpression) {
      // For most practical purposes, running `BalanceExpressionTree(cost_function)` would do the job.
      CURRENT_THROW(DifferentiatorRequiresBalancedTreeException());
    }

    ExpressionNodeIndex const index_to_differentiate = value_to_differentiate;

    PushToStack(index_to_differentiate, 0u);

    while (stack_.NotEmpty()) {
      size_t const current_stack_index = stack_.CurrentStackIndex();
      typename ManualStack::Entry& element = stack_.MutableTop();

      uint64_t const phase = element.index_with_special_bit.ClearSpecialTwoBitsAndReturnWhatTheyWere();

      // The node is `short-lived`, as the const reference to it can and will be invalidated as more nodes are added
      // to the tree. Thus, all the relevant pieces of data must be extracted from this node before adding the new ones.
      // TODO(dkorolev): In `NDEBUG` mode this would just be an unchecked node index extraction.
      size_t const node_index = element.index_with_special_bit.template Dispatch<size_t>(
          [](size_t node_index) -> size_t { return node_index; },
          [](size_t) -> size_t { CURRENT_THROW(OptimizeException("Internal error.")); },
          [](double) -> size_t { CURRENT_THROW(OptimizeException("Internal error.")); },
          []() -> size_t { CURRENT_THROW(OptimizeException("Internal error.")); });
      ExpressionNodeImpl const& short_lived_node = vars_context_[node_index];
      ExpressionNodeType const node_type = short_lived_node.Type();

      if (IsOperationNode(node_type)) {
        value_t const a = short_lived_node.LHSIndex();
        value_t const b = short_lived_node.RHSIndex();
        if (phase < 2) {
          // Going down. Need to differentiate the dependencies of this node first. Use special bits. Flip LHS ad RHS.
          element.index_with_special_bit.SetSpecialTwoBitsValue(phase + 1);
          if (phase == 0) {
            PushToStack(b, ~current_stack_index);  // Two's complement to return the value into `return_value[1]`.
          } else {
            PushToStack(a, current_stack_index);
          }
        } else {
          // Going up, the { lhs, rhs } are already differentiated.
          impl_.DoReturnDifferentiatedOperation(node_type,
                                                a,
                                                b,
                                                element.return_value[0],
                                                element.return_value[1],
                                                stack_.Ref(element.return_value_index));
          stack_.DoPop();
        }
      } else if (IsFunctionNode(node_type)) {
        ExpressionNodeIndex const x = short_lived_node.ArgumentIndex();
        if (phase == 0) {
          // Going down. Need to differentiate the argument of this call first. Use the lowest special bit.
          element.index_with_special_bit.SetSpecialTwoBitsValue(1);
          PushToStack(x, current_stack_index);
        } else {
          // Going up, the argument is already differentiated.
          impl_.DoReturnDifferentiatedFunction(node_type,
                                               value_t(element.index_with_special_bit),
                                               value_t(x),
                                               element.return_value[0],
                                               stack_.Ref(element.return_value_index));
          stack_.DoPop();
        }
      } else {
        CURRENT_THROW(DifferentiatorForThisNodeTypeNotImplementedException());
      }
    }
    return stack_.ExtractReturnValue();
  }
};

// Various implementations of the differentiator: per var, per lambda, maintaining the whole gradient at once.
struct DifferentiateBySingleVarImpl {
  VarsContext const& vars_context_;
  size_t const var_index_;

  DifferentiateBySingleVarImpl(VarsContext const& vars_context, size_t var_index)
      : vars_context_(vars_context), var_index_(var_index) {}

  using retval_t = ExpressionNodeIndex;

  void DoAssignZero(retval_t& placeholder) const { placeholder = ExpressionNodeIndex::DoubleZero(); }
  void DoReturnDerivativeOfVar(size_t var_index, retval_t& placeholder) const {
    if (vars_context_.IsVarTheNonConstantOneBeingDifferentiatedBy(var_index, var_index_)) {
      placeholder = ExpressionNodeIndex::DoubleOne();
    } else {
      placeholder = ExpressionNodeIndex::DoubleZero();
    }
  }
  void DoReturnDerivativeOfLambda(retval_t&) const {
    CURRENT_THROW(SeeingLambdaWhileNotDifferentiatingByLambdaException());
  }
  void DoReturnDifferentiatedOperation(
      ExpressionNodeType node_type, value_t a, value_t b, value_t da, value_t db, retval_t& placeholder) const {
    placeholder = DifferentiateOperation(node_type, a, b, da, db);
  }
  void DoReturnDifferentiatedFunction(
      ExpressionNodeType node_type, value_t f, value_t x, value_t dx, retval_t& placeholder) const {
    placeholder = DifferentiateFunction(node_type, f, x, dx);
  }
};

struct DifferentiateByLambdaImpl {
  DifferentiateByLambdaImpl(VarsContext const&) {}

  using retval_t = ExpressionNodeIndex;

  void DoAssignZero(retval_t& placeholder) const { placeholder = ExpressionNodeIndex::DoubleZero(); }
  void DoReturnDerivativeOfVar(size_t, retval_t& placeholder) const { placeholder = ExpressionNodeIndex::DoubleZero(); }
  void DoReturnDerivativeOfLambda(retval_t& placeholder) const { placeholder = ExpressionNodeIndex::DoubleOne(); }
  void DoReturnDifferentiatedOperation(
      ExpressionNodeType node_type, value_t a, value_t b, value_t da, value_t db, retval_t& placeholder) const {
    placeholder = DifferentiateOperation(node_type, a, b, da, db);
  }
  void DoReturnDifferentiatedFunction(
      ExpressionNodeType node_type, value_t f, value_t x, value_t dx, retval_t& placeholder) const {
    placeholder = DifferentiateFunction(node_type, f, x, dx);
  }
};

// The somewhat compactly represented and fast to iterate gradient of a node.
struct GradientPiece {
  std::vector<ExpressionNodeIndex> elements;
  std::vector<bool> nonzero_indexes_bitset;
  std::vector<size_t> nonzero_indexes_list;

  GradientPiece()
      : elements(VarsManager::TLS().Active().NumberOfVars()),
        nonzero_indexes_bitset(VarsManager::TLS().Active().NumberOfVars()) {}

  void Clear() {
    std::vector<bool>(VarsManager::TLS().Active().NumberOfVars()).swap(nonzero_indexes_bitset);
    nonzero_indexes_list.clear();
  }

  void SetOne(size_t i) {
#ifdef NDEBUG
    if (nonzero_indexes_bitset[i]) {
      CURRENT_THROW(OptimizeException("Internal error."));
    }
#endif
    nonzero_indexes_bitset[i] = true;
    nonzero_indexes_list.push_back(i);
    elements[i] = ExpressionNodeIndex::DoubleOne();
  }

  void ApplyOperation(ExpressionNodeType node_type, value_t a, value_t b, GradientPiece const& db) {
    for (size_t const i : nonzero_indexes_list) {
      if (db.nonzero_indexes_bitset[i]) {
        elements[i] = DifferentiateOperation(node_type, a, b, elements[i], db.elements[i]);
      } else {
        elements[i] = DifferentiateOperation(node_type, a, b, elements[i], 0.0);
      }
    }
    for (size_t const j : db.nonzero_indexes_list) {
      if (!nonzero_indexes_bitset[j]) {
        elements[j] = DifferentiateOperation(node_type, a, b, 0.0, db.elements[j]);
        if (!elements[j].IsIndexDoubleZero()) {
          nonzero_indexes_bitset[j] = true;
          nonzero_indexes_list.push_back(j);
        }
      }
    }
  }

  void ApplyFunction(ExpressionNodeType node_type, value_t f, value_t x) {
    for (size_t const i : nonzero_indexes_list) {
      elements[i] = DifferentiateFunction(node_type, f, x, elements[i]);
    }
  }

  std::vector<value_t> FillOutput(size_t dim) const {
#ifndef NDEBUG
    if (dim != nonzero_indexes_bitset.size()) {
      CURRENT_THROW(OptimizeException("Internal error."));
    }
#endif
    std::vector<value_t> result(dim);
    for (size_t i = 0u; i < dim; ++i) {
      result[i] = nonzero_indexes_bitset[i] ? value_t(elements[i]) : 0.0;
    }
    return result;
  }
};

// TODO(dkorolev): Okay, it's really important to get rid of all those extra copies.
struct DifferentiateByAllVarsTogetherImpl {
  VarsContext const& vars_context_;

  DifferentiateByAllVarsTogetherImpl(VarsContext const& vars_context) : vars_context_(vars_context) {}

  using retval_t = GradientPiece;

  void DoAssignZero(retval_t& placeholder) const { placeholder.Clear(); }

  void DoReturnDerivativeOfVar(size_t var_index, retval_t& placeholder) const {
    placeholder.Clear();
    if (vars_context_.IsVarNotConstant(var_index)) {
      placeholder.SetOne(var_index);
    }
  }

  void DoReturnDerivativeOfLambda(retval_t&) const {
    CURRENT_THROW(SeeingLambdaWhileNotDifferentiatingByLambdaException());
  }

  void DoReturnDifferentiatedOperation(
      ExpressionNodeType node_type, value_t a, value_t b, retval_t da, retval_t db, retval_t& placeholder) const {
    // TODO(dkorolev): Obviously, this is to be optimized away to remove all the copies.
    placeholder = std::move(da);
    placeholder.ApplyOperation(node_type, a, b, db);
  }

  void DoReturnDifferentiatedFunction(
      ExpressionNodeType node_type, value_t f, value_t x, retval_t dx, retval_t& placeholder) const {
    // TODO(dkorolev): Obviously, this is to be optimized away to remove all the copies.
    placeholder = std::move(dx);
    placeholder.ApplyFunction(node_type, f, x);
  }
};

// The per-variable differentiator.
inline value_t Differentiate(value_t f, size_t derivative_per_finalized_var_index) {
  return Differentiator<DifferentiateBySingleVarImpl>(VarsManager::TLS().Active(), derivative_per_finalized_var_index)
      .DoDifferentiate(f);
}

// The single-pass gradient computer.
inline std::vector<value_t> ComputeGradient(value_t f) {
  VarsContext const& vars_context = VarsManager::TLS().Active();
  return Differentiator<DifferentiateByAllVarsTogetherImpl>(vars_context)
      .DoDifferentiate(f)
      .FillOutput(vars_context.NumberOfVars());
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
inline value_t DifferentiateByLambda(value_t f) {
  return Differentiator<DifferentiateByLambdaImpl>(VarsManager::TLS().Active()).DoDifferentiate(f);
}

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_DIFFERENTIATE_DIFFERENTIATE_H
