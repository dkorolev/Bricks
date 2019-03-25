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

  jit::Function const f = jit::JITCompiler(jit_call_context).Compile(value);

  VarsMapper input(jit_call_context.Config());
  EXPECT_EQ(2.0, f(jit_call_context, input.x));

  input["b"] = 2.0;
  EXPECT_EQ(3.0, f(jit_call_context, input));  // Can pass `input` instead of `input.x`.

  input["b"] = -2.0;
  EXPECT_EQ(-1.0, f(jit_call_context, input));
}

TEST(OptimizationJIT, SmokeFunctionReturningVector) {
  using namespace current::expression;

  VarsContext context;

  x["a"] = 1.0;
  x["b"] = 1.0;
  std::vector<value_t> const values({x["a"] + x["b"], x["a"] - x["b"], x["a"] * x["b"], x["a"] / x["b"]});

  jit::JITCallContext jit_call_context;

  jit::FunctionReturningVector const g = jit::JITCompiler(jit_call_context).Compile(values);

  {
    VarsMapper input(jit_call_context.Config());
    input["a"] = 10.0;
    input["b"] = 5.0;
    EXPECT_EQ("[15.0,5.0,50.0,2.0]", JSON(g(jit_call_context, input.x)));
  }

  EXPECT_EQ("[6.0,2.0,8.0,2.0]", JSON(g(jit_call_context, {4.0, 2.0})));
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

TEST(OptimizationJIT, OtherMathFunctions) {
  using namespace current::expression;

  EXPECT_EQ(14u, static_cast<size_t>(ExpressionFunctionIndex::TotalFunctionsCount));

  VarsContext context;

  x["p"] = 0.0;
  value_t const p = x["p"];

  // Functions list deliberately copy-pased here, for the test to double-check the set of them.
  std::vector<value_t> const magic({exp(p),
                                    log(p),
                                    sin(p),
                                    cos(p),
                                    tan(p),
                                    sqr(p),
                                    sqrt(p),
                                    asin(p),
                                    acos(p),
                                    atan(p),
                                    unit_step(p),
                                    ramp(p),
                                    sigmoid(p),
                                    log_sigmoid(p)});
  EXPECT_EQ(static_cast<size_t>(ExpressionFunctionIndex::TotalFunctionsCount), magic.size());

  jit::JITCallContext ctx;
  jit::FunctionReturningVector const f = jit::JITCompiler(ctx).Compile(magic);

  // Test `1.5` as a random point that makes sense.
  EXPECT_EQ(exp(1.5), f(ctx, {1.5})[0]);
  EXPECT_EQ(log(1.5), f(ctx, {1.5})[1]);
  EXPECT_EQ(sin(1.5), f(ctx, {1.5})[2]);
  EXPECT_EQ(cos(1.5), f(ctx, {1.5})[3]);
  EXPECT_EQ(tan(1.5), f(ctx, {1.5})[4]);
  EXPECT_EQ(sqr(1.5), f(ctx, {1.5})[5]);
  EXPECT_EQ(sqrt(1.5), f(ctx, {1.5})[6]);
  EXPECT_EQ(asin(0.75), f(ctx, {0.75})[7]);  // Obviously, no `1.5` input for asin/acos.
  EXPECT_EQ(acos(0.75), f(ctx, {0.75})[8]);
  EXPECT_EQ(atan(1.5), f(ctx, {1.5})[9]);
  EXPECT_EQ(unit_step(1.5), f(ctx, {1.5})[10]);
  EXPECT_EQ(ramp(1.5), f(ctx, {1.5})[11]);
  EXPECT_EQ(sigmoid(1.5), f(ctx, {1.5})[12]);
  EXPECT_EQ(log_sigmoid(1.5), f(ctx, {1.5})[13]);

  // Make sure custom functions do what they should.
  EXPECT_EQ(4, f(ctx, {2})[5]);  // sqr()

  EXPECT_EQ(1.0, f(ctx, {+0.5})[10]);  // unit_step()
  EXPECT_EQ(0.0, f(ctx, {-0.5})[10]);  // unit_step()

  EXPECT_EQ(0.0, f(ctx, {-0.5})[11]);  // ramp()
  EXPECT_EQ(0.5, f(ctx, {+0.5})[11]);  // ramp()
  EXPECT_EQ(2.5, f(ctx, {+2.5})[11]);  // ramp()

  EXPECT_EQ(1.0 / (1.0 + exp(-3.5)), f(ctx, {3.5})[12]);        // sigmoid()
  EXPECT_EQ(log(1.0 / (1.0 + exp(+1.5))), f(ctx, {-1.5})[13]);  // log_sigmoid()
}

TEST(OptimizationJIT, IntermediateResultsAreReused) {
  using namespace current::expression;

  VarsContext context;

  x["p"] = 0.0;
  value_t const a = sqrt(1.0 + (2.0 + sqr(x["p"])) - 3.0);  // To make sure `a` is not "optimized away" :-) -- D.K.
  value_t const b = a + 1.0;
  value_t const c = b + 2.0;

  jit::JITCallContext jit_call_context;
  jit::JITCompiler compiler(jit_call_context);

  // Same instance of `jit::JITCompiler` should be used for caching to take place.
  jit::Function const fa = compiler.Compile(a);
  jit::Function const fb = compiler.Compile(b);
  jit::Function const fc = compiler.Compile(c);

  VarsMapper input(jit_call_context.Config());

  // Compute for `0`.
  EXPECT_EQ(0, fa(jit_call_context, {0.0}));
  EXPECT_EQ(1, fb(jit_call_context, {0.0}));
  EXPECT_EQ(3, fc(jit_call_context, {0.0}));

  // Compute for `1`.
  EXPECT_EQ(1, fa(jit_call_context, {1.0}));
  EXPECT_EQ(2, fb(jit_call_context, {1.0}));
  EXPECT_EQ(4, fc(jit_call_context, {1.0}));

  // The values for `b` and `c`, for `p=100`, should be the same,
  // because `a`, which is equal to `x["p"]`, remains cached as `1`.
  EXPECT_EQ(2, fb(jit_call_context, {100.0}));
  EXPECT_EQ(4, fc(jit_call_context, {100.0}));

  // If the extra safety measure of `jit_call_context.MarkNewPoint()` is used, the above logical "flaw" will throw.
  jit_call_context.MarkNewPoint();
  EXPECT_EQ(0, fa(jit_call_context, {0.0}));
  EXPECT_EQ(1, fb(jit_call_context, {0.0}));
  EXPECT_EQ(3, fc(jit_call_context, {0.0}));
  jit_call_context.MarkNewPoint();
  EXPECT_EQ(1, fa(jit_call_context, {1.0}));
  EXPECT_EQ(2, fb(jit_call_context, {1.0}));
  EXPECT_EQ(4, fc(jit_call_context, {1.0}));
  jit_call_context.MarkNewPoint();
  EXPECT_THROW(fb(jit_call_context, {100.0}), FunctionInvokedBeforeItsPrerequisitesException);
  EXPECT_THROW(fc(jit_call_context, {100.0}), FunctionInvokedBeforeItsPrerequisitesException);
  jit_call_context.MarkNewPoint();
  EXPECT_EQ(100, fa(jit_call_context, {100.0}));
  EXPECT_EQ(101, fb(jit_call_context, {100.0}));
  EXPECT_EQ(103, fc(jit_call_context, {100.0}));
}

TEST(OptimizationJIT, DoublesAreStoredWithPerfectMachinePrecision) {
  using namespace current::expression;

  VarsContext context;

  x["t"] = 0.0;
  value_t const r = x["t"] - sqrt(2.0) + 1.0;

  jit::JITCallContext jit_call_context;
  jit::Function const f = jit::JITCompiler(jit_call_context).Compile(r);

  VarsMapper input(jit_call_context.Config());

  // Should be exactly one, as a machine-imperfect `sqrt(2.0)` is being subtracted from the very same value.
  EXPECT_EQ(1.0, f(jit_call_context, {sqrt(2.0)}));

  // Should be the exact value again.
  EXPECT_EQ(0.0 - sqrt(2.0) + 1.0, f(jit_call_context, {0.0}));
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

TEST(OptimizationJIT, FunctionWithArgument) {
  using namespace current::expression;

  VarsContext context;

  x["a"] = 0.0;
  value_t const lambda = value_t::lambda();
  value_t const formula = x["a"] + lambda;

  jit::JITCallContext jit_call_context;
  jit::JITCompiler code_generator(jit_call_context);
  jit::FunctionWithArgument const f = code_generator.CompileFunctionWithArgument(formula);

  EXPECT_EQ(0.0, f(jit_call_context, {0.0}, 0.0));
  EXPECT_EQ(1.0, f(jit_call_context, {1.0}, 0.0));
  EXPECT_EQ(1.0, f(jit_call_context, {0.0}, 1.0));
  EXPECT_EQ(2.0, f(jit_call_context, {1.0}, 1.0));
}

TEST(OptimizationJIT, FunctionWithArgumentReturningArgumentItself) {
  // This case is special, since the effective implementation of this `lambda` argument
  // never actually has to actually "compute" the expression node of the respective type. -- D.K.
  using namespace current::expression;

  VarsContext context;
  value_t const lambda = value_t::lambda();

  jit::JITCallContext jit_call_context;
  jit::JITCompiler code_generator(jit_call_context);
  jit::FunctionWithArgument const f = code_generator.CompileFunctionWithArgument(lambda);

  EXPECT_EQ(0.0, f(jit_call_context, {}, 0.0));
  EXPECT_EQ(0.5, f(jit_call_context, {}, 0.5));
  EXPECT_EQ(1.0, f(jit_call_context, {}, 1.0));
}

inline void RunOptimizationJITStressTest(size_t dim) {
  using namespace current::expression;

  VarsContext vars_context;

  for (size_t i = 0; i < dim; ++i) {
    x[i] = 0.0;
  }

  value_t f = ExpressionNode::FromImmediateDouble(0.0);
  for (size_t i = 0; i < dim; ++i) {
    f += exp(x[i]);
  }

  VarsMapperConfig const vars_config = vars_context.Freeze();
  jit::JITCallContext jit_call_context(vars_config);
  jit::Function const copiled_f = jit::JITCompiler(jit_call_context).Compile(f);

  VarsMapper input(vars_config);
  EXPECT_EQ(dim, copiled_f(jit_call_context, input.x));
}

TEST(OptimizationJIT, JITStressTest1KExponents) { RunOptimizationJITStressTest(1000u); }

TEST(OptimizationJIT, JITStressTest5KExponents) { RunOptimizationJITStressTest(5000u); }

TEST(OptimizationJIT, JITStressTest10KExponents) { RunOptimizationJITStressTest(10 * 1000u); }

TEST(OptimizationJIT, JITStressTest100KExponents) { RunOptimizationJITStressTest(100 * 1000u); }

TEST(OptimizationJIT, JITStressTest1MExponents) { RunOptimizationJITStressTest(1000 * 1000u); }

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED
