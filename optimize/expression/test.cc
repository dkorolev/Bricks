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

#include "expression.h"

#include "../../3rdparty/gtest/gtest-main.h"
#include "../../3rdparty/gtest/singlequoted.h"

TEST(OptimizationExpression, SimpleVarsAddition) {
  using namespace current::expression;

  VarsContext vars_context;

  x[0] = 0.0;
  x[1] = 0.0;

  value_t const v0(x[0]);
  value_t const v1(x[1]);

  EXPECT_EQ("x[0]", v0.DebugAsString());
  EXPECT_EQ("x[1]", v1.DebugAsString());

  value_t const v_sum = v0 + v1;
  EXPECT_EQ("(x[0]+x[1])", v_sum.DebugAsString());

  EXPECT_EQ("(x[0]+x[1])", (v0 + v1).DebugAsString());
  EXPECT_EQ("(x[0]+x[1])", (v0 + x[1]).DebugAsString());
  EXPECT_EQ("(x[0]+x[1])", (x[0] + v1).DebugAsString());
  EXPECT_EQ("(x[0]+x[1])", (x[0] + x[1]).DebugAsString());

  // Make sure the internal "flippped" trick is tested through.
  EXPECT_EQ("(x[0]+1.000000)", (v0 + 1.0).DebugAsString());
  EXPECT_EQ("(2.000000+x[0])", (2.0 + v0).DebugAsString());
  EXPECT_EQ("(x[0]-3.000000)", (v0 - 3.0).DebugAsString());
  EXPECT_EQ("(4.000000-x[0])", (4.0 - v0).DebugAsString());
  EXPECT_EQ("(x[1]*5.000000)", (v1 * 5.0).DebugAsString());
  EXPECT_EQ("(6.000000*x[1])", (6.0 * v1).DebugAsString());
  EXPECT_EQ("(x[1]/7.000000)", (v1 / 7.0).DebugAsString());
  EXPECT_EQ("(8.000000/x[1])", (8.0 / v1).DebugAsString());
  EXPECT_EQ("(0.000000-x[0])", (-v0).DebugAsString());
  EXPECT_EQ("(lambda+9.000000)", (value_t::lambda() + 9.0).DebugAsString());
  EXPECT_EQ("(9.000000/lambda)", (9.0 / value_t::lambda()).DebugAsString());
  EXPECT_EQ("(0.000000-lambda)", (-value_t::lambda()).DebugAsString());
}

TEST(OptimizationExpression, AddingImmediatesToVars) {
  using namespace current::expression;

  VarsContext vars_context;

  x["test"] = 0.0;
  value_t const v(x["test"]);
  value_t const v_sum_1 = v + 1.0;
  value_t const v_sum_2 = 1.0 + v;

  EXPECT_EQ("x['test']", SingleQuoted(v.DebugAsString()));

  EXPECT_EQ("(x['test']+1.000000)", SingleQuoted(v_sum_1.DebugAsString()));
  EXPECT_EQ("(1.000000+x['test'])", SingleQuoted(v_sum_2.DebugAsString()));

  EXPECT_EQ("(x['test']+1.000000)", SingleQuoted((v + 1.0).DebugAsString()));
  EXPECT_EQ("(1.000000+x['test'])", SingleQuoted((1.0 + v).DebugAsString()));

  EXPECT_EQ("(x['test']+1.000000)", SingleQuoted((x["test"] + 1.0).DebugAsString()));
  EXPECT_EQ("(1.000000+x['test'])", SingleQuoted((1.0 + x["test"]).DebugAsString()));
}

TEST(OptimizationExpression, NestedVarsAddition) {
  using namespace current::expression;

  VarsContext vars_context;

  x["foo"] = 0;
  x["bar"] = 0;
  x["baz"] = 0;

  EXPECT_EQ("(x['foo']+x['bar'])", SingleQuoted((x["foo"] + x["bar"]).DebugAsString()));

  EXPECT_EQ("((x['foo']+x['bar'])+x['baz'])", SingleQuoted((x["foo"] + x["bar"] + x["baz"]).DebugAsString()));
  EXPECT_EQ("((x['foo']+x['bar'])+x['baz'])", SingleQuoted(((x["foo"] + x["bar"]) + x["baz"]).DebugAsString()));
  EXPECT_EQ("(x['foo']+(x['bar']+x['baz']))", SingleQuoted((x["foo"] + (x["bar"] + x["baz"])).DebugAsString()));
}

