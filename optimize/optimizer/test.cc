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

#include "line_search.h"

#include "../../3rdparty/gtest/gtest-main.h"

#ifdef FNCAS_X64_NATIVE_JIT_ENABLED

TEST(OptimizationOptimizerLineSearch, QuadraticFunction) {
  using namespace current::expression;
  using namespace current::expression::optimizer;

  VarsContext vars_context;

  x[0] = 0.0;
  value_t const f = sqr(x[0] - 3.0);

  VarsMapperConfig const config = vars_context.ReindexVars();
  VarsMapper vars_mapper(config);

  std::vector<value_t> const g = ComputeGradient(f);
  value_t const l = GenerateLineSearchFunction(config, f, g);

  value_t const d1 = DifferentiateByLambda(l);
  value_t const d2 = DifferentiateByLambda(d1);
  value_t const d3 = DifferentiateByLambda(d2);

  jit::JITCallContext jit_call_context;
  jit::JITCompiler compiler(jit_call_context);
  jit::Function const compiled_f = compiler.Compile(f);
  jit::FunctionReturningVector const compiled_g = compiler.Compile(g);
  jit::FunctionWithArgument const compiled_l = compiler.CompileFunctionWithArgument(d1);
  jit::FunctionWithArgument const compiled_d1 = compiler.CompileFunctionWithArgument(d1);
  jit::FunctionWithArgument const compiled_d2 = compiler.CompileFunctionWithArgument(d2);
  jit::FunctionWithArgument const compiled_d3 = compiler.CompileFunctionWithArgument(d3);
  std::vector<jit::FunctionWithArgument const*> const compiled_ds({&compiled_d2, &compiled_d3});

  // NOTE(dkorolev): IMPORTANT: The function and its gradient should be computed prior to the line search being invoked,
  // in order for the internal `jit_call_context` nodes to be properly initialized.
  compiled_f(jit_call_context, vars_mapper.x);
  compiled_g(jit_call_context, vars_mapper.x);

  LineSearchContext const line_search_context(jit_call_context, vars_mapper, compiled_l, compiled_d1, compiled_ds);

  // In case of the quadratic function, see the `../differentiate/test.cc` test, the first and best step is `-0.5`.
  EXPECT_EQ(-0.5, LineSearch(line_search_context));

  // This step should take the function to its optimum, which, in this case, is the minimum, equals to zero.
  EXPECT_EQ(0.0, compiled_l(jit_call_context, vars_mapper.x, -0.5));
}

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED
