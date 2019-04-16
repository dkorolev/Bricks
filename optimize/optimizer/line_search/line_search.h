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

#include "../context.h"
#include "../optimizer_base.h"

#include "../../base.h"
#include "../../differentiate/differentiate.h"
#include "../../expression/expression.h"
#include "../../jit/jit.h"
#include "../../vars/vars.h"

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
// * One derivative of `f(lambda)` must be provided (and JIT-compiled).
//
//   In practice, the line search is not looking for the minimum of the function, but for the zero of its derivative.
//   Hence, while higher-order derivatives are nice-to-have-s, the first one is a must-have.
//
//   NOTE(dkorolev): Higher-order derivatives of `f(lambda)` can be available, but this code does not use them.
//
// * Generally, the line search consists of two steps:
//
//   a) Finding the valid range to look for the zero of the first derivative, and
//   b) Finding the zero of the first derivative in this range.
//
//   The valid range is simply the range what has the derivatives of different signs on the ends.
//   It is also important that the value of the derivative and the function is normal (non-NaN) on them.
//
//   For the basic implementation, the search for the valid range can be based on simple exponenially-increasing steps.
//   In reality, the code below starts from exponentially increasing steps, but uses the values of the derivative
//   to make larger and/or fine-tuned steps when appropriate.
//
//   Once the range with the derivatives of the oppposite signes is located, finding the zero of the derivative in it
//   could be trivially implemented using binary search. The code below optimizes this idea by performing iterations
//   in which on the first step the `f'(lambda)` function is approximated by a linear function, and, on the further
//   iterations, by the function of power two.
//
// Down the text the term "left" is referred to the absolute smaller value of the step size, the term "right" refers
// to the larger absolute value. The step sizes along the gradient are themselves negative, so the order is flipped.
//
// If on any step of within-range search the value of the function or its derivative is NaN, the function is declared
// malformed, and an exception is thrown.

namespace current {
namespace expression {
namespace optimizer {

CURRENT_STRUCT(LineSearchParameters) {
  // Initial steps.
  CURRENT_FIELD(default_first_step, double, -1.0 / 32);
  CURRENT_FIELD(default_first_step_as_fraction_of_previous_best_step, double, 1.0 / 16);

  // Exponential search parameters.
  CURRENT_FIELD(range_exp_growth_k, double, 2.5);
  CURRENT_FIELD(min_decrease_in_decreasing_derivative_for_slope_approximation, double, 127.0 / 128);
  CURRENT_FIELD(max_range_search_extrapolation_step_k, double, 4.0);
  CURRENT_FIELD(good_enough_derivative_zero_for_exp_growth_search, double, 1e-10);

  CURRENT_FIELD(zero_search_range_size_small_epsilon, double, 1e-6);

  // These two processes are fast-converging, and throwing in case of no success. Capping iterations at 100 is safe.
  CURRENT_FIELD(max_range_search_iterations, uint32_t, 100u);
  CURRENT_FIELD(max_derivative_zero_search_iterations, uint32_t, 100u);
};

CURRENT_STRUCT(LineSearchIntermediatePointWithOptionalValues) {
  CURRENT_FIELD(x, double);            // The step size.
  CURRENT_FIELD(f, Optional<double>);  // The value of the function.
  CURRENT_FIELD(d, Optional<double>);  // The value of its derivative.
  CURRENT_CONSTRUCTOR(LineSearchIntermediatePointWithOptionalValues)(double x = 0.0) : x(x) {}
  CURRENT_CONSTRUCTOR(LineSearchIntermediatePointWithOptionalValues)(double x, double f, double d) : x(x), f(f), d(d) {}
};

CURRENT_STRUCT(LineSearchIntermediatePoint) {
  CURRENT_FIELD(x, double);  // The step size.
  CURRENT_FIELD(f, double);  // The value of the function.
  CURRENT_FIELD(d, double);  // The value of its derivative.
  CURRENT_CONSTRUCTOR(LineSearchIntermediatePoint)(double x = 0.0, double f = 0.0, double d = 0.0) : x(x), f(f), d(d) {}
};

CURRENT_STRUCT(LineSearchResult) {
  CURRENT_FIELD(best_step, double);                                // The ultimate output.
  CURRENT_FIELD(path1, std::vector<LineSearchIntermediatePoint>);  // The search for the range where `d` changes sign.
  CURRENT_FIELD(path2, std::vector<LineSearchIntermediatePoint>);  // The search for the zero of `d` in that range.
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
  CURRENT_FIELD(comments, std::vector<std::string>);
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
};

// A simple binary search with a few extra safety checks (that do not sacrifice performance).
// Prerequisites: `a != b`, `f(a)` is true, `f(b)` is false.
// Returns: `c` where `f(c)` is true.
template <typename F>
double SlowBinarySearch(
    double a, double b, F&& f, Optional<bool> ova = nullptr, Optional<bool> ovb = nullptr, size_t total_steps = 40) {
  if (!IsNormal(a) || !IsNormal(b)) {
    CURRENT_THROW(OptimizationException("SlowBinarySearch(): must start from normal points."));
  }
  bool va = Exists(ova) ? Value(ova) : f(a);
  if (!va) {
    CURRENT_THROW(OptimizationException("SlowBinarySearch(): must start from `a` where `f(a)` is true."));
  }
  bool vb = Exists(ovb) ? Value(ovb) : f(b);
  if (vb) {
    CURRENT_THROW(OptimizationException("SlowBinarySearch(): must start from `b` where `f(b)` is false."));
  }
  for (size_t iteration = 0; iteration < total_steps; ++iteration) {
    double c = a + 0.5 * (b - a);
    if (!IsNormal(c)) {
      CURRENT_THROW(OptimizationException("SlowBinarySearch(): `c = 0.5 * (a + b)` is not normal; underflow?"));
    }
    if (!(a != c && b != c && ((c < a) != (c < b)))) {
      CURRENT_THROW(OptimizationException("SlowBinarySearch(): `c` not between `a` and `b`; underflow?"));
    }
    (f(c) ? a : b) = c;
  }
// A guaranteed invariant is that `f(returned_a)` is true.
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
  if (!f(a)) {
    CURRENT_THROW(OptimizationException("SlowBinarySearch(): internal return value `f(a) == true` invariant error."));
  }
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
  return a;
}

class LineSearchImpl final {
 public:
  static LineSearchResult DoLineSearch(LineSearchContext const& self,
                                       LineSearchParameters const& params,
                                       Optional<double> previous_best_step) {
    // NOTE(dkorolev): IMPORTANT: To ensure all the values are valid, for each new step size the order of computations
    // should be: `l`, `d`, and then `ds`. This is to ensure the internal node values cache is kept valid all the time.

    // As described in the header comment above, only the first derivative of `f(lambda)` is used by this code.
    static_cast<void>(self.more_ds);

    LineSearchResult result;

    // Copmute the value and the derivative at the starting point.
    double const value_at_0 = self.l(self.vars_values.x, 0.0);
    double const derivative_at_0 = self.d(self.vars_values.x, 0.0);

    result.path1.push_back(LineSearchIntermediatePoint(0.0, value_at_0, derivative_at_0));

    if (!IsNormal(value_at_0) || !IsNormal(derivative_at_0)) {
      CURRENT_THROW(OptimizationException("Both f(l) and f'(l) must be normal at the staritng point of line search."));
    }
    if (derivative_at_0 < 0) {
      // Well, the direction of line search is supposed to be the gradient,
      // and the gradient points in the direction of the function increasing.
      // Something is wrong if what we are observing is the function decreasing in the direction of the gradient.
      CURRENT_THROW(OptimizationException("The derivative at the starting midpoint should be non-negative."));
    }
    if (derivative_at_0 == 0) {
      // Either there's no improvement along the gradient, or the gradient itself is zero.
      // We are at the minimum; or, in the terribly unlikely scenario, we are at the local maximum or a local extremum.
      // Return the starting point as the result of the optimizations
      // Since this implementation is focused on performance, ignore the possibility of being at a plateau now. -- D.K.
      result.best_step = 0.0;
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
      result.comments.push_back("the starting point is already an extremum");
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
      return result;
    }

    double const first_step =
        !Exists(previous_best_step)
            ? params.default_first_step
            : Value(previous_best_step) * params.default_first_step_as_fraction_of_previous_best_step;

    double left_end_of_range = 0.0;
    double value_at_left_end_of_range = value_at_0;  // The left end of the range only moves if the value improves.
    double derivative_at_left_end_of_range = derivative_at_0;  // Invariant: This stays positive.

    double right_end_of_range = first_step;
    double value_at_right_end_of_range;
    double derivative_at_right_end_of_range;

    double delta_right_end_of_range = right_end_of_range * params.range_exp_growth_k;

#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
    std::string range_search_comment = "range search: ";
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS

    uint32_t number_of_range_search_iterations = 0u;
    while (true) {
      if (++number_of_range_search_iterations > params.max_range_search_iterations) {
        CURRENT_THROW(OptimizationException("Too many unsuccessful range search iterations."));
      }

      value_at_right_end_of_range = self.l(self.vars_values.x, right_end_of_range);
      derivative_at_right_end_of_range = self.d(self.vars_values.x, right_end_of_range);

      if (!IsNormal(value_at_right_end_of_range) || !IsNormal(derivative_at_right_end_of_range)) {
        // Entered the NaNs territory, but all is not necessarily lost.
        if (-right_end_of_range < 1e-25) {
          CURRENT_THROW(OptimizationException("Even the tinest first step against the gradient results in a NaN."));
        } else {
          // Keep shrinking, for the very first step may well have to be very small.
          right_end_of_range *= 0.5;
          continue;
        }
      }

      result.path1.push_back(LineSearchIntermediatePoint(
          right_end_of_range, value_at_right_end_of_range, derivative_at_right_end_of_range));

      if (fabs(derivative_at_right_end_of_range) < params.good_enough_derivative_zero_for_exp_growth_search) {
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
        range_search_comment += " miracle";
        result.comments.push_back(range_search_comment);
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
        result.best_step = right_end_of_range;
        // Miracle: the right end of the range fulfills the stopping criteria.
        // Since this implementation is focused on performance, ignore the possibility of being at a plateau now.
        return result;
      } else if (derivative_at_right_end_of_range < 0) {
// The desired search range for zero of the derivative is found, as `f'(lambda)` is positive at the left end.

#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
        range_search_comment += " found";
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS

        break;
      } else {
        // The derivative at the new right end is still negative. Keep moving until it is positive.
        if (derivative_at_right_end_of_range <
            params.min_decrease_in_decreasing_derivative_for_slope_approximation * derivative_at_left_end_of_range) {
          // It is decreasing at decent pace though, so it is possible to approximate where is it supposed to be zero.
          // The check on the decrease pace is to make sure no excessively large step is made.
          bool const shift = (value_at_right_end_of_range <= value_at_left_end_of_range);
          double const new_left_end_of_range = shift ? right_end_of_range : left_end_of_range;
          double const new_value_at_left_end_of_range =
              shift ? value_at_right_end_of_range : value_at_left_end_of_range;
          double const new_derivative_at_left_end_of_range =
              shift ? derivative_at_right_end_of_range : derivative_at_left_end_of_range;

          double const times =
              derivative_at_left_end_of_range / (derivative_at_left_end_of_range - derivative_at_right_end_of_range);
          double const real_times = std::min(params.max_range_search_extrapolation_step_k, times);

#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
          if (times == real_times) {
            range_search_comment += strings::Printf("*(%.1lf)", times);
          } else {
            range_search_comment += strings::Printf("*(%.1lf->%.1lf)", times, real_times);
          }
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS

          double const old_right_end_of_range = right_end_of_range;

          right_end_of_range = left_end_of_range + (right_end_of_range - left_end_of_range) * real_times;
          delta_right_end_of_range = std::min(delta_right_end_of_range, right_end_of_range - old_right_end_of_range);

          left_end_of_range = new_left_end_of_range;
          value_at_left_end_of_range = new_value_at_left_end_of_range;
          derivative_at_left_end_of_range = new_derivative_at_left_end_of_range;
        } else {  // We have to keep moving further, as the function is not even convex in the right direction.
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
          range_search_comment += '.';
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
          if (value_at_right_end_of_range <= value_at_left_end_of_range) {
            // Well, on the other hand, if the function is not convex in the right direction, there's a good chance
            // the very value of the objective function will be improving. So, move the left end of the range as well.
            left_end_of_range = right_end_of_range;
            value_at_left_end_of_range = value_at_right_end_of_range;
            derivative_at_left_end_of_range = derivative_at_right_end_of_range;

#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
            range_search_comment += '+';
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
          }
          // Keep moving the right end of the range further to the right, at exponentially increasing steps.
          // Friendly reminder, that the step size is actually negative, so the right end technically is the left one.
          right_end_of_range += delta_right_end_of_range;
          delta_right_end_of_range *= params.range_exp_growth_k;
        }
      }
    }

#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
    result.comments.push_back(range_search_comment);
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS

    if (right_end_of_range > left_end_of_range) {
      // Should not happen. At all. Friendly reminder: The sign is negative, and left/right are flipped.
      CURRENT_THROW(OptimizationException("Internal error: malformed range."));
    } else if (left_end_of_range == right_end_of_range) {  // Our range is a single point, so we found it!
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
      result.comments.push_back("range is a single point, minimum found");
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
      result.best_step = left_end_of_range;
      return result;
    } else {
      if (derivative_at_left_end_of_range < 0) {
        // It is a strict invariant that is broken, so truly an internal error.
        CURRENT_THROW(OptimizationException("Internal error: `derivative_at_left_end_of_range < 0`."));
      } else if (derivative_at_left_end_of_range == 0) {
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
        result.comments.push_back("zero derivative at the left end of the range");
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
        // For performance reasons, ignore any and all possibilities for a plateau just yet, simply return the point.
        result.best_step = left_end_of_range;
        return result;
      } else {
        if (derivative_at_right_end_of_range == 0) {  // For performance reasons, ignore the possibility of a plateau.
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
          result.comments.push_back("zero derivative at the right end of the range");
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
          result.best_step = right_end_of_range;
          return result;
        } else if (derivative_at_right_end_of_range > 0) {
          CURRENT_THROW(OptimizationException("Internal error: `derivative_at_right_end_of_range > 0`."));
        } else {
          // The range to search for the zero has been found, as `f'(left)` is positive and `f'(right)` is negative.
          result.path1.push_back(LineSearchIntermediatePoint(
              right_end_of_range, value_at_right_end_of_range, derivative_at_right_end_of_range));

          result.path2.push_back(LineSearchIntermediatePoint(
              left_end_of_range, value_at_left_end_of_range, derivative_at_left_end_of_range));
          result.path2.push_back(LineSearchIntermediatePoint(
              right_end_of_range, value_at_right_end_of_range, derivative_at_right_end_of_range));

#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
          {
            {
              double const value_at_lhs = self.l(self.vars_values.x, left_end_of_range);
              double const derivative_at_lhs = self.d(self.vars_values.x, left_end_of_range);
              if (!IsNormal(value_at_lhs) || !IsNormal(derivative_at_lhs)) {
                CURRENT_THROW(OptimizationException("Internal error: inexplicable."));
              }
              if (!(derivative_at_lhs > 0)) {
                CURRENT_THROW(OptimizationException("Internal error: inexplicable."));
              }
            }
            {
              double const value_at_rhs = self.l(self.vars_values.x, right_end_of_range);
              double const derivative_at_rhs = self.d(self.vars_values.x, right_end_of_range);
              if (!IsNormal(value_at_rhs) || !IsNormal(derivative_at_rhs)) {
                CURRENT_THROW(OptimizationException("Internal error: inexplicable."));
              }
              if (!(derivative_at_rhs < 0)) {
                CURRENT_THROW(OptimizationException("Internal error: inexplicable."));
              }
            }
          }
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS

          // Take the first step assuming the derivative is linear; further steps will assume it's of power two.
          double const first_k =
              derivative_at_left_end_of_range / (derivative_at_left_end_of_range - derivative_at_right_end_of_range);
          double midpoint;
          midpoint = left_end_of_range + (right_end_of_range - left_end_of_range) * first_k;

          std::vector<double> range_widths;

#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
          std::string zero_search_comment = "zero search: 1.0";
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS

          uint32_t number_of_final_iterations = 0u;
          while (true) {
            if (++number_of_final_iterations > params.max_derivative_zero_search_iterations) {
              CURRENT_THROW(OptimizationException("Too many unsuccessful derivative zero search iterations."));
            }
            double const current_range_width = left_end_of_range - right_end_of_range;
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
            if (!range_widths.empty()) {
              zero_search_comment += strings::Printf("*=%.1lf%%", 100.0 * current_range_width / range_widths.back());
            }
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
            range_widths.push_back(current_range_width);

            if (current_range_width < params.zero_search_range_size_small_epsilon) {
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
              zero_search_comment = " range shrinked to almost a point";
              result.comments.push_back(zero_search_comment);
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
              result.best_step = midpoint;
              return result;
            }

            double const value_at_midpoint = self.l(self.vars_values.x, midpoint);
            double const derivative_at_midpoint = self.d(self.vars_values.x, midpoint);
            result.path2.push_back(LineSearchIntermediatePoint(midpoint, value_at_midpoint, derivative_at_midpoint));
            if (derivative_at_midpoint == 0.0) {
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
              zero_search_comment += " hit exactly zero derivative";
              result.comments.push_back(zero_search_comment);
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
              result.best_step = midpoint;
              return result;
            } else if (fabs(derivative_at_midpoint) < 1e-10) {
#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
              zero_search_comment += " reached near zero derivative";
              result.comments.push_back(zero_search_comment);
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS
              result.best_step = midpoint;
              return result;
            } else {
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

#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
              double const check_1 = a + b + c;
              double const check_k = a * k * k + b * k + c;
              if (fabs(check_1 - derivative_at_right_end_of_range) > 1e-6) {
                CURRENT_THROW(OptimizationException("Machine precision error."));
              }
              if (fabs(check_k - derivative_at_midpoint) > 1e-6) {
                CURRENT_THROW(OptimizationException("Machine precision error."));
              }
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS

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
                // It's likely that a fixed sign is the right one, but I'm an engineer, not a mathematician. -- D.K.
                CURRENT_THROW(OptimizationException("Machine precision error."));
              }

              double const best_k = (k1_in_range ? k1 : k2);

#ifdef CURRENT_OPTIMIZE_PARANOID_CHECKS
              double const check_best_k = a * best_k * best_k + b * best_k + c;
              if (fabs(check_best_k) > 1e-6) {
                CURRENT_THROW(OptimizationException("Machine precision error."));
              }
#endif  // CURRENT_OPTIMIZE_PARANOID_CHECKS

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
          }

          CURRENT_THROW(OptimizationException("Not enough iterations for final search for the zero of a derivative."));
        }
      }
    }
  }
};

inline LineSearchResult LineSearch(LineSearchContext const& self,
                                   LineSearchParameters const& params = LineSearchParameters(),
                                   Optional<double> previous_best_step = nullptr) {
  return LineSearchImpl::DoLineSearch(self, params, previous_best_step);
}

}  // namespace current::expression::optimizer
}  // namespace current::expression
}  // namespace current

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED

#endif  // #ifndef OPTIMIZE_OPTIMIZER_LINE_SEARCH_LINE_SEARCH_H