TEST(OptimizationExpression, OtherOperations) {
  using namespace current::expression;

  VarsContext vars_context;

  x["a"] = 0.0;
  x["b"] = 0.0;
  value_t const a(x["a"]);
  value_t const b(x["b"]);

  EXPECT_EQ("(x['a']+x['b'])", SingleQuoted((a + b).DebugAsString()));
  EXPECT_EQ("(x['a']-x['b'])", SingleQuoted((a - b).DebugAsString()));
  EXPECT_EQ("(x['a']*x['b'])", SingleQuoted((a * b).DebugAsString()));
  EXPECT_EQ("(x['a']/x['b'])", SingleQuoted((a / b).DebugAsString()));
}

TEST(OptimizationExpression, OperationsWithAssignment) {
  using namespace current::expression;

  VarsContext vars_context;

  x["a"] = 0.0;
  value_t const a(x["a"]);

  {
    value_t add = a;
    add += 1.0;
    EXPECT_EQ("(x['a']+1.000000)", SingleQuoted(add.DebugAsString()));

    value_t sub = a;
    sub -= 2.0;
    EXPECT_EQ("(x['a']-2.000000)", SingleQuoted(sub.DebugAsString()));

    value_t mul = a;
    mul *= 3.0;
    EXPECT_EQ("(x['a']*3.000000)", SingleQuoted(mul.DebugAsString()));

    value_t div = a;
    div /= 4.0;
    EXPECT_EQ("(x['a']/4.000000)", SingleQuoted(div.DebugAsString()));
  }

  x["b"] = 0.0;
  value_t const b(x["b"]);

  {
    value_t add = a;
    add += b;
    EXPECT_EQ("(x['a']+x['b'])", SingleQuoted(add.DebugAsString()));

    value_t sub = a;
    sub -= b;
    EXPECT_EQ("(x['a']-x['b'])", SingleQuoted(sub.DebugAsString()));

    value_t mul = a;
    mul *= b;
    EXPECT_EQ("(x['a']*x['b'])", SingleQuoted(mul.DebugAsString()));

    value_t div = a;
    div /= b;
    EXPECT_EQ("(x['a']/x['b'])", SingleQuoted(div.DebugAsString()));
  }
}

TEST(OptimizationExpression, UnaryOperations) {
  using namespace current::expression;

  VarsContext vars_context;
  x["a"] = 0.0;

  EXPECT_EQ("x['a']", SingleQuoted(value_t(x["a"]).DebugAsString()));
  EXPECT_EQ("x['a']", SingleQuoted((+value_t(x["a"])).DebugAsString()));
  EXPECT_EQ("x['a']", SingleQuoted((+x["a"]).DebugAsString()));
  EXPECT_EQ("x['a']", SingleQuoted((+(+x["a"])).DebugAsString()));
  EXPECT_EQ("(0.000000-x['a'])", SingleQuoted((-value_t(x["a"])).DebugAsString()));
  EXPECT_EQ("(0.000000-x['a'])", SingleQuoted((-x["a"]).DebugAsString()));
  EXPECT_EQ("(0.000000-(0.000000-x['a']))", SingleQuoted((-(-x["a"])).DebugAsString()));
}

TEST(OptimizationExpression, SimpleVarsExponentiation) {
  using namespace current::expression;

  VarsContext vars_context;

  x["foo"] = 0.0;

  value_t const v(x["foo"]);
  EXPECT_EQ("x['foo']", SingleQuoted(v.DebugAsString()));

  value_t const v_exp = exp(v);
  EXPECT_EQ("exp(x['foo'])", SingleQuoted(v_exp.DebugAsString()));

  EXPECT_EQ("exp(x['foo'])", SingleQuoted(exp(v).DebugAsString()));
  EXPECT_EQ("exp(x['foo'])", SingleQuoted(exp(x["foo"]).DebugAsString()));

  // NOTE(dkorolev): The below `EXPECT_EQ` would work just fine w/o "pre-declaring" the "variables", but I'd like
  // to make sure the order of the "variables" is carved in stone to avoid any and all room for the UB.
  x["bar"].SetConstant(0.0);
  x["baz"].SetConstant(0.0);
  x["blah"].SetConstant(0.0);
  EXPECT_EQ("((exp(((x['bar']+1.000000)+x['baz']))+2.500000)+exp(exp(x['blah'])))",
            SingleQuoted((exp(x["bar"] + 1.0 + x["baz"]) + 2.5 + exp(exp(x["blah"]))).DebugAsString()));
}

TEST(OptimizationExpression, OtherFunctions) {
  using namespace current::expression;

  VarsContext vars_context;

  x["p"] = 0.0;
  value_t const p(x["p"]);

  EXPECT_EQ("exp(x['p'])", SingleQuoted((exp(p)).DebugAsString()));
  EXPECT_EQ("log(x['p'])", SingleQuoted((log(p)).DebugAsString()));
  EXPECT_EQ("sin(x['p'])", SingleQuoted((sin(p)).DebugAsString()));
  EXPECT_EQ("cos(x['p'])", SingleQuoted((cos(p)).DebugAsString()));
}

TEST(OptimizationExpression, IndexesAreNonFinalizedIndexes) {
  using namespace current::expression;

  VarsContext vars_context;

  x[0] = 100;
  x[2] = 102;

  value_t const v0(x[0]);
  value_t const v2(x[2]);

  EXPECT_TRUE(!v0.GetExpressionNodeIndex().UnitTestIsNodeIndex());
  EXPECT_TRUE(!v2.GetExpressionNodeIndex().UnitTestIsNodeIndex());
  EXPECT_EQ(0u, v0.GetExpressionNodeIndex().UnitTestVarIndex());
  EXPECT_EQ(1u, v2.GetExpressionNodeIndex().UnitTestVarIndex());

  EXPECT_EQ("x[0]", v0.DebugAsString());
  EXPECT_EQ("x[2]", v2.DebugAsString());

  vars_context.Freeze();
  EXPECT_EQ("x[0]{0}", v0.DebugAsString());
  EXPECT_EQ("x[2]{1}", v2.DebugAsString());

  vars_context.Unfreeze();
  EXPECT_EQ("x[0]{0}", v0.DebugAsString());
  EXPECT_EQ("x[2]{1}", v2.DebugAsString());

  x[1] = 101;
  value_t const v1(x[1]);
  EXPECT_EQ("x[0]{0}", v0.DebugAsString());
  EXPECT_EQ("x[1]", v1.DebugAsString());  // No dense index allocated yet for `x[1]` before `Freeze()` was called.
  EXPECT_EQ("x[2]{1}", v2.DebugAsString());

  EXPECT_TRUE(!v0.GetExpressionNodeIndex().UnitTestIsNodeIndex());
  EXPECT_TRUE(!v1.GetExpressionNodeIndex().UnitTestIsNodeIndex());
  EXPECT_TRUE(!v2.GetExpressionNodeIndex().UnitTestIsNodeIndex());
  EXPECT_EQ(0u, v0.GetExpressionNodeIndex().UnitTestVarIndex());
  EXPECT_EQ(1u, v2.GetExpressionNodeIndex().UnitTestVarIndex());  // `v2`, which is `x[2]`, has an internal index `1`.
  EXPECT_EQ(2u, v1.GetExpressionNodeIndex().UnitTestVarIndex());  // `v1`, which is `x[1]`, has an internal index `2`.

  vars_context.Freeze();

  EXPECT_EQ(0u, v0.GetExpressionNodeIndex().UnitTestVarIndex());
  EXPECT_EQ(1u, v2.GetExpressionNodeIndex().UnitTestVarIndex());
  EXPECT_EQ(2u, v1.GetExpressionNodeIndex().UnitTestVarIndex());

  EXPECT_EQ("x[0]{0}", v0.DebugAsString());
  EXPECT_EQ("x[1]{1}", v1.DebugAsString());
  EXPECT_EQ("x[2]{2}", v2.DebugAsString());  // The allocated frozen index for `x[2]` has changed.
}

#ifndef NDEBUG

TEST(OptimizationExpression, MustBeWithinContextInDEBUGMode) {
  using namespace current::expression;
  std::vector<value_t> const v = []() {
    VarsContext vars_context;
    x[0] = 0.0;
    std::vector<value_t> values;
    values.push_back(x[0]);
    values.push_back(exp(x[0]));
    values.push_back(0.0);
    return values;
  }();
  ASSERT_EQ(3u, v.size());
  ASSERT_THROW(v[0].DebugAsString(), VarsManagementException);
  ASSERT_THROW(v[1].DebugAsString(), VarsManagementException);
  ASSERT_THROW(v[2].DebugAsString(), VarsManagementException);

  try {
    value_t().DebugAsString();
    ASSERT_TRUE(false);
  } catch (VarsManagementException const& e) {
    EXPECT_EQ("The variables context is required.", e.OriginalDescription());
  }
}

TEST(OptimizationExpression, ValuesAreUninitializedInDEBUGMode) {
  using namespace current::expression;

  {
    VarsContext vars_context;
    EXPECT_EQ("Uninitialized", value_t().DebugAsString());
  }
}

#else

TEST(OptimizationExpression, VarsContextIsNotRequiredForValuesInNDEBUGMode) {
  using namespace current::expression;

  // Values and simple opeations over them, as well as lambdas, should work w/o the vars context.
  // Creating expression nodes w/o a valid vars context in scope would most likely cause a segfault in NDEBUG mode.
  EXPECT_EQ("2.000000", value_t(2.0).DebugAsString());
  EXPECT_EQ("3.000000", (value_t(1.0) + value_t(2.0)).DebugAsString());
  EXPECT_EQ("1.000000", exp(value_t(0.0)).DebugAsString());
  EXPECT_EQ("lambda", value_t::lambda().DebugAsString());

  // Creating expression nodes w/o a valid vars context in scope would most likely cause a segfault in NDEBUG mode.
  // So, do not "test" it. -- D.K.
}

#endif

TEST(OptimizationExpression, FreezePreventsNodesCreation) {
  using namespace current::expression;

  VarsContext vars_context;
  x[0] = 0.0;

  value_t const tmp1 = x[0] + 1.0;
  value_t const tmp2 = x[0] + 2.0;
  value_t const tmp3 = x[0] + 3.0;

  EXPECT_EQ(0u, tmp1.GetExpressionNodeIndex().UnitTestNodeIndex());
  EXPECT_EQ(1u, tmp2.GetExpressionNodeIndex().UnitTestNodeIndex());
  EXPECT_EQ(2u, tmp3.GetExpressionNodeIndex().UnitTestNodeIndex());

  VarsMapperConfig const config = vars_context.Freeze();
  EXPECT_EQ(1u, config.total_leaves);  // Just one variable, `x[0]`.
  EXPECT_EQ(3u, config.total_nodes);   // Three `+` nodes.

  EXPECT_THROW(x[0] + 4.0, VarsManagementException);
}

TEST(OptimizationExpression, Lambda) {
  using namespace current::expression;

  VarsContext vars_context;

  x[0] = 0.0;

  value_t const a = x[0];
  value_t const b = 1.0;
  value_t const c = value_t::lambda();

  EXPECT_EQ("x[0]", a.DebugAsString());
  EXPECT_EQ("1.000000", b.DebugAsString());
  EXPECT_EQ("lambda", c.DebugAsString());
  EXPECT_EQ("((x[0]+1.000000)+lambda)", (a + b + c).DebugAsString());
}

TEST(OptimizationExpression, LambdaFunctionGeneration) {
  using namespace current::expression;

  VarsContext vars_context;

  x[0] = 0.0;
  x[1] = 0.0;

  value_t const f = x[0] + 2.0 * x[1];
  value_t const lambda = value_t::lambda();
  std::vector<value_t> substitute({1.0 + 2.0 * lambda, 3.0 + 4.0 * lambda});

  VarsMapperConfig const vars_config = vars_context.ReindexVars();

  value_t const f2 = Build1DFunction(f, vars_config, substitute);

  EXPECT_EQ("(x[0]{0}+(2.000000*x[1]{1}))", f.DebugAsString());
  EXPECT_EQ("((1.000000+(2.000000*lambda))+(2.000000*(3.000000+(4.000000*lambda))))", f2.DebugAsString());
}

TEST(OptimizationExpression, DoubleValuesAsNodes) {
  using namespace current::expression;

  VarsContext vars_context;

  value_t one = 1.0;
  value_t two = 2.0;

  EXPECT_EQ("3.000000", (one + two).DebugAsString());
  EXPECT_EQ("1.000000", sqr(one).DebugAsString());
  EXPECT_EQ("2.000000", sqrt(one + 1.0 + two).DebugAsString());
  EXPECT_EQ("1.000000", exp(one + one - 2.0).DebugAsString());
  EXPECT_EQ("0.000000", atan(two - 1.5 - 0.5).DebugAsString());
}

TEST(OptimizationExpression, ConstantsForInternalRepresentationsAreCorrect) {
  using namespace current::expression;

  EXPECT_EQ(kExpressionNodeIndexForDoubleZero, value_t(0.0).GetExpressionNodeIndex().UnitTestRawCompactifiedIndex())
      << current::strings::Printf("0x%016lx", value_t(0.0).GetExpressionNodeIndex().UnitTestRawCompactifiedIndex());

  EXPECT_EQ(kExpressionNodeIndexForDoubleNegativeZero,
            value_t(-0.0).GetExpressionNodeIndex().UnitTestRawCompactifiedIndex())
      << current::strings::Printf("0x%016lx", value_t(-0.0).GetExpressionNodeIndex().UnitTestRawCompactifiedIndex());

  EXPECT_EQ(kExpressionNodeIndexForDoubleOne, value_t(1.0).GetExpressionNodeIndex().UnitTestRawCompactifiedIndex())
      << current::strings::Printf("0x%016lx", value_t(1.0).GetExpressionNodeIndex().UnitTestRawCompactifiedIndex());

  EXPECT_EQ(kExpressionNodeIndexForDoubleZero, ExpressionNodeIndex::DoubleZero().UnitTestRawCompactifiedIndex());
  EXPECT_EQ(kExpressionNodeIndexForDoubleOne, ExpressionNodeIndex::DoubleOne().UnitTestRawCompactifiedIndex());
}

// See `DoublesUpTo1ePositive77AreRegular` and `DoublesUpTo1eNegative76AreRegular` in `../encoded_double/test.cc`.
TEST(OptimizationExpression, DoubleValuesMustBeRegular) {
  using namespace current::expression;
  value_t const close_to_the_boundary = 1e76;

  close_to_the_boundary * 10;
  close_to_the_boundary*(-10);
  try {
    close_to_the_boundary * 100;
    ASSERT_TRUE(false);
  } catch (DoubleValueNotRegularException const& e) {
    EXPECT_EQ(
        "1000000000000000008493621433689702976148869924598760615894999102702796905906176.000000, "
        "+0x1.145b7e285bf99p+259, 0x502145b7e285bf99",
        e.OriginalDescription());
  }
  try {
    close_to_the_boundary*(-100);
    ASSERT_TRUE(false);
  } catch (DoubleValueNotRegularException const& e) {
    EXPECT_EQ(
        "-1000000000000000008493621433689702976148869924598760615894999102702796905906176.000000, "
        "-0x1.145b7e285bf99p+259, 0xd02145b7e285bf99",
        e.OriginalDescription());
  }

  (1.0 / close_to_the_boundary);
  (-1.0 / close_to_the_boundary);
  try {
    (1.0 / close_to_the_boundary) / 10;
    ASSERT_TRUE(false);
  } catch (DoubleValueNotRegularException const& e) {
    EXPECT_EQ("0.000000, +0x1.286d80ec190dcp-256, 0x2ff286d80ec190dc", e.OriginalDescription());
  }
  try {
    (-1.0 / close_to_the_boundary) / 10;
    ASSERT_TRUE(false);
  } catch (DoubleValueNotRegularException const& e) {
    EXPECT_EQ("-0.000000, -0x1.286d80ec190dcp-256, 0xaff286d80ec190dc", e.OriginalDescription());
  }
}
