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

TEST(OptimizationExpression, SimpleVarsAddition) {
  using namespace current::expression;

  VarsContext vars_context;

  x[0] = 0.0;
  x[1] = 0.0;

  value_t const v0(x[0]);
  value_t const v1(x[1]);

  EXPECT_EQ("x[@0]", v0.DebugAsString());
  EXPECT_EQ("x[@1]", v1.DebugAsString());

  value_t const v_sum = v0 + v1;
  EXPECT_EQ("(x[@0]+x[@1])", v_sum.DebugAsString());

  EXPECT_EQ("(x[@0]+x[@1])", (v0 + v1).DebugAsString());
  EXPECT_EQ("(x[@0]+x[@1])", (v0 + x[1]).DebugAsString());
  EXPECT_EQ("(x[@0]+x[@1])", (x[0] + v1).DebugAsString());
  EXPECT_EQ("(x[@0]+x[@1])", (x[0] + x[1]).DebugAsString());
}

TEST(OptimizationExpression, AddingImmediatesToVars) {
  using namespace current::expression;

  VarsContext vars_context;

  x["test"] = 0.0;
  value_t const v(x["test"]);
  value_t const v_sum_1 = v + 1.0;
  value_t const v_sum_2 = 1.0 + v;

  EXPECT_EQ("x[@0]", v.DebugAsString());

  EXPECT_EQ("(x[@0]+1.000000)", v_sum_1.DebugAsString());
  EXPECT_EQ("(1.000000+x[@0])", v_sum_2.DebugAsString());

  EXPECT_EQ("(x[@0]+1.000000)", (v + 1.0).DebugAsString());
  EXPECT_EQ("(1.000000+x[@0])", (1.0 + v).DebugAsString());

  EXPECT_EQ("(x[@0]+1.000000)", (x["test"] + 1.0).DebugAsString());
  EXPECT_EQ("(1.000000+x[@0])", (1.0 + x["test"]).DebugAsString());
}

TEST(OptimizationExpression, NestedVarsAddition) {
  using namespace current::expression;

  VarsContext vars_context;

  x["foo"] = 0;
  x["bar"] = 0;
  x["baz"] = 0;

  EXPECT_EQ("(x[@0]+x[@1])", (x["foo"] + x["bar"]).DebugAsString());

  EXPECT_EQ("((x[@0]+x[@1])+x[@2])", (x["foo"] + x["bar"] + x["baz"]).DebugAsString());
  EXPECT_EQ("((x[@0]+x[@1])+x[@2])", ((x["foo"] + x["bar"]) + x["baz"]).DebugAsString());
  EXPECT_EQ("(x[@0]+(x[@1]+x[@2]))", (x["foo"] + (x["bar"] + x["baz"])).DebugAsString());
}

TEST(OptimizationExpression, OtherOperations) {
  using namespace current::expression;

  VarsContext vars_context;

  x["a"] = 0.0;
  x["b"] = 0.0;
  value_t const a(x["a"]);
  value_t const b(x["b"]);

  EXPECT_EQ("(x[@0]+x[@1])", (a + b).DebugAsString());
  EXPECT_EQ("(x[@0]-x[@1])", (a - b).DebugAsString());
  EXPECT_EQ("(x[@0]*x[@1])", (a * b).DebugAsString());
  EXPECT_EQ("(x[@0]/x[@1])", (a / b).DebugAsString());
}

TEST(OptimizationExpression, OperationsWithAssignment) {
  using namespace current::expression;

  VarsContext vars_context;

  x["a"] = 0.0;
  value_t const a(x["a"]);

  {
    value_t add = a;
    add += 1.0;
    EXPECT_EQ("(x[@0]+1.000000)", add.DebugAsString());

    value_t sub = a;
    sub -= 2.0;
    EXPECT_EQ("(x[@0]-2.000000)", sub.DebugAsString());

    value_t mul = a;
    mul *= 3.0;
    EXPECT_EQ("(x[@0]*3.000000)", mul.DebugAsString());

    value_t div = a;
    div /= 4.0;
    EXPECT_EQ("(x[@0]/4.000000)", div.DebugAsString());
  }

  x["b"] = 0.0;
  value_t const b(x["b"]);

  {
    value_t add = a;
    add += b;
    EXPECT_EQ("(x[@0]+x[@1])", add.DebugAsString());

    value_t sub = a;
    sub -= b;
    EXPECT_EQ("(x[@0]-x[@1])", sub.DebugAsString());

    value_t mul = a;
    mul *= b;
    EXPECT_EQ("(x[@0]*x[@1])", mul.DebugAsString());

    value_t div = a;
    div /= b;
    EXPECT_EQ("(x[@0]/x[@1])", div.DebugAsString());
  }
}

TEST(OptimizationExpression, SimpleVarsExponentiation) {
  using namespace current::expression;

  VarsContext vars_context;

  x["foo"] = 0.0;

  value_t const v(x["foo"]);
  EXPECT_EQ("x[@0]", v.DebugAsString());

  value_t const v_exp = exp(v);
  EXPECT_EQ("exp(x[@0])", v_exp.DebugAsString());

  EXPECT_EQ("exp(x[@0])", exp(v).DebugAsString());
  EXPECT_EQ("exp(x[@0])", exp(x["foo"]).DebugAsString());

  // NOTE(dkorolev): The below `EXPECT_EQ` would work just fine w/o "pre-declaring" the "variables", but I'd like
  // to make sure the order of the "variables" is carved in stone to avoid any and all room for the UB.
  x["bar"].SetConstant(0.0);
  x["baz"].SetConstant(0.0);
  x["blah"].SetConstant(0.0);
  EXPECT_EQ("((exp(((x[@1]+1.000000)+x[@2]))+2.500000)+exp(exp(x[@3])))",
            (exp(x["bar"] + 1.0 + x["baz"]) + 2.5 + exp(exp(x["blah"]))).DebugAsString());
}

TEST(OptimizationExpression, OtherFunctions) {
  using namespace current::expression;

  VarsContext vars_context;

  x["p"] = 0.0;
  value_t const p(x["p"]);

  EXPECT_EQ("exp(x[@0])", (exp(p)).DebugAsString());
  EXPECT_EQ("log(x[@0])", (log(p)).DebugAsString());
  EXPECT_EQ("sin(x[@0])", (sin(p)).DebugAsString());
  EXPECT_EQ("cos(x[@0])", (cos(p)).DebugAsString());
}

TEST(OptimizationExpression, IndexesAreNonFinalizedIndexes) {
  using namespace current::expression;

  {
    VarsContext vars_context;
    x[0] = 100;
    x[1] = 101;
    value_t const v0(x[0]);
    value_t const v1(x[1]);
    EXPECT_EQ("x[@0]", v0.DebugAsString());
    EXPECT_EQ("x[@1]", v1.DebugAsString());
  }

  {
    VarsContext vars_context;
    // With the order of introduction of `x[1]` and `x[0]` flipped, `DebugAsString()`-s return different results.
    x[1] = 201;
    x[0] = 200;
    value_t const v0(x[0]);
    value_t const v1(x[1]);
    EXPECT_EQ("x[@1]", v0.DebugAsString());
    EXPECT_EQ("x[@0]", v1.DebugAsString());
  }
}

TEST(OptimizationExpression, MustBeWithinContext) {
  using namespace current::expression;
  std::vector<value_t> const v = []() {
    VarsContext vars_context;
    x[0] = 0.0;
    std::vector<value_t> values;
    values.push_back(x[0]);
    values.push_back(x[0] + x[0]);
    values.push_back(exp(x[0]));
    return values;
  }();
  // `v[0]` is just a reference to a variable at certain leaf creation index. It outlives the context.
  EXPECT_EQ("x[@0]", v[0].DebugAsString());
  // Other `v[]`-s require valid context, as they refer to the expression nodes created in the thread-local singleton.
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
  EXPECT_EQ(1u, static_cast<size_t>(ExpressionNodeIndex(tmp1)));
  EXPECT_EQ(3u, static_cast<size_t>(ExpressionNodeIndex(tmp2)));
  EXPECT_EQ(5u, static_cast<size_t>(ExpressionNodeIndex(tmp3)));

  VarsMapperConfig const config = vars_context.Freeze();
  EXPECT_EQ(1u, config.total_leaves);  // Just one variable, `x[0]`.
  EXPECT_EQ(6u, config.total_nodes);   // Three immediate value and three `+` nodes.

  EXPECT_THROW(x[0] + 4.0, VarsManagementException);
}
