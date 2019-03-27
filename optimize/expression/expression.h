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

struct ExpressionNodeInternalError final : OptimizeException {};

struct Build1DFunctionRequiresUpToDateVarsIndexes final : OptimizeException {};
struct Build1DFunctionNumberOfVarsMismatchException final : OptimizeException {};

constexpr static const uint64_t kExpressionNodeIndexForDoubleZero = 0x2000000000000000;
constexpr static const uint64_t kExpressionNodeIndexForDoubleNegativeZero = 0xa000000000000000;
constexpr static const uint64_t kExpressionNodeIndexForDoubleOne = 0x3ff0000000000000;

class ExpressionNode final {
 private:
  ExpressionNodeIndex index_;  // Copyable and assignable; `ExpressionNode` is a lightweight object.

  static ExpressionNodeIndex IndexFromVarNodeOrThrow(VarNode const& var_node) {
    if (var_node.type == VarNodeType::Value) {
      // When the most significant bit is set, the node is just a reference to certain index in the input vector.
      return ExpressionNodeIndex::FromVarIndex(var_node.InternalVarIndex());
    } else {
      CURRENT_THROW(ExpressionVarNodeBoxingException());
    }
  }

  struct ConstructLambdaNode {};
  ExpressionNode(ConstructLambdaNode) : index_(ExpressionNodeIndex::LambdaNodeIndex()) {}

  static ExpressionNodeIndex IndexFromDoubleOrThrow(double x) {
    if (IsRegularDouble(x)) {
      return ExpressionNodeIndex::FromRegularDouble(x);
    } else {
      // NOTE(dkorolev): If you _really_ need to use an irregular double with Current's optimizer,
      // make it a constant variable, via something like `x["my_weird_constant"].SetConstant(1e500);`.
      CURRENT_THROW(DoubleValueNotRegularException(x));
    }
  }

 public:
  ExpressionNode() = default;
  ExpressionNode(double x) : index_(IndexFromDoubleOrThrow(x)) {}
  ExpressionNode(ExpressionNodeIndex index) : index_(index) {}
  ExpressionNode(VarNode const& var_node) : index_(IndexFromVarNodeOrThrow(var_node)) {}

  // No cast into `bool` for performance reasons. -- D.K.
  uint64_t IsImmediateDouble() const { return index_.IsIndexImmediateDouble(); }

  double GetImmediateDouble() const {
#ifndef NDEBUG
    if (!IsImmediateDouble()) {
      CURRENT_THROW(ExpressionNodeInternalError());
    }
#endif
    return index_.GetImmediateDoubleFromIndex();
  }

  bool IsZero() const {
    return index_.RawCompactifiedIndexEquals(kExpressionNodeIndexForDoubleZero) ||
           index_.RawCompactifiedIndexEquals(kExpressionNodeIndexForDoubleNegativeZero);
  }
  bool IsOne() const { return index_.RawCompactifiedIndexEquals(kExpressionNodeIndexForDoubleOne); }

  static ExpressionNode FromNodeIndex(size_t node_index) { return ExpressionNodeIndex::FromNodeIndex(node_index); }
  static ExpressionNode lambda() { return ExpressionNode(ConstructLambdaNode()); }

  operator ExpressionNodeIndex() const { return index_; }

  std::string DebugAsString() const {
    VarsContext const& vars_context = VarsManager::TLS().Active();
    return index_.template Dispatch<std::string>(
        [&](size_t node_index) -> std::string {
          ExpressionNodeImpl const& node = vars_context[node_index];
          ExpressionNodeType const type = node.Type();
          if (type == ExpressionNodeType::Uninitialized) {
            return "<Uninitialized>";
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)                 \
  }                                                                      \
  else if (type == ExpressionNodeType::Operation_##name) {               \
    return "(" + ExpressionNode(node.LHSIndex()).DebugAsString() + #op + \
           ExpressionNode(node.RHSIndex()).DebugAsString() + ')';
#include "../math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)            \
  }                                                     \
  else if (type == ExpressionNodeType::Function_##fn) { \
    return #fn "(" + ExpressionNode(node.ArgumentIndex()).DebugAsString() + ')';
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
          } else {
            CURRENT_THROW(ExpressionNodeInternalError());
          }
        },
        [&](size_t var_index) -> std::string { return vars_context.VarNameByOriginalIndex(var_index); },
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
static_assert(sizeof(ExpressionNode) == 8u, "The `ExpressionNode` type is meant to be ultra-lightweight.");

// The four mathematical operations are explicitly spelled out so that basic optimizations can be applied.
inline ExpressionNode operator+(ExpressionNode lhs, ExpressionNode rhs) {
  if (lhs.IsImmediateDouble() && rhs.IsImmediateDouble()) {
    return lhs.GetImmediateDouble() + rhs.GetImmediateDouble();
  } else if (rhs.IsZero()) {
    return lhs;
  } else if (lhs.IsZero()) {
    return rhs;
  } else {
    return ExpressionNode::FromNodeIndex(VarsManager::TLS().Active().EmplaceExpressionNode(
        ExpressionNodeTypeSelector<ExpressionNodeType::Operation_add>(),
        ExpressionNodeIndex(lhs),
        ExpressionNodeIndex(rhs)));
  }
}

inline ExpressionNode operator-(ExpressionNode lhs, ExpressionNode rhs) {
  if (lhs.IsImmediateDouble() && rhs.IsImmediateDouble()) {
    return lhs.GetImmediateDouble() - rhs.GetImmediateDouble();
  } else if (rhs.IsZero()) {
    return lhs;
  } else if (lhs.IsZero()) {
    return ExpressionNode::FromNodeIndex(VarsManager::TLS().Active().EmplaceExpressionNode(
        ExpressionNodeTypeSelector<ExpressionNodeType::Operation_sub>(),
        ExpressionNode(0.0),
        ExpressionNodeIndex(rhs)));
  } else {
    return ExpressionNode::FromNodeIndex(VarsManager::TLS().Active().EmplaceExpressionNode(
        ExpressionNodeTypeSelector<ExpressionNodeType::Operation_sub>(),
        ExpressionNodeIndex(lhs),
        ExpressionNodeIndex(rhs)));
  }
}

inline ExpressionNode operator*(ExpressionNode lhs, ExpressionNode rhs) {
  if (lhs.IsImmediateDouble() && rhs.IsImmediateDouble()) {
    return lhs.GetImmediateDouble() * rhs.GetImmediateDouble();
  } else if (lhs.IsZero() || rhs.IsZero()) {
    return 0.0;
  } else if (rhs.IsOne()) {
    return lhs;
  } else if (lhs.IsOne()) {
    return rhs;
  } else {
    return ExpressionNode::FromNodeIndex(VarsManager::TLS().Active().EmplaceExpressionNode(
        ExpressionNodeTypeSelector<ExpressionNodeType::Operation_mul>(),
        ExpressionNodeIndex(lhs),
        ExpressionNodeIndex(rhs)));
  }
}

inline ExpressionNode operator/(ExpressionNode lhs, ExpressionNode rhs) {
  if (lhs.IsImmediateDouble() && rhs.IsImmediateDouble()) {
    return lhs.GetImmediateDouble() / rhs.GetImmediateDouble();
  } else if (lhs.IsZero()) {
    return 0.0;
  } else if (rhs.IsZero()) {
    CURRENT_THROW(ExpressionNodeDivisionByZeroDetected());
  } else if (rhs.IsOne()) {
    return lhs;
  } else {
    return ExpressionNode::FromNodeIndex(VarsManager::TLS().Active().EmplaceExpressionNode(
        ExpressionNodeTypeSelector<ExpressionNodeType::Operation_div>(),
        ExpressionNodeIndex(lhs),
        ExpressionNodeIndex(rhs)));
  }
}

// NOTE(dkorolev): Contrary to the C++ convention of returning `Value&` from `operator+=` and the like,
//                 returning the `Value const&` by design, to prevent various `a += b += c;` madness.
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)                                                              \
  inline ExpressionNode operator op(ExpressionNode lhs, VarNode const& rhs) {                                         \
    return operator op(lhs, ExpressionNode(rhs));                                                                     \
  }                                                                                                                   \
  inline ExpressionNode operator op(VarNode const& lhs, ExpressionNode rhs) {                                         \
    return operator op(ExpressionNode(lhs), rhs);                                                                     \
  }                                                                                                                   \
  inline ExpressionNode operator op(VarNode const& lhs, VarNode const& rhs) {                                         \
    return operator op(ExpressionNode(lhs), ExpressionNode(rhs));                                                     \
  }                                                                                                                   \
  inline ExpressionNode operator op(ExpressionNode lhs, double rhs) { return operator op(lhs, ExpressionNode(rhs)); } \
  inline ExpressionNode operator op(double lhs, ExpressionNode rhs) { return operator op(ExpressionNode(lhs), rhs); } \
  inline ExpressionNode operator op(VarNode const& lhs, double rhs) {                                                 \
    return operator op(ExpressionNode(lhs), ExpressionNode(rhs));                                                     \
  }                                                                                                                   \
  inline ExpressionNode operator op(double lhs, VarNode const& rhs) {                                                 \
    return operator op(ExpressionNode(lhs), ExpressionNode(rhs));                                                     \
  }                                                                                                                   \
  inline ExpressionNode const& operator op2(ExpressionNode& lhs, ExpressionNode rhs) {                                \
    lhs = lhs op rhs;                                                                                                 \
    return lhs;                                                                                                       \
  }                                                                                                                   \
  inline ExpressionNode const& operator op2(ExpressionNode& lhs, VarNode const& rhs) {                                \
    lhs = lhs op rhs;                                                                                                 \
    return lhs;                                                                                                       \
  }                                                                                                                   \
  inline ExpressionNode const& operator op2(ExpressionNode& lhs, double rhs) {                                        \
    lhs = lhs op rhs;                                                                                                 \
    return lhs;                                                                                                       \
  }
#include "../math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION

inline ExpressionNode operator+(ExpressionNode op) { return op; }
inline ExpressionNode operator+(VarNode const& op) { return op; }
inline ExpressionNode operator-(ExpressionNode op) { return (0.0 - op); }
inline ExpressionNode operator-(VarNode const& op) { return (0.0 - op); }

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
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)                                                                \
  inline ExpressionNode fn(ExpressionNode argument) {                                                       \
    if (argument.IsImmediateDouble()) {                                                                     \
      return fn(argument.GetImmediateDouble());                                                             \
    } else {                                                                                                \
      return ExpressionNode::FromNodeIndex(VarsManager::TLS().Active().EmplaceExpressionNode(               \
          ExpressionNodeTypeSelector<ExpressionNodeType::Function_##fn>(), ExpressionNodeIndex(argument))); \
    }                                                                                                       \
  }                                                                                                         \
  inline ExpressionNode fn(VarNode const& argument) { return fn(ExpressionNode(argument)); }
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
}  // namespace current::expression::functions

// When `using namespace current::expression`:
// 1) can use `value_t x = ...` instead of `auto x = ...`.
// 2) can use math functions, such as `exp()` or `sqr()` or `sigmoid()`, w/o specifying the namespace.
using value_t = ExpressionNode;
using namespace current::expression::functions;

// A helper method to generate a one-dimensional function from a multi-dimensional one,
// by replacing each occurrence of each variable by a respective provided formula.
// The usecase of this method is the construction of the "cost function" for line search optimization.
class Build1DFunctionImpl {
 private:
  VarsContext const& vars_context_;
  std::vector<value_t> const& substitute_;

 public:
  Build1DFunctionImpl(VarsContext const& vars_context,
                      VarsMapperConfig const& vars_config,
                      std::vector<value_t> const& substitute)
      : vars_context_(vars_context), substitute_(substitute) {
    // TODO(dkorolev): A stricter check that the config matches the context?
    if (vars_context_.NumberOfVars() != vars_config.name.size()) {
      CURRENT_THROW(Build1DFunctionRequiresUpToDateVarsIndexes());
    }
    if (substitute_.size() != vars_context_.NumberOfVars()) {
      CURRENT_THROW(Build1DFunctionNumberOfVarsMismatchException());
    }
  }

  value_t DoBuild1DFunction(value_t f) const {
    return ExpressionNodeIndex(f).template Dispatch<value_t>(
        [&](size_t node_index) -> value_t {
          ExpressionNodeImpl const& node = vars_context_[node_index];
          ExpressionNodeType const type = node.Type();
          if (type == ExpressionNodeType::Uninitialized) {
            CURRENT_THROW(ExpressionNodeInternalError());
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)   \
  }                                                        \
  else if (type == ExpressionNodeType::Operation_##name) { \
    return DoBuild1DFunction(ExpressionNode(node.LHSIndex())) op DoBuild1DFunction(ExpressionNode(node.RHSIndex()));
#include "../math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)            \
  }                                                     \
  else if (type == ExpressionNodeType::Function_##fn) { \
    return fn(DoBuild1DFunction(ExpressionNode(node.ArgumentIndex())));
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
          } else {
            CURRENT_THROW(ExpressionNodeInternalError());
          }
        },
        [&](size_t var_index) -> value_t { return substitute_[var_index]; },
        [&](double) -> value_t { return f; },
        [&]() -> value_t {
          // No `lambda` support here. `DoBuild1DFunction` introduces `lambda`-s, not uses them.
          CURRENT_THROW(ExpressionNodeInternalError());
        });
  }
};
inline value_t Build1DFunction(value_t f, VarsMapperConfig const& config, std::vector<value_t> const& substitute) {
  return Build1DFunctionImpl(VarsManager::TLS().Active(), config, substitute).DoBuild1DFunction(f);
}

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_EXPRESSION_EXPRESSION_H
