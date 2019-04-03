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

  Vars::Scope scope;

  x["a"] = 1.0;
  value_t const value = x["a"] + x["a"];

  // The constuctor of `JITCallContext` allocates the RAM buffer for the temporary computations.
  JITCallContext jit_call_context(scope);

  // The instance of `JITCompiler` can emit one or more compiled functiont, which would all operate on the same
  // instance of `JITCallContext`, so that they, when called in the order of compilation, reuse intermediate results.
  JITCompiler code_generator(jit_call_context);
  JITCompiledFunction const f = code_generator.Compile(value);

  Vars values;
  EXPECT_EQ(2.0, f(values.x));

  // Other calling semantics.
  values["a"] = 2.0;
  EXPECT_EQ(4.0, f(values));

  values["a"] = -2.0;
  EXPECT_EQ(-4.0, f(&values.x[0]));

  EXPECT_EQ(5.0, f({2.5}));
}

TEST(OptimizationJIT, SmokeAddConstant) {
  using namespace current::expression;

  std::unique_ptr<Vars::Scope> scope = std::make_unique<Vars::Scope>();

  x["b"] = 1.0;
  value_t const value = x["b"] + 1.0;
  Vars::Config const vars_config = scope->VarsConfig();

  // Compilation still requires the context, because that's where the expression tree is stored.
  JITCallContext jit_call_context(vars_config);
  JITCompiledFunction const f = JITCompiler(jit_call_context).Compile(value);

  // Once the function is JIT-compiled, the vars context (and the function compilation context) can be freed.
  scope = nullptr;

  Vars values(vars_config);
  EXPECT_EQ(2.0, f(values.x));

  values["b"] = 2.0;
  EXPECT_EQ(3.0, f(values));  // Can pass `values` instead of `values.x`.

  values["b"] = -2.0;
  EXPECT_EQ(-1.0, f(values));
}

TEST(OptimizationJIT, SmokeJITCompiledFunctionReturningVector) {
  using namespace current::expression;

  Vars::Scope scope;

  x["a"] = 1.0;
  x["b"] = 1.0;
  std::vector<value_t> const values({x["a"] + x["b"], x["a"] - x["b"], x["a"] * x["b"], x["a"] / x["b"]});

  JITCallContext jit_call_context;

  JITCompiledFunctionReturningVector const g = JITCompiler(jit_call_context).Compile(values);

  {
    Vars values;
    values["a"] = 10.0;
    values["b"] = 5.0;
    EXPECT_EQ("[15.0,5.0,50.0,2.0]", JSON(g(values)));
  }

  EXPECT_EQ("[6.0,2.0,8.0,2.0]", JSON(g({4.0, 2.0})));

  {
    // Also test `AddTo()`.
    std::vector<double> output(4u, 100.0);
    EXPECT_EQ("[100.0,100.0,100.0,100.0]", JSON(output));
    g.AddTo({10.0, 5.0}, output);
    EXPECT_EQ("[115.0,105.0,150.0,102.0]", JSON(output));
    g.AddTo({10.0, 5.0}, &output[0]);
    EXPECT_EQ("[130.0,110.0,200.0,104.0]", JSON(output));

#ifndef NDEBUG
    output.resize(10u);
    ASSERT_THROW(g.AddTo({10.0, 5.0}, output), JITReturnVectorDimensionsMismatch);
#endif
  }
}

TEST(OptimizationJIT, Exp) {
  using namespace current::expression;

  Vars::Scope scope;

  x["c"] = 0.0;
  value_t const value = exp(x["c"]);

  // No need to provide the `scope`, the thread-local singleton one will be used by default.
  JITCallContext jit_call_context;

  // Confirm that the lifetime of `JITCompiler` is not necessary for the functions to be called.
  std::unique_ptr<JITCompiler> disposable_code_generator = std::make_unique<JITCompiler>(jit_call_context);

  JITCompiledFunction const f = [&]() {
    // Confirm that the very instance of `JITCompiler` does not have to live for the function(s) to be called,
    // it's the lifetime of `JITCallContext` that is important.
    JITCompiler inner_disposable_code_generator(jit_call_context);
    return inner_disposable_code_generator.Compile(value);
  }();

  Vars values;

  disposable_code_generator = nullptr;

  EXPECT_EQ(exp(0.0), f(values));

  values["c"] = 1.0;
  EXPECT_EQ(exp(1.0), f(values));

  values["c"] = 2.0;
  EXPECT_EQ(exp(2.0), f(values));

  values["c"] = -1.0;
  EXPECT_EQ(exp(-1.0), f(values));

  values["c"] = -2.0;
  EXPECT_EQ(exp(-2.0), f(values));
}

