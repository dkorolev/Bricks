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

#include "../jit/jit.h"
#include "differentiate.h"

#include "../../3rdparty/gtest/gtest-main.h"

TEST(OptimizationDifferentiate, Operations) {
  using namespace current::expression;

  Vars::ThreadLocalContext vars_context;

  x[0] = 0.0;
  x[1] = 0.0;

  EXPECT_EQ("1.000000", Differentiate(x[0] + x[1], 0).DebugAsString());
  EXPECT_EQ("1.000000", Differentiate(x[0] + x[1], 1).DebugAsString());

  EXPECT_EQ("1.000000", Differentiate(x[0] - x[1], 0).DebugAsString());
  EXPECT_EQ("(-1.000000)", Differentiate(x[0] - x[1], 1).DebugAsString());

  EXPECT_EQ("x[1]", Differentiate(x[0] * x[1], 0).DebugAsString());
  EXPECT_EQ("x[0]", Differentiate(x[0] * x[1], 1).DebugAsString());

  EXPECT_EQ("(x[1]/(x[1]*x[1]))", Differentiate(x[0] / x[1], 0).DebugAsString());
  EXPECT_EQ("((0.000000-x[0])/(x[1]*x[1]))", Differentiate(x[0] / x[1], 1).DebugAsString());
}

TEST(OptimizationDifferentiate, Functions) {
  using namespace current::expression;

  Vars::ThreadLocalContext vars_context;
  x[0] = 0.0;

  EXPECT_EQ("exp(x[0])", Differentiate(exp(x[0]), 0).DebugAsString());
  EXPECT_EQ("(1.000000/x[0])", Differentiate(log(x[0]), 0).DebugAsString());
  EXPECT_EQ("cos(x[0])", Differentiate(sin(x[0]), 0).DebugAsString());
  EXPECT_EQ("((-1.000000)*sin(x[0]))", Differentiate(cos(x[0]), 0).DebugAsString());
  EXPECT_EQ("(1.000000/sqr(cos(x[0])))", Differentiate(tan(x[0]), 0).DebugAsString());
  EXPECT_EQ("(2.000000*x[0])", Differentiate(sqr(x[0]), 0).DebugAsString());
  EXPECT_EQ("(1.000000/(2.000000*sqrt(x[0])))", Differentiate(sqrt(x[0]), 0).DebugAsString());
  EXPECT_EQ("(1.000000/sqrt((1.000000-sqr(x[0]))))", Differentiate(asin(x[0]), 0).DebugAsString());
  EXPECT_EQ("((-1.000000)/sqrt((1.000000-sqr(x[0]))))", Differentiate(acos(x[0]), 0).DebugAsString());
  EXPECT_EQ("(1.000000/(1.000000+sqr(x[0])))", Differentiate(atan(x[0]), 0).DebugAsString());
  EXPECT_EQ("unit_step(x[0])", Differentiate(ramp(x[0]), 0).DebugAsString());
  EXPECT_EQ("sigmoid((0.000000-x[0]))", Differentiate(log_sigmoid(x[0]), 0).DebugAsString());
}

TEST(OptimizationDifferentiate, ChainRule) {
  using namespace current::expression;

  Vars::ThreadLocalContext vars_context;
  x[0] = 0.0;

  EXPECT_EQ("((2.000000*x[0])/sqr(x[0]))", Differentiate(log(sqr(x[0])), 0).DebugAsString());
}

TEST(OptimizationDifferentiate, RegressionTest1DFunctionsExpanded) {
  // This test contains the expanded version of the macro in the `RegressionTest1DFunctions` test below.
  // The checks, on a few user-provided points, include:
  // 1) That the JIT-compiled function evaluates to what it should evaluate to.
  // 2) That the derivative, approximated as `(f(x+d) - f(x-d)) / (d*2)`, matches the derived and JIT-compiled one.

  using namespace current::expression;
  Vars::ThreadLocalContext vars_context;
  x[0] = 0.0;

  value_t const f = []() {
    value_t const x0 = x[0];
    {
      value_t const x = x0;  // This trick is to just use `x` in the formula below.
      return sqr(sqr(x + 1) + 1);
    }
  }();
  value_t const df = Differentiate(f, 0);

  JITCallContext ctx;
  JITCompiledFunctionReturningVector const compiled = JITCompiler(ctx).Compile({f, df});

  double const delta = 1e-5;
  double const eps = 1e-5;

  EXPECT_EQ(4, compiled(ctx, {0.0})[0]);
  EXPECT_EQ(8, compiled(ctx, {0.0})[1]);
  EXPECT_NEAR(
      (sqr(sqr(0.0 + delta + 1) + 1) - sqr(sqr(0.0 - delta + 1) + 1)) / (delta * 2), compiled(ctx, {0.0})[1], eps);
  EXPECT_NEAR(
      (compiled(ctx, {0.0 + delta})[0] - compiled(ctx, {0.0 - delta})[0]) / (delta * 2), compiled(ctx, {0.0})[1], eps);

  EXPECT_EQ(25, compiled(ctx, {1.0})[0]);
  EXPECT_EQ(40, compiled(ctx, {1.0})[1]);
  EXPECT_NEAR(
      (sqr(sqr(1.0 + delta + 1) + 1) - sqr(sqr(1.0 - delta + 1) + 1)) / (delta * 2), compiled(ctx, {1.0})[1], eps);
  EXPECT_NEAR(
      (compiled(ctx, {1.0 + delta})[0] - compiled(ctx, {1.0 - delta})[0]) / (delta * 2), compiled(ctx, {1.0})[1], eps);

  EXPECT_EQ(100, compiled(ctx, {2.0})[0]);
  EXPECT_EQ(120, compiled(ctx, {2.0})[1]);
  EXPECT_NEAR(
      (sqr(sqr(1.0 + delta + 1) + 1) - sqr(sqr(1.0 - delta + 1) + 1)) / (delta * 2), compiled(ctx, {1.0})[1], eps);
  EXPECT_NEAR(
      (compiled(ctx, {2.0 + delta})[0] - compiled(ctx, {2.0 - delta})[0]) / (delta * 2), compiled(ctx, {2.0})[1], eps);
}

TEST(OptimizationDifferentiate, RegressionTest1DFunctions) {
  // See the `RegressionTest1DFunctionsExpanded` test above for the reference implementation of the within-macro code.
  using namespace current::expression;

  double const delta = 1e-5;
  double const eps = 1e-5;

#define TEST_1D_FUNCTION(function_of_x, test_points_in_parens)                                                        \
  {                                                                                                                   \
    Vars::ThreadLocalContext vars_context;                                                                            \
    x[0] = 0.0;                                                                                                       \
    value_t const f = []() {                                                                                          \
      value_t const x0 = x[0];                                                                                        \
      {                                                                                                               \
        value_t const x = x0;                                                                                         \
        return function_of_x;                                                                                         \
      }                                                                                                               \
    }();                                                                                                              \
    value_t const df = Differentiate(f, 0);                                                                           \
    JITCallContext ctx;                                                                                               \
    JITCompiledFunctionReturningVector const compiled = JITCompiler(ctx).Compile({f, df});                            \
    std::function<double(double)> const eval_x = [](double x) { return function_of_x; };                              \
    std::vector<double> const test_points_vector test_points_in_parens;                                               \
    for (double const x_value : test_points_vector) {                                                                 \
      double const true_f_x = eval_x(x_value);                                                                        \
      double const jit_computed_f_x = compiled(ctx, {x_value})[0];                                                    \
      EXPECT_EQ(true_f_x, jit_computed_f_x) << "VALUE " << #function_of_x << " @ " << x_value;                        \
      double const approximated_df_dx = (eval_x(x_value + delta) - eval_x(x_value - delta)) / (delta * 2);            \
      double const jit_computed_df_dx = compiled(ctx, {x_value})[1];                                                  \
      EXPECT_GT(jit_computed_df_dx, approximated_df_dx - eps) << "DERIVATIVE LEFT BOUND " << #function_of_x << " @ "  \
                                                              << x_value;                                             \
      EXPECT_LT(jit_computed_df_dx, approximated_df_dx + eps) << "DERIVATIVE RIGHT BOUND " << #function_of_x << " @ " \
                                                              << x_value;                                             \
    }                                                                                                                 \
  }

  // From the above example.
  TEST_1D_FUNCTION(sqr(sqr(x + 1) + 1), ({0.0, 1.0, 2.0}));

  // The mathematical functions themselves.
  TEST_1D_FUNCTION(exp(x), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(log(x), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.1}));  // Machine precision in approximations, keep >= 0.1.
  TEST_1D_FUNCTION(sin(x), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(cos(x), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(tan(x), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(sqr(x), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(sqrt(x), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.1}));  // No zero and near-zero for `sqrt()`.
  TEST_1D_FUNCTION(asin(x), ({0.0, 0.5, -0.5, 0.25, -0.25, 0.75, -0.75, 0.9, -0.9}));
  TEST_1D_FUNCTION(acos(x), ({0.0, 0.5, -0.5, 0.25, -0.25, 0.75, -0.75, 0.9, -0.9}));
  TEST_1D_FUNCTION(atan(x), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(ramp(x), ({0.5, 1.0, 1.5, 2.0, 2.5, -0.5, -2.0, -2.5}));  // Exclude `0.0` from testing `ramp()`.
  TEST_1D_FUNCTION(log_sigmoid(x), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));

  // Chain rule to cover the argument of each of the supported functions that can be differentiated.
  TEST_1D_FUNCTION(exp(sqr(x) + 1), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(log(exp(x) + 1), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(sin(exp(x) + x), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(cos(log_sigmoid(x) + x), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(tan(sqr(x)), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(sqr(log(1 + exp(x))), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(sqrt(exp(x)), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(asin(0.75 * ((atan(x) / M_PI) - 0.5)), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(acos(0.75 * ((atan(x) / M_PI) - 0.5)), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(atan(log_sigmoid(x) - sqr(x)), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(ramp(log(1 + exp(x))), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));
  TEST_1D_FUNCTION(log_sigmoid(x + tan(x)), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));

  // Nontrivial tests I could think of. Feel free to add. -- D.K.
  TEST_1D_FUNCTION(log_sigmoid(x + 1 + sqr(x)), ({0.5, 1.0, 1.5, 2.0, 2.5, 0.0, -0.5, -2.0, -2.5}));

#undef TEST_1D_FUNCTION
}

TEST(OptimizationDifferentiate, RegressionTest2DFunctions) {
  // See the `RegressionTest1DFunctions` test above for the baseline for the macro introduced below.
  using namespace current::expression;

  double const delta = 1e-5;
  double const eps = 1e-5;

#define TEST_2D_FUNCTION(function_of_x_and_y, test_points_pairs_in_parens)                                           \
  {                                                                                                                  \
    Vars::ThreadLocalContext vars_context;                                                                           \
    x[0] = 0.0;                                                                                                      \
    x[1] = 0.0;                                                                                                      \
    value_t const f = []() {                                                                                         \
      value_t const x0 = x[0];                                                                                       \
      value_t const x1 = x[1];                                                                                       \
      {                                                                                                              \
        value_t const x = x0;                                                                                        \
        value_t const y = x1;                                                                                        \
        return function_of_x_and_y;                                                                                  \
      }                                                                                                              \
    }();                                                                                                             \
    value_t const df_dx = Differentiate(f, 0);                                                                       \
    value_t const df_dy = Differentiate(f, 1);                                                                       \
    JITCallContext ctx;                                                                                              \
    JITCompiledFunctionReturningVector const compiled = JITCompiler(ctx).Compile({f, df_dx, df_dy});                 \
    std::function<double(double, double)> const eval_x_y = [](double x, double y) { return function_of_x_and_y; };   \
    std::vector<std::pair<double, double>> const test_points_vector_of_pairs test_points_pairs_in_parens;            \
    for (std::pair<double, double> const& x_y_values : test_points_vector_of_pairs) {                                \
      double const x_value = x_y_values.first;                                                                       \
      double const y_value = x_y_values.second;                                                                      \
      double const true_f_x_y = eval_x_y(x_value, y_value);                                                          \
      double const jit_computed_f_x_y = compiled(ctx, {x_value, y_value})[0];                                        \
      EXPECT_EQ(true_f_x_y, jit_computed_f_x_y) << "VALUE " << #function_of_x_and_y << " @ (" << x_value << ", "     \
                                                << y_value << ")";                                                   \
      double const approximated_df_dx =                                                                              \
          (eval_x_y(x_value + delta, y_value) - eval_x_y(x_value - delta, y_value)) / (delta * 2);                   \
      double const jit_computed_df_dx = compiled(ctx, {x_value, y_value})[1];                                        \
      EXPECT_GT(jit_computed_df_dx, approximated_df_dx - eps) << "X DERIVATIVE LEFT BOUND " << #function_of_x_and_y  \
                                                              << " @ (" << x_value << ", " << y_value << ")";        \
      EXPECT_LT(jit_computed_df_dx, approximated_df_dx + eps) << "X DERIVATIVE RIGHT BOUND " << #function_of_x_and_y \
                                                              << " @ (" << x_value << ", " << y_value << ")";        \
      double const approximated_df_dy =                                                                              \
          (eval_x_y(x_value, y_value + delta) - eval_x_y(x_value, y_value - delta)) / (delta * 2);                   \
      double const jit_computed_df_dy = compiled(ctx, {x_value, y_value})[2];                                        \
      EXPECT_GT(jit_computed_df_dy, approximated_df_dy - eps) << "Y DERIVATIVE LEFT BOUND " << #function_of_x_and_y  \
                                                              << " @ (" << x_value << ", " << y_value << ")";        \
      EXPECT_LT(jit_computed_df_dy, approximated_df_dy + eps) << "Y DERIVATIVE RIGHT BOUND " << #function_of_x_and_y \
                                                              << " @ (" << x_value << ", " << y_value << ")";        \
    }                                                                                                                \
  }

  // Addition and multiplication, basic chain rule as well.
  TEST_2D_FUNCTION(x + y, ({{0, 0}, {0, 1}, {1, 0}, {1, 1}, {-1, 0}, {0, -1}, {2, 2}, {2, 3}, {3, -4}, {-5, 6}}));
  TEST_2D_FUNCTION(x * y, ({{0, 0}, {0, 1}, {1, 0}, {1, 1}, {-1, 0}, {0, -1}, {2, 2}, {2, 3}, {3, -4}, {-5, 6}}));
  TEST_2D_FUNCTION(x * (y + 1), ({{0, 0}, {0, 1}, {1, 0}, {1, 1}, {-1, 0}, {0, -1}, {2, 2}, {2, 3}, {3, -4}, {-5, 6}}));
  TEST_2D_FUNCTION((x + 2) * y, ({{0, 0}, {0, 1}, {1, 0}, {1, 1}, {-1, 0}, {0, -1}, {2, 2}, {2, 3}, {3, -4}, {-5, 6}}));
  TEST_2D_FUNCTION(x + y * 4, ({{0, 0}, {0, 1}, {1, 0}, {1, 1}, {-1, 0}, {0, -1}, {2, 2}, {2, 3}, {3, -4}, {-5, 6}}));
  TEST_2D_FUNCTION(x * 5 + y, ({{0, 0}, {0, 1}, {1, 0}, {1, 1}, {-1, 0}, {0, -1}, {2, 2}, {2, 3}, {3, -4}, {-5, 6}}));

  // Division.
  TEST_2D_FUNCTION(x / (sqr(y) + 1),
                   ({{0, 0}, {0, 1}, {1, 0}, {1, 1}, {-1, 0}, {0, -1}, {2, 2}, {2, 3}, {3, -4}, {-5, 6}}));

  // Subtraction too.
  TEST_2D_FUNCTION(x / (sqr(y - log_sigmoid(x)) + 1),
                   ({{0, 0}, {0, 1}, {1, 0}, {1, 1}, {-1, 0}, {0, -1}, {2, 2}, {2, 3}, {3, -4}, {-5, 6}}));

  // Nontrivial tests I could think of. Feel free to add. -- D.K.
  TEST_2D_FUNCTION(log(1.5 + sqr(x) + log(1.5 + sqr(y))),
                   ({{0, 0}, {0, 1}, {1, 0}, {1, 1}, {-1, 0}, {0, -1}, {2, 2}, {2, 3}, {3, -4}, {-5, 6}}));
  TEST_2D_FUNCTION(log_sigmoid(sqr(x) * log(1.5 + sqr(y))),
                   ({{0, 0}, {0, 1}, {1, 0}, {1, 1}, {-1, 0}, {0, -1}, {2, 2}, {2, 3}, {3, -4}, {-5, 6}}));

#undef TEST_2D_FUNCTION
}

TEST(OptimizationDifferentiate, Constants) {
  using namespace current::expression;

  Vars::ThreadLocalContext vars_context;

  x[0] = 0.0;
  x[1] = 0.0;
  x[2] = 0.0;

  x[0].SetConstant();
  x[2].SetConstant();

  EXPECT_EQ("0.000000", Differentiate(x[0], 0).DebugAsString());  // Not `1` because `x[0]` is a constant.
  EXPECT_EQ("0.000000", Differentiate(x[0], 1).DebugAsString());
  EXPECT_EQ("0.000000", Differentiate(x[0], 2).DebugAsString());

  EXPECT_EQ("0.000000", Differentiate(x[1], 0).DebugAsString());
  EXPECT_EQ("1.000000", Differentiate(x[1], 1).DebugAsString());
  EXPECT_EQ("0.000000", Differentiate(x[1], 2).DebugAsString());

  EXPECT_EQ("0.000000", Differentiate(x[2], 0).DebugAsString());
  EXPECT_EQ("0.000000", Differentiate(x[2], 1).DebugAsString());
  EXPECT_EQ("0.000000", Differentiate(x[2], 2).DebugAsString());  // Not `1` because `x[2]` is a constant.

  value_t two = 2.0;
  EXPECT_EQ("0.000000", Differentiate(two, 0).DebugAsString());
  EXPECT_EQ("0.000000", Differentiate(two, 1).DebugAsString());
  EXPECT_EQ("0.000000", Differentiate(two, 2).DebugAsString());
}

TEST(OptimizationDifferentiate, Gradient) {
  using namespace current::expression;

  Vars::ThreadLocalContext vars_context;

  x[0] = 0.0;
  x[1] = 0.0;

  std::vector<value_t> const g = ComputeGradient(sqr(x[0]) + 2.0 * sqr(x[1]));
  ASSERT_EQ(2u, g.size());
  EXPECT_EQ("(2.000000*x[0])", g[0].DebugAsString());
  EXPECT_EQ("(2.000000*(2.000000*x[1]))", g[1].DebugAsString());
}

TEST(OptimizationDifferentiate, DirectionalDerivative) {
  using namespace current::expression;

  Vars::ThreadLocalContext vars_context;

  x[0] = 0.0;
  x[1] = 0.0;

  // This is a function of order two, with a minimum of `f(2,4) = 0`.
  value_t const f = sqr(x[0] - 2.0) + sqr(x[1] - 4.0);

  std::vector<value_t> const g = ComputeGradient(f);
  value_t const l = GenerateLineSearchFunction(f, g);

  value_t const d1 = DifferentiateByLambda(l);
  value_t const d2 = DifferentiateByLambda(d1);
  value_t const d3 = DifferentiateByLambda(d2);

  JITCallContext ctx;
  JITCompiler compiler(ctx);
  JITCompiledFunction const compiled_f = compiler.Compile(f);
  JITCompiledFunctionReturningVector const compiled_g = compiler.Compile(g);
  // NOTE(dkorolev): In an aggressively optimized code, we may not even need the very `l` compiled. -- D.K.
  JITCompiledFunctionWithArgument const compiled_l = compiler.CompileFunctionWithArgument(d1);
  JITCompiledFunctionWithArgument const compiled_d1 = compiler.CompileFunctionWithArgument(d1);
  JITCompiledFunctionWithArgument const compiled_d2 = compiler.CompileFunctionWithArgument(d2);
  JITCompiledFunctionWithArgument const compiled_d3 = compiler.CompileFunctionWithArgument(d3);

  // Everything is zero at `f(2,4)`, as it is the minumum.
  EXPECT_EQ(0.0, compiled_f(ctx, {2.0, 4.0}));
  EXPECT_EQ("[0.0,0.0]", JSON(compiled_g(ctx, {2.0, 4.0})));
  compiled_l(ctx, {2.0, 4.0}, 0.0);  // Need to evalute `l` after `g` and before `d1`.
  EXPECT_EQ(0.0, compiled_d1(ctx, {2.0, 4.0}, 0.0));
  EXPECT_EQ(0.0, compiled_d2(ctx, {2.0, 4.0}, 0.0));
  EXPECT_EQ(0.0, compiled_d3(ctx, {2.0, 4.0}, 0.0));

  // A step towards the minimum is required from `{1,1}`.
  std::vector<double> p({1.0, 1.0});
  EXPECT_EQ(10.0, compiled_f(ctx, p));
  EXPECT_EQ("[-2.0,-6.0]", JSON(compiled_g(ctx, p)));
  compiled_l(ctx, p, 0.0);
  EXPECT_EQ(40.0, compiled_d1(ctx, p, 0.0));

  EXPECT_EQ(80.0,
            compiled_d2(ctx, p, 0.0));  // The 2nd derivative by lambda is a constant, as the function is of oder two.
  EXPECT_EQ(80.0, compiled_d2(ctx, p, -1.0));
  EXPECT_EQ(80.0, compiled_d2(ctx, p, +1.0));
  EXPECT_EQ(80.0, compiled_d2(ctx, p, -5.0));
  EXPECT_EQ(80.0, compiled_d2(ctx, p, +5.0));

  EXPECT_EQ(0.0, compiled_d3(ctx, p, 0.0));  // The 3rd derivative by lambda is zero, as the function is of order two.
  EXPECT_EQ(0.0, compiled_d3(ctx, p, -1.0));
  EXPECT_EQ(0.0, compiled_d3(ctx, p, +1.0));
  EXPECT_EQ(0.0, compiled_d3(ctx, p, -5.0));
  EXPECT_EQ(0.0, compiled_d3(ctx, p, +5.0));

  // Effectively, as the function is of order two, making a step of `lambda = -f_lambda'(x)/f_lambda''(x)` hits the min.
  EXPECT_EQ(0.5, compiled_d1(ctx, p, 0.0) / compiled_d2(ctx, p, 0.0));
  double const step = -(compiled_d1(ctx, p, 0.0) / compiled_d2(ctx, p, 0.0));

  // For the new value of lambda (step size, in this case), need to re-evalute `l` before `d1`.
  // In this case of a function of order two, the first step takes us to the minimum, which is zero.
  EXPECT_EQ(0.0, compiled_l(ctx, p, step));
  // The derivative is also zero, an it would be (approximately) zero even for the functions that are not of order two.
  EXPECT_EQ(0.0, compiled_d1(ctx, p, -(compiled_d1(ctx, p, 0.0) / compiled_d2(ctx, p, 0.0))));

  // Now, the above should work for any starting point, given the function being tested is of order two.
  {
    p = {1.5, 3.5};
    compiled_f(ctx, p);
    compiled_g(ctx, p);
    compiled_l(ctx, p, 0.0);
    EXPECT_EQ(2, compiled_d1(ctx, p, 0.0));
    EXPECT_EQ(4, compiled_d2(ctx, p, 0.0));
    EXPECT_EQ(0.5, compiled_d1(ctx, p, 0.0) / compiled_d2(ctx, p, 0.0));
    EXPECT_EQ(0.0, compiled_l(ctx, p, -0.5));
    EXPECT_EQ(0.0, compiled_d1(ctx, p, -(compiled_d1(ctx, p, 0.0) / compiled_d2(ctx, p, 0.0))));
    EXPECT_EQ(compiled_d2(ctx, p, 0.0), compiled_d2(ctx, p, -1.0));
    EXPECT_EQ(compiled_d2(ctx, p, 0.0), compiled_d2(ctx, p, +1.0));
    EXPECT_EQ(compiled_d2(ctx, p, 0.0), compiled_d2(ctx, p, -5.0));
    EXPECT_EQ(compiled_d2(ctx, p, 0.0), compiled_d2(ctx, p, +5.0));
    EXPECT_EQ(0.0, compiled_d3(ctx, p, 0.0));
    EXPECT_EQ(0.0, compiled_d3(ctx, p, -1.0));
    EXPECT_EQ(0.0, compiled_d3(ctx, p, +1.0));
  }

  // And step size will always be 0.5.
  {
    p = {-9.25, 17.75};
    compiled_f(ctx, p);
    compiled_g(ctx, p);
    compiled_l(ctx, p, 0.0);
    EXPECT_EQ(0.5, compiled_d1(ctx, p, 0.0) / compiled_d2(ctx, p, 0.0));
    // For the new value of lambda (step size `-0.5`), need to re-evalute `l` before `d1` and other `d`-s.
    EXPECT_EQ(0, compiled_l(ctx, p, -0.5));
    EXPECT_EQ(0.0, compiled_d1(ctx, p, -(compiled_d1(ctx, p, 0.0) / compiled_d2(ctx, p, 0.0))));
    EXPECT_EQ(compiled_d2(ctx, p, 0.0), compiled_d2(ctx, p, -1.0));
    EXPECT_EQ(compiled_d2(ctx, p, 0.0), compiled_d2(ctx, p, +1.0));
    EXPECT_EQ(compiled_d2(ctx, p, 0.0), compiled_d2(ctx, p, -5.0));
    EXPECT_EQ(compiled_d2(ctx, p, 0.0), compiled_d2(ctx, p, +5.0));
    EXPECT_EQ(0.0, compiled_d3(ctx, p, 0.0));
    EXPECT_EQ(0.0, compiled_d3(ctx, p, -1.0));
    EXPECT_EQ(0.0, compiled_d3(ctx, p, +1.0));
  }
  {
    p = {131.75, +293.25};
    compiled_f(ctx, p);
    compiled_g(ctx, p);
    compiled_l(ctx, p, 0.0);
    EXPECT_EQ(0.5, compiled_d1(ctx, p, 0.0) / compiled_d2(ctx, p, 0.0));
    // For the new value of lambda (step size `-0.5`), need to re-evalute `l` before `d1` and other `d`-s.
    EXPECT_EQ(0, compiled_l(ctx, p, -0.5));
    EXPECT_EQ(0.0, compiled_d1(ctx, p, -(compiled_d1(ctx, p, 0.0) / compiled_d2(ctx, p, 0.0))));
    EXPECT_EQ(compiled_d2(ctx, p, 0.0), compiled_d2(ctx, p, -1.0));
    EXPECT_EQ(compiled_d2(ctx, p, 0.0), compiled_d2(ctx, p, +1.0));
    EXPECT_EQ(compiled_d2(ctx, p, 0.0), compiled_d2(ctx, p, -5.0));
    EXPECT_EQ(compiled_d2(ctx, p, 0.0), compiled_d2(ctx, p, +5.0));
    EXPECT_EQ(0.0, compiled_d3(ctx, p, 0.0));
    EXPECT_EQ(0.0, compiled_d3(ctx, p, -1.0));
    EXPECT_EQ(0.0, compiled_d3(ctx, p, +1.0));
  }
}

TEST(OptimizationDifferentiate, GradientComponentsAreNullified) {
  using namespace current::expression;

  size_t const dim = 4;

  Vars::ThreadLocalContext vars_context;

  for (size_t i = 0; i < dim; ++i) {
    x[i] = 0.0;
  }

  value_t f = 0.0;
  for (size_t i = 0; i < dim; ++i) {
    f += exp(x[i]);
  }

  std::vector<value_t> const g = ComputeGradient(f);

  EXPECT_EQ(dim, g.size());
  EXPECT_EQ("exp(x[0])", g[0].DebugAsString());
  EXPECT_EQ("exp(x[1])", g[1].DebugAsString());
  EXPECT_EQ("exp(x[2])", g[2].DebugAsString());
  EXPECT_EQ("exp(x[3])", g[3].DebugAsString());
}

TEST(OptimizationDifferentiate, NeedBalancedExpressionTree) {
  using namespace current::expression;

  Vars::ThreadLocalContext vars_context;

  value_t f = 0.0;
  for (size_t i = 0; i < 2000; ++i) {
    x[i] = 0.0;
    f += sqr(x[i] - i);
  }

  // Without `BalanceExpressionTree(f)` the next line should throw. Depth of 2K+ is too much.
  try {
    ComputeGradient(f);
    ASSERT_TRUE(false);
  } catch (DifferentiatorRequiresBalancedTreeException const&) {
  }

  // With `BalanceExpressionTree(f)` it's good to go.
  BalanceExpressionTree(f);
  ComputeGradient(f);
}

inline void RunOptimizationDifferentiateGradientStressTest(size_t dim) {
  using namespace current::expression;

  Vars::ThreadLocalContext vars_context;

  for (size_t i = 0; i < dim; ++i) {
    x[i] = 0.0;
  }

  value_t f = 0.0;
  for (size_t i = 0; i < dim; ++i) {
    f += exp(x[i]);
  }

  BalanceExpressionTree(f);

  ComputeGradient(f);
}

TEST(OptimizationDifferentiate, GradientStressTest100Exponents) {
  RunOptimizationDifferentiateGradientStressTest(100u);
}

TEST(OptimizationDifferentiate, GradientStressTest1KExponents) {
  RunOptimizationDifferentiateGradientStressTest(1000u);
}

TEST(OptimizationDifferentiate, GradientStressTest3KExponents) {
  RunOptimizationDifferentiateGradientStressTest(3000u);
}

TEST(OptimizationDifferentiate, GradientStressTest10KExponents) {
  RunOptimizationDifferentiateGradientStressTest(10000u);
}
