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

#ifndef OPTIMIZE_OPTIMIZER_CONTEXT_H
#define OPTIMIZE_OPTIMIZER_CONTEXT_H

#include "../base.h"
#include "../differentiate/differentiate.h"
#include "../expression/expression.h"
#include "../jit/jit.h"
#include "../vars/vars.h"

#ifdef FNCAS_X64_NATIVE_JIT_ENABLED

namespace current {
namespace expression {
namespace optimizer {

class LineSearchContext final {
 private:
  friend class LineSearchImpl;

  Vars const& vars_values;
  JITCompiledFunctionWithArgument const& l;
  JITCompiledFunctionWithArgument const& d;
  std::vector<JITCompiledFunctionWithArgument const*> const& more_ds;

 public:
  LineSearchContext(Vars const& vars_values,
                    JITCompiledFunctionWithArgument const& l,
                    JITCompiledFunctionWithArgument const& d,
                    std::vector<JITCompiledFunctionWithArgument const*> const& more_ds)
      : vars_values(vars_values), l(l), d(d), more_ds(more_ds) {}
};

struct OptimizationContext {
  value_t const f;  // The function to optimize.

  std::vector<value_t> const g;   // The gradient.
  value_t const l;                // The "line" 1D function f(lambda), to optimize along the gradient.
  std::vector<value_t> const ds;  // The derivatives of the 1D "line" function; one requires, others optional.

  // The setup for the variables, to operate with vars tree after vars scope is gone.
  // NOTE(dkorolev): Should absolutely be a copy, not a const reference.
  Vars::Config const vars_config;

  // Vars values, initialized after all the gradients are taken, and all the nodes are added.
  // Effectively, the starting or the current point of optimization, which is the only "dynamic" part of this context.
  Vars vars_values;

  JITCallContext jit_call_context;  // The holder of the RAM block to run the JIT-compiled functions.
  JITCompiler jit_compiler;         // The JIT compiler, single scope for maximum cache reuse.

  // The JIT-compiled everything.
  JITCompiledFunction const compiled_f;
  JITCompiledFunctionReturningVector const compiled_g;
  JITCompiledFunctionWithArgument const compiled_l;
  std::vector<std::unique_ptr<JITCompiledFunctionWithArgument>> const compiled_ds;
  std::vector<JITCompiledFunctionWithArgument const*> const compiled_ds_pointers;

  static std::vector<value_t> ComputeDS(value_t l) {
    // TODO(dkorolev): The number of the derivatives to take should be a parameter.
    value_t const d1 = DifferentiateByLambda(l);
#if 1
    return std::vector<value_t>({d1});
#else
    value_t const d2 = DifferentiateByLambda(d1);
    value_t const d3 = DifferentiateByLambda(d2);
    return std::vector<value_t>({d1, d2, d3});
#endif
  }

  static std::vector<std::unique_ptr<JITCompiledFunctionWithArgument>> CompileDS(JITCompiler& jit_compiler,
                                                                                 std::vector<value_t> const& ds) {
    std::vector<std::unique_ptr<JITCompiledFunctionWithArgument>> result;
    result.reserve(ds.size());
    for (value_t v : ds) {
      result.emplace_back(
          std::make_unique<JITCompiledFunctionWithArgument>(jit_compiler.CompileFunctionWithArgument(v)));
    }
    return result;
  }

  static std::vector<JITCompiledFunctionWithArgument const*> GetCompiledDSPointers(
      std::vector<std::unique_ptr<JITCompiledFunctionWithArgument>> const& compiled_ds) {
    std::vector<JITCompiledFunctionWithArgument const*> result;
    if (!compiled_ds.empty()) {
      result.reserve(compiled_ds.size() - 1u);
      for (size_t i = 1u; i < compiled_ds.size(); ++i) {
        result.push_back(compiled_ds[i].get());
      }
    }
    return result;
  }

  // NOTE(dkorolev): Important to initialize `vars_values` after the differentiation took place,
  // as differentiating adds nodes to the expression tree, which is frozen after the vars context is exported.
  OptimizationContext(value_t f, Vars::ThreadLocalContext& vars_context = InternalTLS())
      : f(f),
        g(ComputeGradient(f)),
        l(GenerateLineSearchFunction(f, g)),
        ds(ComputeDS(l)),
        vars_config(vars_context.VarsConfig()),
        vars_values(vars_config),
        jit_call_context(),
        jit_compiler(jit_call_context),
        compiled_f(jit_compiler.Compile(f)),
        compiled_g(jit_compiler.Compile(g)),
        compiled_l(jit_compiler.CompileFunctionWithArgument(l)),
        compiled_ds(CompileDS(jit_compiler, ds)),
        compiled_ds_pointers(GetCompiledDSPointers(compiled_ds)) {}

  std::vector<double> const CurrentPoint() const { return vars_values.x; }

  // This method is only used for the unit tests, as the context keeps the value at the current point.
  double UnitTestComputeCurrentObjectiveFunctionValue() const { return compiled_f(vars_values); }

  void MovePointAlongGradient(double gradient_k) {
    vars_values.MovePoint(jit_call_context.ConstRAMPointer(), g, gradient_k);
  }

  operator LineSearchContext() const {
    return LineSearchContext(vars_values, compiled_l, *compiled_ds.front(), compiled_ds_pointers);
  }
};

}  // namespace current::expression::optimizer
}  // namespace current::expression
}  // namespace current

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED

#endif  // #ifndef OPTIMIZE_OPTIMIZER_CONTEXT_H
