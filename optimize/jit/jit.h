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

// TODO(dkorolev): Introduce the "dry run" logic for JIT code generation, to avoid a copy and generate code inplace.

#ifndef OPTIMIZE_JIT_JIT_H
#define OPTIMIZE_JIT_JIT_H

#include "../base.h"
#include "../expression/expression.h"
#include "../vars/vars.h"

#include <cmath>

#include "../../fncas/x64_native_jit/x64_native_jit.h"  // TODO(dkorolev): Move the X64 System V JIT code outside FnCAS.

#ifdef FNCAS_X64_NATIVE_JIT_ENABLED

namespace current {
namespace expression {

struct FunctionInvokedBeforeItsPrerequisitesException final : OptimizeException {};

namespace jit {

static_assert(sizeof(double) == 8u, "The System V JIT is designed for 8-byte `double`-s.");

struct JITFunctionCallContextMismatchException final : OptimizeException {};

struct JITNotEnoughExtraNodesAllocatedInJITCallContext final : OptimizeException {};

struct JITInternalErrorException final : OptimizeException {};

struct JITCallContextFunctionPointers {
  std::vector<double (*)(double x)> fns;
  JITCallContextFunctionPointers() {
#define CURRENT_EXPRESSION_MATH_FUNCTION(fn) fns.push_back(functions::fn);
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

  mutable std::vector<double> ram_;  // The temporary buffer that must be allocated to run JIT functions of this config.

  friend class FunctionImpl;
  friend class FunctionReturningVectorImpl;
  friend class FunctionWithArgumentImpl;
  friend class JITCompiler;

  double* RAMPointer() const { return &ram_[0]; }

  size_t functions_declared_ = 0u;
  mutable size_t next_legal_function_index_to_compute_ = 0u;

  // The "recursive" call to `*EnsureNodeComputed` should not be recursive because of the possibility of large depth.
  // For its stack to not be re-allocated and re-grown multiple times, keep its RAM allocation in the JIT context.
  // The underlying type is `ExpressionNodeIndex`, and, same as in `../differentiate/differentiate.h`, the "special" bit
  // of the stack entry indicates whether the node is being seen on the way "down" or on the way "up" of "recursion".
  mutable std::vector<ExpressionNodeIndex> ensure_node_computed_manual_stack_;

 public:
  // Allocate an extra one `double` so that an external parameter could be passed in it,
  // to later intoduce functions of one variable, i.e. for directional derivatives.
  explicit JITCallContext(VarsMapperConfig const& config) : config_(config), ram_(config_.total_nodes + 1u) {}
  JITCallContext()
      : owned_config_if_default_constructed_(VarsManager::TLS().Active().Freeze()),
        config_(Value(owned_config_if_default_constructed_)),
        ram_(config_.total_nodes + 1u) {}

  ~JITCallContext() {
    if (Exists(owned_config_if_default_constructed_)) {
      VarsManager::TLS().Active().Unfreeze();
    }
  }

  VarsMapperConfig const& Config() const { return config_; }

  // The `MarkNewPoint()` mechanism is the means to guard against the situation where the functions compiler later
  // within the same context are called on a new input point before the previous functions have been called.
  // Since, when declared within one JIT context, the later-compiled functions can and do re-use the previously computed
  // nodes from the previously called functions, on a new input point calling compiled-later functions before the
  // compiler-earlier ones may (and will!) result in unexpected results.
  void MarkNewPoint() { next_legal_function_index_to_compute_ = 0; }
  size_t CurrentFunctionIndexAndPostIncrementIt() { return functions_declared_++; }
  void MarkFunctionComputedOrThrowIfPrerequisitesNotMet(size_t current_function_index) const {
    if (current_function_index > next_legal_function_index_to_compute_) {
      // Effectively, the user has to "climb this ladder" of computed functions step by step, one function at a time.
      CURRENT_THROW(FunctionInvokedBeforeItsPrerequisitesException());
    } else {
      next_legal_function_index_to_compute_ =
          std::max(next_legal_function_index_to_compute_, current_function_index + 1u);
    }
  }

  double const* ConstRAMPointer() const { return &ram_[0]; }

  std::vector<ExpressionNodeIndex>& EnsureNodeComputedManualStack() const { return ensure_node_computed_manual_stack_; }
};

class FunctionImpl final {
 private:
  JITCallContext const& call_context_;
  size_t const this_function_index_in_order_;  // For the `MarkNewPoint()` check.
  size_t const code_size_;
  current::fncas::x64_native_jit::CallableVectorUInt8 f_;

  FunctionImpl(FunctionImpl const&) = delete;
  FunctionImpl(FunctionImpl&&) = delete;
  FunctionImpl& operator=(FunctionImpl const&) = delete;
  FunctionImpl& operator=(FunctionImpl&&) = delete;

 public:
  FunctionImpl(JITCallContext& call_context, std::vector<uint8_t> code)
      : call_context_(call_context),
        this_function_index_in_order_(call_context.CurrentFunctionIndexAndPostIncrementIt()),
        code_size_(code.size()),
        f_(code) {}

  double CallFunction(JITCallContext const& call_context, double const* x) const {
    if (&call_context != &call_context_) {
      CURRENT_THROW(JITFunctionCallContextMismatchException());
    }
    call_context.MarkFunctionComputedOrThrowIfPrerequisitesNotMet(this_function_index_in_order_);
    return f_(x, call_context_.RAMPointer(), &current::Singleton<JITCallContextFunctionPointers>().fns[0]);
  }

  size_t CodeSize() const { return code_size_; }
};

class Function final {
 private:
  std::unique_ptr<FunctionImpl> f_;

  friend class JITCompiler;
  Function(JITCallContext& call_context, std::vector<uint8_t> code)
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
  size_t CodeSize() const { return f_->CodeSize(); }
};

class FunctionReturningVectorImpl final {
 private:
  JITCallContext const& call_context_;
  size_t const this_function_index_in_order_;  // For the `MarkNewPoint()` check.
  size_t const code_size_;
  current::fncas::x64_native_jit::CallableVectorUInt8 f_;
  std::vector<ExpressionNodeIndex> const output_node_indexes_;

  FunctionReturningVectorImpl(FunctionReturningVectorImpl const&) = delete;
  FunctionReturningVectorImpl(FunctionReturningVectorImpl&&) = delete;
  FunctionReturningVectorImpl& operator=(FunctionReturningVectorImpl const&) = delete;
  FunctionReturningVectorImpl& operator=(FunctionReturningVectorImpl&&) = delete;

 public:
  FunctionReturningVectorImpl(JITCallContext& call_context,
                              std::vector<uint8_t> code,
                              std::vector<ExpressionNodeIndex> output_node_indexes)
      : call_context_(call_context),
        this_function_index_in_order_(call_context.CurrentFunctionIndexAndPostIncrementIt()),
        code_size_(code.size()),
        f_(code),
        output_node_indexes_(std::move(output_node_indexes)) {}

  std::vector<double> CallFunctionReturningVector(JITCallContext const& call_context, double const* x) const {
    if (&call_context != &call_context_) {
      CURRENT_THROW(JITFunctionCallContextMismatchException());
    }
    call_context.MarkFunctionComputedOrThrowIfPrerequisitesNotMet(this_function_index_in_order_);
    std::vector<double> result(output_node_indexes_.size());
    f_(x, call_context_.RAMPointer(), &current::Singleton<JITCallContextFunctionPointers>().fns[0]);
    for (size_t i = 0; i < output_node_indexes_.size(); ++i) {
      result[i] = output_node_indexes_[i].template Dispatch<double>(
          [&](size_t node_index) -> double { return call_context_.ConstRAMPointer()[node_index]; },
          [&](size_t var_index) -> double { return x[var_index]; },
          [&]() -> double {
            // No `lambda`-s should be encountered when the gradient is being evaluated,
            // because the lambdas are the territory of `FunctionWithArgument`.
            CURRENT_THROW(JITInternalErrorException());
          });
    }
    return result;
  }

  size_t CodeSize() const { return code_size_; }
};

class FunctionReturningVector final {
 private:
  std::unique_ptr<FunctionReturningVectorImpl> f_;