TEST(OptimizationJIT, OtherMathFunctions) {
  using namespace current::expression;

  EXPECT_EQ(14u, static_cast<size_t>(ExpressionFunctionIndex::TotalFunctionsCount));

  Vars::Scope scope;

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

  JITCallContext ctx;
  JITCompiledFunctionReturningVector const f = JITCompiler(ctx).Compile(magic);

  // Test `1.5` as a random point that makes sense.
  EXPECT_EQ(exp(1.5), f({1.5})[0]);
  EXPECT_EQ(log(1.5), f({1.5})[1]);
  EXPECT_EQ(sin(1.5), f({1.5})[2]);
  EXPECT_EQ(cos(1.5), f({1.5})[3]);
  EXPECT_EQ(tan(1.5), f({1.5})[4]);
  EXPECT_EQ(sqr(1.5), f({1.5})[5]);
  EXPECT_EQ(sqrt(1.5), f({1.5})[6]);
  EXPECT_EQ(asin(0.75), f({0.75})[7]);  // Obviously, no `1.5` input for asin/acos.
  EXPECT_EQ(acos(0.75), f({0.75})[8]);
  EXPECT_EQ(atan(1.5), f({1.5})[9]);
  EXPECT_EQ(unit_step(1.5), f({1.5})[10]);
  EXPECT_EQ(ramp(1.5), f({1.5})[11]);
  EXPECT_EQ(sigmoid(1.5), f({1.5})[12]);
  EXPECT_EQ(log_sigmoid(1.5), f({1.5})[13]);

  // Make sure custom functions do what they should.
  EXPECT_EQ(4, f({2})[5]);  // sqr()

  EXPECT_EQ(1.0, f({+0.5})[10]);  // unit_step()
  EXPECT_EQ(0.0, f({-0.5})[10]);  // unit_step()

  EXPECT_EQ(0.0, f({-0.5})[11]);  // ramp()
  EXPECT_EQ(0.5, f({+0.5})[11]);  // ramp()
  EXPECT_EQ(2.5, f({+2.5})[11]);  // ramp()

  EXPECT_EQ(1.0 / (1.0 + exp(-3.5)), f({3.5})[12]);        // sigmoid()
  EXPECT_EQ(log(1.0 / (1.0 + exp(+1.5))), f({-1.5})[13]);  // log_sigmoid()
}

TEST(OptimizationJIT, IntermediateResultsAreReused) {
  using namespace current::expression;

  Vars::Scope scope;

  x["p"] = 0.0;
  value_t const a = sqrt(1.0 + (2.0 + sqr(x["p"])) - 3.0);  // To make sure `a` is not "optimized away" :-) -- D.K.
  value_t const b = a + 1.0;
  value_t const c = b + 2.0;

  JITCallContext jit_call_context;
  JITCompiler compiler(jit_call_context);

  // Same instance of `JITCompiler` should be used for caching to take place.
  JITCompiledFunction const fa = compiler.Compile(a);
  JITCompiledFunction const fb = compiler.Compile(b);
  JITCompiledFunction const fc = compiler.Compile(c);

  Vars values;

  // Compute for `0`.
  EXPECT_EQ(0, fa({0.0}));
  EXPECT_EQ(1, fb({0.0}));
  EXPECT_EQ(3, fc({0.0}));

  // Compute for `1`.
  EXPECT_EQ(1, fa({1.0}));
  EXPECT_EQ(2, fb({1.0}));
  EXPECT_EQ(4, fc({1.0}));

  // The values for `b` and `c`, for `p=100`, should be the same,
  // because `a`, which is equal to `x["p"]`, remains cached as `1`.
  EXPECT_EQ(2, fb({100.0}));
  EXPECT_EQ(4, fc({100.0}));

  // If the extra safety measure of `jit_call_context.MarkNewPoint()` is used, the above logical "flaw" will throw.
  jit_call_context.MarkNewPoint();
  EXPECT_EQ(0, fa({0.0}));
  EXPECT_EQ(1, fb({0.0}));
  EXPECT_EQ(3, fc({0.0}));
  jit_call_context.MarkNewPoint();
  EXPECT_EQ(1, fa({1.0}));
  EXPECT_EQ(2, fb({1.0}));
  EXPECT_EQ(4, fc({1.0}));
  jit_call_context.MarkNewPoint();
  EXPECT_THROW(fb({100.0}), JITCompiledFunctionInvokedBeforeItsPrerequisitesException);
  EXPECT_THROW(fc({100.0}), JITCompiledFunctionInvokedBeforeItsPrerequisitesException);
  jit_call_context.MarkNewPoint();
  EXPECT_EQ(100, fa({100.0}));
  EXPECT_EQ(101, fb({100.0}));
  EXPECT_EQ(103, fc({100.0}));
}

