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

// For large expressions, `BalanceExpressionTree()` still does miracles, and 200 is way above any reasonable number.
constexpr static const size_t kNodeHeightCutoffIndicatingUnbalancedExpression = 200;
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
#ifndef NDEBUG
    TriggerSegmentationFault();
    throw false;
#else
    return 0.0;
#endif
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
#ifndef NDEBUG
    TriggerSegmentationFault();
    throw false;
#else
    return 0.0;
#endif
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
    size_t const allocated_size_;

   public:
    // The default size of the stack is one, and it never gets to zero.
    // This is to not make an exception for the generic case of the "ultimate return value". This ultimate return value
    // of the derivative will just be placed into `call_stack_[0].return_value[0]`, where it is up for grabs.
    explicit ManualStack(size_t max_depth)
        : call_stack_(max_depth + 1), actual_size_(1u), allocated_size_(max_depth + 1) {}

    bool NotEmpty() const { return actual_size_ > 1u; }

    size_t DoPush(ExpressionNodeIndex index, size_t return_value_index) {
#ifndef NDEBUG
      if (actual_size_ == allocated_size_) {
        TriggerSegmentationFault();
      }
#endif
      call_stack_[actual_size_].index_with_special_bit = index;
      call_stack_[actual_size_].return_value_index = return_value_index;
      return actual_size_++;
    }
    size_t CurrentStackIndex() const { return actual_size_ - 1u; }
    Entry& MutableTop() { return call_stack_[actual_size_ - 1u]; }
    void DoPop() { --actual_size_; }

    typename IMPL::retval_t& RetvalPlaceholder(size_t return_value_index_lhs) {
      size_t const return_value_index_rhs = ~return_value_index_lhs;
      return return_value_index_lhs < return_value_index_rhs ? call_stack_[return_value_index_lhs].return_value[0]
                                                             : call_stack_[return_value_index_rhs].return_value[1];
    }

    template <typename F>
    void CollectReturnValue(F&& f) const {
      f(call_stack_[0].return_value[0]);
    }
  };

 public:
  template <typename F, typename... ARGS>
  static void DoDifferentiate(VarsContext const& vars_context, value_t value_to_differentiate, F&& f, ARGS&&... args) {
    size_t const max_stack_depth = ExpressionTreeHeight(value_to_differentiate);
    if (max_stack_depth > kNodeHeightCutoffIndicatingUnbalancedExpression) {
      // For most practical purposes, running `BalanceExpressionTree(cost_function)` would do the job.
      CURRENT_THROW(DifferentiatorRequiresBalancedTreeException());
    }

    IMPL const impl(vars_context, std::forward<ARGS>(args)...);

    ManualStack stack(max_stack_depth);

    std::function<void(ExpressionNodeIndex, size_t)> const PushToStack = [&stack, &impl](ExpressionNodeIndex index,
                                                                                         size_t return_value_index) {
      index.CheckedDispatch([&](size_t) { stack.DoPush(index, return_value_index); },
                            [&](size_t var_index) {
                              impl.DoReturnDerivativeOfVar(var_index, stack.RetvalPlaceholder(return_value_index));
                            },
                            [&](double) { impl.DoAssignZero(stack.RetvalPlaceholder(return_value_index)); },
                            [&]() { impl.DoReturnDerivativeOfLambda(stack.RetvalPlaceholder(return_value_index)); });
    };

    PushToStack(value_to_differentiate, 0u);

    while (stack.NotEmpty()) {
      size_t const current_stack_index = stack.CurrentStackIndex();

      typename ManualStack::Entry& element = stack.MutableTop();

      uint64_t const phase = element.index_with_special_bit.ClearSpecialTwoBitsAndReturnWhatTheyWere();

// The node is `short-lived`, as the const reference to it can and will be invalidated as more nodes are added
// to the tree. Thus, all the relevant pieces of data must be extracted from this node before adding the new ones.
#ifdef NDEBUG
      size_t const node_index = element.index_with_special_bit.UncheckedNodeIndex();
#else
      // TODO(dkorolev): In `NDEBUG` mode this would just be an unchecked node index extraction.
      size_t const node_index = element.index_with_special_bit.template CheckedDispatch<size_t>(
          [](size_t node_index) -> size_t { return node_index; },
          [](size_t) -> size_t {
            TriggerSegmentationFault();
            throw false;
          },
          [](double) -> size_t {
            TriggerSegmentationFault();
            throw false;
          },
          []() -> size_t {
            TriggerSegmentationFault();
            throw false;
          });
#endif
      ExpressionNodeImpl const& short_lived_node = vars_context[node_index];
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
          impl.DoReturnDifferentiatedOperation(node_type,
                                               a,
                                               b,
                                               element.return_value[0],
                                               element.return_value[1],
                                               stack.RetvalPlaceholder(element.return_value_index));
          stack.DoPop();
        }
      } else if (IsFunctionNode(node_type)) {
        ExpressionNodeIndex const x = short_lived_node.ArgumentIndex();
        if (phase == 0) {
          // Going down. Need to differentiate the argument of this call first. Use the lowest special bit.
          element.index_with_special_bit.SetSpecialTwoBitsValue(1);
          PushToStack(x, current_stack_index);
        } else {
          // Going up, the argument is already differentiated.
          impl.DoReturnDifferentiatedFunction(node_type,
                                              value_t(element.index_with_special_bit),
                                              value_t(x),
                                              element.return_value[0],
                                              stack.RetvalPlaceholder(element.return_value_index));
          stack.DoPop();
        }
      } else {
        CURRENT_THROW(DifferentiatorForThisNodeTypeNotImplementedException());
      }
    }
    stack.CollectReturnValue(std::forward<F>(f));
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

struct DifferentiateByAllVarsTogetherImpl {
  // The somewhat compactly represented and fast to iterate gradient of a node.
  class GradientPiece {
   private:
#ifndef NDEBUG
    size_t m_;  // The number of components.
#endif
    size_t current_epoch_ = 0u;  // The current epoch index, to save on `memset`-s when clearing.
    std::vector<ExpressionNodeIndex> components_;
    std::vector<uint64_t> nonzero_index_epoch_version_;
    std::vector<uint32_t> nonzero_indexes_list_;
    size_t nonzero_indexes_count_ = 0;

    std::vector<uint32_t> removal_candidates_;  // A pre-allocated array to nullify gradient components that became 0.

   public:
    GradientPiece()
#ifndef NDEBUG
        : m_(VarsManager::TLS().Active().NumberOfVars()),
          components_(VarsManager::TLS().Active().NumberOfVars()),
#else
        : components_(VarsManager::TLS().Active().NumberOfVars()),
#endif
          nonzero_index_epoch_version_(VarsManager::TLS().Active().NumberOfVars()),
          nonzero_indexes_list_(VarsManager::TLS().Active().NumberOfVars()),
          removal_candidates_(VarsManager::TLS().Active().NumberOfVars()) {
    }

    void Clear() {
      ++current_epoch_;
      nonzero_indexes_count_ = 0u;
    }

    bool Has(size_t i) const { return nonzero_index_epoch_version_[i] == current_epoch_; }

    void SetOne(size_t i) {
#ifndef NDEBUG
      if (Has(i)) {
        TriggerSegmentationFault();
      }
      if (nonzero_indexes_count_ >= m_) {
        TriggerSegmentationFault();
      }
#endif
      components_[i] = ExpressionNodeIndex::DoubleOne();
      nonzero_index_epoch_version_[i] = current_epoch_;
      nonzero_indexes_list_[nonzero_indexes_count_++] = i;
    }

    void ApplyOperation(ExpressionNodeType node_type, value_t a, value_t b, GradientPiece const& rhs) {
      size_t removal_candidates_count = 0u;
      for (size_t lhs_index = 0u; lhs_index < nonzero_indexes_count_; ++lhs_index) {
        uint32_t const i = nonzero_indexes_list_[lhs_index];
        if (rhs.Has(i)) {
          components_[i] = DifferentiateOperation(node_type, a, b, components_[i], rhs.components_[i]);
        } else {
          components_[i] = DifferentiateOperation(node_type, a, b, components_[i], 0.0);
        }
        if (components_[i].IsIndexDoubleZero()) {
          removal_candidates_[removal_candidates_count++] = lhs_index;
        }
      }
      for (size_t rhs_index = 0u; rhs_index < rhs.nonzero_indexes_count_; ++rhs_index) {
        uint32_t const j = rhs.nonzero_indexes_list_[rhs_index];
        if (!Has(j)) {
          components_[j] = DifferentiateOperation(node_type, a, b, 0.0, rhs.components_[j]);
          if (!components_[j].IsIndexDoubleZero()) {
#ifndef NDEBUG
            if (nonzero_indexes_count_ >= m_) {
              TriggerSegmentationFault();
            }
#endif
            nonzero_index_epoch_version_[j] = current_epoch_;
            nonzero_indexes_list_[nonzero_indexes_count_++] = j;
          }
        }
      }
      for (size_t index = 0; index < removal_candidates_count; ++index) {
        uint32_t const i = removal_candidates_[index];
        nonzero_index_epoch_version_[nonzero_indexes_list_[i]] = current_epoch_ - 1u;
        nonzero_indexes_list_[i] = nonzero_indexes_list_[--nonzero_indexes_count_];
      }
    }

