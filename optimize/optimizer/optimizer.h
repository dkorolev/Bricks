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
  size_t iterations;
  std::vector<std::vector<double>> trace;
  std::vector<double> values;
  std::vector<double> steps;
};

CURRENT_STRUCT(OptimizationParameters) {
  CURRENT_FIELD(max_iterations, uint32_t, 100u);
  CURRENT_FIELD(min_improvement_per_iteration, double, 1e-10);
  CURRENT_FIELD(min_improvement_per_two_iterations, double, 1e-9);
  CURRENT_FIELD(min_step, double, 1e-9);
};

class OptimizationStrategy {
 private:
  OptimizationParameters const parameters_;

 public:
  explicit OptimizationStrategy(OptimizationParameters parameters) : parameters_(std::move(parameters)) {}
  OptimizationParameters const& Parameters() const { return parameters_; }
};

inline OptimizationResult Optimize(OptimizationContext& optimization_context, OptimizationStrategy const& strategy) {
  OptimizationParameters const& parameters = strategy.Parameters();

  LineSearchParameters line_search_parameters;

  OptimizationResult result;

  LineSearchContext const line_search_context(optimization_context);

  double const starting_value = optimization_context.compiled_f(optimization_context.vars_values.x);

  result.values.push_back(starting_value);
  result.trace.push_back(optimization_context.vars_values.x);

  result.iterations = 1;
  Optional<double> step;
  do {
    optimization_context.compiled_g(optimization_context.vars_values.x);

    step = LineSearch(line_search_context, line_search_parameters, step).best_step;
    if (-Value(step) < parameters.min_step) {
      break;
    }

    ++result.iterations;

    optimization_context.MovePointAlongGradient(Value(step));

    result.steps.push_back(Value(step));
    result.trace.push_back(optimization_context.vars_values.x);

    result.values.push_back(optimization_context.compiled_f(optimization_context.vars_values.x));

    if (result.values.size() >= 2 &&
        ((*(result.values.rbegin() + 1) - result.values.back()) < parameters.min_improvement_per_iteration)) {
      break;
    }
    if (result.values.size() >= 3 &&
        ((*(result.values.rbegin() + 2) - result.values.back()) < parameters.min_improvement_per_two_iterations)) {
      break;
    }
  } while (result.iterations < parameters.max_iterations);

  result.final_value = result.values.back();
  result.final_point = result.trace.back();

  return result;
}

inline OptimizationResult Optimize(OptimizationContext& optimization_context,
                                   OptimizationParameters const& parameters = OptimizationParameters()) {
  return Optimize(optimization_context, OptimizationStrategy(parameters));
}

}  // namespace current::expression::optimizer
}  // namespace current::expression
}  // namespace current

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED

#endif  // #ifndef OPTIMIZE_OPTIMIZER_OPTIMIZER_H
