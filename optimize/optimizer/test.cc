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

#include "optimizer.h"

#include "../../3rdparty/gtest/gtest-main-with-dflags.h"

TEST(OptimizationOptimizer, TrivialSingleStepOptimization) {
  using namespace current::expression;
  using namespace current::expression::optimizer;

  VarsContext vars_context;

  x[0] = 0;
  x[1] = 0;

  value_t const f = sqr(x[0] - 3) + sqr(x[1] - 5);

  OptimizationContext optimization_context(vars_context, f);

  OptimizationResult const result = Optimize(optimization_context);

  EXPECT_EQ("[34.0,0.0]", JSON(result.values));
  EXPECT_EQ("[[0.0,0.0],[3.0,5.0]]", JSON(result.trace));
  EXPECT_EQ("[-0.5]", JSON(result.steps));

  // A single integer step, the result is exact.
  EXPECT_EQ("[3.0,5.0]", JSON(result.final_point));
  ASSERT_EQ(2u, result.final_point.size());
  EXPECT_EQ(3, result.final_point[0]);
  EXPECT_EQ(5, result.final_point[1]);
  EXPECT_EQ(0.0, result.final_value);

  EXPECT_EQ(0.0, optimization_context.compiled_f(optimization_context.jit_call_context, result.final_point));
  EXPECT_EQ("[0.0,0.0]",
            JSON(optimization_context.compiled_g(optimization_context.jit_call_context, result.final_point)));
}

TEST(OptimizationOptimizer, MultiStepOptimization) {
  using namespace current::expression;
  using namespace current::expression::optimizer;

  VarsContext vars_context;

  x[0] = 0.0;
  x[1] = 0.0;

  value_t const f = log(1 + exp(x[0] - 3)) + log(1 + exp(3 - x[0])) + log(1 + exp(x[1] - 5)) + log(1 + exp(5 - x[1]));

  OptimizationContext optimization_context(vars_context, f);
  OptimizationResult const result = Optimize(optimization_context);

  EXPECT_EQ(
      "["
      "[0.0,0.0],"
      "[3.8657811794817156,4.213713024184646],"
      "[3.0039879355304568,5.0043468627153129],"
      "[2.999999999460174,5.000000000495253],"
      "[3.0,5.0]"
      "]",
      JSON(result.trace));
  EXPECT_EQ("[8.11060540012572,3.105144018869468,2.7725974219447426,2.772588722239781,2.772588722239781]",
            JSON(result.values));
  EXPECT_EQ("[-4.270881774245182,-2.11361538852622,-2.0000029213342627,-1.9999998287475738]", JSON(result.steps));
}
