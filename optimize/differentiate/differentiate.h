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
  class ManualStack {
   public:
    struct Entry {
      // The manual stack stores the "indexes" of the nodes to work with, with the special bit used to tell whether
      // during the "recursive" call the node being considered does already or does not yet have its dependencies ready.
      // Effectively, it's the flag to tell whether the "recursive" call is seeing this node on the way "down" or "up".
      ExpressionNodeIndex index_with_special_bit;

      // The results of the execution of the "recursive" calls down the stack. For `lhs` and `rhs` respectively.
      typename IMPL::retval_t return_value[2];

      // Stack element index to return the value. It is multiplied by two, and the LSB is the LHS/RHS bit.
      size_t return_value_index_times2;
    };
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

    size_t DoPush(ExpressionNodeIndex index, size_t return_value_index_times2) {
      GrowIfNecessary();
      call_stack_[actual_size_].index_with_special_bit = index;
      call_stack_[actual_size_].return_value_index_times2 = return_value_index_times2;
      return actual_size_++;
    }

    Entry const& DoPop() { return call_stack_[--actual_size_]; }

    typename IMPL::retval_t& Ref(size_t return_value_index_times2) {
      return call_stack_[return_value_index_times2 >> 1].return_value[return_value_index_times2 & 1];
    }

    typename IMPL::retval_t const& ExtractReturnValue() const { return call_stack_[0].return_value[0]; }
  };

  VarsContext const& vars_context_;
  IMPL const impl_;
  mutable ManualStack stack_;

  void PushToStack(ExpressionNodeIndex index, size_t return_value_index_times2) const {
    index.Dispatch(
        [&](size_t) { stack_.DoPush(index, return_value_index_times2); },
        [&](size_t var_index) { impl_.DoReturnDerivativeOfVar(var_index, stack_.Ref(return_value_index_times2)); },
        [&](double) { impl_.DoAssignZero(stack_.Ref(return_value_index_times2)); },
        [&]() { impl_.DoReturnDerivativeOfLambda(stack_.Ref(return_value_index_times2)); });
  }

 public:
  template <typename... ARGS>
  Differentiator(VarsContext const& vars_context, ARGS&&... args)
      : vars_context_(vars_context), impl_(vars_context, std::forward<ARGS>(args)...) {}

  typename IMPL::retval_t const& Differentiate(value_t value_to_differentiate) const {
    ExpressionNodeIndex const index_to_differentiate = value_to_differentiate;

    PushToStack(index_to_differentiate, 0u);

    while (stack_.NotEmpty()) {
      typename ManualStack::Entry const& element = stack_.DoPop();

      ExpressionNodeIndex index = element.index_with_special_bit;
      bool const ready_to_differentiate = index.ClearSpecialBitAndReturnWhatItWas();

      // The node is `short-lived`, as the const reference to it can and will be invalidated as more nodes are added
      // to the tree. Thus, all the relevant pieces of data must be extracted from this node before adding the new ones.
      // TODO(dkorolev): In `NDEBUG` mode this would just be an unchecked node index extraction.
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
          impl_.DoReturnDifferentiatedOperation(node_type,
                                                a,
                                                b,
                                                element.return_value[0],
                                                element.return_value[1],
                                                stack_.Ref(element.return_value_index_times2));
        }
      } else if (IsFunctionNode(node_type)) {
        ExpressionNodeIndex const x = short_lived_node.ArgumentIndex();
        if (!ready_to_differentiate) {
          // Going down. Need to differentiate the argument of this call first. Use the special bit.
          index.SetSpecialBit();
          size_t const dfs_call_return_value_index = stack_.DoPush(index, element.return_value_index_times2);
          PushToStack(x, dfs_call_return_value_index * 2u);
        } else {
          // Going up, the argument is already differentiated.
          impl_.DoReturnDifferentiatedFunction(node_type,
                                               value_t(index),
                                               value_t(x),
                                               element.return_value[0],
                                               stack_.Ref(element.return_value_index_times2));
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

// TODO(dkorolev): Obviously, the `map<>` is about the least effective solution here. Refactor it.
struct DifferentiateByAllVarsTogetherImpl {
  VarsContext const& vars_context_;

  DifferentiateByAllVarsTogetherImpl(VarsContext const& vars_context) : vars_context_(vars_context) {}

  using retval_t = std::map<size_t, ExpressionNodeIndex>;

  void DoAssignZero(retval_t& placeholder) const { placeholder.clear(); }

  void DoReturnDerivativeOfVar(size_t var_index, retval_t& placeholder) const {
    placeholder.clear();
    if (vars_context_.IsVarNotConstant(var_index)) {
      placeholder[var_index] = ExpressionNodeIndex::DoubleOne();
    }
  }

  void DoReturnDerivativeOfLambda(retval_t&) const {
    CURRENT_THROW(SeeingLambdaWhileNotDifferentiatingByLambdaException());
  }

  void DoReturnDifferentiatedOperation(
      ExpressionNodeType node_type, value_t a, value_t b, retval_t da, retval_t db, retval_t& placeholder) const {
    // TODO(dkorolev): Obviously, this is to be optimized away to remove all the copies.
    std::set<size_t> indexes;
    for (auto const& lhs : da) {
      indexes.insert(lhs.first);
    }
    for (auto const& rhs : db) {
      indexes.insert(rhs.first);
    }
    placeholder.clear();
    for (size_t const i : indexes) {
      auto const lhs = da.find(i);
      auto const rhs = db.find(i);
      placeholder[i] = DifferentiateOperation(node_type,
                                              a,
                                              b,
                                              lhs != da.end() ? lhs->second : ExpressionNodeIndex::DoubleZero(),
                                              rhs != db.end() ? rhs->second : ExpressionNodeIndex::DoubleZero());
    }
  }

  void DoReturnDifferentiatedFunction(
      ExpressionNodeType node_type, value_t f, value_t x, retval_t dx, retval_t& placeholder) const {
    // TODO(dkorolev): Obviously, this is to be optimized away to remove all the copies.
    placeholder = std::move(dx);
    for (auto& v : placeholder) {
      v.second = DifferentiateFunction(node_type, f, x, v.second);
    }
  }
};

// The per-variable differentiator.
inline value_t Differentiate(value_t f, size_t derivative_per_finalized_var_index) {
  return Differentiator<DifferentiateBySingleVarImpl>(VarsManager::TLS().Active(), derivative_per_finalized_var_index)
      .Differentiate(f);
}

// The single-pass gradient computer.
inline std::vector<value_t> ComputeGradient(value_t f) {
  VarsContext const& vars_context = VarsManager::TLS().Active();
  std::map<size_t, ExpressionNodeIndex> const g =
      Differentiator<DifferentiateByAllVarsTogetherImpl>(VarsManager::TLS().Active()).Differentiate(f);
  std::vector<value_t> result(vars_context.NumberOfVars());
  for (size_t i = 0u; i < result.size(); ++i) {
    auto const cit = g.find(i);
    if (cit != g.end()) {
      result[i] = cit->second;
    } else {
      result[i] = 0.0;
    }
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
inline value_t DifferentiateByLambda(value_t f) {
  return Differentiator<DifferentiateByLambdaImpl>(VarsManager::TLS().Active()).Differentiate(f);
}

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_DIFFERENTIATE_DIFFERENTIATE_H
