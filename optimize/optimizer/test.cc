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

  OptimizationContext optimization_context(vars_context, f);
  LineSearchContext const line_search_context(optimization_context);

  // The function and its gradient must be computed prior to the line search being invoked, in order for the internal
  // `jit_call_context` nodes to be properly initialized. In the case of multidimensional gradient descent optimization
  // this will happen organically, as no line search begins w/o computing the gradient (and computing the function is
  // the prerequisite for computing the gradient). In the case of explicitly testing the 1D optimizer, the compuatation
  // of both the function and the gradient must be invoked manually beforehand, for each starting point.
  optimization_context.compiled_f(optimization_context.jit_call_context, optimization_context.vars_mapper.x);
  optimization_context.compiled_g(optimization_context.jit_call_context, optimization_context.vars_mapper.x);

  // In case of the quadratic function, see `../differentiate/test.cc`, the first and best step is always  `-0.5`.
  EXPECT_EQ(-0.5, LineSearch(line_search_context));

  // This step should take the function to its optimum, which, in this case, is the minimum, equals to zero.
  EXPECT_EQ(
      0.0,
      optimization_context.compiled_l(optimization_context.jit_call_context, optimization_context.vars_mapper.x, -0.5));

  EXPECT_EQ("[0.0]", JSON(optimization_context.CurrentPoint()));
  EXPECT_EQ(9.0, optimization_context.ComputeCurrentObjectiveFunctionValue());
  optimization_context.MovePointAlongGradient(-0.5);
  EXPECT_EQ("[3.0]", JSON(optimization_context.CurrentPoint()));
  EXPECT_EQ(0.0, optimization_context.ComputeCurrentObjectiveFunctionValue());
}

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED
