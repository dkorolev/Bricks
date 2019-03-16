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

#ifndef OPTIMIZE_OPTIMIZER_OPTIMIZER_H
#define OPTIMIZE_OPTIMIZER_OPTIMIZER_H

#include "context.h"
#include "optimizer_base.h"

#include "line_search/line_search.h"

#ifdef FNCAS_X64_NATIVE_JIT_ENABLED

namespace current {
namespace expression {
namespace optimizer {

struct OptimizationResult {
  // The final result.
  double final_value;
  std::vector<double> final_point;

  // Some rudimentary history.
  std::vector<std::vector<double>> trace;
  std::vector<double> values;
  std::vector<double> steps;
};

inline OptimizationResult Optimize(OptimizationContext& optimization_context) {
  size_t const kMaxIterations = 10;
  double const kMinImprovementPerIteration = 1e-6;
  double const kMinStep = 1e-6;

  OptimizationResult result;

  LineSearchContext const line_search_context(optimization_context);

  double const starting_value =
      optimization_context.compiled_f(optimization_context.jit_call_context, optimization_context.vars_mapper.x);

  result.values.push_back(starting_value);
  result.trace.push_back(optimization_context.vars_mapper.x);

  for (size_t iteration = 0; iteration < kMaxIterations; ++iteration) {
    optimization_context.compiled_g(optimization_context.jit_call_context, optimization_context.vars_mapper.x);

    double const step = LineSearch(line_search_context).best_step;
    if (-step < kMinStep) {
      break;
    }
    optimization_context.MovePointAlongGradient(step);

    result.steps.push_back(step);
    result.trace.push_back(optimization_context.vars_mapper.x);

    result.values.push_back(
        optimization_context.compiled_f(optimization_context.jit_call_context, optimization_context.vars_mapper.x));

    if (result.values.size() >= 2 &&
        ((*(result.values.rbegin() + 1) - result.values.back()) < kMinImprovementPerIteration)) {
      break;
    }
  }

  result.final_point = result.trace.back();

  return result;
}

}  // namespace current::expression::optimizer
}  // namespace current::expression
}  // namespace current

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED

#endif  // #ifndef OPTIMIZE_OPTIMIZER_OPTIMIZER_H