    void ApplyFunction(ExpressionNodeType node_type, value_t f, value_t x) {
      size_t removal_candidates_count = 0u;
      for (size_t index = 0u; index < nonzero_indexes_count_; ++index) {
        uint32_t const i = nonzero_indexes_list_[index];
        components_[i] = DifferentiateFunction(node_type, f, x, components_[i]);
        if (components_[i].IsIndexDoubleZero()) {
          removal_candidates_[removal_candidates_count++] = index;
        }
      }
      for (size_t index = 0; index < removal_candidates_count; ++index) {
        uint32_t const i = removal_candidates_[index];
        nonzero_index_epoch_version_[nonzero_indexes_list_[i]] = current_epoch_ - 1u;
        nonzero_indexes_list_[i] = nonzero_indexes_list_[--nonzero_indexes_count_];
      }
    }

    std::vector<value_t> FillOutput() const {
      std::vector<value_t> result(components_.size());
      for (size_t i = 0u; i < result.size(); ++i) {
        result[i] = Has(i) ? value_t(components_[i]) : 0.0;
      }
      return result;
    }
  };

  VarsContext const& vars_context_;

  DifferentiateByAllVarsTogetherImpl(VarsContext const& vars_context) : vars_context_(vars_context) {}

  using retval_t = GradientPiece;

  void DoAssignZero(GradientPiece& placeholder) const { placeholder.Clear(); }

  void DoReturnDerivativeOfVar(size_t var_index, GradientPiece& placeholder) const {
    placeholder.Clear();
    if (vars_context_.IsVarNotConstant(var_index)) {
      placeholder.SetOne(var_index);
    }
  }

  void DoReturnDerivativeOfLambda(GradientPiece&) const {
    CURRENT_THROW(SeeingLambdaWhileNotDifferentiatingByLambdaException());
  }

  void DoReturnDifferentiatedOperation(ExpressionNodeType node_type,
                                       value_t a,
                                       value_t b,
                                       GradientPiece& da,
                                       GradientPiece& db,
                                       GradientPiece& placeholder) const {
    // TODO(dkorolev): This `std::swap()` is already fast, but perhaps we can do better?
    std::swap(placeholder, da);
    placeholder.ApplyOperation(node_type, a, b, db);
  }

  void DoReturnDifferentiatedFunction(
      ExpressionNodeType node_type, value_t f, value_t x, GradientPiece& dx, GradientPiece& placeholder) const {
    // TODO(dkorolev): This `std::swap()` is already fast, but perhaps we can do better?
    std::swap(placeholder, dx);
    placeholder.ApplyFunction(node_type, f, x);
  }
};

// The per-variable differentiator.
inline value_t Differentiate(value_t f, size_t derivative_per_finalized_var_index) {
  value_t result;
  Differentiator<DifferentiateBySingleVarImpl>::DoDifferentiate(VarsManager::TLS().Active(),
                                                                f,
                                                                [&result](value_t retval) { result = retval; },
                                                                derivative_per_finalized_var_index);
  return result;
}

// The single-pass gradient computer.
inline std::vector<value_t> ComputeGradient(value_t f) {
  std::vector<value_t> result;
  Differentiator<DifferentiateByAllVarsTogetherImpl>::DoDifferentiate(
      VarsManager::TLS().Active(), f, [&result](DifferentiateByAllVarsTogetherImpl::GradientPiece const& retval) {
        result = retval.FillOutput();
      });
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
  value_t result;
  Differentiator<DifferentiateByLambdaImpl>::DoDifferentiate(
      VarsManager::TLS().Active(), f, [&result](value_t retval) { result = retval; });
  return result;
}

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_DIFFERENTIATE_DIFFERENTIATE_H
