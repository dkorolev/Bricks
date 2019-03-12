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
