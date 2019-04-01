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
// TODO(dkorolev): Add a test that no `lambda`-s can be present when compiling an expression as a function w/o argument.

#ifndef OPTIMIZE_JIT_JIT_H
#define OPTIMIZE_JIT_JIT_H

#include "../base.h"
#include "../expression/expression.h"
#include "../vars/vars.h"

#include "../../bricks/sync/owned_borrowed.h"  // JIT-compiled funcions have `Borrowed<JITCallContextImpl>`.

#include <cmath>

#include "../../fncas/x64_native_jit/x64_native_jit.h"  // TODO(dkorolev): Move the X64 System V JIT code outside FnCAS.

#ifdef FNCAS_X64_NATIVE_JIT_ENABLED

namespace current {
namespace expression {

struct JITCompiledFunctionInvokedBeforeItsPrerequisitesException final : OptimizeException {};

static_assert(sizeof(double) == 8u, "The System V JIT is designed for 8-byte `double`-s.");

struct JITNotEnoughExtraNodesAllocatedInJITCallContext final : OptimizeException {};

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
//   It is because `JITCallContext` is what holds the RAM for the temporary `double`-s.
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
//
// * At the end of the day, the JIT-compiled functions need the `JITCallContext`, and they borrow it for their lifetime.

class JITCallContextImpl final {
 private:
  // An instance of `JITCallContext` keeps a copy of `Vars::Config`, as `JITCallContext` may outlive `Vars::Scope`.
  Vars::Config const vars_config_;

  mutable std::vector<double> ram_;  // The temporary buffer that must be allocated to run JIT functions of this config.

  friend class JITCompiledFunctionImpl;
  friend class JITCompiledFunctionReturningVectorImpl;
  friend class JITCompiledFunctionWithArgumentImpl;
  friend class JITCompiler;

  double* InternalRAMPointer() const { return &ram_[0]; }
  size_t LambdaRAMOffset() const { return vars_config_.NumberOfNodes(); }

  size_t functions_declared_ = 0u;
  mutable size_t next_legal_function_index_to_compute_ = 0u;

 public:
  // Allocate an extra one `double` so that an external parameter could be passed in it,
  // to later intoduce functions of one variable, i.e. for directional derivatives.
  JITCallContextImpl(Vars::Config const& vars_config)
      : vars_config_(vars_config), ram_(vars_config_.NumberOfNodes() + 1u) {}

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
      CURRENT_THROW(JITCompiledFunctionInvokedBeforeItsPrerequisitesException());
    } else {
      next_legal_function_index_to_compute_ =
          std::max(next_legal_function_index_to_compute_, current_function_index + 1u);
    }
  }

  double const* ConstRAMPointer() const { return &ram_[0]; }
};

class JITCallContext {
 private:
  current::sync::Owned<JITCallContextImpl> impl_;

 public:
  explicit JITCallContext(Vars::Config const& vars_config) : impl_(vars_config) {}
  JITCallContext(Vars::Scope& scope = InternalTLS()) : JITCallContext(scope.VarsConfig()) {}

  current::sync::Borrowed<JITCallContextImpl> BorrowImpl() { return impl_; }

  double const* ConstRAMPointer() const { return impl_->ConstRAMPointer(); }
  void MarkNewPoint() { impl_->MarkNewPoint(); }
};

class JITCompiledFunctionImpl final {
 private:
  current::sync::Borrowed<JITCallContextImpl> ctx_;
  size_t const this_function_index_in_order_;  // For the `MarkNewPoint()` check.
  size_t const code_size_;
  current::fncas::x64_native_jit::CallableVectorUInt8 f_;

  JITCompiledFunctionImpl(JITCompiledFunctionImpl const&) = delete;
  JITCompiledFunctionImpl(JITCompiledFunctionImpl&&) = delete;
  JITCompiledFunctionImpl& operator=(JITCompiledFunctionImpl const&) = delete;
  JITCompiledFunctionImpl& operator=(JITCompiledFunctionImpl&&) = delete;

 public:
  JITCompiledFunctionImpl(current::sync::Borrowed<JITCallContextImpl> call_context_impl, std::vector<uint8_t> code)
      : ctx_(std::move(call_context_impl)),
        this_function_index_in_order_(ctx_->CurrentFunctionIndexAndPostIncrementIt()),
        code_size_(code.size()),
        f_(code) {}

