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
  CURRENT_FIELD(line_search_parameters, LineSearchParameters);
};

// `MutableGradientAccessor` is the way the user-provided gradient-tweaking function can change the computed gradient,
// directly in the JIT RAM space. This special accessor is not only required to manipulate the values, but also
// to make sure the user is not accidentally trying to change the values of the gradient that are not constants.
// (Yes, this can be seen as an over-optimization and a limitation of the approach. I will worry about this later.)
struct MutableGradientAssigningToNonRAMNode final : OptimizeException {};
class MutableGradientAccessor {
 private:
  std::vector<double> const& vars_values_;
  double* ram_;
  std::vector<value_t> const& g_;

 public:
  explicit MutableGradientAccessor(OptimizationContext const& optimization_context)
      : vars_values_(optimization_context.vars_values.x),
        ram_(optimization_context.jit_call_context.MutableRAMPointer()),
        g_(optimization_context.g) {}

  size_t size() const { return g_.size(); }

  struct GradientValueAccessor {
    MutableGradientAccessor const& self;
    ExpressionNodeIndex const node_index;
    GradientValueAccessor(MutableGradientAccessor const& self, ExpressionNodeIndex const node_index)
        : self(self), node_index(node_index) {}

    operator double() const {
      return node_index.CheckedDispatch<double>(
          [&](size_t node_index) { return self.ram_[node_index]; },
          [&](size_t var_index) { return self.vars_values_[var_index]; },
          [&](double x) { return x; },
          []() -> double { CURRENT_THROW(VarsMapperMovePointUnexpectedLambdaException()); });
    }

    // TODO(dkorolev): More operators.
    void operator=(double x) const {
      node_index.CheckedDispatch([&](size_t node_index) { self.ram_[node_index] = x; },
                                 [&](size_t) { CURRENT_THROW(MutableGradientAssigningToNonRAMNode()); },
                                 [&](double) { CURRENT_THROW(MutableGradientAssigningToNonRAMNode()); },
                                 []() { CURRENT_THROW(MutableGradientAssigningToNonRAMNode()); });
    }
  };

  GradientValueAccessor operator[](RawVarIndex i) const {
#ifndef NDEBUG
    if (!(static_cast<size_t>(i) < g_.size())) {
      TriggerSegmentationFault();
    }
#endif
    return GradientValueAccessor(*this, ExpressionNodeIndexFromExpressionNodeOrValue(g_[static_cast<size_t>(i)]));
  }
};

// `PointAccessor` and `MutablePointAccessor` are the means to access `Vars` using `RawVarIndex`-es.
// TODO(dkorolev): Refactor and move it out of the `optimizer/` dir.
class PointAccessor {
 private:
  std::vector<double> const& values_;

 public:
  explicit PointAccessor(std::vector<double> const& values) : values_(values) {}

  size_t size() const { return values_.size(); }

  double operator[](RawVarIndex i) const {
    size_t const var_index = static_cast<size_t>(i);
#ifndef NDEBUG
    if (!(var_index < values_.size())) {
      TriggerSegmentationFault();
    }
#endif
    return values_[var_index];
  }
};
class MutablePointAccessor {
 private:
  std::vector<double>& values_;

 public:
  explicit MutablePointAccessor(std::vector<double>& values) : values_(values) {}

  size_t size() const { return values_.size(); }

  double& operator[](RawVarIndex i) const {
    size_t const var_index = static_cast<size_t>(i);
#ifndef NDEBUG
    if (!(var_index < values_.size())) {
      TriggerSegmentationFault();
    }
#endif
    return values_[var_index];
  }
};

// `GradientAccessor` is used for read-only access of the values of the gradient, to make a step along it.
class GradientAccessor {
 private:
  std::vector<double> const& vars_values_;
  double const* ram_;
  std::vector<value_t> const& g_;

 public:
  explicit GradientAccessor(OptimizationContext const& optimization_context)
      : vars_values_(optimization_context.vars_values.x),
        ram_(optimization_context.jit_call_context.ConstRAMPointer()),
        g_(optimization_context.g) {}

  size_t size() const { return g_.size(); }

  double operator[](RawVarIndex i) const {
    size_t const var_index = static_cast<size_t>(i);
#ifndef NDEBUG
    if (!(var_index < g_.size())) {
      TriggerSegmentationFault();
    }
#endif
    return ExpressionNodeIndexFromExpressionNodeOrValue(g_[var_index])
        .CheckedDispatch<double>([&](size_t node_index) { return ram_[node_index]; },
                                 [&](size_t var_index) { return vars_values_[var_index]; },
                                 [&](double x) { return x; },
                                 []() -> double { CURRENT_THROW(VarsMapperMovePointUnexpectedLambdaException()); });
  }
};

class OptimizationStrategy {
 public:
  using t_impl_move_point_along_gradient =
      std::function<void(MutablePointAccessor const&, GradientAccessor const&, double)>;
  using t_impl_tweak_gradient = std::function<void(PointAccessor const&, MutableGradientAccessor const&)>;

 private:
  OptimizationParameters const parameters_;

  t_impl_move_point_along_gradient impl_move_point_along_gradient_ = [](
      MutablePointAccessor const& x, GradientAccessor const& dx, double step) {
    for (size_t i = 0; i < x.size(); ++i) {
      x[static_cast<RawVarIndex>(i)] += dx[static_cast<RawVarIndex>(i)] * step;
    }
  };

  t_impl_tweak_gradient impl_tweak_gradient = [](PointAccessor const& x, MutableGradientAccessor const& dx) {
    static_cast<void>(x);
    static_cast<void>(dx);
  };

 public:
  OptimizationStrategy(OptimizationParameters parameters = OptimizationParameters())
      : parameters_(std::move(parameters)) {}

  OptimizationStrategy& InjectMovePointAlongGradient(t_impl_move_point_along_gradient f) {
    impl_move_point_along_gradient_ = f;
    return *this;
  }

  OptimizationStrategy& InjectTweakGradient(t_impl_tweak_gradient f) {
    impl_tweak_gradient = f;
    return *this;
  }

  OptimizationParameters const& GetParameters() const { return parameters_; }
  LineSearchParameters const& GetLineSearchParameters() const { return parameters_.line_search_parameters; }

  bool StopByStepSize(uint32_t one_based_iteration_index, double step_size) const {
    static_cast<void>(one_based_iteration_index);
    return step_size < parameters_.min_step;
  }

  bool StopByNoImprovement(OptimizationResult const& result) const {
    std::vector<double> const& values = result.values;
    if (values.size() >= 2 && ((*(values.rbegin() + 1) - values.back()) < parameters_.min_improvement_per_iteration)) {
      return true;
    }
    if (values.size() >= 3 &&
        ((*(values.rbegin() + 2) - values.back()) < parameters_.min_improvement_per_two_iterations)) {
      return true;
    }
    return false;
  }

  bool StopByMaxIterations(OptimizationResult const& result) const {
    return result.iterations >= parameters_.max_iterations;
  }

  void MovePointAlongGradient(MutablePointAccessor const& x, GradientAccessor const& dx, double step) const {
    impl_move_point_along_gradient_(x, dx, step);
  }

  void TweakGradient(PointAccessor const& x, MutableGradientAccessor const& dx) const { impl_tweak_gradient(x, dx); }
};

inline OptimizationResult Optimize(OptimizationContext& optimization_context, OptimizationStrategy const& strategy) {
  OptimizationResult result;

  LineSearchContext const line_search_context(optimization_context);

  double const starting_value = optimization_context.compiled_f(optimization_context.vars_values.x);

  result.values.push_back(starting_value);
  result.trace.push_back(optimization_context.vars_values.x);

  result.iterations = 1;
  Optional<double> step;
  do {
    optimization_context.compiled_g(optimization_context.vars_values.x);
    strategy.TweakGradient(PointAccessor(optimization_context.vars_values.x),
                           MutableGradientAccessor(optimization_context));

    // `Optional<double>` is provided to `LineSearch` as the best step on the previous iteration.
    // Line search is stateful this way.
    step = LineSearch(line_search_context, strategy.GetLineSearchParameters(), step).best_step;
    double const step_value = Value(step);
    if (strategy.StopByStepSize(static_cast<uint32_t>(result.iterations), -step_value)) {
      break;
    }

    ++result.iterations;

    std::vector<double> new_value(optimization_context.vars_values.x);
    strategy.MovePointAlongGradient(
        MutablePointAccessor(new_value), GradientAccessor(optimization_context), step_value);
    optimization_context.vars_values.InjectPoint(new_value);

    result.steps.push_back(Value(step));
    result.trace.push_back(optimization_context.vars_values.x);

    result.values.push_back(optimization_context.compiled_f(optimization_context.vars_values.x));

    if (strategy.StopByNoImprovement(result)) {
      break;
    }
  } while (!strategy.StopByMaxIterations(result));

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
