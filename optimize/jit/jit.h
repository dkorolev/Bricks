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

// TODO(dkorolev): Replace recursion by a manually allocated `std::stack` to prevent stack overflows.
// TODO(dkorolev): Add a test for a deep expression tree. Which would fail w/o the above taken care of.
// TODO(dkorolev): Introduce the "dry run" logic for JIT code generation, to avoid a copy and generate code inplace.

#ifndef OPTIMIZE_JIT_JIT_H
#define OPTIMIZE_JIT_JIT_H

#include "../base.h"
#include "../expression/expression.h"
#include "../vars/vars.h"

#include "../../fncas/x64_native_jit/x64_native_jit.h"  // TODO(dkorolev): Move the X64 System V JIT code outside FnCAS.

#ifdef FNCAS_X64_NATIVE_JIT_ENABLED

namespace current {
namespace expression {
namespace jit {

struct JITInternalErrorException final : OptimizeException {
  using OptimizeException::OptimizeException;
};

// The interface of `JITCodeGenerator` is:
//
// 1) It is constructed from the frozen vars context config.
// 2) It exposes the `size_t RAMRequired() const` method to return the number of intermediate `double`-s required.
// 3) It exposes the `FunctionForNode(node)` method to generate the code of what will return the value for this `node`.
// 4) The generated function is assumed to be called:
//    4.a) after all the functions generated by this instance of `JITCodeGenerator` were called,
//    4.b) in the order they were generated, and
//    4.c) obviously, on the same input vector.
//
// The above ensures that, of the intermediate nodes, only the ones that have to be computed are being computed.
//
// For example, the components of the gradient are computed after the very value of the function is available,
// and the n-th order derivative can and should safely assume the (n-1)-st order one has already been computed.

class JITCodeGenerator final {
 private:
  Optional<VarsMapperConfig> owned_config_if_default_constructed_;
  VarsMapperConfig const& config_;

  size_t const ram_total_;   // The total number of `double`-s in the "intermediate" buffer.
  size_t const ram_offset_;  // The offset of "index zero" of the expression tree, in the "ram buffer".

  std::vector<bool> computed;

  void EnsureNodeComputed(std::vector<uint8_t>& code, expression_node_index_t index) {
    if (index < ~index) {
      // If the MSB of `index` is set, the node is a var or constant from the input vector, which is already computed.
      if (!(index < computed.size())) {
        CURRENT_THROW(JITInternalErrorException());
      }

      if (!computed[index]) {
        computed[index] = true;

        using namespace current::fncas::x64_native_jit;

        ExpressionNodeImpl const& node = VarsManager::TLS().Active()[index];

        if (node.type_ == ExpressionNodeType::ImmediateDouble) {
          opcodes::load_immediate_to_memory_by_rbx_offset(code, index + ram_offset_, node.value_);
        } else if (node.type_ == ExpressionNodeType::Plus) {
          expression_node_index_t const lhs = static_cast<expression_node_index_t>(node.lhs_);
          expression_node_index_t const rhs = static_cast<expression_node_index_t>(node.rhs_);
          EnsureNodeComputed(code, lhs);
          EnsureNodeComputed(code, rhs);
          if (lhs < ~lhs) {
            opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, lhs + ram_offset_);
          } else {
            opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, config_.dense_index[~lhs]);
          }
          if (rhs < ~rhs) {
            opcodes::add_from_memory_by_rbx_offset_to_xmm0(code, rhs + ram_offset_);
          } else {
            opcodes::add_from_memory_by_rdi_offset_to_xmm0(code, config_.dense_index[~rhs]);
          }
          opcodes::store_xmm0_to_memory_by_rbx_offset(code, index + ram_offset_);
        } else if (node.type_ == ExpressionNodeType::Exp) {
          expression_node_index_t const argument = static_cast<expression_node_index_t>(node.lhs_);
          EnsureNodeComputed(code, argument);
          using namespace current::fncas::x64_native_jit;
          if (argument < ~argument) {
            opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, argument + ram_offset_);
          } else {
            opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, config_.dense_index[~argument]);
          }
          opcodes::push_rdi(code);
          opcodes::push_rdx(code);
          opcodes::call_function_from_rdx_pointers_array_by_index(code, static_cast<uint8_t>(0));
          opcodes::pop_rdx(code);
          opcodes::pop_rdi(code);
          opcodes::store_xmm0_to_memory_by_rbx_offset(code, index + ram_offset_);
        } else {
          CURRENT_THROW(JITInternalErrorException());
        }
      }
    }
  }

 public:
  JITCodeGenerator()
      : owned_config_if_default_constructed_(VarsManager::TLS().Active().Freeze()),
        config_(Value(owned_config_if_default_constructed_)),
        ram_total_(config_.total_nodes),
        ram_offset_(0),
        computed(config_.total_nodes) {}

  explicit JITCodeGenerator(VarsMapperConfig const& config)
      : config_(config), ram_total_(config_.total_nodes), ram_offset_(0), computed(config_.total_nodes) {}

  ~JITCodeGenerator() {
    if (Exists(owned_config_if_default_constructed_)) {
      VarsManager::TLS().Active().Unfreeze();
    }
  }

  VarsMapperConfig const& Config() const { return config_; }

  size_t RAMRequired() const { return ram_total_; }

  std::vector<uint8_t> FunctionForNode(ExpressionNode const& node) {
    using namespace current::fncas::x64_native_jit;

    std::vector<uint8_t> code;

    expression_node_index_t const index = static_cast<expression_node_index_t>(ExpressionNodeIndex(node));
    if (index < ~index) {
      opcodes::push_rbx(code);
      opcodes::mov_rsi_rbx(code);
      EnsureNodeComputed(code, static_cast<expression_node_index_t>(ExpressionNodeIndex(node)));
      opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, index + ram_offset_);
      opcodes::pop_rbx(code);
      opcodes::ret(code);
    } else {
      opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, config_.dense_index[~index]);
    }

    return code;
  }
};

}  // namespace current::expression::jit
}  // namespace current::expression
}  // namespace current

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED

#endif  // #ifndef OPTIMIZE_JIT_JIT_H
