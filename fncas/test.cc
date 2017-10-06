/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2015 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>
          (c) 2015 Maxim Zhurovich <zhurovich@gmail.com>

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

#include "../3rdparty/gtest/gtest-main.h"

// #define FNCAS_USE_LONG_DOUBLE  // Tested with this option too. -- D.K., 2016/12/15.

// Support the test both ways: With FnCAS functions in `std::` and with them not in `std::`.
// #define INJECT_FNCAS_INTO_NAMESPACE_STD  // Tested with this option too. -- D.K., 2016/12/15.

#ifndef INJECT_FNCAS_INTO_NAMESPACE_STD
#define unittest_fncas_namespace fncas
#else
#define unittest_fncas_namespace std
#endif  // INJECT_FNCAS_INTO_NAMESPACE_STD

#include "fncas.h"

#include <functional>
#include <thread>

#ifdef FNCAS_JIT_COMPILED
// The docu is pointless w/o JIT, which `FNCAS_USE_LONG_DOUBLE`, if set, does disable.
#include "docu/docu_10.cc"
#endif

template <typename T>
T ParametrizedFunction(const std::vector<T>& x, size_t c) {
  CURRENT_ASSERT(x.size() == 2u);
  return (x[0] + x[1] * c) * (x[0] + x[1] * c);
}

// Need an explicit specialization, as `SimpleFunction<double_t>` is used directly in the test.
template <typename T>
T SimpleFunction(const std::vector<T>& x) {
  return ParametrizedFunction(x, 2u);
}

// `term_vector_t` behaves in a way that makes it a drop-in substitute of `std::vector<term_t>`.
static_assert(std::is_same<typename fncas::term_vector_t::value_type, fncas::term_t>::value, "");

// To accomplish the above, `std::vector<fncas::term_t>` is overridden accordingly.
static_assert(std::is_same<fncas::term_vector_t, std::vector<fncas::term_t>>::value, "");

// And the `fncas::impl::X` type is the lifetime of the function to thread-locally analyze.
// It's derived from the now-custom `std::vector<fncas::term_t>`.
static_assert(std::is_base_of<fncas::term_vector_t, fncas::variables_vector_t>::value, "");

TEST(FnCAS, ReallyNativeComputationJustToBeSure) {
  EXPECT_EQ(25, SimpleFunction(std::vector<fncas::double_t>({1, 2})));
}

TEST(FnCAS, NativeWrapper) {
  fncas::function_t<fncas::JIT::NativeWrapper> fn(SimpleFunction<fncas::double_t>, 2);
  EXPECT_EQ(25.0, fn({1.0, 2.0}));
}

TEST(FnCAS, IntermediateWrapper) {
  fncas::variables_vector_t x(2);
  fncas::function_t<fncas::JIT::Blueprint> fi = SimpleFunction(x);
  EXPECT_EQ(25.0, fi({1.0, 2.0}));
  EXPECT_EQ("((x[0]+(x[1]*2))*(x[0]+(x[1]*2)))", fi.debug_as_string());
}

TEST(FnCAS, MathFunctionsOnDoublesSupportedInFnCASNamespace) {
  EXPECT_EQ(4.0, unittest_fncas_namespace::sqrt(16.0));
  EXPECT_EQ(25.0, unittest_fncas_namespace::sqr(5.0));
}

#ifdef FNCAS_JIT_COMPILED
// This test is here mostly to have the easily locatable autogenerated files in `/tmp` (on Linux).
TEST(FnCAS, JITTrivialCompiledFunctions) {
  {
    // grep for `42.000`.
    const fncas::variables_vector_t x(1);
    const fncas::function_t<fncas::JIT::Blueprint> intermediate_function(
        [](const fncas::term_vector_t& x) { return x[0] + 42; }(x));
    EXPECT_EQ(1042, intermediate_function({1000}));

    const fncas::function_t<fncas::JIT::Default> compiled_function(intermediate_function);
    EXPECT_EQ(10042, compiled_function({10000})) << compiled_function.lib_filename();
  }
  {
    // grep for `12345.000`.
    const fncas::variables_vector_t x(1);
    const fncas::function_t<fncas::JIT::Blueprint> intermediate_function(
        [](const fncas::term_vector_t& x) { return unittest_fncas_namespace::exp(x[0]) + 12345.0; }(x));
    EXPECT_EQ(12346.0, intermediate_function({0}));
    EXPECT_NEAR(12345.0 + std::exp(1.0), intermediate_function({1}), 1e-6);

    const fncas::function_t<fncas::JIT::Default> compiled_function(intermediate_function);
    EXPECT_EQ(12346.0, compiled_function({0})) << compiled_function.lib_filename();
    // EXPECT_NEAR(12345.0 + unittest_fncas_namespace::exp(1.0), compiled_function({1}), 1e-6)
    EXPECT_NEAR(12345.0 + std::exp(1.0), compiled_function({1}), 1e-6) << compiled_function.lib_filename();
  }
}

TEST(FnCAS, JITCompiledFunctionWrapper) {
  fncas::variables_vector_t x(2);
  fncas::function_t<fncas::JIT::Blueprint> fi = SimpleFunction(x);
  fncas::function_t<fncas::JIT::Default> fc(fi);
  EXPECT_EQ(25.0, fc({1.0, 2.0})) << fc.lib_filename();
}