  double CallFunction(double const* x) const {
    ctx_->MarkFunctionComputedOrThrowIfPrerequisitesNotMet(this_function_index_in_order_);
    return f_(x, ctx_->InternalRAMPointer(), &current::Singleton<JITCallContextFunctionPointers>().fns[0]);
  }

  size_t CodeSize() const { return code_size_; }
};

class JITCompiledFunction final {
 private:
  std::unique_ptr<JITCompiledFunctionImpl> f_;

  friend class JITCompiler;
  JITCompiledFunction(current::sync::Borrowed<JITCallContextImpl> call_context_impl, std::vector<uint8_t> code)
      : f_(std::make_unique<JITCompiledFunctionImpl>(std::move(call_context_impl), code)) {}

 public:
  double operator()(double const* x) const { return f_->CallFunction(x); }
  double operator()(std::vector<double> const& x) const { return f_->CallFunction(&x[0]); }
  double operator()(Vars const& values) const { return f_->CallFunction(&values.x[0]); }
  size_t CodeSize() const { return f_->CodeSize(); }
};

class JITCompiledFunctionReturningVectorImpl final {
 private:
  current::sync::Borrowed<JITCallContextImpl> ctx_;
  size_t const this_function_index_in_order_;  // For the `MarkNewPoint()` check.
  size_t const code_size_;
  current::fncas::x64_native_jit::CallableVectorUInt8 f_;
  std::vector<ExpressionNodeIndex> const output_node_indexes_;

  JITCompiledFunctionReturningVectorImpl(JITCompiledFunctionReturningVectorImpl const&) = delete;
  JITCompiledFunctionReturningVectorImpl(JITCompiledFunctionReturningVectorImpl&&) = delete;
  JITCompiledFunctionReturningVectorImpl& operator=(JITCompiledFunctionReturningVectorImpl const&) = delete;
  JITCompiledFunctionReturningVectorImpl& operator=(JITCompiledFunctionReturningVectorImpl&&) = delete;

 public:
  JITCompiledFunctionReturningVectorImpl(current::sync::Borrowed<JITCallContextImpl> call_context_impl,
                                         std::vector<uint8_t> code,
                                         std::vector<ExpressionNodeIndex> output_node_indexes)
      : ctx_(std::move(call_context_impl)),
        this_function_index_in_order_(ctx_->CurrentFunctionIndexAndPostIncrementIt()),
        code_size_(code.size()),
        f_(code),
        output_node_indexes_(std::move(output_node_indexes)) {}

  std::vector<double> CallFunctionReturningVector(double const* x) const {
    ctx_->MarkFunctionComputedOrThrowIfPrerequisitesNotMet(this_function_index_in_order_);
    std::vector<double> result(output_node_indexes_.size());
    f_(x, ctx_->InternalRAMPointer(), &current::Singleton<JITCallContextFunctionPointers>().fns[0]);
    for (size_t i = 0; i < output_node_indexes_.size(); ++i) {
      result[i] = output_node_indexes_[i].template CheckedDispatch<double>(
          [&](size_t node_index) -> double { return ctx_->ConstRAMPointer()[node_index]; },
          [&](size_t var_index) -> double { return x[var_index]; },
          [&](double value) -> double { return value; },
          [&]() -> double {
#ifndef NDEBUG
            // No `lambda`-s should be encountered when the gradient is being evaluated,
            // because the lambdas are the territory of `JITCompiledFunctionWithArgument`.
            TriggerSegmentationFault();
            throw false;
#else
            return 0.0;
#endif
          });
    }
    return result;
  }

  size_t CodeSize() const { return code_size_; }
};

class JITCompiledFunctionReturningVector final {
 private:
  std::unique_ptr<JITCompiledFunctionReturningVectorImpl> f_;

  friend class JITCompiler;
  JITCompiledFunctionReturningVector(current::sync::Borrowed<JITCallContextImpl> call_context_impl,
                                     std::vector<uint8_t> code,
                                     std::vector<ExpressionNodeIndex> output_node_indexes)
      : f_(std::make_unique<JITCompiledFunctionReturningVectorImpl>(
            std::move(call_context_impl), code, std::move(output_node_indexes))) {}

