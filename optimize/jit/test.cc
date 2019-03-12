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

#include "jit.h"

#include "../../3rdparty/gtest/gtest-main.h"

#ifdef FNCAS_X64_NATIVE_JIT_ENABLED

TEST(OptimizationJIT, NeedVarsContext) {
  using namespace current::expression;
  ASSERT_THROW(jit::JITCodeGenerator illegal_code_generator, VarsManagementException);
}

TEST(OptimizationJIT, Add) {
  using namespace current::expression;

  VarsContext context;

  x["a"] = 1.0;
  value_t const value = x["a"] + x["a"];

  VarsMapperConfig const config = context.Freeze();

  jit::JITCodeGenerator code_generator(config);
  std::vector<double> ram(code_generator.RAMRequired());

  current::fncas::x64_native_jit::CallableVectorUInt8 f(code_generator.FunctionForNode(value));

  VarsMapper input(config);
  EXPECT_EQ(2.0, f(&input.x[0], &ram[0], nullptr));

  input["a"] = 2.0;
  EXPECT_EQ(4.0, f(&input.x[0], &ram[0], nullptr));

  input["a"] = -2.0;
  EXPECT_EQ(-4.0, f(&input.x[0], &ram[0], nullptr));
}

TEST(OptimizationJIT, AddConstant) {
  using namespace current::expression;

  VarsContext context;

  x["b"] = 1.0;
  value_t const value = x["b"] + 1.0;

  jit::JITCodeGenerator code_generator;
  std::vector<double> ram(code_generator.RAMRequired());

  current::fncas::x64_native_jit::CallableVectorUInt8 f(code_generator.FunctionForNode(value));

  VarsMapper input(code_generator.Config());
  EXPECT_EQ(2.0, f(&input.x[0], &ram[0], nullptr));

  input["b"] = 2.0;
  EXPECT_EQ(3.0, f(&input.x[0], &ram[0], nullptr));

  input["b"] = -2.0;
  EXPECT_EQ(-1.0, f(&input.x[0], &ram[0], nullptr));
}

TEST(OptimizationJIT, Exp) {
  using namespace current::expression;

  VarsContext context;

  x["c"] = 0.0;
  value_t const value = exp(x["c"]);

  // No need to provide `context`, the thread-local singleton will be used by default, and it will be `.Freeze()`-ed.
  jit::JITCodeGenerator code_generator;
  std::vector<double> ram(code_generator.RAMRequired());

  current::fncas::x64_native_jit::CallableVectorUInt8 f(code_generator.FunctionForNode(value));

  std::vector<double (*)(double x)> fns;
  fns.push_back(std::exp);

  VarsMapper input(code_generator.Config());
  EXPECT_EQ(exp(0.0), f(&input.x[0], &ram[0], &fns[0]));

  input["c"] = 1.0;
  EXPECT_EQ(exp(1.0), f(&input.x[0], &ram[0], &fns[0]));

  input["c"] = 2.0;
  EXPECT_EQ(exp(2.0), f(&input.x[0], &ram[0], &fns[0]));

  input["c"] = -1.0;
  EXPECT_EQ(exp(-1.0), f(&input.x[0], &ram[0], &fns[0]));

  input["c"] = -2.0;
  EXPECT_EQ(exp(-2.0), f(&input.x[0], &ram[0], &fns[0]));
}

TEST(OptimizationJIT, NoIntersectingJITGeneratorsAllowed) {
  using namespace current::expression;

  VarsContext context;

  jit::JITCodeGenerator code_generator;
  ASSERT_THROW(jit::JITCodeGenerator illegal_code_generator, VarsAlreadyFrozenException);
}

TEST(OptimizationJIT, JITGeneratorUnfreezesVarsContext) {
  using namespace current::expression;

  VarsContext context;

  { jit::JITCodeGenerator code_generator_1; }
  { jit::JITCodeGenerator code_generator_2; }
  {
    context.Freeze();
    ASSERT_THROW(jit::JITCodeGenerator illegal_code_generator, VarsAlreadyFrozenException);
  }
}

#endif  // FNCAS_X64_NATIVE_JIT_ENABLED
