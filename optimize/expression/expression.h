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

#include <cmath>

namespace current {
namespace expression {

struct ExpressionVarNodeBoxingException final : OptimizeException {};

struct ExpressionNodeInternalError final : OptimizeException {};

struct Build1DFunctionRequiresUpToDateVarsIndexes final : OptimizeException {};
struct Build1DFunctionNumberOfVarsMismatchException final : OptimizeException {};

class ExpressionNode final {
 private:
  expression_node_index_t index_;  // Copyable and assignable; `ExpressionNode` is a lightweight object.

  static expression_node_index_t IndexFromVarNodeOrThrow(VarNode const& var_node) {
    if (var_node.type == VarNodeType::Value) {
      // When the most significant bit is set, the node is just a reference to certain index in the input vector.
      return ~static_cast<expression_node_index_t>(var_node.InternalVarIndex());
    } else {
      CURRENT_THROW(ExpressionVarNodeBoxingException());
    }
  }

  struct ConstructFromIndex {};

 public:
  ExpressionNode(ConstructFromIndex, expression_node_index_t index) : index_(index) {}
  ExpressionNode(VarNode const& var_node) : index_(IndexFromVarNodeOrThrow(var_node)) {}

  static ExpressionNode FromIndex(expression_node_index_t index) { return ExpressionNode(ConstructFromIndex(), index); }
  static ExpressionNode FromIndex(ExpressionNodeIndex index) {
    return ExpressionNode(ConstructFromIndex(), static_cast<expression_node_index_t>(index));
  }
  static ExpressionNode FromImmediateDouble(double x) {
    return ExpressionNode(ConstructFromIndex(),
                          VarsManager::TLS().Active().EmplaceExpressionNode(
                              ExpressionNodeTypeSelector<ExpressionNodeType::ImmediateDouble>(), x));
  }
  static ExpressionNode lambda() {
    return ExpressionNode(
        ConstructFromIndex(),
        VarsManager::TLS().Active().EmplaceExpressionNode(ExpressionNodeTypeSelector<ExpressionNodeType::Lambda>()));
  }
  static ExpressionNode ConstructDefaultExpressionNode() {
    return ExpressionNode(ConstructFromIndex(), static_cast<expression_node_index_t>(-1));
  }

  operator ExpressionNodeIndex() const { return ExpressionNodeIndex(index_); }

