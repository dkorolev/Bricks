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

#ifndef OPTIMIZE_BASE_H
#define OPTIMIZE_BASE_H

#include "double.h"

#include "../bricks/exception.h"
#include "../bricks/strings/util.h"
#include "../port.h"

namespace current {

struct OptimizeException : Exception {
  using Exception::Exception;
};

namespace expression {

// The universe of { indexes of nodes, indexes of vars, lambda } is the whole seven bytes.
// Of them, the MSB is the "is lambda" bit, and the next bit is the "is node or var index" bit.
constexpr static uint64_t kBitLambda = (1ull << 55);
constexpr static uint64_t kBitCompactIndexIsVar = (1ull << 54);

// If the lambda bit is set, the rest should be all zeroes, hence "plus one".
constexpr static uint64_t kFirstIllegalIndexRepresentingNodeOrVarOrLambda = kBitLambda + 1ull;

// The first illegal-to-represent actual node index or actual var index is 2^54.
constexpr static uint64_t kFirstIllegalNodeOrVarIndex = kBitCompactIndexIsVar;
static_assert(kFirstIllegalNodeOrVarIndex - 1ull == 0x3fffffffffffffull,
              "Math is off on this architecture, glad we checked.");

// The "special" bit is the MSB.
// It is used for manual stack in the "recursive" calls to differentiate the node or to JIT-compile it.
constexpr static uint64_t kBitSpecial1 = (1ull << 63);
constexpr static uint64_t kBitSpecial2 = (1ull << 62);
constexpr static uint64_t kBitSpecial1OrSpecial2 = kBitSpecial1 | kBitSpecial2;

// The "this is actually a double value" bit is the 3rd most significant bit, `(1ull << 61)`.
// TL;DR: `2^63` double values, which are the vast, vast majority of what is ever used in ML,
// are packed into the very 8-byte `ExpressionNodeIndex` to save on RAM for nodes. See `double.h` for more details.
constexpr static uint64_t kBitDouble = (1ull << 61);

#ifndef NDEBUG
// In `!NDEBUG` mode a value is also reserved to indicate that the index was not initialized.
constexpr static uint64_t kCompactifiedIndexValueUninitialized = 0x55555555deadbeef;
#endif

// Some immediate `uint64_t` values for compactified indexes for the constants.
constexpr static const uint64_t kExpressionNodeIndexForDoubleZero = 0x2000000000000000;
constexpr static const uint64_t kExpressionNodeIndexForDoubleNegativeZero = 0xa000000000000000;
constexpr static const uint64_t kExpressionNodeIndexForDoubleOne = 0x3ff0000000000000;

// A dedicated type for the index of a user-defined variable in the variables vector.
enum class RawVarIndex : size_t {};

// The data type for the expression should be defined in this `base.h` header, as the thread-local context
// for expression management is the same as the thread-local context for variables management.
//
// There is no expression node type for variables or constants in `ExpressionNodeImpl`, as this class only holds
// the information about what is being stored in the thread-local singleton of the expression nodes vector.
// Variables and constants are just references to certain values, and they do not require dedicated expression nodes.

class ExpressionNodeIndex {
 private:
  // Expression nodes indexes:
  // - Can store 2^63 different double values, which are all we need for optimization purposes.
  // - Have a "special" bit dedicated to them, for "manual" "recursion" stack tracking. It's the very MSB.
  // - Have a special "lambda" bit dedicated to them, to save space in the expression tree RAM.
  // - When not the above, can be an expression node index or an expression var index. The flag for whether an "index"
  //   is a node index or a var index is (1ull << 54), the 2nd most significant bit of the 2nd most significant byte.
  // - All the above fits 8 bytes, and that's it for the size of `ExpressionNodeIndex`.
  uint64_t compactified_index_;

  friend class ExpressionNodeImpl;
  uint64_t RawCompactifiedIndex() const { return compactified_index_; }
  static ExpressionNodeIndex FromRawAlreadyCompactifiedIndex(uint64_t compactified_index) {
    ExpressionNodeIndex result;
    result.compactified_index_ = compactified_index;
    return result;
  }

