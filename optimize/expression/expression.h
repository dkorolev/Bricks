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

#ifndef OPTIMIZE_EXPRESSION_EXPRESSION_H
#define OPTIMIZE_EXPRESSION_EXPRESSION_H

#include "../base.h"
#include "../vars/vars.h"

#include "../../bricks/strings/printf.h"

#include <cmath>

namespace current {
namespace expression {

struct DoubleValueNotRegularException final : OptimizeException {
  explicit DoubleValueNotRegularException(double x)
      : OptimizeException(current::strings::Printf("%lf, %+la, 0x%016lx", x, x, *reinterpret_cast<uint64_t*>(&x))) {}
};

struct ExpressionNodeDivisionByZeroDetected final : OptimizeException {};

struct ExpressionVarNodeBoxingException final : OptimizeException {};

struct Build1DFunctionNumberOfVarsMismatchException final : OptimizeException {};

// When `using namespace current::expression`:
// 1) can use `value_t x = ...` instead of `auto x = ...`.
// 2) can use math functions, such as `exp()` or `sqr()` or `sigmoid()`, w/o specifying the namespace.
class value_t final {
 private:
  ExpressionNodeIndex index_;  // Copyable and assignable; `value_t` is a lightweight object.

  static ExpressionNodeIndex IndexFromVarNodeOrThrow(VarNode const& var_node) {
    if (var_node.type == VarNodeType::Value) {
      // When the most significant bit is set, the node is just a reference to certain index in the input vector.
      return ExpressionNodeIndex::FromVarIndex(var_node.VarIndex());
    } else {
      CURRENT_THROW(ExpressionVarNodeBoxingException());
    }
  }

  struct ConstructLambdaNode {};
  value_t(ConstructLambdaNode) : index_(ExpressionNodeIndex::LambdaNodeIndex()) {}

  static ExpressionNodeIndex IndexFromDoubleOrThrow(double x) {
    if (IsRegularDouble(x)) {
      return ExpressionNodeIndex::FromRegularDouble(x);
    } else {
      // NOTE(dkorolev): If you _really_ need to use an irregular double with Current's optimizer,
      // make it a constant variable, via something like `x["my_weird_constant"].SetConstant(1e500);`.
      CURRENT_THROW(DoubleValueNotRegularException(x));
    }
  }

  struct ConstructFromExpressionNodeIndex {};

 public:
  value_t() = default;
  value_t(double x) : index_(IndexFromDoubleOrThrow(x)) {}
  value_t(VarNode const& var_node) : index_(IndexFromVarNodeOrThrow(var_node)) {}
  value_t(ConstructFromExpressionNodeIndex, ExpressionNodeIndex index) : index_(index) {}
  static value_t FromExpressionNodeIndex(ExpressionNodeIndex index) {
    return value_t(ConstructFromExpressionNodeIndex(), index);
  }

  static value_t lambda() { return value_t(ConstructLambdaNode()); }

  // No cast into `bool` for performance reasons. -- D.K.
  uint64_t IsImmediateDouble() const { return index_.IsIndexImmediateDouble(); }

  double GetImmediateDouble() const {
#ifndef NDEBUG
    if (!IsImmediateDouble()) {
      TriggerSegmentationFault();
    }
#endif
    return index_.GetImmediateDoubleFromIndex();
  }

  bool IsZero() const { return index_.IsIndexDoubleZero(); }
  bool IsOne() const { return index_.IsIndexDoubleOne(); }

  template <typename... ARGS>
  static ExpressionNodeIndex Emplace(ARGS&&... args) {
    return ExpressionNodeIndex::FromNodeIndex(InternalTLS().DoEmplace(std::forward<ARGS>(args)...));
  }

  ExpressionNodeIndex GetExpressionNodeIndex() const { return index_; }

  std::string DebugAsString() const {
#ifndef NDEBUG
    InternalTLS();  // This will throw in `!NDEBUG` mode if there is no active thread-local context.
    if (index_.IsUninitialized()) {
      return "Uninitialized";
    }
#endif
    return index_.template CheckedDispatch<std::string>(
        [&](size_t node_index) -> std::string {
          ExpressionNodeImpl const& node = InternalTLS()[node_index];
          ExpressionNodeType const type = node.Type();
          if (false) {
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)                                   \
  }                                                                                        \
  else if (type == ExpressionNodeType::Operation_##name) {                                 \
    return "(" + value_t::FromExpressionNodeIndex(node.LHSIndex()).DebugAsString() + #op + \
           value_t::FromExpressionNodeIndex(node.RHSIndex()).DebugAsString() + ')';
#include "../math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)            \
  }                                                     \
  else if (type == ExpressionNodeType::Function_##fn) { \
    return #fn "(" + value_t::FromExpressionNodeIndex(node.ArgumentIndex()).DebugAsString() + ')';
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
          } else {
#ifndef NDEBUG
            TriggerSegmentationFault();
            throw false;
#else
            return "<InternalError>";
#endif
          }
        },
        [&](size_t var_index) -> std::string { return InternalTLS().VarName(var_index); },
        [](double value) -> std::string {
          if (value >= 0) {
            return current::ToString(value);
          } else {
            return "(" + current::ToString(value) + ')';
          }
        },
        [&]() -> std::string { return "lambda"; });
  }
};
static_assert(sizeof(value_t) == 8u, "The `value_t` type is meant to be ultra-lightweight.");

// For `MovePoint` in `../vars/vars.h`.
inline ExpressionNodeIndex ExpressionNodeIndexFromExpressionNodeOrValue(value_t value) {
  return value.GetExpressionNodeIndex();
}

// The four mathematical operations are explicitly spelled out so that basic optimizations can be applied.
inline value_t operator+(value_t lhs, value_t rhs) {
  if (lhs.IsImmediateDouble() && rhs.IsImmediateDouble()) {
    return lhs.GetImmediateDouble() + rhs.GetImmediateDouble();
  } else if (rhs.IsZero()) {
    return lhs;
  } else if (lhs.IsZero()) {
    return rhs;
  } else {
    return value_t::FromExpressionNodeIndex(
        value_t::Emplace(ExpressionNodeTypeSelector<ExpressionNodeType::Operation_add>(),
                         lhs.GetExpressionNodeIndex(),
                         rhs.GetExpressionNodeIndex()));
  }
}

inline value_t operator-(value_t lhs, value_t rhs) {
  if (lhs.IsImmediateDouble() && rhs.IsImmediateDouble()) {
    return lhs.GetImmediateDouble() - rhs.GetImmediateDouble();
  } else if (rhs.IsZero()) {
    return lhs;
  } else if (lhs.IsZero()) {
    return value_t::FromExpressionNodeIndex(
        value_t::Emplace(ExpressionNodeTypeSelector<ExpressionNodeType::Operation_sub>(),
                         ExpressionNodeIndex::DoubleZero(),
                         rhs.GetExpressionNodeIndex()));
  } else {
    return value_t::FromExpressionNodeIndex(
        value_t::Emplace(ExpressionNodeTypeSelector<ExpressionNodeType::Operation_sub>(),
                         lhs.GetExpressionNodeIndex(),
                         rhs.GetExpressionNodeIndex()));
  }
}

inline value_t operator*(value_t lhs, value_t rhs) {
  if (lhs.IsImmediateDouble() && rhs.IsImmediateDouble()) {
    return lhs.GetImmediateDouble() * rhs.GetImmediateDouble();
  } else if (lhs.IsZero() || rhs.IsZero()) {
    return value_t::FromExpressionNodeIndex(ExpressionNodeIndex::DoubleZero());
  } else if (rhs.IsOne()) {
    return lhs;
  } else if (lhs.IsOne()) {
    return rhs;
  } else {
    return value_t::FromExpressionNodeIndex(
        value_t::Emplace(ExpressionNodeTypeSelector<ExpressionNodeType::Operation_mul>(),
                         lhs.GetExpressionNodeIndex(),
                         rhs.GetExpressionNodeIndex()));
  }
}

inline value_t operator/(value_t lhs, value_t rhs) {
  if (lhs.IsImmediateDouble() && rhs.IsImmediateDouble()) {
    return lhs.GetImmediateDouble() / rhs.GetImmediateDouble();
  } else if (lhs.IsZero()) {
    return value_t::FromExpressionNodeIndex(ExpressionNodeIndex::DoubleZero());
  } else if (rhs.IsZero()) {
    CURRENT_THROW(ExpressionNodeDivisionByZeroDetected());
  } else if (rhs.IsOne()) {
    return lhs;
  } else {
    return value_t::FromExpressionNodeIndex(
        value_t::Emplace(ExpressionNodeTypeSelector<ExpressionNodeType::Operation_div>(),
                         lhs.GetExpressionNodeIndex(),
                         rhs.GetExpressionNodeIndex()));
  }
}

inline value_t operator+(value_t op) { return op; }
inline value_t operator+(VarNode const& op) { return op; }
inline value_t operator-(value_t op) { return (0.0 - op); }
inline value_t operator-(VarNode const& op) { return (0.0 - op); }

// NOTE(dkorolev): Contrary to the C++ convention of returning `Value&` from `operator+=` and the like,
//                 returning the `Value const&` by design, to prevent various `a += b += c;` madness.
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)                                                 \
  inline value_t operator op(value_t lhs, VarNode const& rhs) { return operator op(lhs, +rhs); }         \
  inline value_t operator op(VarNode const& lhs, value_t rhs) { return operator op(+lhs, rhs); }         \
  inline value_t operator op(VarNode const& lhs, VarNode const& rhs) { return operator op(+lhs, +rhs); } \
  inline value_t operator op(value_t lhs, double rhs) { return operator op(lhs, value_t(rhs)); }         \
  inline value_t operator op(double lhs, value_t rhs) { return operator op(value_t(lhs), rhs); }         \
  inline value_t operator op(VarNode const& lhs, double rhs) { return operator op(+lhs, value_t(rhs)); } \
  inline value_t operator op(double lhs, VarNode const& rhs) { return operator op(value_t(lhs), +rhs); } \
  inline value_t const& operator op2(value_t& lhs, value_t rhs) {                                        \
    lhs = lhs op rhs;                                                                                    \
    return lhs;                                                                                          \
  }                                                                                                      \
  inline value_t const& operator op2(value_t& lhs, VarNode const& rhs) {                                 \
    lhs = lhs op rhs;                                                                                    \
    return lhs;                                                                                          \
  }                                                                                                      \
  inline value_t const& operator op2(value_t& lhs, double rhs) {                                         \
    lhs = lhs op rhs;                                                                                    \
    return lhs;                                                                                          \
  }