  friend class JITCompiler;
  FunctionReturningVector(JITCallContext& call_context,
                          std::vector<uint8_t> code,
                          std::vector<ExpressionNodeIndex> output_node_indexes)
      : f_(std::make_unique<FunctionReturningVectorImpl>(call_context, code, std::move(output_node_indexes))) {}

 public:
  std::vector<double> operator()(JITCallContext const& call_context, double const* x) const {
    return f_->CallFunctionReturningVector(call_context, x);
  }
  std::vector<double> operator()(JITCallContext const& call_context, std::vector<double> const& x) const {
    return f_->CallFunctionReturningVector(call_context, &x[0]);
  }
  std::vector<double> operator()(JITCallContext const& call_context, VarsMapper const& vars) const {
    return f_->CallFunctionReturningVector(call_context, &vars.x[0]);
  }
  size_t CodeSize() const { return f_->CodeSize(); }
};

class FunctionWithArgumentImpl final {
 private:
  JITCallContext const& call_context_;
  size_t const this_function_index_in_order_;  // For the `MarkNewPoint()` check.
  size_t const code_size_;
  current::fncas::x64_native_jit::CallableVectorUInt8 f_;

  FunctionWithArgumentImpl(FunctionWithArgumentImpl const&) = delete;
  FunctionWithArgumentImpl(FunctionWithArgumentImpl&&) = delete;
  FunctionWithArgumentImpl& operator=(FunctionWithArgumentImpl const&) = delete;
  FunctionWithArgumentImpl& operator=(FunctionWithArgumentImpl&&) = delete;

 public:
  FunctionWithArgumentImpl(JITCallContext& call_context, std::vector<uint8_t> code)
      : call_context_(call_context),
        this_function_index_in_order_(call_context.CurrentFunctionIndexAndPostIncrementIt()),
        code_size_(code.size()),
        f_(code) {}

  double CallFunctionWithArgument(JITCallContext const& call_context, double const* x, double p) const {
    if (&call_context != &call_context_) {
      CURRENT_THROW(JITFunctionCallContextMismatchException());
    }
    call_context.MarkFunctionComputedOrThrowIfPrerequisitesNotMet(this_function_index_in_order_);
    call_context_.RAMPointer()[call_context_.config_.total_nodes] = p;
    return f_(x, call_context_.RAMPointer(), &current::Singleton<JITCallContextFunctionPointers>().fns[0]);
  }

  size_t CodeSize() const { return code_size_; }
};

class FunctionWithArgument final {
 private:
  std::unique_ptr<FunctionWithArgumentImpl> f_;

  friend class JITCompiler;
  FunctionWithArgument(JITCallContext& call_context, std::vector<uint8_t> code)
      : f_(std::make_unique<FunctionWithArgumentImpl>(call_context, code)) {}

 public:
  double operator()(JITCallContext const& call_context, double const* x, double p) const {
    return f_->CallFunctionWithArgument(call_context, x, p);
  }
  double operator()(JITCallContext const& call_context, std::vector<double> const& x, double p) const {
    return f_->CallFunctionWithArgument(call_context, &x[0], p);
  }
  double operator()(JITCallContext const& call_context, VarsMapper const& vars, double p) const {
    return f_->CallFunctionWithArgument(call_context, &vars.x[0], p);
  }
  size_t CodeSize() const { return f_->CodeSize(); }
};

class JITCompiler final {
 private:
  JITCallContext& context_;
  std::vector<bool> node_computed_;

  void NonRecursiveEnsureNodeComputed(std::vector<uint8_t>& code, ExpressionNodeIndex requested_index) {
    std::vector<ExpressionNodeIndex>& manual_stack = context_.EnsureNodeComputedManualStack();

#ifndef NDEBUG
    if (!manual_stack.empty()) {
      CURRENT_THROW(JITInternalErrorException());
    }
#endif

    auto const PushNodeToStack = [this, &manual_stack](ExpressionNodeIndex index, bool ready_to_compute_flag) {
      index.Dispatch(
          [&](size_t node_index) {
            if (!node_computed_[node_index]) {
              if (ready_to_compute_flag) {
                index.SetSpecialBit();
              }
              manual_stack.push_back(index);
            }
          },
          [](size_t) {
            // No need to push variables to the "generate JIT code" stack, as their values are already available.
          },
          []() {
            // No need to do anything for the value of the lambda either.
          });
    };

    PushNodeToStack(requested_index, false);

    while (!manual_stack.empty()) {
      ExpressionNodeIndex current_node_full_index = manual_stack.back();
      manual_stack.pop_back();
      // size_t current_node_index;
      bool const ready_to_compute = current_node_full_index.ClearSpecialBitAndReturnWhatItWas();

      current_node_full_index.Dispatch(
          [&](size_t current_node_index) {
#ifndef NDEBUG
            if (!(current_node_index < node_computed_.size())) {
              CURRENT_THROW(JITInternalErrorException());
            }
#endif
            if (!node_computed_[current_node_index]) {
              using namespace current::fncas::x64_native_jit;
              ExpressionNodeImpl const& node = VarsManager::TLS().Active()[current_node_index];
              ExpressionNodeType const type = node.Type();
              if (type == ExpressionNodeType::ImmediateDouble) {
                opcodes::load_immediate_to_memory_by_rbx_offset(code, current_node_index, node.Value());
                node_computed_[current_node_index] = true;
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)                                                           \
  }                                                                                                                \
  else if (type == ExpressionNodeType::Operation_##name) {                                                         \
    ExpressionNodeIndex const lhs = node.LHSIndex();                                                               \
    ExpressionNodeIndex const rhs = node.RHSIndex();                                                               \
    if (!ready_to_compute) {                                                                                       \
      PushNodeToStack(ExpressionNodeIndex::FromNodeIndex(current_node_index), true);                               \
      PushNodeToStack(rhs, false);                                                                                 \
      PushNodeToStack(lhs, false);                                                                                 \
    } else {                                                                                                       \
      lhs.Dispatch(                                                                                                \
          [&](size_t idx) { opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, idx); },                         \
          [&](size_t var) { opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, Config().dense_index[var]); },   \
          [&]() { opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, Config().total_nodes); });                 \
      rhs.Dispatch(                                                                                                \
          [&](size_t idx) { opcodes::name##_from_memory_by_rbx_offset_to_xmm0(code, idx); },                       \
          [&](size_t var) { opcodes::name##_from_memory_by_rdi_offset_to_xmm0(code, Config().dense_index[var]); }, \
          [&]() { opcodes::name##_from_memory_by_rbx_offset_to_xmm0(code, Config().total_nodes); });               \
      opcodes::store_xmm0_to_memory_by_rbx_offset(code, current_node_index);                                       \
      node_computed_[current_node_index] = true;                                                                   \
    }
#include "../math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION

#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)                                                                     \
  }                                                                                                              \
  else if (type == ExpressionNodeType::Function_##fn) {                                                          \
    ExpressionNodeIndex const argument = node.ArgumentIndex();                                                   \
    if (!ready_to_compute) {                                                                                     \
      PushNodeToStack(ExpressionNodeIndex::FromNodeIndex(current_node_index), true);                             \
      PushNodeToStack(argument, false);                                                                          \
    } else {                                                                                                     \
      using namespace current::fncas::x64_native_jit;                                                            \
      argument.Dispatch(                                                                                         \
          [&](size_t idx) { opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, idx); },                       \
          [&](size_t var) { opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, Config().dense_index[var]); }, \
          [&]() { opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, Config().total_nodes); });               \
      opcodes::push_rdi(code);                                                                                   \
      opcodes::push_rdx(code);                                                                                   \
      opcodes::call_function_from_rdx_pointers_array_by_index(                                                   \
          code, static_cast<uint8_t>(ExpressionFunctionIndex::FunctionIndexOf_##fn));                            \
      opcodes::pop_rdx(code);                                                                                    \
      opcodes::pop_rdi(code);                                                                                    \
      opcodes::store_xmm0_to_memory_by_rbx_offset(code, current_node_index);                                     \
      node_computed_[current_node_index] = true;                                                                 \
    }
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
              } else {
                CURRENT_THROW(JITInternalErrorException());
              }
            }
          },
          [](size_t) {
            // Seeing a var node. The stack should only contain indexes that are expression nodes.
            CURRENT_THROW(JITInternalErrorException());
          },
          []() {
            // Seeing a lambda node. The stack should only contain indexes that are expression nodes.
            CURRENT_THROW(JITInternalErrorException());
          });
    }
  }