 public:
  std::vector<double> operator()(double const* x) const { return f_->CallFunctionReturningVector(x); }
  std::vector<double> operator()(std::vector<double> const& x) const { return f_->CallFunctionReturningVector(&x[0]); }
  std::vector<double> operator()(Vars const& values) const { return f_->CallFunctionReturningVector(&values.x[0]); }
  size_t CodeSize() const { return f_->CodeSize(); }
};

class JITCompiledFunctionWithArgumentImpl final {
 private:
  current::sync::Borrowed<JITCallContextImpl> ctx_;
  size_t const this_function_index_in_order_;  // For the `MarkNewPoint()` check.
  size_t const code_size_;
  current::fncas::x64_native_jit::CallableVectorUInt8 f_;

  JITCompiledFunctionWithArgumentImpl(JITCompiledFunctionWithArgumentImpl const&) = delete;
  JITCompiledFunctionWithArgumentImpl(JITCompiledFunctionWithArgumentImpl&&) = delete;
  JITCompiledFunctionWithArgumentImpl& operator=(JITCompiledFunctionWithArgumentImpl const&) = delete;
  JITCompiledFunctionWithArgumentImpl& operator=(JITCompiledFunctionWithArgumentImpl&&) = delete;

 public:
  JITCompiledFunctionWithArgumentImpl(current::sync::Borrowed<JITCallContextImpl> call_context_impl,
                                      std::vector<uint8_t> code)
      : ctx_(std::move(call_context_impl)),
        this_function_index_in_order_(ctx_->CurrentFunctionIndexAndPostIncrementIt()),
        code_size_(code.size()),
        f_(code) {}

  double CallFunctionWithArgument(double const* x, double p) const {
    ctx_->MarkFunctionComputedOrThrowIfPrerequisitesNotMet(this_function_index_in_order_);
    ctx_->InternalRAMPointer()[ctx_->LambdaRAMOffset()] = p;
    return f_(x, ctx_->InternalRAMPointer(), &current::Singleton<JITCallContextFunctionPointers>().fns[0]);
  }

  size_t CodeSize() const { return code_size_; }
};

class JITCompiledFunctionWithArgument final {
 private:
  std::unique_ptr<JITCompiledFunctionWithArgumentImpl> f_;

  friend class JITCompiler;
  JITCompiledFunctionWithArgument(current::sync::Borrowed<JITCallContextImpl> call_context_impl,
                                  std::vector<uint8_t> code)
      : f_(std::make_unique<JITCompiledFunctionWithArgumentImpl>(std::move(call_context_impl), code)) {}

 public:
  double operator()(double const* x, double p) const { return f_->CallFunctionWithArgument(x, p); }
  double operator()(std::vector<double> const& x, double p) const { return f_->CallFunctionWithArgument(&x[0], p); }
  double operator()(Vars const& values, double p) const { return f_->CallFunctionWithArgument(&values.x[0], p); }
  size_t CodeSize() const { return f_->CodeSize(); }
};

class JITCompiler final {
 private:
  // All the JIT-compiled functions will borrow this borrowed context and tie to it.
  current::sync::Borrowed<JITCallContextImpl> jit_call_context_impl_;

  // The vars context is where all the expression nodes are stored, and it's essential
  // it exists during the lifetime of the very `JITCompiler`.
  Vars::Scope const& vars_scope_;

  // Effecively, the offset in the array of `double`-s where the `lambda` is stored.
  size_t const number_of_nodes_;

  // Each node is computed only once per the instance of `JITCompiler`, hence if multiple functions were compiled per
  // one `JITCompiler`, the user must, for any new point or for any new lambda, call them in the order of compilation.
  std::vector<bool> node_computed_;

  // The "recursive" call to `*EnsureNodeComputed` should not be recursive because of the possibility of large depth.
  // For its stack to not be re-allocated and re-grown multiple times, keep its RAM allocated.
  std::vector<ExpressionNodeIndex> manual_stack_;

