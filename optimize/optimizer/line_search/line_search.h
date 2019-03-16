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

#ifndef OPTIMIZE_OPTIMIZER_LINE_SEARCH_LINE_SEARCH_H
#define OPTIMIZE_OPTIMIZER_LINE_SEARCH_LINE_SEARCH_H

#include "../optimizer_base.h"
#include "../context.h"

#include "../../base.h"
#include "../../differentiate/differentiate.h"
#include "../../expression/expression.h"
#include "../../jit/jit.h"
#include "../../vars/vars.h"

#ifdef FNCAS_X64_NATIVE_JIT_ENABLED

// TODO(dkorolev): Update this comment.
//
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
namespace optimizer {

struct LineSearchResult {
  double best_step;
  struct IntermediatePoint {
    double step;
    double f;
    double df;
  };
  std::vector<IntermediatePoint> path1;
  std::vector<IntermediatePoint> path2;
  std::vector<std::string> comments;
};

inline bool IsNormal(double arg) { return (std::isnormal(arg) || arg == 0.0); }

// A simple binary search with a few extra safety checks (that do not sacrifice performance).
// Prerequisites: `a != b`, `f(a)` is true, `f(b)` is false.
// Returns: `c` where `f(c)` is true.
template <typename F>
double BinarySearch(
    double a, double b, F&& f, Optional<bool> ova = nullptr, Optional<bool> ovb = nullptr, size_t total_steps = 40) {
  if (!IsNormal(a) || !IsNormal(b)) {
    CURRENT_THROW(OptimizationException("BinarySearch(): must start from normal points."));
  }
  bool va = Exists(ova) ? Value(ova) : f(a);
  if (!va) {
    CURRENT_THROW(OptimizationException("BinarySearch(): must start from `a` where `f(a)` is true."));
  }
  bool vb = Exists(ovb) ? Value(ovb) : f(b);
  if (vb) {
    CURRENT_THROW(OptimizationException("BinarySearch(): must start from `b` where `f(b)` is false."));
  }
  for (size_t iteration = 0; iteration < total_steps; ++iteration) {
    double c = a + 0.5 * (b - a);
    if (!IsNormal(c)) {
      CURRENT_THROW(OptimizationException("BinarySearch(): `c = 0.5 * (a + b)` is not normal; underflow?"));
    }
    if (!(a != c && b != c && ((c < a) != (c < b)))) {
      CURRENT_THROW(OptimizationException("BinarySearch(): `c` not between `a` and `b`; underflow?"));
    }
    (f(c) ? a : b) = c;
  }
// A guaranteed invariant is that `f(returned_a)` is true.
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
  if (!f(a)) {
    CURRENT_THROW(OptimizationException("BinarySearch(): internal return value `f(a) == true` invariant error."));
  }
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
  return a;
}

class LineSearchImpl final {
 public:
  static LineSearchResult DoLineSearch(LineSearchContext const& self) {
    LineSearchResult result;

    // TODO(dkorolev): The proper implementation will use more derivatives!
    static_cast<void>(self.more_ds);

    // NOTE(dkorolev): IMPORTANT: To ensure all the values are valid, for each new step size the order of computations
    // should be: `l`, `d`, and then `ds`. This is to ensure the internal node values cache is kept valid all the time.
    double const value_at_0 = self.l(self.jit_call_context, self.vars_mapper.x, 0.0);
    double const derivative_at_0 = self.d(self.jit_call_context, self.vars_mapper.x, 0.0);

    result.path1.push_back(LineSearchResult::IntermediatePoint{0.0, value_at_0, derivative_at_0});

    if (!IsNormal(value_at_0) || !IsNormal(derivative_at_0)) {
      CURRENT_THROW(OptimizationException("Both f(l) and f'(l) must be normal at the staritng point of line search."));
    }
    if (derivative_at_0 < 0) {
      // Well, the gradient should point toward the direction of the function increasing.
      CURRENT_THROW(OptimizationException("The derivative at the starting midpoint should ne non-negative."));
    }
    if (derivative_at_0 == 0) {
      // We are at the minimum (or, in the terribly unlikely scenario, we are at the local maximum).
      // Either there's no improvement along the gradient, or the gradient itself is zero.
      // Return the starting point as the result of the optimization.
      result.comments.push_back("the starting point is already an extremum");
      result.best_step = 0.0;
      return result;
    }

#if 0
    // TODO(dkorolev): This logic, in some form, is to be used if NaN-s are hit.
    //                 I'd rather finish the end-to-end logic, and then revisit this part. -- D.K.
    // TODO(dkorolev): This should be a parameter.
    double step = -std::pow(0.5, 22);
    double value_at_step = self.l(self.jit_call_context, self.vars_mapper.x, step);
    double derivative_at_step = self.d(self.jit_call_context, self.vars_mapper.x, step);

    if (!IsNormal(value_at_step) || !IsNormal(derivative_at_step)) {
      CURRENT_THROW(OptimizationException("Both f(l) and f'(l) must be normal at near-zero step of line search."));
    }
    if (derivative_at_step < 0) {
      CURRENT_THROW(OptimizationException("Derivative at a near-zero step should ne non-negative."));
    }

    // Friendly reminder: As `step` is negative, the "left" and "right" are actually right and left.
    double left_end_of_range = 0.0;
    double value_at_left_end_of_range = value_at_0;  // The left end of the range only moves if the value improves.
    double derivative_at_left_end_of_range = derivative_at_0;  // Invariant: This stays positive.
    double right_end_of_range = step;
    double value_at_right_end_of_range = value_at_step;
    double derivative_at_right_end_of_range = derivative_at_step;

    [&]() {
      for (size_t top_level_iteration = 0; top_level_iteration < 100u; ++top_level_iteration) {
        result.path.push_back(LineSearchResult::IntermediatePoint{step, value_at_step, derivative_at_step});

        double new_step = step * 1.5;

        // Friendly reminder: `new_step` is negative,
        if (!IsNormal(new_step) || (step > 0 || new_step > 0) || (-new_step < -step)) {
          CURRENT_THROW(OptimizationException("Internal error: wrong `new_step`. Overflow?"));
        }

        double const value_at_new_step = self.l(self.jit_call_context, self.vars_mapper.x, new_step);
        double const derivative_at_new_step = self.d(self.jit_call_context, self.vars_mapper.x, new_step);

        if (IsNormal(value_at_new_step) && IsNormal(derivative_at_new_step)) {
          // No NaN-s in sight.
          if (derivative_at_new_step <= 0) {
            // First of all, if the derivative is now non-positive, we've found our range.
            // TODO(dkorolev): If the derivative is zero, just return the point, right?
            right_end_of_range = new_step;
            value_at_right_end_of_range = value_at_new_step;
            derivative_at_right_end_of_range = derivative_at_new_step;
            result.comments.push_back(
                current::strings::Printf("perfect search range: (%lf .. %lf), f = { %lf, %lf } d = { %lf, %lf }",
                                         left_end_of_range,
                                         right_end_of_range,
                                         value_at_left_end_of_range,
                                         value_at_right_end_of_range,
                                         derivative_at_left_end_of_range,
                                         derivative_at_right_end_of_range));
            return;
          } else {
            // If the derivative is still positive, good, keep (exponentially) increasing the step size.
            step = new_step;
            derivative_at_step = derivative_at_new_step;
            value_at_step = value_at_new_step;

            // One trick here is to move the left end of the range, but only if the very value is decreasing too.
            if (value_at_new_step <= value_at_left_end_of_range) {
              left_end_of_range = new_step;
              value_at_left_end_of_range = value_at_new_step;
              derivative_at_left_end_of_range = derivative_at_new_step;
            }
          }
        } else {
          // NaNs. Abandon searching for the "best" right end, just find the max. non-NaN step and use it.
          double const safe_right_end_of_range = BinarySearch(step, new_step, [&](double candidate_binary_search_step) {
            double const value_at_x = self.l(self.jit_call_context, self.vars_mapper.x, candidate_binary_search_step);
            double const derivative_at_x =
                self.d(self.jit_call_context, self.vars_mapper.x, candidate_binary_search_step);
            return IsNormal(value_at_x) && IsNormal(derivative_at_x);
          });
          // TODO(dkorolev): Cache these values in the binary search inner function, to not recompute them.
          double const value_at_right_end_of_range =
              self.l(self.jit_call_context, self.vars_mapper.x, safe_right_end_of_range);
          double const derivative_at_right_end_of_range =
              self.d(self.jit_call_context, self.vars_mapper.x, safe_right_end_of_range);
          if (!IsNormal(value_at_right_end_of_range) || !IsNormal(derivative_at_right_end_of_range)) {
            CURRENT_THROW(OptimizationException("Internal error: can't find a normal step at all."));
          }
          right_end_of_range = safe_right_end_of_range;
        }
      }
      // Well, it's weird: all the expansion iterations didn't get us anywhere.
      CURRENT_THROW(OptimizationException("Internal error: exponential expansion steps didn't end?"));
    }();
#else
    double const first_step = -(1.0 / 32.0);  // TODO(dkorolev): Or, if the previous step is available, use 1/8 of it.

    double left_end_of_range = 0.0;
    double value_at_left_end_of_range = value_at_0;  // The left end of the range only moves if the value improves.
    double derivative_at_left_end_of_range = derivative_at_0;  // Invariant: This stays positive.
    double right_end_of_range = first_step;
    double value_at_right_end_of_range;
    double derivative_at_right_end_of_range;

    double const delta_right_end_of_range_exp_growth_k = 2.5;
    double delta_right_end_of_range = right_end_of_range * delta_right_end_of_range_exp_growth_k;
    for (size_t blah = 0; blah < 1000u; ++blah) {
      result.comments.push_back(current::strings::Printf("step of %lf", right_end_of_range));

      value_at_right_end_of_range = self.l(self.jit_call_context, self.vars_mapper.x, right_end_of_range);
      derivative_at_right_end_of_range = self.d(self.jit_call_context, self.vars_mapper.x, right_end_of_range);

      if (!IsNormal(value_at_right_end_of_range) || !IsNormal(derivative_at_right_end_of_range)) {
        // TODO(dkorolev): Begin shrinking.
        CURRENT_THROW(OptimizationException("TODO(dkorolev): Don't just fail here, check smaller steps first."));
      }

      result.path1.push_back(LineSearchResult::IntermediatePoint{
          right_end_of_range, value_at_right_end_of_range, derivative_at_right_end_of_range});

      if (derivative_at_right_end_of_range == 0) {
        // Miracle: Hit the perfect point on the first step.
        // TODO(dkorolev): Check for plaleau?
        result.comments.push_back("magically hit the gold, could be a plateau");
        result.best_step = right_end_of_range;
        return result;
      } else if (derivative_at_right_end_of_range < 0) {
        result.comments.push_back("valid overstep, good");
        break;
      } else {
        // Well, had the derivative at step been positive, we'd have a range.
        // Otherwise, we'll have to keep moving until it is positive.
        if (derivative_at_right_end_of_range < (127.0 / 128.0) * derivative_at_left_end_of_range) {
          // The check is to not extrapolate too far in case of machine precision issues.
          bool const shift = (value_at_right_end_of_range <= value_at_left_end_of_range);
          double const new_left_end_of_range = shift ? right_end_of_range : left_end_of_range;
          double const new_value_at_left_end_of_range =
              shift ? value_at_right_end_of_range : value_at_left_end_of_range;
          double const new_derivative_at_left_end_of_range =
              shift ? derivative_at_right_end_of_range : derivative_at_left_end_of_range;

          double const times =
              derivative_at_left_end_of_range / (derivative_at_left_end_of_range - derivative_at_right_end_of_range);
          result.comments.push_back(strings::Printf("extrapolating, times = %lf", times));

          double const old_right_end_of_range = right_end_of_range;
          right_end_of_range = left_end_of_range + (right_end_of_range - left_end_of_range) * std::min(4.0, times);
          delta_right_end_of_range = std::min(delta_right_end_of_range, right_end_of_range - old_right_end_of_range);

          left_end_of_range = new_left_end_of_range;
          value_at_left_end_of_range = new_value_at_left_end_of_range;
          derivative_at_left_end_of_range = new_derivative_at_left_end_of_range;
        } else {
          result.comments.push_back("can't extrapolate just yet");
          if (value_at_right_end_of_range <= value_at_left_end_of_range) {
            result.comments.push_back("but can move the left end of the range");
            left_end_of_range = right_end_of_range;
            value_at_left_end_of_range = value_at_right_end_of_range;
            derivative_at_left_end_of_range = derivative_at_right_end_of_range;
          }
          right_end_of_range +=
              delta_right_end_of_range;  // 1.5;  // left_end_of_range + (right_end_of_range - left_end_of_range) * 10;
          delta_right_end_of_range *= delta_right_end_of_range_exp_growth_k;
        }
      }
    }
#endif

    result.path1.push_back(LineSearchResult::IntermediatePoint{
        right_end_of_range, value_at_right_end_of_range, derivative_at_right_end_of_range});

    result.path2.push_back(LineSearchResult::IntermediatePoint{
        left_end_of_range, value_at_left_end_of_range, derivative_at_left_end_of_range});
    result.path2.push_back(LineSearchResult::IntermediatePoint{
        right_end_of_range, value_at_right_end_of_range, derivative_at_right_end_of_range});

    if (right_end_of_range > left_end_of_range) {
      // Should not happen. At all. Friendly reminder: The sign is negative, and left/right are flipped.
      CURRENT_THROW(OptimizationException("Internal error: malformed range."));
    } else if (left_end_of_range == right_end_of_range) {
      // Our range is a single point, so we found it!
      result.comments.push_back("range is a single point, minimum found");
      result.best_step = left_end_of_range;
      return result;
    } else {
      if (derivative_at_left_end_of_range < 0) {
        // It is a strict invariant that is broken, so truly an internal error.
        CURRENT_THROW(OptimizationException("Internal error: `derivative_at_left_end_of_range < 0`."));
      } else if (derivative_at_left_end_of_range == 0) {
        result.comments.push_back("zero derivative at the left end of the range");
        // TODO(dkorolev): What's there on the right end of the range? If it's also zero and the function is better?..
        result.best_step = left_end_of_range;  //  + 0.5 * (right_end_of_range - left_end_of_range);
        return result;
      } else {
        // Range non-empty, left end derivative is strictly positive, all as it should be.
        if (derivative_at_right_end_of_range == 0) {
          // TODO(dkorolev): What's there in the middle of the range?
          result.comments.push_back("zero derivative at the right end of the range");
          result.best_step = right_end_of_range;
          return result;
        } else if (derivative_at_right_end_of_range > 0) {
          // TODO(dkorolev): This will happen only if the "just anything non-NaN" was the rule to find the right end.
          result.comments.push_back("suboptimal, perhaps no minimum?");
          result.best_step = right_end_of_range;
          return result;
        } else {
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
          {
            {
              double const value_at_lhs = self.l(self.jit_call_context, self.vars_mapper.x, left_end_of_range);
              double const derivative_at_lhs = self.d(self.jit_call_context, self.vars_mapper.x, left_end_of_range);
              if (!IsNormal(value_at_lhs) || !IsNormal(derivative_at_lhs)) {
                CURRENT_THROW(OptimizationException("Internal error: inexplicable."));
              }
              if (!(derivative_at_lhs > 0)) {
                CURRENT_THROW(OptimizationException("Internal error: inexplicable."));
              }
            }
            {
              double const value_at_rhs = self.l(self.jit_call_context, self.vars_mapper.x, right_end_of_range);
              double const derivative_at_rhs = self.d(self.jit_call_context, self.vars_mapper.x, right_end_of_range);
              if (!IsNormal(value_at_rhs) || !IsNormal(derivative_at_rhs)) {
                CURRENT_THROW(OptimizationException("Internal error: inexplicable."));
              }
              if (!(derivative_at_rhs < 0)) {
                CURRENT_THROW(OptimizationException("Internal error: inexplicable."));
              }
            }
          }
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
          // Perfect situation: a non-empty range, the derivatives of different signs, in the right order, at its ends.
          // Take the first step assuming the derivative is linear; further steps will assume it's of power two.
          double const first_k =
              derivative_at_left_end_of_range / (derivative_at_left_end_of_range - derivative_at_right_end_of_range);
          double range_width;
          double midpoint;
          midpoint = left_end_of_range + (right_end_of_range - left_end_of_range) * first_k;

          size_t final_iteration = 0;
          do {
            ++final_iteration;
            double const value_at_midpoint = self.l(self.jit_call_context, self.vars_mapper.x, midpoint);
            double const derivative_at_midpoint = self.d(self.jit_call_context, self.vars_mapper.x, midpoint);
            result.path2.push_back(
                LineSearchResult::IntermediatePoint{midpoint, value_at_midpoint, derivative_at_midpoint});
            if (derivative_at_midpoint == 0.0) {
              result.comments.push_back(
                  current::strings::Printf("perfect, exactly zero derivative @ step = %lf", midpoint));
              break;
            } else if (fabs(derivative_at_midpoint) < 1e-10) {
              result.comments.push_back(
                  current::strings::Printf("perfect, near zero derivative @ step = %lf", midpoint));
              break;
            } else {
              result.comments.push_back(
                  current::strings::Printf("next iteration, log(range_width) = %lf", log(range_width)));

              // This is the perfect step size for power two approximation of the line search function.
              // I.e., it assumes the derivative is a parabola. Solving for the coefficient for `k^2` we get:
              //
              // f(k = 0) == derivative_at_left_end_of_range
              // f(k = 1) == derivative_at_right_end_of_range
              // f(k = k) == derivative_at_midpoint, while it's supposed to be 0.
              // f(k) = a * k^2 + b * k + c
              //
              // c         = derivative_at_left_end_of_range
              // a + b + c = derivative_at_right_end_of_range
              // a + b     = derivative_at_right_end_of_range - derivative_at_left_end_of_range
              // a * k^2 + b * k + c = derivative_at_midpoint
              // k * (a * k + b) = (derivative_at_midpoint - derivative_at_left_end_of_range)
              // a * k + b = (derivative_at_midpoint - derivative_at_left_end_of_range) / k
              // a * (k - 1) =
              //     (derivative_at_midpoint - derivative_at_left_end_of_range) / k -
              //   - (derivative_at_right_end_of_range - derivative_at_left_end_of_range)
              // a =
              //      ((derivative_at_midpoint - derivative_at_left_end_of_range) / k -
              //        - (derivative_at_right_end_of_range - derivative_at_left_end_of_range)) / (k - 1)
              double const k = (midpoint - left_end_of_range) / (right_end_of_range - left_end_of_range);
              if (!(k > 0 && k < 1)) {
                CURRENT_THROW(OptimizationException("Machine precision error."));
              }

              double const c = derivative_at_left_end_of_range;
              double const a = ((derivative_at_midpoint - derivative_at_left_end_of_range) / k -
                                (derivative_at_right_end_of_range - derivative_at_left_end_of_range)) /
                               (k - 1);
              double const b = (derivative_at_right_end_of_range - derivative_at_left_end_of_range) - a;

              double const check_1 = a + b + c;
              double const check_k = a * k * k + b * k + c;
              if (fabs(check_1 - derivative_at_right_end_of_range) > 1e-6) {
                CURRENT_THROW(OptimizationException("Machine precision error."));
              }
              if (fabs(check_k - derivative_at_midpoint) > 1e-6) {
                CURRENT_THROW(OptimizationException("Machine precision error."));
              }
              double const d = b * b - 4 * a * c;
              if (d < 0) {
                CURRENT_THROW(OptimizationException("Machine precision error."));
              }

              double const sqrt_d = sqrt(d);
              double const k1 = (-b - sqrt_d) / (a + a);
              double const k2 = (-b + sqrt_d) / (a + a);

              bool const k1_in_range = (k1 > 0 && k1 < 1);
              bool const k2_in_range = (k2 > 0 && k2 < 1);

              if (!(k1_in_range || k2_in_range)) {
                CURRENT_THROW(OptimizationException("Machine precision error."));
              }

              if (k1_in_range && k2_in_range) {
                // Given the sign of derivative is different at `k=0` and `k=1`, there can not be two roots in between.
                // NOTE(dkorolev): Mathematically, it's likely that a fixed sign is the right one, but I'm an engineer.
                CURRENT_THROW(OptimizationException("Machine precision error."));
              }

              double const best_k = (k1_in_range ? k1 : k2);
              double const check_best_k = a * best_k * best_k + b * best_k + c;
              if (fabs(check_best_k) > 1e-6) {
                CURRENT_THROW(OptimizationException("Machine precision error."));
              }

              double best_midpoint = left_end_of_range + (right_end_of_range - left_end_of_range) * best_k;

              if (derivative_at_midpoint > 0) {
                left_end_of_range = midpoint;
                value_at_left_end_of_range = value_at_midpoint;
                derivative_at_left_end_of_range = derivative_at_midpoint;
              } else {
                right_end_of_range = midpoint;
                value_at_right_end_of_range = value_at_midpoint;
                derivative_at_right_end_of_range = derivative_at_midpoint;
              }

              midpoint = best_midpoint;
            }
          } while (final_iteration < 100u && (range_width = left_end_of_range - right_end_of_range) > 1e-6);

          result.comments.push_back("converged");
          result.best_step = midpoint;
          return result;
        }
      }
    }
  }
};

inline LineSearchResult LineSearch(LineSearchContext const& self) { return LineSearchImpl::DoLineSearch(self); }

}  // namespace current::expression::optimizer
}  // namespace current::expression
}  // namespace current

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED

#endif  // #ifndef OPTIMIZE_OPTIMIZER_LINE_SEARCH_LINE_SEARCH_H
