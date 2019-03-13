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

struct JITFunctionCallContextMismatchException final : OptimizeException {
  using OptimizeException::OptimizeException;
};

struct JITNotEnoughExtraNodesAllocatedInJITCallContext final : OptimizeException {
  using OptimizeException::OptimizeException;
};

struct JITInternalErrorException final : OptimizeException {
  using OptimizeException::OptimizeException;
};

struct JITCallContextFunctionPointers {
  std::vector<double (*)(double x)> fns;
  JITCallContextFunctionPointers() {
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn) fns.push_back(std::fn);
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
  }
};

// It's best to refer to the two `Smoke*` tests in `test.cc`, but the gist is:
//
// * An instance of `JITCallContext` must live both in order to JIT-compile functions and in order to call them.
//   It is because `JITCallContext` is what holds the RAM for temporary `double`-s.
//
// * An instance of `JITCompiler` requires a const reference to `JITCallContext` upon its construction.
//   This is to make sure the functions it JIT-compiled can confirm they tie to the right context.
//
// * The very `JITCompiler` can be disposed as soon as it was used to code-generate all the function(s).
//   The `JITCompiler` contains the scope of what intermediate expression nodes have already been evaluated.
//
//   Thus, if there is a chain of functions generated one after another by an instance of `JITCompiler`, they
//   should be called strictly in the order of their compilation, but they would not perform redundant computations.
//   An example here is that the gradient (vector of derivatives) can and does rely on the very function value
//   already being computed. And the computation of the (n+1)-st derivative can safely assume the up-to-n-th ones
//   have already been computed as well.
//
//   By "have already been computed here" is meant that the RAM allocated within the `JITCallContext` reflects
//   the ultimate values of certain expression nodes.
//
//   Needless to say, the above requires the consecutive computations to happen over the same vars/constants vector.

class JITCallContext final {
 private:
  Optional<VarsMapperConfig> owned_config_if_default_constructed_;
  VarsMapperConfig const& config_;

  size_t const ram_offset_;          // The offset of "index zero" of the expression tree, in the "ram buffer".
  size_t const ram_total_;           // The total number of `double`-s in the "intermediate" buffer.
  mutable std::vector<double> ram_;  // The temporary buffer that must be allocated to run JIT functions of this config.

  friend class FunctionImpl;
  friend class FunctionReturningVectorImpl;
  friend class JITCompiler;

  double* RAMPointer() const { return &ram_[0]; }
  size_t ExtraNodesAllocated() const { return ram_offset_; }

  static size_t ComputeExtraRAMSlotsToAllocate(size_t extra_ram_slots_to_allocate, VarsMapperConfig const& config) {
    if (extra_ram_slots_to_allocate != static_cast<size_t>(-1)) {
      return extra_ram_slots_to_allocate;
    } else {
      // The default number of RAM slots to allocate is `n_vars + 4`. The logic here is that:
      // a) it's a small enough overhead, and
      // b) we better allocate enough room for along-the-gradient derivatives and for up-to-4th derivatives right away.
      return config.total_leaves + 4u;
    }
  }

 public:
  explicit JITCallContext(VarsMapperConfig const& config, size_t extra_ram_slots_to_allocate = static_cast<size_t>(-1))
      : config_(config),
        ram_offset_(ComputeExtraRAMSlotsToAllocate(extra_ram_slots_to_allocate, config_)),
        ram_total_(ram_offset_ + config_.total_nodes),
        ram_(ram_total_) {}

  JITCallContext(size_t extra_ram_slots_to_allocate = static_cast<size_t>(-1))
      : owned_config_if_default_constructed_(VarsManager::TLS().Active().Freeze()),
        config_(Value(owned_config_if_default_constructed_)),
        ram_offset_(ComputeExtraRAMSlotsToAllocate(extra_ram_slots_to_allocate, config_)),
        ram_total_(ram_offset_ + config_.total_nodes),
        ram_(ram_total_) {}

  ~JITCallContext() {
    if (Exists(owned_config_if_default_constructed_)) {
      VarsManager::TLS().Active().Unfreeze();
    }
  }

  VarsMapperConfig const& Config() const { return config_; }
  size_t RAMOffset() const { return ram_offset_; }
};

class FunctionImpl final {
 private:
  JITCallContext const& call_context_;
  current::fncas::x64_native_jit::CallableVectorUInt8 f_;

  FunctionImpl(FunctionImpl const&) = delete;
  FunctionImpl(FunctionImpl&&) = delete;
  FunctionImpl& operator=(FunctionImpl const&) = delete;
  FunctionImpl& operator=(FunctionImpl&&) = delete;

 public:
  FunctionImpl(JITCallContext const& call_context, std::vector<uint8_t> code) : call_context_(call_context), f_(code) {}

  double CallFunction(JITCallContext const& call_context, double const* x) const {
    if (&call_context != &call_context_) {
      CURRENT_THROW(JITFunctionCallContextMismatchException());
    }
    return f_(x, call_context_.RAMPointer(), &current::Singleton<JITCallContextFunctionPointers>().fns[0]);
  }
};

class Function final {
 private:
  std::unique_ptr<FunctionImpl> f_;

  friend class JITCompiler;
  Function(JITCallContext const& call_context, std::vector<uint8_t> code)
      : f_(std::make_unique<FunctionImpl>(call_context, code)) {}

 public:
  double operator()(JITCallContext const& call_context, double const* x) const {
    return f_->CallFunction(call_context, x);
  }
  double operator()(JITCallContext const& call_context, std::vector<double> const& x) const {
    return f_->CallFunction(call_context, &x[0]);
  }
  double operator()(JITCallContext const& call_context, VarsMapper const& vars) const {
    return f_->CallFunction(call_context, &vars.x[0]);
  }
};

class FunctionReturningVectorImpl final {
 private:
  JITCallContext const& call_context_;
  current::fncas::x64_native_jit::CallableVectorUInt8 f_;
  size_t const return_vector_size_;

  FunctionReturningVectorImpl(FunctionReturningVectorImpl const&) = delete;
  FunctionReturningVectorImpl(FunctionReturningVectorImpl&&) = delete;
  FunctionReturningVectorImpl& operator=(FunctionReturningVectorImpl const&) = delete;
  FunctionReturningVectorImpl& operator=(FunctionReturningVectorImpl&&) = delete;

 public:
  FunctionReturningVectorImpl(JITCallContext const& call_context, std::vector<uint8_t> code, size_t return_vector_size)
      : call_context_(call_context), f_(code), return_vector_size_(return_vector_size) {}

  std::vector<double> CallFunction(JITCallContext const& call_context, double const* x) const {
    if (&call_context != &call_context_) {
      CURRENT_THROW(JITFunctionCallContextMismatchException());
    }
    f_(x, call_context_.RAMPointer(), &current::Singleton<JITCallContextFunctionPointers>().fns[0]);
    return std::vector<double>(call_context_.RAMPointer(), call_context_.RAMPointer() + return_vector_size_);
  }
};

class FunctionReturningVector final {
 private:
  std::unique_ptr<FunctionReturningVectorImpl> f_;

  friend class JITCompiler;
  FunctionReturningVector(JITCallContext const& call_context, std::vector<uint8_t> code, size_t return_vector_size)
      : f_(std::make_unique<FunctionReturningVectorImpl>(call_context, code, return_vector_size)) {}

 public:
  std::vector<double> operator()(JITCallContext const& call_context, double const* x) const {
    return f_->CallFunction(call_context, x);
  }
  std::vector<double> operator()(JITCallContext const& call_context, std::vector<double> const& x) const {
    return f_->CallFunction(call_context, &x[0]);
  }
  std::vector<double> operator()(JITCallContext const& call_context, VarsMapper const& vars) const {
    return f_->CallFunction(call_context, &vars.x[0]);
  }
};

class JITCompiler final {
 private:
  JITCallContext const& context_;
  std::vector<bool> node_computed_;

  void EnsureNodeComputed(std::vector<uint8_t>& code, expression_node_index_t index) {
    if (index < ~index) {
      // If the MSB of `index` is set, the node is a var or constant from the input vector, which is already computed.
      if (!(index < node_computed_.size())) {
        CURRENT_THROW(JITInternalErrorException());
      }

      if (!node_computed_[index]) {
        node_computed_[index] = true;

        using namespace current::fncas::x64_native_jit;

        ExpressionNodeImpl const& node = VarsManager::TLS().Active()[index];

        if (node.type_ == ExpressionNodeType::ImmediateDouble) {
          opcodes::load_immediate_to_memory_by_rbx_offset(code, index + context_.RAMOffset(), node.value_);
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)                                   \
  }                                                                                        \
  else if (node.type_ == ExpressionNodeType::Operation_##name) {                           \
    expression_node_index_t const lhs = static_cast<expression_node_index_t>(node.lhs_);   \
    expression_node_index_t const rhs = static_cast<expression_node_index_t>(node.rhs_);   \
    EnsureNodeComputed(code, lhs);                                                         \
    EnsureNodeComputed(code, rhs);                                                         \
    if (lhs < ~lhs) {                                                                      \
      opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, lhs + context_.RAMOffset());   \
    } else {                                                                               \
      opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, Config().dense_index[~lhs]);   \
    }                                                                                      \
    if (rhs < ~rhs) {                                                                      \
      opcodes::name##_from_memory_by_rbx_offset_to_xmm0(code, rhs + context_.RAMOffset()); \
    } else {                                                                               \
      opcodes::name##_from_memory_by_rdi_offset_to_xmm0(code, Config().dense_index[~rhs]); \
    }                                                                                      \
    opcodes::store_xmm0_to_memory_by_rbx_offset(code, index + context_.RAMOffset());
#include "../math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION

#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)                                                  \
  }                                                                                           \
  else if (node.type_ == ExpressionNodeType::Function_##fn) {                                 \
    expression_node_index_t const argument = static_cast<expression_node_index_t>(node.lhs_); \
    EnsureNodeComputed(code, argument);                                                       \
    using namespace current::fncas::x64_native_jit;                                           \
    if (argument < ~argument) {                                                               \
      opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, argument + context_.RAMOffset()); \
    } else {                                                                                  \
      opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, Config().dense_index[~argument]); \
    }                                                                                         \
    opcodes::push_rdi(code);                                                                  \
    opcodes::push_rdx(code);                                                                  \
    opcodes::call_function_from_rdx_pointers_array_by_index(                                  \
        code, static_cast<uint8_t>(ExpressionFunctionIndex::FunctionIndexOf_##fn));           \
    opcodes::pop_rdx(code);                                                                   \
    opcodes::pop_rdi(code);                                                                   \
    opcodes::store_xmm0_to_memory_by_rbx_offset(code, index + context_.RAMOffset());
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
        } else {
          CURRENT_THROW(JITInternalErrorException());
        }
      }
    }
  }

 public:
  explicit JITCompiler(JITCallContext const& context)
      : context_(context), node_computed_(context_.Config().total_nodes) {
    // Throw if a thread-local vars manager is not available at the moment of constructing the `JITCompiler`.
    VarsManager::TLS().Active();
  }

  VarsMapperConfig const& Config() const { return context_.Config(); }

  Function Compile(ExpressionNode node) {
    using namespace current::fncas::x64_native_jit;

    // TODO(dkorolev): Inplace code generation.
    std::vector<uint8_t> code;

    expression_node_index_t const index = static_cast<expression_node_index_t>(ExpressionNodeIndex(node));
    if (index < ~index) {
      opcodes::push_rbx(code);
      opcodes::mov_rsi_rbx(code);
      EnsureNodeComputed(code, static_cast<expression_node_index_t>(ExpressionNodeIndex(node)));
      opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, index + context_.RAMOffset());
      opcodes::pop_rbx(code);
    } else {
      opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, Config().dense_index[~index]);
    }
    opcodes::ret(code);

    return Function(context_, std::move(code));
  }

  FunctionReturningVector Compile(std::vector<ExpressionNode> const& nodes) {
    using namespace current::fncas::x64_native_jit;

    if (nodes.size() > context_.ExtraNodesAllocated()) {
      CURRENT_THROW(JITNotEnoughExtraNodesAllocatedInJITCallContext());
    }

    // TODO(dkorolev): Inplace code generation.
    std::vector<uint8_t> code;

    opcodes::push_rbx(code);
    opcodes::mov_rsi_rbx(code);

    for (size_t i = 0; i < nodes.size(); ++i) {
      ExpressionNode node = nodes[i];
      expression_node_index_t const index = static_cast<expression_node_index_t>(ExpressionNodeIndex(node));
      if (index < ~index) {
        EnsureNodeComputed(code, static_cast<expression_node_index_t>(ExpressionNodeIndex(node)));
        opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, index + context_.RAMOffset());
      } else {
        opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, Config().dense_index[~index]);
      }
      opcodes::store_xmm0_to_memory_by_rbx_offset(code, i);
    }

    opcodes::pop_rbx(code);
    opcodes::ret(code);

    return FunctionReturningVector(context_, std::move(code), nodes.size());
  }
};

}  // namespace current::expression::jit
}  // namespace current::expression
}  // namespace current

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED

#endif  // #ifndef OPTIMIZE_JIT_JIT_H