  void NonRecursiveEnsureNodeComputed(std::vector<uint8_t>& code, ExpressionNodeIndex requested_index) {
#ifndef NDEBUG
    if (!manual_stack_.empty()) {
      TriggerSegmentationFault();
    }
#endif

    auto const PushNodeToStack = [this](ExpressionNodeIndex index, bool ready_to_compute_flag) {
#ifdef NDEBUG
      if (index.UncheckedIsSpecificallyNodeIndex()) {
        size_t const node_index = static_cast<size_t>(index.UncheckedNodeIndex());
        if (!node_computed_[node_index]) {
          if (ready_to_compute_flag) {
            index.SetSpecialTwoBitsValue(1);
          }
          manual_stack_.push_back(index);
        }
      }
#else
      // This is the safe, `Checked`, implementation.
      index.CheckedDispatch(
          [&](size_t node_index) {
            if (!node_computed_[node_index]) {
              if (ready_to_compute_flag) {
                index.SetSpecialTwoBitsValue(1);
              }
              manual_stack_.push_back(index);
            }
          },
          [](size_t) {
            // No need to push variables to the "generate JIT code" stack, as their values are already available.
          },
          [](double) {
            // No need to "compile" immediate `double` values.
          },
          []() {
            // No need to do anything for the value of the lambda either.
          });
#endif
    };

    PushNodeToStack(requested_index, false);

    while (!manual_stack_.empty()) {
      ExpressionNodeIndex current_node_full_index = manual_stack_.back();
      manual_stack_.pop_back();
      bool const ready_to_compute = current_node_full_index.ClearSpecialTwoBitsAndReturnWhatTheyWere();

      if (current_node_full_index.UncheckedIsSpecificallyNodeIndex()) {
        size_t const current_node_index = current_node_full_index.UncheckedNodeIndex();
#ifndef NDEBUG
        if (!(current_node_index < node_computed_.size())) {
          TriggerSegmentationFault();
        }
#endif
        if (!node_computed_[current_node_index]) {
          using namespace current::fncas::x64_native_jit;
          ExpressionNodeImpl const& node = vars_scope_[current_node_index];
          ExpressionNodeType const type = node.Type();
          if (false) {
#define CURRENT_EXPRESSION_MATH_OPERATION(op, op2, name)                                                         \
  }                                                                                                              \
  else if (type == ExpressionNodeType::Operation_##name) {                                                       \
    ExpressionNodeIndex const lhs = node.LHSIndex();                                                             \
    ExpressionNodeIndex const rhs = node.RHSIndex();                                                             \
    if (!ready_to_compute) {                                                                                     \
      PushNodeToStack(ExpressionNodeIndex::FromNodeIndex(current_node_index), true);                             \
      PushNodeToStack(rhs, false);                                                                               \
      PushNodeToStack(lhs, false);                                                                               \
    } else {                                                                                                     \
      lhs.CheckedDispatch([&](size_t idx) { opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, idx); },       \
                          [&](size_t var) { opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, var); },       \
                          [&](double val) { opcodes::load_immediate_to_xmm0(code, val); },                       \
                          [&]() { opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, number_of_nodes_); });   \
      rhs.CheckedDispatch([&](size_t idx) { opcodes::name##_from_memory_by_rbx_offset_to_xmm0(code, idx); },     \
                          [&](size_t var) { opcodes::name##_from_memory_by_rdi_offset_to_xmm0(code, var); },     \
                          [&](double val) {                                                                      \
                            opcodes::load_immediate_to_xmm1(code, val);                                          \
                            opcodes::name##_xmm1_xmm0(code);                                                     \
                          },                                                                                     \
                          [&]() { opcodes::name##_from_memory_by_rbx_offset_to_xmm0(code, number_of_nodes_); }); \
      opcodes::store_xmm0_to_memory_by_rbx_offset(code, current_node_index);                                     \
      node_computed_[current_node_index] = true;                                                                 \
    }
#include "../math_operations.inl"
#undef CURRENT_EXPRESSION_MATH_OPERATION

#define CURRENT_EXPRESSION_MATH_FUNCTION(fn)                                                                   \
  }                                                                                                            \
  else if (type == ExpressionNodeType::Function_##fn) {                                                        \
    ExpressionNodeIndex const argument = node.ArgumentIndex();                                                 \
    if (!ready_to_compute) {                                                                                   \
      PushNodeToStack(ExpressionNodeIndex::FromNodeIndex(current_node_index), true);                           \
      PushNodeToStack(argument, false);                                                                        \
    } else {                                                                                                   \
      using namespace current::fncas::x64_native_jit;                                                          \
      argument.CheckedDispatch(                                                                                \
          [&](size_t idx) { opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, idx); },                     \
          [&](size_t var) { opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, var); },                     \
          [&](double val) { opcodes::load_immediate_to_memory_by_rbx_offset(code, current_node_index, val); }, \
          [&]() { opcodes::load_from_memory_by_rbx_offset_to_xmm0(code, number_of_nodes_); });                 \
      opcodes::push_rdi(code);                                                                                 \
      opcodes::push_rdx(code);                                                                                 \
      opcodes::call_function_from_rdx_pointers_array_by_index(                                                 \
          code, static_cast<uint8_t>(ExpressionFunctionIndex::FunctionIndexOf_##fn));                          \
      opcodes::pop_rdx(code);                                                                                  \
      opcodes::pop_rdi(code);                                                                                  \
      opcodes::store_xmm0_to_memory_by_rbx_offset(code, current_node_index);                                   \
      node_computed_[current_node_index] = true;                                                               \
    }
#include "../math_functions.inl"
#undef CURRENT_EXPRESSION_MATH_FUNCTION
          } else {
#ifndef NDEBUG
            TriggerSegmentationFault();
#endif
          }
        }
      } else {
// Seeing a var node, a double value node, or a lambda node.
// The stack should only contain indexes that are expression nodes.
#ifndef NDEBUG
        TriggerSegmentationFault();
#endif
      }
    }
  }

 public:
  JITCompiler(JITCallContext& jit_call_context, Vars::Scope& scope = InternalTLS())
      : jit_call_context_impl_(jit_call_context.BorrowImpl()),
        vars_scope_(scope),
        number_of_nodes_(scope.VarsConfig().NumberOfNodes()),
        node_computed_(number_of_nodes_) {}

  JITCompiledFunction Compile(value_t node) {
    using namespace current::fncas::x64_native_jit;

    // TODO(dkorolev): Inplace code generation.
    std::vector<uint8_t> code;

    ExpressionNodeIndex index = node.GetExpressionNodeIndex();
    index.CheckedDispatch(
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
          opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, var_index);
        },
        [&](double value) {
          // Value. Just load it into xmm0.
          opcodes::load_immediate_to_xmm0(code, value);
        },
        [&]() {
          // Lambda. Just load its value into xmm0.
          opcodes::load_from_memory_by_rsi_offset_to_xmm0(code, number_of_nodes_);
        });
    opcodes::ret(code);

