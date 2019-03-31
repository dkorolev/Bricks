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

// TODO(dkorolev): Access to `x[]` from some `object.x[]`, w/o the thread-local singleton.
// TODO(dkorolev): Once the expression builder is here, test uninitialized variables.
// TODO(dkorolev): Possibly refactor freeze/unfreeze to be scope-based, when introducing JIT.

#include "vars.h"

#include "../../3rdparty/gtest/gtest-main.h"
#include "../../3rdparty/gtest/singlequoted.h"

#define CHECK_VAR_NAME(var) EXPECT_EQ(#var, (var).FullVarName())
#define CHECK_VAR_NAME_AND_INDEX(var, idx) \
  {                                        \
    EXPECT_EQ(#var, (var).FullVarName());  \
    EXPECT_EQ(idx, (var).VarIndex());      \
  }

TEST(OptimizationVars, SparseByInt) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  x[1] = 2;
  x[100] = 101;
  x[42] = 0;
  EXPECT_EQ(0u, x[1].VarIndex());
  EXPECT_EQ(1u, x[100].VarIndex());
  EXPECT_EQ(2u, x[42].VarIndex());
  CHECK_VAR_NAME_AND_INDEX(x[1], 0u);
  CHECK_VAR_NAME_AND_INDEX(x[100], 1u);
  CHECK_VAR_NAME_AND_INDEX(x[42], 2u);
  // The elements in the JSON are ordered, and the `q` index is in the order of introduction of the leaves in this tree.
  // Here and in all the tests below.
  EXPECT_EQ("{'I':{'z':[[1,{'X':{'i':0,'x':2.0}}],[42,{'X':{'i':2,'x':0.0}}],[100,{'X':{'i':1,'x':101.0}}]]}}",
            SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
  ASSERT_THROW(x.DenseDoubleVector(100), VarNodeTypeMismatchException);
  ASSERT_THROW(x["foo"], VarNodeTypeMismatchException);
  ASSERT_THROW(x[1][2], VarNodeTypeMismatchException);
  ASSERT_THROW(x[1]["blah"], VarNodeTypeMismatchException);
  ASSERT_THROW(x[1].DenseDoubleVector(100), VarNodeTypeMismatchException);
  EXPECT_EQ(
      "{'I':{'z':["
      "[1,{'X':{'i':0,'x':2.0}}],"
      "[42,{'X':{'i':2,'x':0.0}}],"
      "[100,{'X':{'i':1,'x':101.0}}]"
      "]}}",
      SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
}

TEST(OptimizationVars, SparseByString) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  x["foo"] = 1;
  x["bar"] = 2;
  x["baz"] = 3;
  CHECK_VAR_NAME(x["foo"]);
  CHECK_VAR_NAME(x["bar"]);
  CHECK_VAR_NAME(x["baz"]);
  EXPECT_EQ("{'S':{'z':{'bar':{'X':{'i':1,'x':2.0}},'baz':{'X':{'i':2,'x':3.0}},'foo':{'X':{'i':0,'x':1.0}}}}}",
            SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
  ASSERT_THROW(x.DenseDoubleVector(100), VarNodeTypeMismatchException);
  ASSERT_THROW(x[42], VarNodeTypeMismatchException);
  ASSERT_THROW(x["foo"][2], VarNodeTypeMismatchException);
  ASSERT_THROW(x["foo"]["blah"], VarNodeTypeMismatchException);
  ASSERT_THROW(x["foo"].DenseDoubleVector(100), VarNodeTypeMismatchException);
  EXPECT_EQ(
      "{'S':{'z':{"
      "'bar':{'X':{'i':1,'x':2.0}},"
      "'baz':{'X':{'i':2,'x':3.0}},"
      "'foo':{'X':{'i':0,'x':1.0}}"
      "}}}",
      SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
}

TEST(OptimizationVars, EmptyStringAllowedAsVarName) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  x["ok"] = 1;
  x[""] = 2;
  x["nested"]["also ok"] = 3;
  x["nested"][""] = 4;
  CHECK_VAR_NAME(x["ok"]);
  CHECK_VAR_NAME(x[""]);
  CHECK_VAR_NAME(x["nested"]["ok"]);
  CHECK_VAR_NAME(x["nested"][""]);
}

TEST(OptimizationVars, DenseVector) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  x.DenseDoubleVector(5);
  x[2] = 2;
  x[4] = 4;
  CHECK_VAR_NAME(x[2]);
  CHECK_VAR_NAME(x[4]);
  EXPECT_EQ("{'V':{'z':[{'U':{}},{'U':{}},{'X':{'i':0,'x':2.0}},{'U':{}},{'X':{'i':1,'x':4.0}}]}}",
            SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
  ASSERT_THROW(x[42], VarsManagementException);
  ASSERT_THROW(x["foo"], VarNodeTypeMismatchException);
  x.DenseDoubleVector(5);  // Same size, a valid no-op.
  ASSERT_THROW(x.DenseDoubleVector(100), VarNodeTypeMismatchException);
  x[2] = 2;  // Same value, a valid no-op.
  ASSERT_THROW(x[2] = 3, VarNodeReassignmentAttemptException);
  EXPECT_EQ("{'V':{'z':[{'U':{}},{'U':{}},{'X':{'i':0,'x':2.0}},{'U':{}},{'X':{'i':1,'x':4.0}}]}}",
            SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
}

TEST(OptimizationVars, InternalVarIndexes) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  x["foo"][1] = 2;
  CHECK_VAR_NAME(x["foo"][1]);
  // Should keep track of allocated internal leaf indexes.
  EXPECT_EQ(0u, x["foo"][1].VarIndex());
  // These are valid "var paths", but with no leaves allocated (they can still be nodes).
  ASSERT_THROW(x["foo"].VarIndex(), VarIsNotLeafException);
  ASSERT_THROW(x["foo"][0].VarIndex(), VarIsNotLeafException);
  // And for the invalid paths the other exception type is thrown.
  ASSERT_THROW(x["foo"]["bar"].VarIndex(), VarNodeTypeMismatchException);
  ASSERT_THROW(x[0].VarIndex(), VarNodeTypeMismatchException);
}

TEST(OptimizationVars, VarsTreeFinalizedExceptions) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  x["dense"].DenseDoubleVector(2);
  x["sparse"][42] = 42;
  x["strings"]["foo"] = 1;
  CHECK_VAR_NAME(x["dense"][0]);
  CHECK_VAR_NAME(x["dense"][1]);
  CHECK_VAR_NAME(x["sparse"][42]);
  CHECK_VAR_NAME(x["strings"]["foo"]);
  x.GetConfig();
  x["dense"][0];
  x["dense"][1];
  x["sparse"][42];
  x["strings"]["foo"];
  ASSERT_THROW(x["dense"][2], NoNewVarsCanBeAddedException);
  ASSERT_THROW(x["sparse"][100], NoNewVarsCanBeAddedException);
  ASSERT_THROW(x["strings"]["bar"], NoNewVarsCanBeAddedException);
  ASSERT_THROW(x["foo"], NoNewVarsCanBeAddedException);
}

TEST(OptimizationVars, MultiDimensionalIntInt) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  x[1][2] = 3;
  x[4][5] = 6;
  EXPECT_EQ("{'I':{'z':[[1,{'I':{'z':[[2,{'X':{'i':0,'x':3.0}}]]}}],[4,{'I':{'z':[[5,{'X':{'i':1,'x':6.0}}]]}}]]}}",
            SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
  x.GetConfig();
  EXPECT_EQ(
      "{'I':{'z':["
      "[1,{'I':{'z':[[2,{'X':{'i':0,'x':3.0}}]]}}],"
      "[4,{'I':{'z':[[5,{'X':{'i':1,'x':6.0}}]]}}]"
      "]}}",
      SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
}

TEST(OptimizationVars, MultiDimensionalIntString) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  x[1]["foo"] = 2;
  x[3]["bar"] = 4;
  EXPECT_EQ("{'I':{'z':[[1,{'S':{'z':{'foo':{'X':{'i':0,'x':2.0}}}}}],[3,{'S':{'z':{'bar':{'X':{'i':1,'x':4.0}}}}}]]}}",
            SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
  x.GetConfig();
  EXPECT_EQ(
      "{'I':{'z':["
      "[1,{'S':{'z':{'foo':{'X':{'i':0,'x':2.0}}}}}],"
      "[3,{'S':{'z':{'bar':{'X':{'i':1,'x':4.0}}}}}]"
      "]}}",
      SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
}

TEST(OptimizationVars, MultiDimensionalStringInt) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  x["foo"][1] = 2;
  x["bar"][3] = 4;
  EXPECT_EQ("{'S':{'z':{'bar':{'I':{'z':[[3,{'X':{'i':1,'x':4.0}}]]}},'foo':{'I':{'z':[[1,{'X':{'i':0,'x':2.0}}]]}}}}}",
            SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
  x.GetConfig();
  EXPECT_EQ(
      "{'S':{'z':{"
      "'bar':{'I':{'z':[[3,{'X':{'i':1,'x':4.0}}]]}},"
      "'foo':{'I':{'z':[[1,{'X':{'i':0,'x':2.0}}]]}}"
      "}}}",
      SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
}

TEST(OptimizationVars, Constants) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  x["one"] = 1;
  x["two"] = 2;
  x["three"] = 3;
  EXPECT_EQ(
      "{'S':{'z':{"
      "'one':{'X':{'i':0,'x':1.0}},"
      "'three':{'X':{'i':2,'x':3.0}},"
      "'two':{'X':{'i':1,'x':2.0}}"
      "}}}",
      SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
  x["two"].SetConstant();
  x["three"].SetConstant(3.0);
  x["four"].SetConstant(4);
  ASSERT_THROW(x["one"].SetConstant(42), VarNodeReassignmentAttemptException);
  EXPECT_EQ(
      "{'S':{'z':{"
      "'four':{'X':{'i':3,'x':4.0,'c':true}},"
      "'one':{'X':{'i':0,'x':1.0}},"
      "'three':{'X':{'i':2,'x':3.0,'c':true}},"
      "'two':{'X':{'i':1,'x':2.0,'c':true}}"
      "}}}",
      SingleQuoted(JSON<JSONFormat::Minimalistic>(x.UnitTestDump())));
}

TEST(OptimizationVars, DenseRepresentation) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  x["x"]["x1"] = 101;  // Add the values in an arbitrary order to test they get sorted before being flattened.
  x["x"]["x3"] = 103;
  x["x"]["x2"] = 102;
  x["y"][0][0] = 200;
  x["y"][1][1] = 211;
  x["y"][0][1] = 201;
  x["y"][1][0] = 210;
  x["x"]["x2"].SetConstant();  // Make two of them constants for the test.
  x["y"][1][0].SetConstant();
  CHECK_VAR_NAME(x["x"]["x1"]);
  CHECK_VAR_NAME(x["x"]["x2"]);
  CHECK_VAR_NAME(x["x"]["x3"]);
  CHECK_VAR_NAME(x["y"][0][0]);
  CHECK_VAR_NAME(x["y"][0][1]);
  CHECK_VAR_NAME(x["y"][1][0]);
  CHECK_VAR_NAME(x["y"][1][1]);
  Vars::Config const config = x.GetConfig();
  ASSERT_EQ(7u, config.NumberOfVars());
  // The indexes on the right are flipped.
  CHECK_VAR_NAME_AND_INDEX(x["x"]["x1"], 0u);
  CHECK_VAR_NAME_AND_INDEX(x["x"]["x2"], 2u);
  CHECK_VAR_NAME_AND_INDEX(x["x"]["x3"], 1u);
  CHECK_VAR_NAME_AND_INDEX(x["y"][0][0], 3u);
  CHECK_VAR_NAME_AND_INDEX(x["y"][0][1], 5u);
  CHECK_VAR_NAME_AND_INDEX(x["y"][1][0], 6u);
  CHECK_VAR_NAME_AND_INDEX(x["y"][1][1], 4u);
  // The order is the same as in which the variables were introduced.
  EXPECT_EQ("x['x']['x1']", SingleQuoted(config[0]));
  EXPECT_EQ("x['x']['x3']", SingleQuoted(config[1]));
  EXPECT_EQ("x['x']['x2']", SingleQuoted(config[2]));
  EXPECT_EQ("x['y'][0][0]", SingleQuoted(config[3]));
  EXPECT_EQ("x['y'][1][1]", SingleQuoted(config[4]));
  EXPECT_EQ("x['y'][0][1]", SingleQuoted(config[5]));
  EXPECT_EQ("x['y'][1][0]", SingleQuoted(config[6]));
  // The order of values is the order in which the variables were introduced.
  EXPECT_EQ("[101.0,103.0,102.0,200.0,211.0,201.0,210.0]", JSON(config.StartingPoint()));
  EXPECT_EQ("[false,false,true,false,false,false,true]", JSON(config.VarIsConstant()));

  {
    Vars a(config);
    Vars b(config);  // To confirm no global or thread-local context is used by `Vars`-s.
    Vars c;          // The default config from the thread-local singleton would be used regardless.

    EXPECT_EQ(JSON(a.x), JSON(config.StartingPoint()));
    EXPECT_EQ(JSON(b.x), JSON(config.StartingPoint()));
    EXPECT_EQ(JSON(c.x), JSON(config.StartingPoint()));

    EXPECT_EQ(101, a.x[0]);
    EXPECT_EQ(102, a.x[2]);
    EXPECT_EQ(211, a.x[4]);
    EXPECT_EQ(101, b.x[0]);
    EXPECT_EQ(102, b.x[2]);
    EXPECT_EQ(211, b.x[4]);

    a["x"]["x1"] = 70101;
    a["x"]["x2"].SetConstantValue(70102);
    a["y"][1][1] = 70211;

    b["x"]["x1"] = 80101;
    b["y"][1][1].Ref() = 80211;
    b["x"]["x2"].RefEvenForAConstant() = 80102;

    EXPECT_EQ(70101, a.x[0]);
    EXPECT_EQ(70102, a.x[2]);
    EXPECT_EQ(70211, a.x[4]);

    EXPECT_EQ(80101, b.x[0]);
    EXPECT_EQ(80102, b.x[2]);
    EXPECT_EQ(80211, b.x[4]);

    EXPECT_EQ(101, c.x[0]);
    EXPECT_EQ(102, c.x[2]);
    EXPECT_EQ(211, c.x[4]);

    ASSERT_THROW(a[42] = 0, VarsMapperWrongVarException);
    ASSERT_THROW(a["z"] = 0, VarsMapperWrongVarException);
    ASSERT_THROW(a["x"][42] = 0, VarsMapperWrongVarException);
    ASSERT_THROW(a["x"]["x4"] = 0, VarsMapperWrongVarException);
    ASSERT_THROW(a["x"]["x1"]["foo"] = 0, VarsMapperWrongVarException);

    ASSERT_THROW(a["y"] = 0, VarsMapperNodeNotVarException);

    ASSERT_THROW(a["x"]["x2"].Ref(), VarsMapperVarIsConstant);
    ASSERT_THROW(a["x"]["x2"] = 0, VarsMapperVarIsConstant);
  }
}

TEST(OptimizationVars, DenseVectorDimensions) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  ASSERT_THROW(x.DenseDoubleVector(0), VarsManagementException);
  ASSERT_THROW(x.DenseDoubleVector(static_cast<size_t>(1e6) + 1), VarsManagementException);
}

TEST(OptimizationVars, NeedContext) {
  using namespace current::expression;
  ASSERT_THROW(x["should fail"], VarsManagementException);
  ASSERT_THROW(x[42], VarsManagementException);
  ASSERT_THROW(x.DenseDoubleVector(1), VarsManagementException);
}

TEST(OptimizationVars, NoNestedContextsAllowed) {
  using namespace current::expression;
  Vars::ThreadLocalContext context;
  ASSERT_THROW(Vars::ThreadLocalContext illegal_inner_context, VarsManagementException);
}

#undef CHECK_VAR_NAME_AND_INDEX
#undef CHECK_VAR_NAME
