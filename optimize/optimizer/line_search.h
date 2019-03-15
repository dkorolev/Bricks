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