#include "../math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION

namespace functions {
using std::exp;
using std::log;
using std::sin;
using std::cos;
using std::tan;
using std::sqrt;
using std::asin;
using std::acos;
using std::atan;
inline double sqr(double x) { return x * x; }
inline double unit_step(double x) { return x >= 0 ? 1 : 0; }
inline double ramp(double x) { return x > 0 ? x : 0; }
inline double sigmoid(double x) {
  // Designed to fight overflow and underflow. -- D.K.
  if (x >= 25) {
    return 1.0;
  } else if (x <= -25) {
    return 0.0;
  } else {
    return 1.0 / (1.0 + std::exp(-x));
  }
}
inline double log_sigmoid(double x) {
  // Designed to fight overflow and underflow. -- D.K.
  if (x >= 25) {
    return 0.0;
  } else if (x <= -25) {
    return x;
  } else {
    return -log(1.0 + exp(-x));
  }
}
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)                                                                    \
  inline value_t fn(value_t argument) {                                                                         \
    if (argument.IsImmediateDouble()) {                                                                         \
      return fn(argument.GetImmediateDouble());                                                                 \
    } else {                                                                                                    \
      return value_t::FromExpressionNodeIndex(value_t::Emplace(                                                 \
          ExpressionNodeTypeSelector<ExpressionNodeType::Function_##fn>(), argument.GetExpressionNodeIndex())); \
    }                                                                                                           \
  }                                                                                                             \
  inline value_t fn(VarNode const& argument) { return fn(value_t(argument)); }
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
}  // namespace current::expression::functions

using namespace expression::functions;

// A helper method to generate a one-dimensional function from a multi-dimensional one,
// by replacing each occurrence of each variable by a respective provided formula.
// The usecase of this method is the construction of the "cost function" for line search optimization.
class Build1DFunctionImpl {
 private:
  Vars::Scope const& vars_scope_;
  std::vector<ExpressionNodeIndex> const& substitute_;

 public:
  Build1DFunctionImpl(Vars::Scope const& scope, std::vector<ExpressionNodeIndex> const& substitute)
      : vars_scope_(scope), substitute_(substitute) {}

  // TODO(dkorolev): This `DoBuild1DFunction` is a) `Checked`, meaning slow, and b) recursive. Something to fix.
  ExpressionNodeIndex DoBuild1DFunction(ExpressionNodeIndex f) const {
    return f.template CheckedDispatch<ExpressionNodeIndex>(
        [&](size_t node_index) -> ExpressionNodeIndex {
          ExpressionNodeImpl const& short_lived_node = vars_scope_[node_index];
          ExpressionNodeType const type = short_lived_node.Type();
          if (false) {
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)                     \
  }                                                                          \
  else if (type == ExpressionNodeType::Operation_##name) {                   \
    ExpressionNodeIndex const lhs = short_lived_node.LHSIndex();             \
    ExpressionNodeIndex const rhs = short_lived_node.RHSIndex();             \
    return (value_t::FromExpressionNodeIndex(DoBuild1DFunction(lhs))         \
                op value_t::FromExpressionNodeIndex(DoBuild1DFunction(rhs))) \
        .GetExpressionNodeIndex();
#include "../math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)                                                         \
  }                                                                                                  \
  else if (type == ExpressionNodeType::Function_##fn) {                                              \
    return fn(value_t::FromExpressionNodeIndex(DoBuild1DFunction(short_lived_node.ArgumentIndex()))) \
        .GetExpressionNodeIndex();
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
          } else {
#ifndef NDEBUG
            TriggerSegmentationFault();
            throw false;
#else
            return ExpressionNodeIndex::DoubleZero();
#endif
          }
        },
        [&](size_t var_index) -> ExpressionNodeIndex {
#ifndef NDEBUG
          if (!(var_index < substitute_.size())) {
            TriggerSegmentationFault();
          }
#endif
          return substitute_[var_index];
        },
        [&](double) -> ExpressionNodeIndex { return f; },
        [&]() -> ExpressionNodeIndex {
// No `lambda` support here. `DoBuild1DFunction` introduces `lambda`-s, not uses them.
#ifndef NDEBUG
          TriggerSegmentationFault();
          throw false;
#else
          return ExpressionNodeIndex::DoubleZero();
#endif
        });
  }
};
inline value_t Build1DFunction(value_t f, std::vector<value_t> const& substitute, Vars::Scope& scope = InternalTLS()) {
  std::vector<ExpressionNodeIndex> actual_substitute(substitute.size());
  for (size_t i = 0u; i < substitute.size(); ++i) {
    actual_substitute[i] = substitute[i].GetExpressionNodeIndex();
  }
  return value_t::FromExpressionNodeIndex(
      Build1DFunctionImpl(scope, actual_substitute).DoBuild1DFunction(f.GetExpressionNodeIndex()));
}
inline value_t Build1DFunction(value_t f,
                               std::vector<ExpressionNodeIndex> const& substitute,
                               Vars::Scope& scope = InternalTLS()) {
  return value_t::FromExpressionNodeIndex(
      Build1DFunctionImpl(scope, substitute).DoBuild1DFunction(f.GetExpressionNodeIndex()));
}

}  // namespace current::expression
}  // namespace current

// Expose mathematical functions defined on `value_t`, such as `exp()` or `sqrt()`, to the global namespace.
// So that user-defined functions, that take some `template <typename T> ... (T& result, std::vector<T> const& x)`,
// could invoke `result += log(exp(x[i]) + 1.0)` on `value_t`-s, without having to use `namespace current::expression`.
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn) using current::expression::functions::fn;
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION

#endif  // #ifndef OPTIMIZE_EXPRESSION_EXPRESSION_H
