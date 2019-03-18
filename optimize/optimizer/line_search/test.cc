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

#include "../../../bricks/graph/gnuplot.h"

#include "../../../3rdparty/gtest/gtest-main-with-dflags.h"

#ifdef FNCAS_X64_NATIVE_JIT_ENABLED

DEFINE_bool(save_line_search_test_plots, false, "Set to have each 1D optimization regression test to be plotted.");

TEST(OptimizationOptimizerLineSearch, FunctionOfOrderTwo) {
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

  // In case of the function of order two, see `../differentiate/test.cc`, the first and best step is always  `-0.5`.
  EXPECT_NEAR(-0.5, LineSearch(line_search_context).best_step, 1e-6);

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

inline void SavePlotAndLineSearchPath(std::string const& test_name,
                                      std::string const& function_as_string,
                                      current::expression::optimizer::OptimizationContext const& optimization_context,
                                      current::expression::optimizer::LineSearchResult const& result,
                                      double derivative_value) {
  using namespace current::gnuplot;

#ifndef CURRENT_APPLE
  const char* const format = "pngcairo";
#else
  const char* const format = "png";
#endif
  const char* const extension = "png";

  const std::string plot_body =
      GNUPlot()
          .Title(current::strings::Printf("f(x) = %s\\n%d path1 steps\\n%d path2 steps",
                                          function_as_string.c_str(),
                                          static_cast<int>(result.path1.size()),
                                          static_cast<int>(result.path2.size())))
          .Grid("back")
          .XLabel("x")
          .YLabel("f(x), f'(x), steps")
          .Plot(WithMeta([&result, &optimization_context, derivative_value](Plotter p) {
                  for (current::expression::optimizer::LineSearchIntermediatePoint const& pt : result.path1) {
                    double const y1 = optimization_context.compiled_f(optimization_context.jit_call_context,
                                                                      {
                                                                          pt.x * derivative_value,
                                                                      });
                    double const y2 = optimization_context.compiled_g(optimization_context.jit_call_context,
                                                                      {
                                                                          pt.x * derivative_value,
                                                                      })[0];
                    p(pt.x * derivative_value, 0);
                    p(pt.x * derivative_value, y1);
                    p(pt.x * derivative_value, y2);
                    p(pt.x * derivative_value, 0);
                  }
                  for (current::expression::optimizer::LineSearchIntermediatePoint const& pt : result.path2) {
                    double const y1 = optimization_context.compiled_f(optimization_context.jit_call_context,
                                                                      {
                                                                          pt.x * derivative_value,
                                                                      });
                    double const y2 = optimization_context.compiled_g(optimization_context.jit_call_context,
                                                                      {
                                                                          pt.x * derivative_value,
                                                                      })[0];
                    p(pt.x * derivative_value, 0);
                    p(pt.x * derivative_value, y1);
                    p(pt.x * derivative_value, y2);
                    p(pt.x * derivative_value, 0);
                  }
#if 0
                  for (size_t i = 0u; i < result.path2.size(); ++i) {
                    current::expression::optimizer::LineSearchIntermediatePoint const& pt =
                        result.path2[result.path2.size() - 1u - i];
                    double const x = pt.x * derivative_value;
                    optimization_context.compiled_f(optimization_context.jit_call_context, {x});
                    double const y = optimization_context.compiled_g(optimization_context.jit_call_context, {x})[0];
                    p(x, y);
                  }
#endif
                })
                    .Name("points")
                    .LineWidth(1)
                    .Color("rgb '#D0D0D0'"))
          .Plot(WithMeta([&optimization_context](Plotter p) {
                  for (int i = -50; i <= +1050; ++i) {
                    double const x = 0.01 * i;
                    double const y = optimization_context.compiled_f(optimization_context.jit_call_context, {x});
                    p(x, y);
                  }
                })
                    .Name("f(x)")
                    .LineWidth(10)
                    .Color("rgb '#D0FFD0'"))
          .Plot(WithMeta([&optimization_context](Plotter p) {
                  for (int i = -50; i <= +1050; ++i) {
                    double const x = 0.01 * i;
                    double const unused_but_MUST_be_computed =
                        optimization_context.compiled_f(optimization_context.jit_call_context, {x});
                    static_cast<void>(unused_but_MUST_be_computed);
                    double const y = optimization_context.compiled_g(optimization_context.jit_call_context, {x})[0];
                    p(x, y);
                  }
                })
                    .Name("f'(x)")
                    .LineWidth(5)
                    .Color("rgb '#000000'"))
          .Plot(WithMeta([&result, derivative_value](Plotter p) {
                  for (current::expression::optimizer::LineSearchIntermediatePoint const& pt : result.path1) {
                    p(pt.x * derivative_value, pt.f);
                  }
                })
                    .Name("path1")
                    .LineWidth(2)
                    .Color("rgb '#0000FF'"))
          .Plot(WithMeta([&result, derivative_value](Plotter p) {
                  for (current::expression::optimizer::LineSearchIntermediatePoint const& pt : result.path2) {
                    p(pt.x * derivative_value, pt.f);
                  }
                })
                    .Name("path2")
                    .LineWidth(2)
                    .Color("rgb '#FF0000'"))
          .ImageSize(800)
          .OutputFormat(format);

  // OK to use the non-portable `.current/` here, as this code is a) flag-protected, and b) System V only. -- D.K.
  current::FileSystem::WriteStringToFile(
      plot_body, (".current/" + test_name.substr(0, 2) + '-' + test_name.substr(2) + '.' + extension).c_str());
}

#define TEST_1D_LINE_SEARCH(                                                                                           \
    test_name, function_body, expected_final_value, expected_path1_steps, expected_path2_steps, expected_comments)     \
  TEST(OptimizationOptimizerLineSearch, RegressionTest##test_name) {                                                   \
    using namespace current::expression;                                                                               \
    VarsContext vars_context;                                                                                          \
    x[0] = 0.0;                                                                                                        \
    vars_context.ReindexVars();                                                                                        \
    value_t const f = [](value_t x) { return function_body; }(x[0]);                                                   \
    using namespace current::expression::optimizer;                                                                    \
    OptimizationContext optimization_context(vars_context, f);                                                         \
    LineSearchContext const line_search_context(optimization_context);                                                 \
    optimization_context.compiled_f(optimization_context.jit_call_context, optimization_context.vars_mapper.x);        \
    double const derivative_value =                                                                                    \
        optimization_context.compiled_g(optimization_context.jit_call_context, optimization_context.vars_mapper.x)[0]; \
    LineSearchResult const result = LineSearch(line_search_context);                                                   \
    EXPECT_EQ(expected_path1_steps, result.path1.size()) << "number of path1 steps.";                                  \
    EXPECT_EQ(expected_path2_steps, result.path2.size()) << "number of path2 steps.";                                  \
    double const step_size = result.best_step;                                                                         \
    optimization_context.MovePointAlongGradient(step_size);                                                            \
    double const final_value = optimization_context.ComputeCurrentObjectiveFunctionValue();                            \
    if (FLAGS_save_line_search_test_plots) {                                                                           \
      SavePlotAndLineSearchPath(#test_name, #function_body, optimization_context, result, derivative_value);           \
    }                                                                                                                  \
    EXPECT_EQ(expected_comments, current::strings::Join(result.comments, "; "));                                       \
    if (!std::isnan(expected_final_value)) {                                                                           \
      EXPECT_NEAR(expected_final_value, final_value, 1e-6);                                                            \
    }                                                                                                                  \
  }

// This is a simple order-two function, with a clearly visible minumum at `x = 6`, found in a single extrapolation step.
TEST_1D_LINE_SEARCH(01Parabola, 5 + sqr(x - 6), 5.0, 5u, 0u, "range search: *(16.0->4.0)*(5.0->4.0)*(1.3) miracle");

// A modification to the above test to make it an order-three function, so that the first step "overshoots" `x = 6`.
TEST_1D_LINE_SEARCH(02SlightlyCubicParabola,
                    5 + (x - 6) * (x - 6) * (1 + 0.03 * (x - 6)),
                    5.0,
                    6u,
                    4u,
                    "range search: *(33.9->4.0)*(9.9->4.0)*(2.3) found; "
                    "zero search: 1.0*=27.7% reached near zero derivative");

// A sine.
TEST_1D_LINE_SEARCH(03Sine,
                    2 - sin(0.35 * x - 0.75),
                    1.0,
                    10u,
                    7u,
                    "range search: .+.+.+.+.+.+*(31.1->4.0) found; "
                    "zero search: 1.0*=45.2%*=7.5%*=16.2%*=0.8% reached near zero derivative");

// A piece of a circle.
TEST_1D_LINE_SEARCH(
    04CircleArc,
    10 - sqrt(sqr(9) - sqr(x - 6)),
    1.0,
    11u,
    0u,
    "range search: *(119.9->4.0)*(40.5->4.0)*(14.0->4.0)*(5.1->4.0)*(1.9)*(1.3)*(1.1)*(1.0)*(1.0) miracle");

// An power-(-2) hump.
TEST_1D_LINE_SEARCH(
    05PowerNegativeTwoHump,
    2 - 1 / (1 + sqr(x - 6)),
    1.0,
    14u,
    11u,
    "range search: .+.+.+.+.+.+.+.+.+.+.+ found; "
    "zero search: 1.0*=93.8%*=65.7%*=44.9%*=68.5%*=21.1%*=44.0%*=12.2%*=3.2% reached near zero derivative");

// A bell-curve-resembling arc.
TEST_1D_LINE_SEARCH(06NormalHump,
                    2 - exp(-sqr(x / 2 - 3)),
                    1.0,
                    17u,
                    9u,
                    "range search: .+.+.+.+.+.+.+.+.+.+.+.+.+.+ found; "
                    "zero search: 1.0*=75.4%*=73.5%*=25.0%*=51.0%*=9.7%*=2.2% reached near zero derivative");

// A valley formed by two softmaxes.
TEST_1D_LINE_SEARCH(07HumpOfTwoSoftmaxes,
                    2 + (log(1 + exp(x - 6)) + log(1 + exp(6 - x))),
                    2.0 + 2.0 * log(2.0),
                    9u,
                    6u,
                    "range search: .+.+.+.+*(39.2->4.0)*(1.2) found; "
                    "zero search: 1.0*=43.9%*=0.4%*=1.3% reached near zero derivative");

// The shapes resembling "The Little Prince". Lessons learned:
// 1) Bell curves really get to zero derivative quickly. But regularization helps a lot. `+ 0.001 * sqr(x - 5)` is it.
// 2) Looking for the zero of the derivative carries the risk of missing a better minimum.
//    Not sure if it's important for real-life problems; something to look into later.
TEST_1D_LINE_SEARCH(08LittlePrince,
                    2 - exp(-sqr(x - 6)) - 0.2 * exp(-sqr(x - 4)) + 0.001 * sqr(x - 5),
                    std::numeric_limits<double>::quiet_NaN(),
                    14u,
                    16u,
                    "range search: .+.+.+.+.+.+*(65.0->4.0)*(21.4->4.0)*(7.8->4.0).+.+ found; "
                    "zero search: "
                    "1.0*=98.5%*=89.2%*=92.9%*=92.0%*=92.1%*=89.8%*=79.7%*=45.2%*=58.4%*=62.1%*=18.1%*=11.5%*="
                    "2.3% reached near zero derivative");
TEST_1D_LINE_SEARCH(09LittlePrince,
                    2 - exp(-sqr(x - 6)) - 0.3 * exp(-sqr(x - 4)) + 0.001 * sqr(x - 5),
                    std::numeric_limits<double>::quiet_NaN(),
                    14u,
                    15u,
                    "range search: .+.+.+.+.+.+*(65.0->4.0)*(21.4->4.0)*(8.5->4.0).+.+ found; "
                    "zero search: "
                    "1.0*=98.4%*=88.7%*=92.6%*=91.7%*=91.6%*=88.2%*=72.9%*=68.0%*=25.9%*=77.1%*=5.5%*=3.3% "
                    "reached near zero derivative");
TEST_1D_LINE_SEARCH(10LittlePrince,
                    2 - exp(-sqr(x - 6)) - 0.4 * exp(-sqr(x - 4)) + 0.001 * sqr(x - 5),
                    std::numeric_limits<double>::quiet_NaN(),
                    14u,
                    16u,
                    "range search: .+.+.+.+.+.+*(65.1->4.0)*(21.5->4.0)*(9.3->4.0).+.+ found; "
                    "zero search: "
                    "1.0*=98.2%*=88.2%*=92.2%*=91.3%*=90.9%*=85.6%*=62.3%*=76.7%*=62.2%*=69.1%*=26.1%*=20.0%*="
                    "6.1% reached near zero derivative");
TEST_1D_LINE_SEARCH(11LittlePrince,
                    2 - exp(-sqr(x - 6)) - 0.5 * exp(-sqr(x - 4)) + 0.001 * sqr(x - 5),
                    std::numeric_limits<double>::quiet_NaN(),
                    14u,
                    15u,
                    "range search: .+.+.+.+.+.+*(65.1->4.0)*(21.5->4.0)*(10.2->4.0).+.+ found; "
                    "zero search: "
                    "1.0*=98.0%*=87.7%*=91.8%*=90.8%*=89.9%*=81.4%*=47.0%*=71.5%*=64.4%*=28.0%*=22.4%*=5.9% "
                    "reached near zero derivative");
TEST_1D_LINE_SEARCH(12LittlePrince,
                    2 - exp(-sqr(x - 6)) - 0.6 * exp(-sqr(x - 4)) + 0.001 * sqr(x - 5),
                    std::numeric_limits<double>::quiet_NaN(),
                    14u,
                    17u,
                    "range search: .+.+.+.+.+.+*(65.1->4.0)*(21.6->4.0)*(11.4->4.0).+*(28.4->4.0) found; "
                    "zero search: "
                    "1.0*=97.4%*=86.0%*=90.6%*=89.9%*=90.7%*=90.1%*=84.6%*=57.2%*=80.3%*=65.7%*=63.8%*=36.3%*="
                    "20.4%*=7.3% reached near zero derivative");
TEST_1D_LINE_SEARCH(13LittlePrince,
                    2 - exp(-sqr(x - 6)) - 0.02 * exp(-sqr(x - 3)) + 0.001 * sqr(x - 5),
                    std::numeric_limits<double>::quiet_NaN(),
                    13u,
                    6u,
                    "range search: .+.+.+.+.+.+*(69.6->4.0)*(25.0->4.0).+.+ found; "
                    "zero search: 1.0*=19.1%*=9.1%*=60.0% reached near zero derivative");
TEST_1D_LINE_SEARCH(14LittlePrince,
                    2 - exp(-sqr(x - 6)) - 0.03 * exp(-sqr(x - 3)) + 0.001 * sqr(x - 5),
                    std::numeric_limits<double>::quiet_NaN(),
                    13u,
                    8u,
                    "range search: .+.+.+.+.+.+*(72.2->4.0)*(27.4->4.0).+.+ found; "
                    "zero search: 1.0*=34.1%*=59.1%*=42.4%*=12.8%*=4.2% reached near zero derivative");
TEST_1D_LINE_SEARCH(15LittlePrince,
                    2 - exp(-sqr(x - 6)) - 0.04 * exp(-sqr(x - 3)) + 0.001 * sqr(x - 5),
                    std::numeric_limits<double>::quiet_NaN(),
                    13u,
                    8u,
                    "range search: .+.+.+.+.+.+*(75.0->4.0)*(30.3->4.0).+.+ found; "
                    "zero search: 1.0*=40.9%*=64.4%*=37.6%*=17.0%*=5.8% reached near zero derivative");
TEST_1D_LINE_SEARCH(16LittlePrince,
                    2 - exp(-sqr(x - 6)) - 0.05 * exp(-sqr(x - 3)) + 0.001 * sqr(x - 5),
                    std::numeric_limits<double>::quiet_NaN(),
                    13u,
                    8u,
                    "range search: .+.+.+.+.+.+*(78.0->4.0)*(33.9->4.0).+.+ found; "
                    "zero search: 1.0*=44.9%*=66.2%*=35.3%*=19.1%*=6.4% reached near zero derivative");
TEST_1D_LINE_SEARCH(17LittlePrince,
                    2 - exp(-sqr(x - 6)) - 0.06 * exp(-sqr(x - 3)) + 0.001 * sqr(x - 5),
                    std::numeric_limits<double>::quiet_NaN(),
                    13u,
                    8u,
                    "range search: .+.+.+.+.+.+*(81.3->4.0)*(38.5->4.0).+.+ found; "
                    "zero search: 1.0*=47.4%*=67.0%*=33.9%*=20.4%*=6.7% reached near zero derivative");

#undef TEST_1D_LINE_SEARCH

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED
