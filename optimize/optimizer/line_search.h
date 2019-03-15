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

class LineSearchContext final {
 private:
  friend class LineSearchImpl;

  jit::JITCallContext const& jit_call_context;
  VarsMapper const& vars_mapper;
  jit::FunctionWithArgument const& l;
  jit::FunctionWithArgument const& d;
  std::vector<jit::FunctionWithArgument const*> const& more_ds;

 public:
  LineSearchContext(jit::JITCallContext const& jit_call_context,
                    VarsMapper const& vars_mapper,
                    jit::FunctionWithArgument const& l,
                    jit::FunctionWithArgument const& d,
                    std::vector<jit::FunctionWithArgument const*> const& more_ds)
      : jit_call_context(jit_call_context), vars_mapper(vars_mapper), l(l), d(d), more_ds(more_ds) {}
};

struct OptimizationContext {
  value_t const f;  // The function to optimize.

  VarsMapperConfig const config;  // The config with the vars indexed.
  VarsMapper vars_mapper;         // The holder of the starting point, and then the point being optimized.

  std::vector<value_t> const g;   // The gradient.
  value_t const l;                // The "line" 1D function f(lambda), to optimize along the gradient.
  std::vector<value_t> const ds;  // The derivatives of the 1D "line" function; one requires, others optional.

  jit::JITCallContext jit_call_context;  // The holder of the RAM block to run the JIT-compiled functions.
  jit::JITCompiler jit_compiler;         // The JIT compiler, single scope for maximum cache reuse.

  // And the JIT-compiled everything.
  jit::Function const compiled_f;
  jit::FunctionReturningVector const compiled_g;
  jit::FunctionWithArgument const compiled_l;
  std::vector<std::unique_ptr<jit::FunctionWithArgument>> const compiled_ds;
  std::vector<jit::FunctionWithArgument const*> const compiled_ds_pointers;

  static std::vector<value_t> ComputeDS(value_t l) {
    // TODO(dkorolev): The number of the derivatives to take should be a parameter.
    value_t const d1 = DifferentiateByLambda(l);
    value_t const d2 = DifferentiateByLambda(d1);
    value_t const d3 = DifferentiateByLambda(d2);
    return std::vector<value_t>({d1, d2, d3});
  }

  static std::vector<std::unique_ptr<jit::FunctionWithArgument>> CompileDS(jit::JITCompiler& jit_compiler,
                                                                           std::vector<value_t> const& ds) {
    std::vector<std::unique_ptr<jit::FunctionWithArgument>> result;
    result.reserve(ds.size());
    for (value_t v : ds) {
      result.emplace_back(std::make_unique<jit::FunctionWithArgument>(jit_compiler.CompileFunctionWithArgument(v)));
    }
    return result;
  }

  static std::vector<jit::FunctionWithArgument const*> GetCompiledDSPointers(
      std::vector<std::unique_ptr<jit::FunctionWithArgument>> const& compiled_ds) {
    std::vector<jit::FunctionWithArgument const*> result;
    if (!compiled_ds.empty()) {
      result.reserve(compiled_ds.size() - 1u);
      for (size_t i = 1u; i < compiled_ds.size(); ++i) {
        result.push_back(compiled_ds[i].get());
      }
    }
    return result;
  }

  OptimizationContext(VarsContext& vars_context, value_t f)
      : f(f),
        config(vars_context.ReindexVars()),
        vars_mapper(config),
        g(ComputeGradient(f)),
        l(GenerateLineSearchFunction(config, f, g)),
        ds(ComputeDS(l)),
        jit_call_context(),
        jit_compiler(jit_call_context),
        compiled_f(jit_compiler.Compile(f)),
        compiled_g(jit_compiler.Compile(g)),
        compiled_l(jit_compiler.CompileFunctionWithArgument(l)),
        compiled_ds(CompileDS(jit_compiler, ds)),
        compiled_ds_pointers(GetCompiledDSPointers(compiled_ds)) {}

  operator LineSearchContext() const {
    return LineSearchContext(jit_call_context, vars_mapper, compiled_l, *compiled_ds.front(), compiled_ds_pointers);
  }
};

class LineSearchImpl final {
 public:
  static double DoLineSearch(LineSearchContext const& self) {
    // NOTE(dkorolev): IMPORTANT: To ensure all the values are valid, for each new step size the order of computations
    // should be: `l`, `d`, and then `ds`. This is to ensure the internal node values cache is kept valid all the time.
    double const value_at_0 = self.l(self.jit_call_context, self.vars_mapper.x, 0.0);
    static_cast<void>(value_at_0);
    double const derivative_at_0 = self.d(self.jit_call_context, self.vars_mapper.x, 0.0);
    if (derivative_at_0 < 0) {
      CURRENT_THROW(OptimizationException("Line search should begin in the direction of the gradient."));
    }
    double const second_derivative_at_0 = (*self.more_ds[0])(self.jit_call_context, self.vars_mapper.x, 0.0);
    return -(derivative_at_0 / second_derivative_at_0);
  }
};

inline double LineSearch(LineSearchContext const& self) { return LineSearchImpl::DoLineSearch(self); }

}  // namespace current::expression::optimizer
}  // namespace current::expression
}  // namespace current

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED

#endif  // #ifndef OPTIMIZE_OPTIMIZER_LINE_SEARCH_H