  std::string DebugAsString() const {
    VarsContext const& vars_context = VarsManager::TLS().Active();
    if (~index_ < index_) {
      return vars_context.VarNameByOriginalIndex(~index_);
    } else {
      ExpressionNodeImpl const& node = vars_context[index_];
      if (node.type_ == ExpressionNodeType::Uninitialized) {
        return "<Uninitialized>";
      } else if (node.type_ == ExpressionNodeType::ImmediateDouble) {
        if (node.value_ >= 0) {
          return current::ToString(node.value_);
        } else {
          return "(" + current::ToString(node.value_) + ')';
        }
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)                      \
  }                                                                           \
  else if (node.type_ == ExpressionNodeType::Operation_##name) {              \
    return "(" + ExpressionNode::FromIndex(node.lhs_).DebugAsString() + #op + \
           ExpressionNode::FromIndex(node.rhs_).DebugAsString() + ')';
#include "../math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)                  \
  }                                                           \
  else if (node.type_ == ExpressionNodeType::Function_##fn) { \
    return #fn "(" + ExpressionNode::FromIndex(node.lhs_).DebugAsString() + ')';
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
      } else if (node.type_ == ExpressionNodeType::Lambda) {
        return "lambda";
      } else {
        CURRENT_THROW(ExpressionNodeInternalError());
      }
    }
  }
};
static_assert(sizeof(ExpressionNode) == 8u, "The `ExpressionNode` type is meant to be ultra-lightweight.");

// NOTE(dkorolev): Contrary to the C++ convention of returning `Value&` from `operator+=` and the like,
//                 returning the `Value const&` by design, to prevent various `a += b += c;` madness.
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)                                \
  inline ExpressionNode operator op(ExpressionNode lhs, ExpressionNode rhs) {           \
    return ExpressionNode::FromIndex(VarsManager::TLS().Active().EmplaceExpressionNode( \
        ExpressionNodeTypeSelector<ExpressionNodeType::Operation_##name>(),             \
        ExpressionNodeIndex(lhs),                                                       \
        ExpressionNodeIndex(rhs)));                                                     \
  }                                                                                     \
  inline ExpressionNode operator op(ExpressionNode lhs, VarNode const& rhs) {           \
    return operator op(lhs, ExpressionNode(rhs));                                       \
  }                                                                                     \
  inline ExpressionNode operator op(VarNode const& lhs, ExpressionNode rhs) {           \
    return operator op(ExpressionNode(lhs), rhs);                                       \
  }                                                                                     \
  inline ExpressionNode operator op(VarNode const& lhs, VarNode const& rhs) {           \
    return operator op(ExpressionNode(lhs), ExpressionNode(rhs));                       \
  }                                                                                     \
  inline ExpressionNode operator op(ExpressionNode lhs, double rhs) {                   \
    return operator op(lhs, ExpressionNode::FromImmediateDouble(rhs));                  \
  }                                                                                     \
  inline ExpressionNode operator op(double lhs, ExpressionNode rhs) {                   \
    return operator op(ExpressionNode::FromImmediateDouble(lhs), rhs);                  \
  }                                                                                     \
  inline ExpressionNode operator op(VarNode const& lhs, double rhs) {                   \
    return operator op(ExpressionNode(lhs), ExpressionNode::FromImmediateDouble(rhs));  \
  }                                                                                     \
  inline ExpressionNode operator op(double lhs, VarNode const& rhs) {                   \
    return operator op(ExpressionNode::FromImmediateDouble(lhs), ExpressionNode(rhs));  \
  }                                                                                     \
  inline ExpressionNode const& operator op2(ExpressionNode& lhs, ExpressionNode rhs) {  \
    lhs = lhs op rhs;                                                                   \
    return lhs;                                                                         \
  }                                                                                     \
  inline ExpressionNode const& operator op2(ExpressionNode& lhs, VarNode const& rhs) {  \
    lhs = lhs op rhs;                                                                   \
    return lhs;                                                                         \
  }                                                                                     \
  inline ExpressionNode const& operator op2(ExpressionNode& lhs, double rhs) {          \
    lhs = lhs op rhs;                                                                   \
    return lhs;                                                                         \
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
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)                                                              \
  inline ExpressionNode fn(ExpressionNode argument) {                                                     \
    return ExpressionNode::FromIndex(VarsManager::TLS().Active().EmplaceExpressionNode(                   \
        ExpressionNodeTypeSelector<ExpressionNodeType::Function_##fn>(), ExpressionNodeIndex(argument))); \
  }                                                                                                       \
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
    expression_node_index_t const index = static_cast<expression_node_index_t>(ExpressionNodeIndex(f));
    if (~index < index) {
      return substitute_[~index];
    } else {
      ExpressionNodeImpl const& node = vars_context_[index];
      if (node.type_ == ExpressionNodeType::Uninitialized) {
        CURRENT_THROW(ExpressionNodeInternalError());
      } else if (node.type_ == ExpressionNodeType::ImmediateDouble) {
        return f;
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)           \
  }                                                                \
  else if (node.type_ == ExpressionNodeType::Operation_##name) {   \
    return DoBuild1DFunction(ExpressionNode::FromIndex(node.lhs_)) \
        op DoBuild1DFunction(ExpressionNode::FromIndex(node.rhs_));
#include "../math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)                  \
  }                                                           \
  else if (node.type_ == ExpressionNodeType::Function_##fn) { \
    return fn(DoBuild1DFunction(ExpressionNode::FromIndex(node.lhs_)));
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
      } else if (node.type_ == ExpressionNodeType::Lambda) {
        CURRENT_THROW(ExpressionNodeInternalError());
      } else {
        CURRENT_THROW(ExpressionNodeInternalError());
      }
    }
  }
};
inline value_t Build1DFunction(value_t f, VarsMapperConfig const& config, std::vector<value_t> const& substitute) {
  return Build1DFunctionImpl(VarsManager::TLS().Active(), config, substitute).DoBuild1DFunction(f);
}

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_EXPRESSION_EXPRESSION_H
