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

TEST(OptimizationOptimizer, IsNormal) {
  using namespace current::expression;  // For both `IsNormal()` and custom functions s.a. `sigmoid()`.

  EXPECT_TRUE(IsNormal(1));
  EXPECT_TRUE(IsNormal(42));
  EXPECT_TRUE(IsNormal(0.5));
  EXPECT_TRUE(IsNormal(-1));

  EXPECT_TRUE(IsNormal(0.0));
  EXPECT_TRUE(IsNormal(-0.0));

  EXPECT_TRUE(IsNormal(sqrt(2.0)));
  EXPECT_TRUE(IsNormal(log(2.0)));
  EXPECT_TRUE(IsNormal(exp(2.0)));
  EXPECT_TRUE(IsNormal(sin(2.0)));
  EXPECT_TRUE(IsNormal(cos(2.0)));
  EXPECT_TRUE(IsNormal(tan(2.0)));
  EXPECT_TRUE(IsNormal(asin(0.5)));
  EXPECT_TRUE(IsNormal(acos(0.5)));
  EXPECT_TRUE(IsNormal(atan(2.0)));
  EXPECT_TRUE(IsNormal(sigmoid(+10)));
  EXPECT_TRUE(IsNormal(sigmoid(-10)));
  EXPECT_TRUE(IsNormal(sigmoid(+100)));
  EXPECT_TRUE(IsNormal(sigmoid(-100)));
  EXPECT_TRUE(IsNormal(sigmoid(+1000)));
  EXPECT_TRUE(IsNormal(sigmoid(-1000)));
  EXPECT_TRUE(IsNormal(log_sigmoid(+10)));
  EXPECT_TRUE(IsNormal(log_sigmoid(-10)));
  EXPECT_TRUE(IsNormal(log_sigmoid(+100)));
  EXPECT_TRUE(IsNormal(log_sigmoid(-100)));
  EXPECT_TRUE(IsNormal(log_sigmoid(+1000)));
  EXPECT_TRUE(IsNormal(log_sigmoid(-1000)));

  EXPECT_FALSE(IsNormal(sqrt(-2.0)));
  EXPECT_FALSE(IsNormal(log(-2.0)));
  EXPECT_FALSE(IsNormal(log(0.0)));
  EXPECT_FALSE(IsNormal(exp(1000.0)));
  EXPECT_FALSE(IsNormal(asin(+1.5)));
  EXPECT_FALSE(IsNormal(acos(-1.5)));
}

TEST(OptimizationOptimizer, OptimizationDoesNotRequireVarsContext) {
  using namespace current::expression;
  using namespace current::expression::optimizer;

  std::unique_ptr<Vars::ThreadLocalContext> vars_context = std::make_unique<Vars::ThreadLocalContext>();

  x[0] = 0;
  x[1] = 0;

  value_t const f = sqr(x[0] - 3) + sqr(x[1] - 5);

  OptimizationContext optimization_context(f, *vars_context);

  Vars::Config const vars_config = vars_context->VarsConfig();

  vars_context = nullptr;

  OptimizationResult const result = Optimize(optimization_context);

  EXPECT_EQ(2u, result.iterations);

  EXPECT_EQ("[34.0,0.0]", JSON(result.values));
  EXPECT_EQ("[[0.0,0.0],[3.0,5.0]]", JSON(result.trace));
  EXPECT_EQ("[-0.5]", JSON(result.steps));

  EXPECT_EQ("[3.0,5.0]", JSON(result.final_point));
  ASSERT_EQ(2u, result.final_point.size());
  EXPECT_NEAR(3, result.final_point[0], 1e-6);
  EXPECT_NEAR(5, result.final_point[1], 1e-6);
  EXPECT_NEAR(0.0, result.final_value, 1e-6);

  EXPECT_EQ(0.0, optimization_context.compiled_f(result.final_point));
  EXPECT_EQ("[0.0,0.0]", JSON(optimization_context.compiled_g(result.final_point)));
}