 public:
  explicit JITCompiler(JITCallContext& context) : context_(context), node_computed_(context_.Config().total_nodes) {
    // Throw if a thread-local vars manager is not available at the moment of constructing the `JITCompiler`.
    VarsManager::TLS().Active();
  }

  VarsMapperConfig const& Config() const { return context_.Config(); }

  Function Compile(ExpressionNode node) {
    using namespace current::fncas::x64_native_jit;

    // TODO(dkorolev): Inplace code generation.
    std::vector<uint8_t> code;

    ExpressionNodeIndex index = ExpressionNodeIndex(node);
    index.Dispatch(
        [&](size_t node_index) {
          // The true expression node. Need to JIT-generate the proper function body.
          opcodes::push_rbx(code);
          opcodes::mov_rsi_rbx(code);
          NonRecursiveEnsureNodeComputed(code, index);
          opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, node_index);
          opcodes::pop_rbx(code);
        },
        [&](size_t var_index) {
          // Var. Just load its value into xmm0.
          opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, Config().dense_index[var_index]);
        },
        [&]() {
          // Lambda. Just load its value into xmm0.
          opcodes::load_from_memory_by_rsi_offset_to_xmm0(code, Config().total_nodes);
        });
    opcodes::ret(code);

    return Function(context_, std::move(code));
  }

  FunctionReturningVector Compile(std::vector<ExpressionNode> const& nodes) {
    using namespace current::fncas::x64_native_jit;

    // TODO(dkorolev): Inplace code generation.
    std::vector<uint8_t> code;

    opcodes::push_rbx(code);
    opcodes::mov_rsi_rbx(code);

    std::vector<ExpressionNodeIndex> output_node_indexes(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
      output_node_indexes[i] = static_cast<ExpressionNodeIndex>(ExpressionNodeIndex(nodes[i]));
    }

    for (ExpressionNodeIndex const index : output_node_indexes) {
      index.Dispatch(
          [&](size_t) {
            // Expression nodes should be JIT-compiled.
            NonRecursiveEnsureNodeComputed(code, index);
          },
          [](size_t) {
            // Var "nodes" are already conveniently available right by the memory location pointed to. A no-op.
          },
          []() {
            // Same is true for the lambda "nodes".
          });
    }

    opcodes::pop_rbx(code);
    opcodes::ret(code);

    return FunctionReturningVector(context_, std::move(code), std::move(output_node_indexes));
  }

  FunctionWithArgument CompileFunctionWithArgument(ExpressionNode node) {
    using namespace current::fncas::x64_native_jit;

    // TODO(dkorolev): Inplace code generation.
    std::vector<uint8_t> code;

    ExpressionNodeIndex const index = ExpressionNodeIndex(node);
    index.Dispatch(
        [&](size_t node_index) {
          // The true expression node. Need to JIT-generate the proper function body.
          opcodes::push_rbx(code);
          opcodes::mov_rsi_rbx(code);
          // TODO(dkorolev): Allow computing function with parameter. And test this.
          NonRecursiveEnsureNodeComputed(code, index);
          // TODO(dkorolev): Disallow computing function with parameter.
          opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, node_index);
          opcodes::pop_rbx(code);
        },
        [&](size_t var_index) {
          // Var. Just load its value into xmm0.
          opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, Config().dense_index[var_index]);
        },
        [&]() {
          // Lambda. Just load its value into xmm0.
          opcodes::load_from_memory_by_rsi_offset_to_xmm0(code, context_.config_.total_nodes);
        });
    opcodes::ret(code);

    return FunctionWithArgument(context_, std::move(code));
  }
};

}  // namespace current::expression::jit
}  // namespace current::expression
}  // namespace current

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED

#endif  // #ifndef OPTIMIZE_JIT_JIT_H