 public:
#ifndef NDEBUG
  ExpressionNodeIndex() : compactified_index_(kCompactifiedIndexValueUninitialized) {}
  bool IsUninitialized() const { return compactified_index_ == kCompactifiedIndexValueUninitialized; }
#else
  ExpressionNodeIndex() = default;
#endif
  ExpressionNodeIndex(ExpressionNodeIndex const&) = default;
  ExpressionNodeIndex(ExpressionNodeIndex&&) = default;
  ExpressionNodeIndex& operator=(ExpressionNodeIndex const&) = default;
  ExpressionNodeIndex& operator=(ExpressionNodeIndex&&) = default;

  ExpressionNodeIndex(RawVarIndex var_index) {
#ifndef NDEBUG
    if (!(static_cast<size_t>(var_index) < kFirstIllegalNodeOrVarIndex)) {
      TriggerSegmentationFault();
    }
#endif
    compactified_index_ = static_cast<uint64_t>(var_index) | kBitCompactIndexIsVar;
  }

  struct ConstructDoubleZero {};
  struct ConstructDoubleOne {};
  ExpressionNodeIndex(ConstructDoubleZero) : compactified_index_(kExpressionNodeIndexForDoubleZero) {}
  ExpressionNodeIndex(ConstructDoubleOne) : compactified_index_(kExpressionNodeIndexForDoubleOne) {}
  bool IsIndexDoubleZero() const {
    return compactified_index_ == kExpressionNodeIndexForDoubleZero ||
           compactified_index_ == kExpressionNodeIndexForDoubleNegativeZero;
  }
  bool IsIndexDoubleOne() const { return compactified_index_ == kExpressionNodeIndexForDoubleOne; }
  static ExpressionNodeIndex DoubleZero() { return ExpressionNodeIndex(ConstructDoubleZero()); }
  static ExpressionNodeIndex DoubleOne() { return ExpressionNodeIndex(ConstructDoubleOne()); }

  static ExpressionNodeIndex FromNodeIndex(size_t node_index) {
#ifndef NDEBUG
    if (!(node_index < kFirstIllegalNodeOrVarIndex)) {
      TriggerSegmentationFault();
    }
#endif
    return FromRawAlreadyCompactifiedIndex(static_cast<uint64_t>(node_index));
  }

  static ExpressionNodeIndex FromRegularDouble(double x) {
#ifndef NDEBUG
    if (!IsRegularDouble(x)) {
      TriggerSegmentationFault();
    }
#endif
    return FromRawAlreadyCompactifiedIndex(PackDouble(x));
  }

  static ExpressionNodeIndex LambdaNodeIndex() { return FromRawAlreadyCompactifiedIndex(kBitLambda); }

  bool RawCompactifiedIndexEquals(uint64_t value) const { return compactified_index_ == value; }

  void SetSpecialTwoBitsValue(uint64_t v) {
#ifndef NDEBUG
    if (!(v < 4)) {
      TriggerSegmentationFault();
    }
#endif
    compactified_index_ |= (v << 62);
  }
  uint64_t GetSpecialTwoBitsValue() const { return compactified_index_ >> 62; }
  uint64_t ClearSpecialTwoBitsAndReturnWhatTheyWere() {
    uint64_t const result = compactified_index_ >> 62;
    compactified_index_ &= ((1ull << 62) - 1);
    return result;
  }

  // No cast into `bool` for performance reasons. -- D.K.
  uint64_t IsIndexImmediateDouble() const { return (compactified_index_ & kBitDouble); }
  double GetImmediateDoubleFromIndex() const {
#ifndef NDEBUG
    if (!IsIndexImmediateDouble()) {
      TriggerSegmentationFault();
    }
#endif
    return UnpackDouble(compactified_index_);
  }

  // The `Unchecked*` methods assume the user knows what they are doing.
  // If in doubt, use the `CheckedDispatch` first.
  uint64_t UncheckedIsIndexLambda() const { return (compactified_index_ & kBitLambda); }
  uint64_t UncheckedIsIndexVarIndex() const { return (compactified_index_ & kBitCompactIndexIsVar); }
  bool UncheckedIsSpecificallyNodeIndex() const {
    return (compactified_index_ & (kBitDouble | kBitLambda | kBitCompactIndexIsVar)) == 0ull;
  }
  uint64_t UncheckedVarIndex() const { return compactified_index_ ^ kBitCompactIndexIsVar; }
  uint64_t UncheckedNodeIndex() const { return compactified_index_; }

  // NOTE(dkorolev): This is a *temporary* (~5% slower) solution implemented to make sure
  // I don't forget to handle all the corner cases as the number of them grows larger than two.
  template <typename T_RETVAL = void, class F_NODE, class F_VAR, class F_DOUBLE, class F_LAMBDA>
  T_RETVAL CheckedDispatch(F_NODE&& f_node, F_VAR&& f_var, F_DOUBLE&& f_double, F_LAMBDA&& f_lambda) const {
#ifndef NDEBUG
    if (IsUninitialized()) {
      TriggerSegmentationFault();
    }
#endif
    if (compactified_index_ & kBitDouble) {
#ifndef NDEBUG
      if (!IsUInt64PackedDouble(compactified_index_)) {
        // NOTE(dkorolev): This check will always pass unless the code in `double.h` is altered substantially.
        TriggerSegmentationFault();
      }
#endif
      return f_double(UnpackDouble(compactified_index_));
    } else {
#ifndef NDEBUG
      // Important: The "special" bits are "allowed" to be set in case of `double` values.
      if (IsUninitialized()) {
        TriggerSegmentationFault();
      }
      if (compactified_index_ & kBitSpecial1OrSpecial2) {
        TriggerSegmentationFault();
      }
#endif
      if (compactified_index_ & kBitLambda) {
        return f_lambda();
      } else if (compactified_index_ & kBitCompactIndexIsVar) {
        uint64_t const var_index = compactified_index_ ^ kBitCompactIndexIsVar;
#ifndef NDEBUG
        if (!(var_index < kFirstIllegalNodeOrVarIndex)) {
          TriggerSegmentationFault();
        }
#endif
        return f_var(static_cast<size_t>(var_index));
      } else {
#ifndef NDEBUG
        if (!(compactified_index_ < kFirstIllegalNodeOrVarIndex)) {
          TriggerSegmentationFault();
        }
#endif
        return f_node(static_cast<size_t>(compactified_index_));
      }
    }
  }

  std::string IndexDebugAsString() const {
    return CheckedDispatch<std::string>([](size_t node_index) { return "z[" + current::ToString(node_index) + "]"; },
                                        [](size_t var_index) { return "x{" + current::ToString(var_index) + "}"; },
                                        [](double x) { return "(" + current::ToString(x) + ")"; },
                                        []() { return "lambda"; });
  }

  // NOTE(dkorolev): The below section is now "commented out" to make sure I don't miss out on handling all the
  // corner cases as the "node index" can also be a `lambda` and an encoded `double` value. Once just the simple
  // check of `if (x.IsNodeIndex()) { ... } else { ... }` is no longer correct, I'd rather have the compiler
  // highlight all the usecases of this potentially and likely erroneus construct.
  // NOTE(dkorolev): Do not cast to bool for performance reasons.
  uint64_t UnitTestIsVarIndex() const { return compactified_index_ & kBitCompactIndexIsVar; }
  bool UnitTestIsNodeIndex() const { return !UnitTestIsVarIndex(); }
  size_t UnitTestNodeIndex() const {
#ifndef NDEBUG
    if (!UnitTestIsNodeIndex()) {
      TriggerSegmentationFault();
    }
#endif
    size_t const node_index = static_cast<size_t>(compactified_index_);
#ifndef NDEBUG
    if (!(node_index < kFirstIllegalNodeOrVarIndex)) {
      TriggerSegmentationFault();
    }
#endif
    return static_cast<size_t>(node_index);
  }
  size_t UnitTestVarIndex() const {
#ifndef NDEBUG
    if (UnitTestIsNodeIndex()) {
      TriggerSegmentationFault();
    }
#endif
    size_t const var_index = static_cast<size_t>(compactified_index_ ^ kBitCompactIndexIsVar);
#ifndef NDEBUG
    if (!(var_index < kFirstIllegalNodeOrVarIndex)) {
      TriggerSegmentationFault();
    }
#endif
    return var_index;
  }

  uint64_t UnitTestRawCompactifiedIndex() const { return RawCompactifiedIndex(); }
};
static_assert(sizeof(ExpressionNodeIndex) == 8, "`ExpressionNodeIndex` should be 8 bytes.");

enum class ExpressionNodeType {
#ifndef NDEBUG
  UninitializedNodeType,
#endif

  MarkerOperationsBeginAfterThisIndex,
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name) Operation_##name,
#include "math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION
  MarkerOperationsEndedBeforeThisIndex,

  MarkerFunctionsBeginAfterThisIndex,
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn) Function_##fn,
#include "math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
  MarkerFunctionsEndedBeforeThisIndex,

  Total
};
static_assert(static_cast<size_t>(ExpressionNodeType::Total) <= (1 << 6), "`ExpressionNodeType` should fit 6 bits.");

inline bool IsOperationNode(ExpressionNodeType type) {
  return type > ExpressionNodeType::MarkerOperationsBeginAfterThisIndex &&
         type < ExpressionNodeType::MarkerOperationsEndedBeforeThisIndex;
}

inline bool IsFunctionNode(ExpressionNodeType type) {
  return type > ExpressionNodeType::MarkerFunctionsBeginAfterThisIndex &&
         type < ExpressionNodeType::MarkerFunctionsEndedBeforeThisIndex;
}

enum class ExpressionFunctionIndex {
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn) FunctionIndexOf_##fn,
#include "math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
  TotalFunctionsCount
};

template <ExpressionNodeType>
struct ExpressionNodeTypeSelector {};

// The class `ExpressionNodeImpl` is a thin wrapper of what is stored in the thread-local context. The actual
// expression buliding, including managing the fields of this very class, is implemented in class `ExpressionNode`,
// which is defined in `expression/expression.h` and tested in `expression/test.cc`.
class ExpressionNodeImpl final {
 private:
  uint8_t compact_type_ : 6;

  // The logic behind this possible "flipping" is simple:
  // * Node, or, rather, the 8-byte `ExpressionNodeIndex`-es, can contain immediate double values instead of indexes.
  // * Unlike 8-byte node indexes, that only need seven bytes, immediate double values need the whopping 64 bits.
  // * Since `ExpressionNodeImpl` should fit everything into 16 bytes, one of its {lhs, rhs} operands is 7 bytes.
  // * However, there can be no math operation that takes two doubles as the input -- it will be computed right away.
  // * Still, since the order of the operands does matter, and that one operand that is a double value can be either the
  //   left hand side one or the right hand side one, of the first bit-packed byte one bit holds the "flipped" flag.
  // * It makes more sense to "declare" the "secondary" index first, as it's the one chopped to seven bytes.
  //   For functions (i.e., `exp`), not operators (i.e. `+`), it is easier to retrieve the index of its argument.
  bool compact_flipped_ : 1;
  uint64_t compact_secondary_index_ : (8 * 7);
  uint64_t compact_primary_index_;

  void InitArgument(ExpressionNodeIndex argument) { compact_primary_index_ = argument.RawCompactifiedIndex(); }