    return JITCompiledFunction(jit_call_context_impl_, std::move(code));
  }

  JITCompiledFunctionReturningVector Compile(std::vector<value_t> const& nodes) {
    using namespace current::fncas::x64_native_jit;

    // TODO(dkorolev): Inplace code generation.
    std::vector<uint8_t> code;

    opcodes::push_rbx(code);
    opcodes::mov_rsi_rbx(code);

    std::vector<ExpressionNodeIndex> output_node_indexes(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
      output_node_indexes[i] = static_cast<ExpressionNodeIndex>(nodes[i].GetExpressionNodeIndex());
    }

    for (ExpressionNodeIndex const index : output_node_indexes) {
      // Outside the very index nodes, var nodes, double value nodes, and lamda nodes do not need to be JIT-compiled.
      if (index.UncheckedIsSpecificallyNodeIndex()) {
        NonRecursiveEnsureNodeComputed(code, index);
      }
    }

    opcodes::pop_rbx(code);
    opcodes::ret(code);

    return JITCompiledFunctionReturningVector(jit_call_context_impl_, std::move(code), std::move(output_node_indexes));
  }

  JITCompiledFunctionWithArgument CompileFunctionWithArgument(value_t node) {
    using namespace current::fncas::x64_native_jit;

    // TODO(dkorolev): Inplace code generation.
    std::vector<uint8_t> code;

    ExpressionNodeIndex const index = node.GetExpressionNodeIndex();
    index.CheckedDispatch(
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
          opcodes::load_from_memory_by_rdi_offset_to_xmm0(code, var_index);
        },
        [&](double value) {
          // Value. Just load it into xmm0.
          opcodes::load_immediate_to_xmm0(code, value);
        },
        [&]() {
          // Lambda. Just load its value into xmm0.
          opcodes::load_from_memory_by_rsi_offset_to_xmm0(code, number_of_nodes_);
        });
    opcodes::ret(code);

    return JITCompiledFunctionWithArgument(jit_call_context_impl_, std::move(code));
  }
};

}  // namespace current::expression
}  // namespace current

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED

#endif  // #ifndef OPTIMIZE_JIT_JIT_H