TEST(OptimizationOptimizer, TrivialSingleStepOptimization) {
  using namespace current::expression;
  using namespace current::expression::optimizer;

  Vars::ThreadLocalContext vars_context;

  x[0] = 0;
  x[1] = 0;

  value_t const f = sqr(x[0] - 3) + sqr(x[1] - 5);

  OptimizationContext optimization_context(f);

  OptimizationResult const result = Optimize(optimization_context);

  EXPECT_EQ(2u, result.iterations);

  EXPECT_EQ("[34.0,0.0]", JSON(result.values));
  EXPECT_EQ("[[0.0,0.0],[3.0,5.0]]", JSON(result.trace));
  EXPECT_EQ("[-0.5]", JSON(result.steps));

  EXPECT_EQ("[3.0,5.0]", JSON(result.final_point));
  ASSERT_EQ(2u, result.final_point.size());
  EXPECT_NEAR(3, result.final_point[0], 1e-6);
  EXPECT_NEAR(5, result.final_point[1], 1e-6);
  EXPECT_NEAR(0.0, result.final_value, 1e-6);

  EXPECT_EQ(0.0, optimization_context.compiled_f(result.final_point));
  EXPECT_EQ("[0.0,0.0]", JSON(optimization_context.compiled_g(result.final_point)));
}

TEST(OptimizationOptimizer, MultiStepOptimization) {
  using namespace current::expression;
  using namespace current::expression::optimizer;

  Vars::ThreadLocalContext vars_context;

  x[0] = 0.0;
  x[1] = 0.0;

  value_t const f = log(1 + exp(x[0] - 3)) + log(1 + exp(3 - x[0])) + log(1 + exp(x[1] - 5)) + log(1 + exp(5 - x[1]));

  OptimizationContext optimization_context(f);
  OptimizationResult const result = Optimize(optimization_context);

  EXPECT_EQ(5u, result.iterations);

  EXPECT_EQ("[3.0,5.0]", JSON(result.final_point));
  ASSERT_EQ(2u, result.final_point.size());
  EXPECT_NEAR(3, result.final_point[0], 1e-6);
  EXPECT_NEAR(5, result.final_point[1], 1e-6);
  EXPECT_NEAR(4 * log(2), result.final_value, 1e-6);

  EXPECT_EQ(
      "["
      "[0.0,0.0],"
      "[3.8657811794817156,4.213713024184646],"
      "[3.0039879355307259,5.0043468627150669],"
      "[2.9999999994601729,5.000000000495253],"
      "[3.0,5.0]]",
      JSON(result.trace));
  EXPECT_EQ("[8.11060540012572,3.105144018869468,2.7725974219447426,2.772588722239781,2.772588722239781]",
            JSON(result.values));
  EXPECT_EQ("[-4.270881774245182,-2.1136153885255606,-2.0000029213346766,-2.000000352575796]", JSON(result.steps));
}

#if 0
// TODO(dkorolev): Maybe it's time to add some conjugate algorithm. Maybe later.
// http://en.wikipedia.org/wiki/Rosenbrock_function
TEST(OptimizationOptimizer, RosenbrockFunction) {
  using namespace current::expression;
  using namespace current::expression::optimizer;

  Vars::ThreadLocalContext vars_context;

  x[0] = -3.0;
  x[1] = -4.0;

  double const a = 1.0;
  double const b = 100.0;
  value_t const d1 = (a - x[0]);
  value_t const d2 = (x[1] - x[0] * x[0]);
  value_t const f = d1 * d1 + b * d2 * d2;

  OptimizationContext optimization_context(f);
  OptimizationResult const result = Optimize(optimization_context);

  EXPECT_EQ( ??? , result.iterations);

}
#endif

// http://en.wikipedia.org/wiki/Himmelblau%27s_function
// Non-convex function with four local minima:
// f(3.0, 2.0) = 0.0
// f(-2.805118, 3.131312) = 0.0
// f(-3.779310, -3.283186) = 0.0
// f(3.584428, -1.848126) = 0.0
// TODO(dkorolev): Find the other three minimums by starting from different points.
TEST(OptimizationOptimizer, HimmelblauFunction) {
  using namespace current::expression;
  using namespace current::expression::optimizer;

  Vars::ThreadLocalContext vars_context;

  x[0] = 5.0;
  x[1] = 5.0;

  value_t const d1 = (x[0] * x[0] + x[1] - 11);
  value_t const d2 = (x[0] + x[1] * x[1] - 7);
  value_t const f = (d1 * d1 + d2 * d2);

  OptimizationContext optimization_context(f);
  OptimizationResult const result = Optimize(optimization_context);

  EXPECT_EQ("[3.0000001668093208,2.000001044657872]", JSON(result.final_point));
  ASSERT_EQ(2u, result.final_point.size());
  EXPECT_NEAR(3, result.final_point[0], 5e-5);
  EXPECT_NEAR(2, result.final_point[1], 5e-5);
  EXPECT_NEAR(0.0, result.final_value, 1e-6);
}