  friend class NodesCluster;  // For expression tree balancing, see `tree_balancer/tree_balancer.h`.
  void InitLHSRHS(ExpressionNodeIndex lhs, ExpressionNodeIndex rhs) {
    if (!rhs.IsIndexImmediateDouble()) {
      compact_primary_index_ = lhs.RawCompactifiedIndex();
      compact_secondary_index_ = rhs.RawCompactifiedIndex();
      compact_flipped_ = false;
    } else {
#ifndef NDEBUG
      if (lhs.IsIndexImmediateDouble()) {
        // Can't have both LHS and RHS nodes as immediate doubles.
        TriggerSegmentationFault();
      }
#endif
      compact_secondary_index_ = lhs.RawCompactifiedIndex();
      compact_primary_index_ = rhs.RawCompactifiedIndex();
      compact_flipped_ = true;
    }
  }

 public:
#ifndef NDEBUG
  ExpressionNodeImpl() : compact_type_(static_cast<uint8_t>(ExpressionNodeType::UninitializedNodeType)) {}
#else
  ExpressionNodeImpl() = default;
#endif

#ifndef NDEBUG
  void AssertValid() const {
    if (static_cast<ExpressionNodeType>(compact_type_) == ExpressionNodeType::UninitializedNodeType) {
      TriggerSegmentationFault();
    }
  }
#endif

  ExpressionNodeType Type() const {
#ifndef NDEBUG
    AssertValid();
#endif
    return static_cast<ExpressionNodeType>(compact_type_);
  }
  ExpressionNodeIndex ArgumentIndex() const {
#ifndef NDEBUG
    AssertValid();
    if (!IsFunctionNode(Type())) {
      TriggerSegmentationFault();
    }
#endif
    return ExpressionNodeIndex::FromRawAlreadyCompactifiedIndex(compact_primary_index_);
  }
  ExpressionNodeIndex LHSIndex() const {
#ifndef NDEBUG
    AssertValid();
    if (!IsOperationNode(Type())) {
      TriggerSegmentationFault();
    }
#endif
    return ExpressionNodeIndex::FromRawAlreadyCompactifiedIndex(compact_flipped_ ? compact_secondary_index_
                                                                                 : compact_primary_index_);
  }
  ExpressionNodeIndex RHSIndex() const {
#ifndef NDEBUG
    AssertValid();
    if (!IsOperationNode(Type())) {
      TriggerSegmentationFault();
    }
#endif
    return ExpressionNodeIndex::FromRawAlreadyCompactifiedIndex(compact_flipped_ ? compact_primary_index_
                                                                                 : compact_secondary_index_);
  }

  std::string NodeDebugAsString() const {
#ifndef NDEBUG
    AssertValid();
#endif
    ExpressionNodeType const type = Type();
#ifndef NDEBUG
    if (type == ExpressionNodeType::UninitializedNodeType) {
      return "UnititializedNode";
#else
    if (false) {
#endif

#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)   \
  }                                                        \
  else if (type == ExpressionNodeType::Operation_##name) { \
    return "`" #op "` " + LHSIndex().IndexDebugAsString() + ' ' + RHSIndex().IndexDebugAsString();
#include "math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)            \
  }                                                     \
  else if (type == ExpressionNodeType::Function_##fn) { \
    return #fn " " + ArgumentIndex().IndexDebugAsString();
#include "math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
    } else {
      return "InternalError";
    }
  }

#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)                               \
  ExpressionNodeImpl(ExpressionNodeTypeSelector<ExpressionNodeType::Operation_##name>, \
                     ExpressionNodeIndex lhs,                                          \
                     ExpressionNodeIndex rhs)                                          \
      : compact_type_(static_cast<uint8_t>(ExpressionNodeType::Operation_##name)) {    \
    InitLHSRHS(lhs, rhs);                                                              \
  }
#include "math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION

#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)                                                                      \
  ExpressionNodeImpl(ExpressionNodeTypeSelector<ExpressionNodeType::Function_##fn>, ExpressionNodeIndex argument) \
      : compact_type_(static_cast<uint8_t>(ExpressionNodeType::Function_##fn)) {                                  \
    InitArgument(argument);                                                                                       \
  }
#include "math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
};
static_assert(sizeof(ExpressionNodeImpl) == 16, "`ExpressionNodeImpl` should be 16 bytes.");

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_BASE_H