template <typename T>
T SmokeTestFunction(const std::vector<T>& x) {
  // The tricky functions with custom JIT implentations.
  return fncas::sqr(x[0]) + fncas::ramp(x[1]) + fncas::unit_step(x[2]);
}

TEST(FnCAS, JITSmokeBaseline) {
  fncas::variables_vector_t x(3);
  fncas::function_t<fncas::JIT::Blueprint> a = SmokeTestFunction(x);

  EXPECT_EQ(0.0, a({0.0, 0.0, -2.0}));
  EXPECT_EQ(1.0, a({0.0, 0.0, +0.0}));
  EXPECT_EQ(1.0, a({0.0, 0.0, +2.0}));
  EXPECT_EQ(0.0, a({0.0, -2.0, -1.0}));
  EXPECT_EQ(0.0, a({0.0, +0.0, -1.0}));
  EXPECT_EQ(2.0, a({0.0, +2.0, -1.0}));
  EXPECT_EQ(9.0, a({3.0, 0.0, -1.0}));
}

TEST(FnCAS, JITSmokeAS) {
  fncas::variables_vector_t x(3);
  fncas::function_t<fncas::JIT::Blueprint> a = SmokeTestFunction(x);
  fncas::function_t<fncas::JIT::AS> b(a);
  EXPECT_EQ(0.0, b({0.0, 0.0, -2.0}));
  EXPECT_EQ(1.0, b({0.0, 0.0, +0.0}));
  EXPECT_EQ(1.0, b({0.0, 0.0, +2.0}));
  EXPECT_EQ(0.0, b({0.0, -2.0, -1.0}));
  EXPECT_EQ(0.0, b({0.0, +0.0, -1.0}));
  EXPECT_EQ(2.0, b({0.0, +2.0, -1.0}));
  EXPECT_EQ(9.0, b({3.0, 0.0, -1.0}));
}

TEST(FnCAS, JITSmokeCLANG) {
  fncas::variables_vector_t x(3);
  fncas::function_t<fncas::JIT::Blueprint> a = SmokeTestFunction(x);
  fncas::function_t<fncas::JIT::CLANG> c(a);
  EXPECT_EQ(0.0, c({0.0, 0.0, -2.0}));
  EXPECT_EQ(1.0, c({0.0, 0.0, +0.0}));
  EXPECT_EQ(1.0, c({0.0, 0.0, +2.0}));
  EXPECT_EQ(0.0, c({0.0, -2.0, -1.0}));
  EXPECT_EQ(0.0, c({0.0, +0.0, -1.0}));
  EXPECT_EQ(2.0, c({0.0, +2.0, -1.0}));
  EXPECT_EQ(9.0, c({3.0, 0.0, -1.0}));
}

TEST(FnCAS, JITSmokeNASM) {
  fncas::variables_vector_t x(3);
  fncas::function_t<fncas::JIT::Blueprint> a = SmokeTestFunction(x);
  fncas::function_t<fncas::JIT::NASM> d(a);
  EXPECT_EQ(0.0, d({0.0, 0.0, -2.0}));
  EXPECT_EQ(1.0, d({0.0, 0.0, +0.0}));
  EXPECT_EQ(1.0, d({0.0, 0.0, +2.0}));
  EXPECT_EQ(0.0, d({0.0, -2.0, -1.0}));
  EXPECT_EQ(0.0, d({0.0, +0.0, -1.0}));
  EXPECT_EQ(2.0, d({0.0, +2.0, -1.0}));
  EXPECT_EQ(9.0, d({3.0, 0.0, -1.0}));
}
#endif  // FNCAS_JIT_COMPILED

TEST(FnCAS, GradientsWrapper) {
  std::vector<fncas::double_t> p_3_3({3.0, 3.0});

  fncas::gradient_t<fncas::JIT::NativeWrapper> ga(SimpleFunction<fncas::double_t>, 2);
  auto d_3_3_approx = ga(p_3_3);
  EXPECT_NEAR(18.0, d_3_3_approx[0], 1e-5);
  EXPECT_NEAR(36.0, d_3_3_approx[1], 1e-5);

  const fncas::variables_vector_t x(2);
  const fncas::gradient_t<fncas::JIT::Blueprint> gi(x, SimpleFunction(x));
  const auto d_3_3_intermediate = gi(p_3_3);
  EXPECT_EQ(18, d_3_3_intermediate[0]);
  EXPECT_EQ(36, d_3_3_intermediate[1]);
}

#ifdef FNCAS_JIT_COMPILED
TEST(FnCAS, JITGradientsWrapper) {
  std::vector<fncas::double_t> p_3_3({3.0, 3.0});

  const fncas::variables_vector_t x(2);
  const fncas::function_t<fncas::JIT::Blueprint> fi = SimpleFunction(x);
  const fncas::gradient_t<fncas::JIT::Blueprint> gi(x, fi);

  const fncas::gradient_t<fncas::JIT::Default> gc(fi, gi);

  // TODO(dkorolev): Maybe return return function value and its gradient together from a call to `gc`?
  const auto d_3_3_compiled = gc(p_3_3);

  EXPECT_EQ(18, d_3_3_compiled[0]) << gc.lib_filename();
  EXPECT_EQ(36, d_3_3_compiled[1]) << gc.lib_filename();
}

TEST(FnCAS, JITSqrGradientWrapper) {
  std::vector<fncas::double_t> p_3_3({3.0, 3.0});

  const fncas::variables_vector_t x(2);
  const fncas::function_t<fncas::JIT::Blueprint> fi = SimpleFunction(x);
  const fncas::gradient_t<fncas::JIT::Blueprint> gi(x, SimpleFunction(x));

  const fncas::function_t<fncas::JIT::Default> fc(fi);
  const fncas::double_t f_3_3_compiled = fc(p_3_3);
  EXPECT_EQ(81, f_3_3_compiled);

  const fncas::gradient_t<fncas::JIT::Default> gc(fi, gi);
  const auto d_3_3_compiled = gc(p_3_3);

  EXPECT_EQ(18, d_3_3_compiled[0]) << gc.lib_filename();
  EXPECT_EQ(36, d_3_3_compiled[1]) << gc.lib_filename();
}
#endif  // FNCAS_JIT_COMPILED

TEST(FnCAS, SupportsConcurrentThreadsViaThreadLocal) {
  const auto advanced_math = []() {
    for (size_t i = 0; i < 1000; ++i) {
      fncas::variables_vector_t x(2);
      fncas::function_t<fncas::JIT::Blueprint> fi = ParametrizedFunction(x, i + 1);
      EXPECT_EQ(fncas::sqr(1.0 + 2.0 * (i + 1)), fi({1.0, 2.0}));
    }
  };
  std::thread t1(advanced_math);
  std::thread t2(advanced_math);
  std::thread t3(advanced_math);
  t1.join();
  t2.join();
  t3.join();
}

TEST(FnCAS, CannotEvaluateMoreThanOneFunctionPerThreadAtOnce) {
  fncas::variables_vector_t x(1);
  ASSERT_THROW(fncas::variables_vector_t x(2), fncas::exceptions::FnCASConcurrentEvaluationAttemptException);
}

// An obviously convex function with a single minimum `f(3, 4) == 1`.
struct StaticFunction {
  template <typename T>
  static fncas::optimize::ObjectiveFunctionValue<T> ObjectiveFunction(const std::vector<T>& x) {
    const auto dx = x[0] - 3;
    const auto dy = x[1] - 4;
    const auto d = dx * dx + dy * dy;
    return fncas::optimize::ObjectiveFunctionValue<T>(unittest_fncas_namespace::exp(0.01 * d)).AddPoint("param", d);
  }
};

// An obviously convex function with a single minimum `f(a, b) == 1`.
struct MemberFunction {
  fncas::double_t a = 0.0;
  fncas::double_t b = 0.0;
  fncas::double_t c = 0.0;
  fncas::double_t k = +1.0;

  template <typename T>
  T ObjectiveFunction(const std::vector<T>& x) const {
    const auto dx = x[0] - a;
    const auto dy = x[1] - b;
    return (unittest_fncas_namespace::exp(0.01 * (dx * dx + dy * dy)) + c) * k;
  }
  MemberFunction() = default;
  MemberFunction(const MemberFunction&) = delete;
};

struct MemberFunctionWithReferences {
  fncas::double_t& a;
  fncas::double_t& b;
  MemberFunctionWithReferences(fncas::double_t& a, fncas::double_t& b) : a(a), b(b) {}
  template <typename T>
  T ObjectiveFunction(const std::vector<T>& x) const {
    const auto dx = x[0] - a;
    const auto dy = x[1] - b;
    return unittest_fncas_namespace::exp(0.01 * (dx * dx + dy * dy));
  }
  MemberFunctionWithReferences() = default;
  MemberFunctionWithReferences(const MemberFunctionWithReferences&) = delete;
};

// An obviously convex function with a single minimum `f(0, 0) == 0`.
struct PolynomialFunction {
  template <typename T>
  T ObjectiveFunction(const std::vector<T>& x) const {
    const fncas::double_t a = 10.0;
    const fncas::double_t b = 0.5;
    return (a * x[0] * x[0] + b * x[1] * x[1]);
  }
};

// http://en.wikipedia.org/wiki/Rosenbrock_function
// Non-convex function with global minimum `f(a, a^2) == 0`.
struct RosenbrockFunction {
  template <typename T>
  T ObjectiveFunction(const std::vector<T>& x) const {
    const fncas::double_t a = 1.0;
    const fncas::double_t b = 100.0;
    const auto d1 = (a - x[0]);
    const auto d2 = (x[1] - x[0] * x[0]);
    return (d1 * d1 + b * d2 * d2);
  }
};

// http://en.wikipedia.org/wiki/Himmelblau%27s_function
// Non-convex function with four local minima:
// f(3.0, 2.0) = 0.0
// f(-2.805118, 3.131312) = 0.0
// f(-3.779310, -3.283186) = 0.0
// f(3.584428, -1.848126) = 0.0
struct HimmelblauFunction {
  template <typename T>
  T ObjectiveFunction(const std::vector<T>& x) const {
    const auto d1 = (x[0] * x[0] + x[1] - 11);
    const auto d2 = (x[0] + x[1] * x[1] - 7);
    return (d1 * d1 + d2 * d2);
  }
};

#ifdef FNCAS_JIT_COMPILED
TEST(FnCAS, JITOptimizationOfAStaticFunctionWith) {
  const auto result = fncas::optimize::GradientDescentOptimizer<StaticFunction>().Optimize({0, 0});
  EXPECT_NEAR(1.0, result.value, 1e-3);
  ASSERT_EQ(2u, result.point.size());
  EXPECT_NEAR(3.0, result.point[0], 1e-3);
  EXPECT_NEAR(4.0, result.point[1], 1e-3);
}

TEST(FnCAS, JITOptimizationOfAMemberFunction) {
  MemberFunction f;
  f.a = 2.0;
  f.b = 1.0;
  const auto result = fncas::optimize::GradientDescentOptimizer<MemberFunction>(f).Optimize({0, 0});
  EXPECT_NEAR(1.0, result.value, 1e-3);
  ASSERT_EQ(2u, result.point.size());
  EXPECT_NEAR(2.0, result.point[0], 1e-3);
  EXPECT_NEAR(1.0, result.point[1], 1e-3);
}
#endif  // FNCAS_JIT_COMPILED

TEST(FnCAS, OptimizationOfAStaticFunctionNoJIT) {
  size_t iterations = 0;
  {
    // A run without computing the original function at each step.
    const auto result =
        fncas::optimize::GradientDescentOptimizer<StaticFunction>(fncas::optimize::OptimizerParameters().DisableJIT())
            .Optimize({0, 0});
    iterations = result.optimization_iterations;
    EXPECT_GE(iterations, 3u);  // Should be at least three iterations.
    EXPECT_NEAR(1.0, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(3.0, result.point[0], 1e-3);
    EXPECT_NEAR(4.0, result.point[1], 1e-3);
    ASSERT_FALSE(Exists(result.progress));
  }
  {
    // Another run that does compute the original function at each step, confirm
    // the "param" intermediate value has been recorded.
    const auto result = fncas::optimize::GradientDescentOptimizer<StaticFunction>(
                            fncas::optimize::OptimizerParameters().DisableJIT().TrackOptimizationProgress())
                            .Optimize({0, 0});
    EXPECT_EQ(iterations, result.optimization_iterations);
    EXPECT_NEAR(1.0, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(3.0, result.point[0], 1e-3);
    EXPECT_NEAR(4.0, result.point[1], 1e-3);
    ASSERT_TRUE(Exists(result.progress));
    const auto& progress = Value(result.progress);
    EXPECT_EQ(iterations, progress.iterations);
    EXPECT_EQ(iterations, progress.objective_function_values.size());
    ASSERT_EQ(1u, progress.additional_values.size());
    ASSERT_TRUE(progress.additional_values.count("param"));
    const auto& param_values = progress.additional_values.at("param");
    ASSERT_EQ(iterations, param_values.size());
    for (size_t i = 1; i < iterations; ++i) {
      EXPECT_LT(progress.objective_function_values[i], progress.objective_function_values[i - 1])
          << JSON(progress) << '\n'
          << i;
    }
    for (size_t i = 0; i < iterations; ++i) {
      EXPECT_EQ(progress.objective_function_values[i], std::exp(0.01 * param_values[i])) << JSON(progress) << '\n' << i;
    }
  }
}

TEST(FnCAS, OptimizationOfAMemberFunctionNoJIT) {
  MemberFunction f;
  f.a = 3.0;
  f.b = 4.0;
  const auto result =
      fncas::optimize::GradientDescentOptimizer<MemberFunction>(fncas::optimize::OptimizerParameters().DisableJIT(), f)
          .Optimize({0, 0});
  EXPECT_NEAR(1.0, result.value, 1e-3);
  ASSERT_EQ(2u, result.point.size());
  EXPECT_NEAR(3.0, result.point[0], 1e-3);
  EXPECT_NEAR(4.0, result.point[1], 1e-3);
}

TEST(FnCAS, OptimizationOfAMemberFunctionCapturesFunctionByReferenceNoJIT) {
  MemberFunction f;
  fncas::optimize::GradientDescentOptimizer<MemberFunction> optimizer(
      fncas::optimize::OptimizerParameters().DisableJIT(), f);
  {
    f.a = 2.0;
    f.b = 1.0;
    const auto result = optimizer.Optimize({0, 0});
    EXPECT_NEAR(1.0, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(2.0, result.point[0], 1e-3);
    EXPECT_NEAR(1.0, result.point[1], 1e-3);
  }
  {
    f.a = 3.0;
    f.b = 4.0;
    const auto result = optimizer.Optimize({0, 0});
    EXPECT_NEAR(1.0, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(3.0, result.point[0], 1e-3);
    EXPECT_NEAR(4.0, result.point[1], 1e-3);
  }
}

TEST(FnCAS, OptimizationOfAMemberFunctionConstructsObjectiveFunctionObjectNoJIT) {
  fncas::optimize::GradientDescentOptimizer<MemberFunction> optimizer(
      fncas::optimize::OptimizerParameters().DisableJIT());  // Will construct the object by itself.
  {
    optimizer->a = 2.0;
    optimizer->b = 1.0;
    const auto result = optimizer.Optimize({0, 0});
    EXPECT_NEAR(1.0, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(2.0, result.point[0], 1e-3);
    EXPECT_NEAR(1.0, result.point[1], 1e-3);
  }
  {
    optimizer->a = 3.0;
    optimizer->b = 4.0;
    const auto result = optimizer.Optimize({0, 0});
    EXPECT_NEAR(1.0, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(3.0, result.point[0], 1e-3);
    EXPECT_NEAR(4.0, result.point[1], 1e-3);
  }
}

TEST(FnCAS, OptimizationOfAMemberFunctionForwardsParametersNoJIT) {
  fncas::double_t a = 0;
  fncas::double_t b = 0;
  // `GradientDescentOptimizer` will construct the instance of `MemberFunctionWithReferences`.
  fncas::optimize::GradientDescentOptimizer<MemberFunctionWithReferences> optimizer(
      fncas::optimize::OptimizerParameters().DisableJIT(), a, b);
  {
    a = 2.0;
    b = 1.0;
    const auto result = optimizer.Optimize({0, 0});
    EXPECT_NEAR(1.0, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(2.0, result.point[0], 1e-3);
    EXPECT_NEAR(1.0, result.point[1], 1e-3);
  }
  {
    a = 3.0;
    b = 4.0;
    const auto result = optimizer.Optimize({0, 0});
    EXPECT_NEAR(1.0, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(3.0, result.point[0], 1e-3);
    EXPECT_NEAR(4.0, result.point[1], 1e-3);
  }
}

#ifdef FNCAS_JIT_COMPILED
TEST(FnCAS, JITOptimizationOfAPolynomialMemberFunctionT) {
  const auto result = fncas::optimize::GradientDescentOptimizer<PolynomialFunction>().Optimize({5.0, 20.0});
  EXPECT_NEAR(0.0, result.value, 1e-3);
  ASSERT_EQ(2u, result.point.size());
  EXPECT_NEAR(0.0, result.point[0], 1e-3);
  EXPECT_NEAR(0.0, result.point[1], 1e-3);
}

TEST(FnCAS, JITOptimizationOfAPolynomialUsingBacktrackingGD) {
  const auto result = fncas::optimize::GradientDescentOptimizerBT<PolynomialFunction>().Optimize({5.0, 20.0});
  EXPECT_NEAR(0.0, result.value, 1e-3);
  ASSERT_EQ(2u, result.point.size());
  EXPECT_NEAR(0.0, result.point[0], 1e-3);
  EXPECT_NEAR(0.0, result.point[1], 1e-3);
}

TEST(FnCAS, JITOptimizationOfAPolynomialUsingConjugateGradient) {
  const auto result = fncas::optimize::ConjugateGradientOptimizer<PolynomialFunction>().Optimize({5.0, 20.0});
  EXPECT_NEAR(0.0, result.value, 1e-6);
  ASSERT_EQ(2u, result.point.size());
  EXPECT_NEAR(0.0, result.point[0], 1e-6);
  EXPECT_NEAR(0.0, result.point[1], 1e-6);
}

TEST(FnCAS, JITOptimizationOfRosenbrockUsingConjugateGradient) {
  const auto result = fncas::optimize::ConjugateGradientOptimizer<RosenbrockFunction>().Optimize({-3.0, -4.0});
  EXPECT_NEAR(0.0, result.value, 1e-6);
  ASSERT_EQ(2u, result.point.size());
  EXPECT_NEAR(1.0, result.point[0], 1e-6);
  EXPECT_NEAR(1.0, result.point[1], 1e-6);
}

TEST(FnCAS, JITOptimizationOfHimmelblauUsingConjugateGradient) {
  fncas::optimize::ConjugateGradientOptimizer<HimmelblauFunction> optimizer;

  const auto min1 = optimizer.Optimize({5.0, 5.0});
  EXPECT_NEAR(0.0, min1.value, 1e-6);
  ASSERT_EQ(2u, min1.point.size());
  EXPECT_NEAR(3.0, min1.point[0], 1e-6);
  EXPECT_NEAR(2.0, min1.point[1], 1e-6);

  const auto min2 = optimizer.Optimize({-3.0, 5.0});
  EXPECT_NEAR(0.0, min2.value, 1e-6);
  ASSERT_EQ(2u, min2.point.size());
  EXPECT_NEAR(-2.805118, min2.point[0], 1e-6);
  EXPECT_NEAR(3.131312, min2.point[1], 1e-6);

  const auto min3 = optimizer.Optimize({-5.0, -5.0});
  EXPECT_NEAR(0.0, min3.value, 1e-6);
  ASSERT_EQ(2u, min3.point.size());
  EXPECT_NEAR(-3.779310, min3.point[0], 1e-6);
  EXPECT_NEAR(-3.283186, min3.point[1], 1e-6);

  const auto min4 = optimizer.Optimize({5.0, -5.0});
  EXPECT_NEAR(0.0, min4.value, 1e-6);
  ASSERT_EQ(2u, min4.point.size());
  EXPECT_NEAR(3.584428, min4.point[0], 1e-6);
  EXPECT_NEAR(-1.848126, min4.point[1], 1e-6);
}
#endif  // FNCAS_JIT_COMPILED

TEST(FnCAS, OptimizationOfAPolynomialMemberFunctionNoJIT) {
  const auto result =
      fncas::optimize::GradientDescentOptimizer<PolynomialFunction>(fncas::optimize::OptimizerParameters().DisableJIT())
          .Optimize({5.0, 20.0});
  EXPECT_NEAR(0.0, result.value, 1e-3);
  ASSERT_EQ(2u, result.point.size());
  EXPECT_NEAR(0.0, result.point[0], 1e-3);
  EXPECT_NEAR(0.0, result.point[1], 1e-3);
}

TEST(FnCAS, OptimizationOfAPolynomialUsingBacktrackingGDNoJIT) {
  const auto result = fncas::optimize::GradientDescentOptimizerBT<PolynomialFunction>(
                          fncas::optimize::OptimizerParameters().DisableJIT())
                          .Optimize({5.0, 20.0});
  EXPECT_NEAR(0.0, result.value, 1e-3);
  ASSERT_EQ(2u, result.point.size());
  EXPECT_NEAR(0.0, result.point[0], 1e-3);
  EXPECT_NEAR(0.0, result.point[1], 1e-3);
}

TEST(FnCAS, OptimizationOfAPolynomialUsingConjugateGradientNoJIT) {
  const auto result = fncas::optimize::ConjugateGradientOptimizer<PolynomialFunction>(
                          fncas::optimize::OptimizerParameters().DisableJIT())
                          .Optimize({5.0, 20.0});
  EXPECT_NEAR(0.0, result.value, 1e-6);
  ASSERT_EQ(2u, result.point.size());
  EXPECT_NEAR(0.0, result.point[0], 1e-6);
  EXPECT_NEAR(0.0, result.point[1], 1e-6);
}

TEST(FnCAS, OptimizationOfRosenbrockUsingConjugateGradientNoJIT) {
  const auto result = fncas::optimize::ConjugateGradientOptimizer<RosenbrockFunction>(
                          fncas::optimize::OptimizerParameters().DisableJIT())
                          .Optimize({-3.0, -4.0});
  EXPECT_NEAR(0.0, result.value, 1e-6);
  ASSERT_EQ(2u, result.point.size());
  EXPECT_NEAR(1.0, result.point[0], 1e-6);
  EXPECT_NEAR(1.0, result.point[1], 1e-6);
}

TEST(FnCAS, OptimizationOfHimmelblauUsingConjugateGradientNoJIT) {
  fncas::optimize::ConjugateGradientOptimizer<HimmelblauFunction> optimizer(
      fncas::optimize::OptimizerParameters().DisableJIT());

  const auto min1 = optimizer.Optimize({5.0, 5.0});
  EXPECT_NEAR(0.0, min1.value, 1e-6);
  ASSERT_EQ(2u, min1.point.size());
  EXPECT_NEAR(3.0, min1.point[0], 1e-6);
  EXPECT_NEAR(2.0, min1.point[1], 1e-6);

  const auto min2 = optimizer.Optimize({-3.0, 5.0});
  EXPECT_NEAR(0.0, min2.value, 1e-6);
  ASSERT_EQ(2u, min2.point.size());
  EXPECT_NEAR(-2.805118, min2.point[0], 1e-6);
  EXPECT_NEAR(3.131312, min2.point[1], 1e-6);

  const auto min3 = optimizer.Optimize({-5.0, -5.0});
  EXPECT_NEAR(0.0, min3.value, 1e-6);
  ASSERT_EQ(2u, min3.point.size());
  EXPECT_NEAR(-3.779310, min3.point[0], 1e-6);
  EXPECT_NEAR(-3.283186, min3.point[1], 1e-6);

  const auto min4 = optimizer.Optimize({5.0, -5.0});
  EXPECT_NEAR(0.0, min4.value, 1e-6);
  ASSERT_EQ(2u, min4.point.size());
  EXPECT_NEAR(3.584428, min4.point[0], 1e-6);
  EXPECT_NEAR(-1.848126, min4.point[1], 1e-6);
}

// Check that gradient descent optimizer with backtracking performs better than
// naive optimizer on Rosenbrock function, when maximum step count = 1000.
TEST(FnCAS, NaiveGDvsBacktrackingGDOnRosenbrockFunction1000StepsNoJIT) {
  fncas::optimize::OptimizerParameters params;
  params.SetValue("max_steps", 1000);
  params.SetValue("step_factor", 0.001);  // Used only by naive optimizer. Prevents it from moving to infinity.
  const auto result_naive = fncas::optimize::GradientDescentOptimizer<RosenbrockFunction,
                                                                      fncas::OptimizationDirection::Minimize,
                                                                      fncas::JIT::Blueprint>(params)
                                .Optimize({-3.0, -4.0});
  const auto result_bt = fncas::optimize::GradientDescentOptimizerBT<RosenbrockFunction,
                                                                     fncas::OptimizationDirection::Minimize,
                                                                     fncas::JIT::Blueprint>(params)
                             .Optimize({-3.0, -4.0});
  const fncas::double_t x0_err_n = std::abs(result_naive.point[0] - 1.0);
  const fncas::double_t x0_err_bt = std::abs(result_bt.point[0] - 1.0);
  const fncas::double_t x1_err_n = std::abs(result_naive.point[1] - 1.0);
  const fncas::double_t x1_err_bt = std::abs(result_bt.point[1] - 1.0);
  ASSERT_TRUE(fncas::IsNormal(x0_err_n));
  ASSERT_TRUE(fncas::IsNormal(x1_err_n));
  ASSERT_TRUE(fncas::IsNormal(x0_err_bt));
  ASSERT_TRUE(fncas::IsNormal(x1_err_bt));
  EXPECT_TRUE(x0_err_bt < x0_err_n);
  EXPECT_TRUE(x1_err_bt < x1_err_n);
  EXPECT_NEAR(1.0, result_bt.point[0], 1e-6);
  EXPECT_NEAR(1.0, result_bt.point[1], 1e-6);
}

// Check that conjugate gradient optimizer performs better than gradient descent
// optimizer with backtracking on Rosenbrock function, when maximum step count = 100.
TEST(FnCAS, ConjugateGDvsBacktrackingGDOnRosenbrockFunction100StepsNoJIT) {
  fncas::optimize::OptimizerParameters params;
  params.SetValue("max_steps", 100);
  params.DisableJIT();
  const auto result_cg = fncas::optimize::ConjugateGradientOptimizer<RosenbrockFunction>(params).Optimize({-3.0, -4.0});
  const auto result_bt = fncas::optimize::GradientDescentOptimizerBT<RosenbrockFunction>(params).Optimize({-3.0, -4.0});
  const fncas::double_t x0_err_cg = std::abs(result_cg.point[0] - 1.0);
  const fncas::double_t x0_err_bt = std::abs(result_bt.point[0] - 1.0);
  const fncas::double_t x1_err_cg = std::abs(result_cg.point[1] - 1.0);
  const fncas::double_t x1_err_bt = std::abs(result_bt.point[1] - 1.0);
  ASSERT_TRUE(fncas::IsNormal(x0_err_cg));
  ASSERT_TRUE(fncas::IsNormal(x1_err_cg));
  ASSERT_TRUE(fncas::IsNormal(x0_err_bt));
  ASSERT_TRUE(fncas::IsNormal(x1_err_bt));
  EXPECT_TRUE(x0_err_cg < x0_err_bt);
  EXPECT_TRUE(x1_err_cg < x1_err_bt);
  EXPECT_NEAR(1.0, result_cg.point[0], 1e-6);
  EXPECT_NEAR(1.0, result_cg.point[1], 1e-6);
}

// To test evaluation and differentiation.
template <typename T>
T ZeroOrXFunction(const std::vector<T> x) {
  EXPECT_EQ(1u, x.size());
  return fncas::ramp(x[0]);
}

// To test evaluation and differentiation of `f(g(x))` where `f` is `fncas::ramp`.
template <typename T>
T ZeroOrXOfSquareXMinusTen(const std::vector<T>& x) {
  EXPECT_EQ(1u, x.size());
  return fncas::ramp(fncas::sqr(x[0]) - 10);  // So that the argument is sometimes negative.
}

TEST(FnCAS, CustomFunctions) {
  EXPECT_EQ(0.0, fncas::unit_step(-1.0));
  EXPECT_EQ(1.0, fncas::unit_step(+2.0));

  EXPECT_EQ(0.0, fncas::ramp(-3.0));
  EXPECT_EQ(4.0, fncas::ramp(+4.0));

  const fncas::variables_vector_t x(1);
  const fncas::function_t<fncas::JIT::Blueprint> intermediate_function = ZeroOrXFunction(x);
  EXPECT_EQ(0.0, intermediate_function({-5.0}));
  EXPECT_EQ(6.0, intermediate_function({+6.0}));

#ifdef FNCAS_JIT_COMPILED
  const fncas::function_t<fncas::JIT::Default> compiled_function(intermediate_function);
  EXPECT_EQ(0.0, compiled_function({-5.5})) << compiled_function.lib_filename();
  EXPECT_EQ(6.5, compiled_function({+6.5})) << compiled_function.lib_filename();
#endif

  const fncas::gradient_t<fncas::JIT::NativeWrapper> approximate_gradient(ZeroOrXFunction<fncas::double_t>, 1);
  EXPECT_NEAR(0.0, approximate_gradient({-5.0})[0], 1e-6);
  EXPECT_NEAR(1.0, approximate_gradient({+6.0})[0], 1e-6);

  const fncas::gradient_t<fncas::JIT::Blueprint> intermediate_gradient(x, intermediate_function);
  EXPECT_EQ(0.0, intermediate_gradient({-7.0})[0]);
  EXPECT_EQ(1.0, intermediate_gradient({+8.0})[0]);

#ifdef FNCAS_JIT_COMPILED
  const fncas::gradient_t<fncas::JIT::Default> compiled_gradient(intermediate_function, intermediate_gradient);
  EXPECT_EQ(0.0, compiled_gradient({-9.5})[0]) << compiled_gradient.lib_filename();
  EXPECT_EQ(1.0, compiled_gradient({+9.5})[0]) << compiled_gradient.lib_filename();
#endif
}

TEST(FnCAS, ComplexCustomFunctions) {
  const fncas::variables_vector_t x(1);

  const fncas::function_t<fncas::JIT::Blueprint> intermediate_function = ZeroOrXOfSquareXMinusTen(x);
  EXPECT_EQ(0.0, intermediate_function({3.0}));  // fncas::ramp(3*3 - 10) == 0
  EXPECT_EQ(6.0, intermediate_function({4.0}));  // fncas::ramp(4*4 - 10) == 6

#ifdef FNCAS_JIT_COMPILED
  const fncas::function_t<fncas::JIT::Default> compiled_function(intermediate_function);
  EXPECT_EQ(0.0, compiled_function({3.0})) << compiled_function.lib_filename();
  EXPECT_EQ(6.0, compiled_function({4.0})) << compiled_function.lib_filename();
#endif

  const fncas::gradient_t<fncas::JIT::NativeWrapper> approximate_gradient(ZeroOrXOfSquareXMinusTen<fncas::double_t>, 1);
  EXPECT_NEAR(0.0, approximate_gradient({3.0})[0], 1e-6);
  EXPECT_NEAR(8.0, approximate_gradient({4.0})[0], 1e-6);  // == the derivative of `x^2` with `x = 4`.

  const fncas::gradient_t<fncas::JIT::Blueprint> intermediate_gradient(x, intermediate_function);
  EXPECT_EQ(0.0, intermediate_gradient({3.0})[0]);
  EXPECT_EQ(8.0, intermediate_gradient({4.0})[0]);

#ifdef FNCAS_JIT_COMPILED
  const fncas::gradient_t<fncas::JIT::Default> compiled_gradient(intermediate_function, intermediate_gradient);
  EXPECT_EQ(0.0, compiled_gradient({3.0})[0]) << compiled_gradient.lib_filename();
  EXPECT_EQ(8.0, compiled_gradient({4.0})[0]) << compiled_gradient.lib_filename();
#endif
}

// Confirm the stopping criterion does the job for negative and sign-changing functions.
TEST(FnCAS, OptimizationOfNegativeAndZeroCrossingFunctions) {
  {
    MemberFunction f;
    f.a = 3.0;
    f.b = 4.0;
    f.c = -1.01;
    const auto result = fncas::optimize::GradientDescentOptimizer<MemberFunction>(
                            fncas::optimize::OptimizerParameters().DisableJIT(), f)
                            .Optimize({0, 0});
    EXPECT_NEAR(-0.01, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(3.0, result.point[0], 1e-3);
    EXPECT_NEAR(4.0, result.point[1], 1e-3);
  }
  {
    MemberFunction f;
    f.a = 3.0;
    f.b = 4.0;
    f.c = -0.99;
    const auto result = fncas::optimize::GradientDescentOptimizer<MemberFunction>(
                            fncas::optimize::OptimizerParameters().DisableJIT(), f)
                            .Optimize({0, 0});
    EXPECT_NEAR(+0.01, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(3.0, result.point[0], 1e-3);
    EXPECT_NEAR(4.0, result.point[1], 1e-3);
  }
  {
    MemberFunction f;
    f.a = 3.0;
    f.b = 4.0;
    f.c = -100;
    const auto result = fncas::optimize::GradientDescentOptimizer<MemberFunction>(
                            fncas::optimize::OptimizerParameters().DisableJIT(), f)
                            .Optimize({0, 0});
    EXPECT_NEAR(-99, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(3.0, result.point[0], 1e-3);
    EXPECT_NEAR(4.0, result.point[1], 1e-3);
  }
}

TEST(FnCAS, OptimizationInMaximizingDirection) {
  {
    MemberFunction f;
    f.a = 2.0;
    f.b = 3.0;
    f.k = -1;
    const auto result =
        fncas::optimize::GradientDescentOptimizer<MemberFunction, fncas::OptimizationDirection::Maximize>(
            fncas::optimize::OptimizerParameters().DisableJIT(), f)
            .Optimize({0, 0});
    EXPECT_NEAR(-1, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(2.0, result.point[0], 1e-3);
    EXPECT_NEAR(3.0, result.point[1], 1e-3);
  }

  {
    MemberFunction f;
    f.a = 4.0;
    f.b = 5.0;
    f.k = -1;
    const auto result =
        fncas::optimize::GradientDescentOptimizerBT<MemberFunction, fncas::OptimizationDirection::Maximize>(
            fncas::optimize::OptimizerParameters().DisableJIT(), f)
            .Optimize({0, 0});
    EXPECT_NEAR(-1, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(4.0, result.point[0], 5e-2);
    EXPECT_NEAR(5.0, result.point[1], 5e-2);
  }

  {
    MemberFunction f;
    f.a = 6.0;
    f.b = 7.0;
    f.k = -1;
    const auto result =
        fncas::optimize::ConjugateGradientOptimizer<MemberFunction, fncas::OptimizationDirection::Maximize>(
            fncas::optimize::OptimizerParameters().DisableJIT(), f)
            .Optimize({0, 0});
    EXPECT_NEAR(-1, result.value, 1e-3);
    ASSERT_EQ(2u, result.point.size());
    EXPECT_NEAR(6.0, result.point[0], 5e-2);
    EXPECT_NEAR(7.0, result.point[1], 5e-2);
  }
}
