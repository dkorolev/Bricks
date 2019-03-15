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

#ifndef OPTIMIZE_OPTIMIZER_LINE_SEARCH_H
#define OPTIMIZE_OPTIMIZER_LINE_SEARCH_H

#include "context.h"

#include "../base.h"
#include "../differentiate/differentiate.h"
#include "../expression/expression.h"
#include "../jit/jit.h"
#include "../vars/vars.h"

#ifdef FNCAS_X64_NATIVE_JIT_ENABLED

// The optimization direction is minimization.
// The line search is the crucial component of the optimizer. Here's how it is designed.
//
// First and foremost:
//
// * In order for the line search to begin, the 1D function, `f(lambda)`, has to be provided.
//
//   When running the unit tests this function can be synthetic. In multidimensional optimization,
//   this `f(lambda)` function is generally a step of `lambda` along the direction of the gradient.
//   So, `lambda` will be negative, because the cost function is being minimized.
//
// * At least one derivative of `f(lambda)` must be provided (and JIT-compiled).
//
//   In practice, the line search is not looking for the minimum of the function, but for the zero of its derivative.
//   Hence, while higher-order derivatives are nice-to-have-s, the first one is a must-have.
//
// * Generally, the line search consists of two steps:
//
//   a) Finding the valid range to look for the zero of the first derivative, and
//   b) Finding the zero of the first derivative in this range.
//
//   The valid range is simply the range what has the derivatives of different signs on the ends.
//   It is also important that the value of the derivative and the function is normal (non-NaN) on them.
//
//   Finding the zero in the above range can be thought of as simple binary search, although various heuristics,
//   such as Newton's method or higher-order approximations, are be used.
//
// Finding the valid range.
//
// Down the text the term "left" is referred to the absolute smaller value of the step size, the term "right" refers
// to the larger absolute value. The step sizes along the gradient are themselves negative, so the order is flipped.
//
// Finding the valid range starts at zero. All the provided derivatives are computed; it is expected that if the user
// would not want all of them to be used, they would provide fewer compiled derivatives to begin with.
//
// The cost function is then approximated with the Maclaurin series, and the zero is searched via it. Since this series
// is low-dimensional and generally very quick to compute, and since the within-range search is used later, approximate
// means to find that zero are used.
//
// If the Maclaurin series doesn't have a zero in the negative direction of the gradient, the highest order derivative
// is nullified and the process is tried again. Once all the derivatives have been rules out (i.e., if all of them are
// negative), the fallback algorithm is the simple exponential increase in step size, starting from a very small step.
//
// If the Maclaurin series approach suggests certain step size, the function and its derivative are evaluated at it.
// If the sign of the derivative at that point is different from the sign of the derivative at zero, then the desired
// range is found. If the sign is the same, first of all, 2x, 4x, and 8x this step are tried. If all of them fail,
// the very search continues from that 8x point, which happens up to three times, after which the function is declared
// to not have a minimum. As an extra check, along with comparing the derivatives, the values are being compared. If
// they are increasing, while the derivative suggest they should decrease, then we are dealing with an ill-formed
// function, which warrants an exception.
//
// If at any of the above steps the function or its derivative are NaN, the fallback algorithm is the binary search.
//
// Finding the zero in the valid range.
//
// The general approach is to keep shrinking the range. If only one derivative is provided, which is the bare minimum
// required, then the solution is to iteratively bisect the range by the point that is proportionally closer to one of
// its ends based on the absolute value of the derivative (keep in mind the derivatives have different signs on the
// ends of the range). In other words, the derivative is approximated by a linear function on each iteration. If more
// than one derivative is provided, at most one extra one is used. So, instead of two datapoints, the values of the
// derivative at both ends of the range, there are four datapoints: two values of the derivative and two values of the
// derivative of the derivative. The curve of the derivative in the range is then approximated by a fourth order one,
// and its zero is found. Again, as this approximation is very fast to compute, its zero can be found approximately,
// using the numerical method.
//
// If on any step of within-range search the value of the function or its derivative is NaN, the function is declared
// malformed, and an exception is thrown.

namespace current {
namespace expression {

struct OptimizationException final : OptimizeException {
  using OptimizeException::OptimizeException;
};

namespace optimizer {

struct LineSearchResult {
  double best_step;
  struct IntermediatePoint {
    double step;
    double f;
    double df;
  };
  std::vector<IntermediatePoint> path;
  std::vector<char const*> comments;
};

class LineSearchImpl final {
 public:
  static LineSearchResult DoLineSearch(LineSearchContext const& self) {
    LineSearchResult result;

    // NOTE(dkorolev): IMPORTANT: To ensure all the values are valid, for each new step size the order of computations
    // should be: `l`, `d`, and then `ds`. This is to ensure the internal node values cache is kept valid all the time.
    double const value_at_0 = self.l(self.jit_call_context, self.vars_mapper.x, 0.0);
    double const derivative_at_0 = self.d(self.jit_call_context, self.vars_mapper.x, 0.0);

    result.path.push_back(LineSearchResult::IntermediatePoint{0.0, value_at_0, derivative_at_0});

    if (derivative_at_0 < 0) {
      CURRENT_THROW(OptimizationException("Line search should begin in the direction of the gradient."));
    }
    double const second_derivative_at_0 = (*self.more_ds[0])(self.jit_call_context, self.vars_mapper.x, 0.0);
    // TODO(dkorolev): Need to look at the sign of the second derivative, and to compare it to zero.

    double const step = -(derivative_at_0 / second_derivative_at_0);
    double const value_at_step = self.l(self.jit_call_context, self.vars_mapper.x, step);
    double const derivative_at_step = self.d(self.jit_call_context, self.vars_mapper.x, step);

    result.path.push_back(LineSearchResult::IntermediatePoint{step, value_at_step, derivative_at_step});

    // TODO(dkorolev): No bare `1e-6` here, introduce optimization parameters.
    if (fabs(derivative_at_step) < 1e-6) {
      result.comments.push_back("bingo");
      result.best_step = step;
      return result;
    } else if (derivative_at_step < 0) {
      result.comments.push_back("overshot");
      double a = 0;
      double va = derivative_at_0;
      double b = step;
      double vb = derivative_at_step;

      double c;

      for (size_t iteration = 0; iteration < 10; ++iteration) {
        double const k = va / (va - vb);
        if (!(k > 0 && k < 1)) {
          CURRENT_THROW(OptimizationException("The Newton method is failing, same signs at the ends, internal error."));
        }
        c = a + (b - a) * k;
        double const value_at_c = self.l(self.jit_call_context, self.vars_mapper.x, c);
        double const derivative_at_c = self.d(self.jit_call_context, self.vars_mapper.x, c);

        result.path.push_back(LineSearchResult::IntermediatePoint{c, value_at_c, derivative_at_c});

        if (fabs(derivative_at_c) < 1e-6) {
          result.comments.push_back("bingo");
          result.best_step = c;
          return result;
        } else {
          result.comments.push_back("newton");
          if (derivative_at_step < 0) {
            a = c;
            va = derivative_at_c;
          } else {
            b = c;
            vb = derivative_at_c;
          }
        }
      }

      result.comments.push_back("well, we tried");
      result.best_step = c;
      return result;
    } else {
      result.comments.push_back("not implemented yet");
      result.best_step = step;
      return result;
    }
  }
};

inline LineSearchResult LineSearch(LineSearchContext const& self) { return LineSearchImpl::DoLineSearch(self); }

}  // namespace current::expression::optimizer
}  // namespace current::expression
}  // namespace current

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED

#endif  // #ifndef OPTIMIZE_OPTIMIZER_LINE_SEARCH_H