TEST(OptimizationJIT, DoublesAreStoredWithPerfectMachinePrecision) {
  using namespace current::expression;

  Vars::Scope scope;

  x["t"] = 0.0;
  value_t const r = x["t"] - sqrt(2.0) + 1.0;

  JITCallContext jit_call_context;
  JITCompiledFunction const f = JITCompiler(jit_call_context).Compile(r);

  // Should be exactly one, as a machine-imperfect `sqrt(2.0)` is being subtracted from the very same value.
  EXPECT_EQ(1.0, f({sqrt(2.0)}));

  // Should be the exact value again.
  EXPECT_EQ(0.0 - sqrt(2.0) + 1.0, f({0.0}));
}

TEST(OptimizationJIT, FunctionWithArgument) {
  using namespace current::expression;

  Vars::Scope scope;

  x["a"] = 0.0;
  value_t const lambda = value_t::lambda();
  value_t const formula = x["a"] + lambda + 1.0;

  JITCallContext jit_call_context;
  JITCompiler code_generator(jit_call_context);
  JITCompiledFunctionWithArgument const f = code_generator.CompileFunctionWithArgument(formula);

  EXPECT_EQ(1.0, f({0.0}, 0.0));
  EXPECT_EQ(2.0, f({1.0}, 0.0));
  EXPECT_EQ(2.0, f({0.0}, 1.0));
  EXPECT_EQ(3.0, f({1.0}, 1.0));
}

TEST(OptimizationJIT, FunctionWithArgumentReturningArgumentItself) {
  // This case is special, since the effective implementation of this `lambda` argument
  // never actually has to "compute" the expression node of the respective type. -- D.K.
  using namespace current::expression;

  Vars::Scope scope;
  value_t const lambda = value_t::lambda();

  JITCallContext jit_call_context;
  JITCompiler code_generator(jit_call_context);
  JITCompiledFunctionWithArgument const f = code_generator.CompileFunctionWithArgument(lambda);

  EXPECT_EQ(0.0, f({}, 0.0));
  EXPECT_EQ(0.5, f({}, 0.5));
  EXPECT_EQ(1.0, f({}, 1.0));
}

inline void RunOptimizationJITStressTest(size_t dim) {
  using namespace current::expression;

  Vars::Scope scope;

  for (size_t i = 0; i < dim; ++i) {
    x[i] = 0.0;
  }

  value_t f = 0.0;
  for (size_t i = 0; i < dim; ++i) {
    f += exp(x[i]);
  }

  JITCallContext jit_call_context;
  JITCompiledFunction const compiled_f = JITCompiler(jit_call_context).Compile(f);

  // The default constuctor of `Vars()` initializes itself with the starting point.
  EXPECT_EQ(dim, compiled_f(Vars()));

  // NOTE(dkorolev): The subtraction is because the very first "zero plus ..." node is optimized away.
  EXPECT_EQ(47u * dim - 10u, compiled_f.CodeSize());
}

TEST(OptimizationJIT, JITStressTest1KExponents) { RunOptimizationJITStressTest(1000u); }

TEST(OptimizationJIT, JITStressTest5KExponents) { RunOptimizationJITStressTest(5000u); }

TEST(OptimizationJIT, JITStressTest10KExponents) { RunOptimizationJITStressTest(10 * 1000u); }

// At 47 bytes of JIT code per exponent, 160K of them would be just under 8M, for our CI to not choke. -- D.K.
TEST(OptimizationJIT, JITStressTest160KExponents) { RunOptimizationJITStressTest(250 * 1000u); }

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED
