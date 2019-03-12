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

namespace current {

struct OptimizeException : Exception {
  using Exception::Exception;
};

namespace expression {

// The data type for the expression should be defined in this `base.h` header, as the thread-local context
// for expression management is the same as the thread-local context for variables management.
//
// There is no expression node type for variables or constants in `ExpressionNodeImpl`, as this class only holds
// the information about what is being stored in the thread-local singleton of the expression nodes vector.
// Variables and constants are just references to certain values, and they do not require dedicated expression nodes.
//
// TODO(dkorolev): Compactify the below, as this implementation is just the first TDD approximation.
// It should help hack up the JIT, and perhaps the differentiation and optimization engines, but it's by no means final.

// Expression nodes: small `index`-es map to the indexes in the thread-local singleton, `~index`-es map to variables.
using expression_node_index_t = uint64_t;
enum class ExpressionNodeIndex : expression_node_index_t { Invalid = static_cast<expression_node_index_t>(-1) };

enum class ExpressionNodeType { Uninitialized, ImmediateDouble, Plus, Exp };

template <ExpressionNodeType>
struct ExpressionNodeTypeSelector {};

// The class that implements the expression is `ExpressionNodeImpl` defined in `base.h`, because the ultimate class
// `ExpressionNode` is just a thin wrapper over `ExpressionNodeIndex`, and it is defined in `expression/expression.h`.
class ExpressionNodeImpl final {
 private:
  friend class ExpressionNode;     // To manage the below fields.
  ExpressionNodeType const type_;
  double const value_;             // For `type_ == ImmediateDouble`.
  ExpressionNodeIndex const lhs_;  // For `type_ == Plus` or `type_ == Exp`.
  ExpressionNodeIndex const rhs_;  // For `type_ == Plus`.

 public:
  ExpressionNodeImpl()
      : type_(ExpressionNodeType::Uninitialized),
        value_(0.0),
        lhs_(ExpressionNodeIndex::Invalid),
        rhs_(ExpressionNodeIndex::Invalid) {}

  ExpressionNodeImpl(ExpressionNodeTypeSelector<ExpressionNodeType::ImmediateDouble>, double x)
      : type_(ExpressionNodeType::ImmediateDouble),
        value_(x),
        lhs_(ExpressionNodeIndex::Invalid),
        rhs_(ExpressionNodeIndex::Invalid) {}

  ExpressionNodeImpl(ExpressionNodeTypeSelector<ExpressionNodeType::Plus>,
                     ExpressionNodeIndex lhs,
                     ExpressionNodeIndex rhs)
      : type_(ExpressionNodeType::Plus), value_(0.0), lhs_(lhs), rhs_(rhs) {}

  ExpressionNodeImpl(ExpressionNodeTypeSelector<ExpressionNodeType::Exp>,
                     ExpressionNodeIndex argument)
      : type_(ExpressionNodeType::Exp), value_(0.0), lhs_(argument), rhs_(ExpressionNodeIndex::Invalid) {}
};

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_BASE_H
