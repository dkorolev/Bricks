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

#include "../bricks/exception.h"
#include "../port.h"

namespace current {

struct OptimizeException : Exception {
  using Exception::Exception;
};

namespace expression {

// The universe of indexes of both nodes and vars is the whole seven bytes.
constexpr static uint64_t kFirstIllegalIndexRepresentingNodeOrVar = (1ull << 56);

// Of these seven bytes, the MSB is the `is index the var index` flag.
constexpr static uint64_t kBitCompactIndexIsVar = (1ull << 55);

// The first illegal-to-represent actual node index or actual var index is 2^55.
constexpr static uint64_t kFirstIllegalNodeOrVarIndex = kBitCompactIndexIsVar;
static_assert(kFirstIllegalNodeOrVarIndex - 1ull == 0x7fffffffffffffull,
              "Math is off on this architecture, glad we checked.");

// The "special" bit is the MSB.
// It is used for manual stack in the "recursive" calls to differentiate the node or to JIT-compile it.
constexpr static uint64_t kBitSpecial = (1ull << 63);

// The data type for the expression should be defined in this `base.h` header, as the thread-local context
// for expression management is the same as the thread-local context for variables management.
//
// There is no expression node type for variables or constants in `ExpressionNodeImpl`, as this class only holds
// the information about what is being stored in the thread-local singleton of the expression nodes vector.
// Variables and constants are just references to certain values, and they do not require dedicated expression nodes.

class ExpressionNodeIndex {
 private:
  // Expression nodes indexes:
  // - They can be an expression node index or an expression var index. The flag for whether an `ExpressionNodeIndex`
  //   is a node index or a var index is (1ull << 55), the most significnat bit of the 2nd most significant byte.
  // - They have a "special" bit dedicated to them, for "manual" "recursion" stack tracking. It's the very MSB.
  // - [TBD]: They have a "lambda" bit dedicated to them, to save space in the expression tree RAM. It's the 2nd MSB.
  // - [TBD]: They can store _some_ double values (actually, 2^63 double values)!
  // - They are 8 bytes large, and that's it.
  //   TODO(dkorolev): Implmenet the TBDs.
  uint64_t compactified_index_;

  friend class ExpressionNodeImpl;
  uint64_t RawCompactifiedIndex() const { return compactified_index_; }
  static ExpressionNodeIndex FromRawAlreadyCompactifiedIndex(uint64_t compactified_index) {
    ExpressionNodeIndex result;
    result.compactified_index_ = compactified_index;
    return result;
  }

 public:
  ExpressionNodeIndex() = default;
  ExpressionNodeIndex(ExpressionNodeIndex const&) = default;
  ExpressionNodeIndex(ExpressionNodeIndex&&) = default;
  ExpressionNodeIndex& operator=(ExpressionNodeIndex const&) = default;
  ExpressionNodeIndex& operator=(ExpressionNodeIndex&&) = default;

  static ExpressionNodeIndex FromNodeIndex(size_t node_index) {
#ifndef NDEBUG
    if (!(node_index < kFirstIllegalNodeOrVarIndex)) {
      CURRENT_THROW(OptimizeException("Internal error."));
    }
#endif
    return FromRawAlreadyCompactifiedIndex(static_cast<uint64_t>(node_index));
  }

  static ExpressionNodeIndex FromVarIndex(size_t var_index) {
#ifndef NDEBUG
    if (!(var_index < kFirstIllegalNodeOrVarIndex)) {
      CURRENT_THROW(OptimizeException("Internal error."));
    }
#endif
    return FromRawAlreadyCompactifiedIndex(static_cast<uint64_t>(var_index) | kBitCompactIndexIsVar);
  }

  void SetSpecialBit() { compactified_index_ |= kBitSpecial; }
  bool ClearSpecialBitAndReturnWhatItWas() {
    if (compactified_index_ & kBitSpecial) {
      compactified_index_ ^= kBitSpecial;
      return true;
    } else {
      return false;
    }
  }

  // NOTE(dkorolev): This is a *temporary* (~5% slower) solution implemented to make sure
  // I don't forget to handle all the corner cases as the number of them grows larger than two.
  template <typename T_RETVAL = void, typename F_NODE, typename F_VAR>
  T_RETVAL Dispatch(F_NODE&& f_node, F_VAR&& f_var) const {
    if (compactified_index_ & kBitCompactIndexIsVar) {
      uint64_t const var_index = compactified_index_ ^ kBitCompactIndexIsVar;
#ifndef NDEBUG
      if (!(var_index < kFirstIllegalNodeOrVarIndex)) {
        CURRENT_THROW(OptimizeException("Internal error."));
      }
#endif
      return f_var(var_index);
    } else {
#ifndef NDEBUG
      if (!(compactified_index_ < kFirstIllegalNodeOrVarIndex)) {
        CURRENT_THROW(OptimizeException("Internal error."));
      }
#endif
      return f_node(compactified_index_);
    }
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
      CURRENT_THROW(OptimizeException("Internal error."));
    }
#endif
    size_t const node_index = static_cast<size_t>(compactified_index_);
#ifndef NDEBUG
    if (!(node_index < kFirstIllegalNodeOrVarIndex)) {
      CURRENT_THROW(OptimizeException("Internal error."));
    }
#endif
    return static_cast<size_t>(node_index);
  }
  size_t UnitTestVarIndex() const {
#ifndef NDEBUG
    if (UnitTestIsNodeIndex()) {
      CURRENT_THROW(OptimizeException("Internal error."));
    }
#endif
    size_t const var_index = static_cast<size_t>(compactified_index_ ^ kBitCompactIndexIsVar);
#ifndef NDEBUG
    if (!(var_index < kFirstIllegalNodeOrVarIndex)) {
      CURRENT_THROW(OptimizeException("Internal error."));
    }
#endif
    return var_index;
  }
};
static_assert(sizeof(ExpressionNodeIndex) == 8, "`ExpressionNodeIndex` should be 8 bytes.");

enum class ExpressionNodeType {
  Uninitialized,
  ImmediateDouble,
  Lambda,

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
namespace jit {
class JITCompiler;
}  // namespace current::expression::jit
class ExpressionNodeImpl final {
 private:
  // To manage the below fields.
  friend class ExpressionNode;

  // To use the below fields.
  friend class jit::JITCompiler;
  friend class Differentiator;
  friend class Build1DFunctionImpl;

  uint8_t compact_type_ : 6;
  bool compact_has_double_ : 1;
  bool compact_flipped_ : 1;
  uint64_t compact_secondary_index_ : (8 * 7);  // `lhs` or `lhs` for math operations, depending on `compact_flipped_`.

  union {
    uint64_t compact_primary_index_;  // `rhs` or `lhs` for math operations, depending on `compact_flipped_`.
    double compact_double_;           // the double value for math operations depending on `compact_has_double_`.
  };

  // "Encode" the index into seven bytes, even though the representation is a mere copy after the refactoring.
  static uint64_t EncodeIndex(ExpressionNodeIndex original_index) {
#ifndef NDEBUG
    if (!(original_index.RawCompactifiedIndex() < kFirstIllegalIndexRepresentingNodeOrVar)) {
      CURRENT_THROW(OptimizeException("Internal error."));
    }
#endif
    return original_index.RawCompactifiedIndex();
  }

  // "Decode" the index from seven bytes to eight, into seven bytes, even though the representation is the same.
  static ExpressionNodeIndex DecodeIndex(uint64_t encoded_index) {
#ifndef NDEBUG
    if (!(encoded_index < kFirstIllegalIndexRepresentingNodeOrVar)) {
      CURRENT_THROW(OptimizeException("Internal error."));
    }
#endif
    return ExpressionNodeIndex::FromRawAlreadyCompactifiedIndex(encoded_index);
  }

  void InitArgument(ExpressionNodeIndex argument) { compact_primary_index_ = EncodeIndex(argument); }

  void InitLHSRHS(ExpressionNodeIndex lhs, ExpressionNodeIndex rhs) {
    compact_primary_index_ = EncodeIndex(lhs);
    compact_secondary_index_ = EncodeIndex(rhs);
    compact_flipped_ = false;
  }

 public:
  ExpressionNodeType Type() const { return static_cast<ExpressionNodeType>(compact_type_); }
  double Value() const { return compact_double_; }
  ExpressionNodeIndex ArgumentIndex() const { return DecodeIndex(compact_primary_index_); }
  ExpressionNodeIndex LHSIndex() const {
    return DecodeIndex(compact_flipped_ ? compact_secondary_index_ : compact_primary_index_);
  }
  ExpressionNodeIndex RHSIndex() const {
    return DecodeIndex(compact_flipped_ ? compact_primary_index_ : compact_secondary_index_);
  }

  ExpressionNodeImpl() = default;

  ExpressionNodeImpl(ExpressionNodeTypeSelector<ExpressionNodeType::ImmediateDouble>, double x)
      : compact_type_(static_cast<uint8_t>(ExpressionNodeType::ImmediateDouble)), compact_double_(x) {}

  ExpressionNodeImpl(ExpressionNodeTypeSelector<ExpressionNodeType::Lambda>)
      : compact_type_(static_cast<uint8_t>(ExpressionNodeType::Lambda)) {}

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
