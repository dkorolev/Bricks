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

// TODO(dkorolev): Test several `jit_call_context`-s over the same function (once we have differentiation).

#include "jit.h"

#include "../../3rdparty/gtest/gtest-main.h"

#ifdef FNCAS_X64_NATIVE_JIT_ENABLED

TEST(OptimizationJIT, SmokeAdd) {
  using namespace current::expression;

  VarsContext context;

  x["a"] = 1.0;
  value_t const value = x["a"] + x["a"];

  // The call to `Freeze()` fixes the variables and nodes used.
  VarsMapperConfig const vars_config = context.Freeze();

  // The constuctor of `JITCallContext` allocates the RAM buffer for the temporary computations.
  jit::JITCallContext jit_call_context(vars_config);

  // The instance of `JITCompiler` can emit one or more compiled functiont, which would all operate on the same
  // instance of `JITCallContext`, so that they, when called in the order of compilation, reuse intermediate results.
  jit::JITCompiler code_generator(jit_call_context);
  jit::Function const f = code_generator.Compile(value);

  VarsMapper input(vars_config);
  EXPECT_EQ(2.0, f(jit_call_context, input.x));

  input["a"] = 2.0;
  EXPECT_EQ(4.0, f(jit_call_context, input.x));

  // Other calling synopsis.
  input["a"] = -2.0;
  EXPECT_EQ(-4.0, f(jit_call_context, &input.x[0]));

  EXPECT_EQ(5.0, f(jit_call_context, {2.5}));
}

TEST(OptimizationJIT, SmokeAddConstant) {
  using namespace current::expression;

  VarsContext context;

  x["b"] = 1.0;
  value_t const value = x["b"] + 1.0;

  // No need for `context.Freeze()`, it will happen automatically in the default constructor of `JITCallContext`.
  jit::JITCallContext jit_call_context;
  jit::JITCompiler code_generator(jit_call_context);
  jit::Function const f(code_generator.Compile(value));

  VarsMapper input(code_generator.Config());
  EXPECT_EQ(2.0, f(jit_call_context, input.x));

  input["b"] = 2.0;
  EXPECT_EQ(3.0, f(jit_call_context, input));  // Can pass `input` instead of `input.x`.

  input["b"] = -2.0;
  EXPECT_EQ(-1.0, f(jit_call_context, input));
}

TEST(OptimizationJIT, Exp) {
  using namespace current::expression;

  VarsContext context;

  x["c"] = 0.0;
  value_t const value = exp(x["c"]);

  // No need to provide `context`, the thread-local singleton will be used by default, and it will be `.Freeze()`-ed.
  jit::JITCallContext jit_call_context;

  // Confirm that the lifetime of `JITCompiler` is not necessary for the functions to be called.
  std::unique_ptr<jit::JITCompiler> disposable_code_generator = std::make_unique<jit::JITCompiler>(jit_call_context);

  jit::Function const f = [&]() {
    // Confirm that the very instance of `JITCompiler` does not have to live for the function(s) to be called,
    // it's the lifetime of `JITCallContext` that is important.
    jit::JITCompiler disposable_code_generator(jit_call_context);
    return disposable_code_generator.Compile(value);
  }();

  VarsMapper input(disposable_code_generator->Config());

  disposable_code_generator = nullptr;

  EXPECT_EQ(exp(0.0), f(jit_call_context, input));

  input["c"] = 1.0;
  EXPECT_EQ(exp(1.0), f(jit_call_context, input));

  input["c"] = 2.0;
  EXPECT_EQ(exp(2.0), f(jit_call_context, input));

  input["c"] = -1.0;
  EXPECT_EQ(exp(-1.0), f(jit_call_context, input));

  input["c"] = -2.0;
  EXPECT_EQ(exp(-2.0), f(jit_call_context, input));
}

TEST(OptimizationJIT, NeedActiveVarsContext) {
  using namespace current::expression;

  std::unique_ptr<jit::JITCallContext> illegal_jit_context = []() {
    VarsContext vars_context;
    return std::make_unique<jit::JITCallContext>(vars_context.Freeze());
  }();
  ASSERT_THROW(jit::JITCompiler illegal_code_generator(*illegal_jit_context), VarsManagementException);
}

TEST(OptimizationJIT, NoIntersectingGlobalJITCallContextsAllowed) {
  using namespace current::expression;

  VarsContext context;

  jit::JITCallContext jit_call_context;
  ASSERT_THROW(jit::JITCallContext illegal_call_context, VarsAlreadyFrozenException);
}

TEST(OptimizationJIT, JITGeneratorUnfreezesVarsContext) {
  using namespace current::expression;

  VarsContext context;

  { jit::JITCallContext call_context_1; }
  { jit::JITCallContext call_context_2; }
  {
    context.Freeze();
    ASSERT_THROW(jit::JITCallContext illegal_call_context, VarsAlreadyFrozenException);
  }
}

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED
