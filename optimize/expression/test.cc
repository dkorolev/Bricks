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

  EXPECT_TRUE(!ExpressionNodeIndex(v0).UnitTestIsNodeIndex());
  EXPECT_TRUE(!ExpressionNodeIndex(v2).UnitTestIsNodeIndex());
  EXPECT_EQ(0u, ExpressionNodeIndex(v0).UnitTestVarIndex());
  EXPECT_EQ(1u, ExpressionNodeIndex(v2).UnitTestVarIndex());

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

  EXPECT_TRUE(!ExpressionNodeIndex(v0).UnitTestIsNodeIndex());
  EXPECT_TRUE(!ExpressionNodeIndex(v1).UnitTestIsNodeIndex());
  EXPECT_TRUE(!ExpressionNodeIndex(v2).UnitTestIsNodeIndex());
  EXPECT_EQ(0u, ExpressionNodeIndex(v0).UnitTestVarIndex());
  EXPECT_EQ(1u, ExpressionNodeIndex(v2).UnitTestVarIndex());  // `v2`, which is `x[2]`, has an internal index `1`.
  EXPECT_EQ(2u, ExpressionNodeIndex(v1).UnitTestVarIndex());  // `v1`, which is `x[1]`, has an internal index `2`.

  vars_context.Freeze();

  EXPECT_EQ(0u, ExpressionNodeIndex(v0).UnitTestVarIndex());
  EXPECT_EQ(1u, ExpressionNodeIndex(v2).UnitTestVarIndex());
  EXPECT_EQ(2u, ExpressionNodeIndex(v1).UnitTestVarIndex());

  EXPECT_EQ("x[0]{0}", v0.DebugAsString());
  EXPECT_EQ("x[1]{1}", v1.DebugAsString());
  EXPECT_EQ("x[2]{2}", v2.DebugAsString());  // The allocated frozen index for `x[2]` has changed.
}

TEST(OptimizationExpression, MustBeWithinContext) {
  using namespace current::expression;
  std::vector<value_t> const v = []() {
    VarsContext vars_context;
    x[0] = 0.0;
    std::vector<value_t> values;
    values.push_back(x[0]);
    values.push_back(exp(x[0]));
    values.push_back(ExpressionNode::FromImmediateDouble(0.0));
    return values;
  }();
  ASSERT_THROW(v[0].DebugAsString(), VarsManagementException);
  ASSERT_THROW(v[1].DebugAsString(), VarsManagementException);
  ASSERT_THROW(v[2].DebugAsString(), VarsManagementException);
}

TEST(OptimizationExpression, FreezePreventsNodesCreation) {
  using namespace current::expression;

  VarsContext vars_context;
  x[0] = 0.0;

  // NOTE(dkorolev): Each of these assignments generates two expression nodes: one for the value, adn one for the `+`.
  value_t const tmp1 = x[0] + 1.0;
  value_t const tmp2 = x[0] + 2.0;
  value_t const tmp3 = x[0] + 3.0;
  EXPECT_EQ(1u, ExpressionNodeIndex(tmp1).UnitTestNodeIndex());
  EXPECT_EQ(3u, ExpressionNodeIndex(tmp2).UnitTestNodeIndex());
  EXPECT_EQ(5u, ExpressionNodeIndex(tmp3).UnitTestNodeIndex());

  VarsMapperConfig const config = vars_context.Freeze();
  EXPECT_EQ(1u, config.total_leaves);  // Just one variable, `x[0]`.
  EXPECT_EQ(6u, config.total_nodes);   // Three immediate value and three `+` nodes.

  EXPECT_THROW(x[0] + 4.0, VarsManagementException);
}

TEST(OptimizationExpression, Lambda) {
  using namespace current::expression;

  VarsContext vars_context;

  x[0] = 0.0;

  value_t const a = x[0];
  value_t const b = ExpressionNode::FromImmediateDouble(1.0);
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
