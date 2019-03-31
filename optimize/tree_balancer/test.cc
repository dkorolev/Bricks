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

#include "tree_balancer.h"

#include "../../3rdparty/gtest/gtest-main.h"

// Just a canonical, golden implementation for the unit test.
// Will fail on very deep trees because it is, well, recursive.
namespace current {
namespace expression {
inline size_t UnitTestRecursiveExpressionNodeIndexTreeHeight(
    ExpressionNodeIndex index, Vars::ThreadLocalContext const& vars_context = InternalTLS()) {
  return index.template CheckedDispatch<size_t>(
      [&vars_context](size_t node_index) -> size_t {
        ExpressionNodeImpl const& node = vars_context[node_index];
        ExpressionNodeType const node_type = node.Type();
        if (IsOperationNode(node_type)) {
          return 1u + std::max(UnitTestRecursiveExpressionNodeIndexTreeHeight(node.LHSIndex(), vars_context),
                               UnitTestRecursiveExpressionNodeIndexTreeHeight(node.RHSIndex(), vars_context));
        } else if (IsFunctionNode(node_type)) {
          return 1u + UnitTestRecursiveExpressionNodeIndexTreeHeight(node.ArgumentIndex(), vars_context);
        } else {
#ifndef NDEBUG
          TriggerSegmentationFault();
          throw false;
#else
          return static_cast<size_t>(1e30);
#endif
        }
      },
      [](size_t) -> size_t { return 1u; },
      [](double) -> size_t { return 1u; },
      []() -> size_t { return 1u; });
}
inline size_t UnitTestRecursiveExpressionTreeHeight(value_t value,
                                                    Vars::ThreadLocalContext const& vars_context = InternalTLS()) {
  return UnitTestRecursiveExpressionNodeIndexTreeHeight(value.GetExpressionNodeIndex(), vars_context);
}
}  // namespace current::expression
}  // namespace current

TEST(OptimizationExpressionTreeBalancer, Smoke) {
  using namespace current::expression;

  Vars::ThreadLocalContext vars_context;
#ifdef NDEBUG
  // Make sure the vars are numbered from `x[1]`, not `x[0]`, in the return values of `.DebugAsString()`.
  x[0u] = 0.0;
#endif

  value_t v = 0.0;
  for (size_t i = 0; i < 4; ++i) {
    x[i + 1] = 0.0;
    v += x[i + 1];
  }

  EXPECT_EQ(4u, ExpressionTreeHeight(v));
  EXPECT_EQ(4u, UnitTestRecursiveExpressionTreeHeight(v));
  EXPECT_EQ("(((x[1]+x[2])+x[3])+x[4])", v.DebugAsString());

  BalanceExpressionTree(v);
  EXPECT_EQ(3u, ExpressionTreeHeight(v));
  EXPECT_EQ(3u, UnitTestRecursiveExpressionTreeHeight(v));
  EXPECT_EQ("((x[1]+x[2])+(x[3]+x[4]))", v.DebugAsString());
}

TEST(OptimizationExpressionTreeBalancer, MixingAdditionAndMultiplication) {
  using namespace current::expression;

  Vars::ThreadLocalContext vars_context;
#ifdef NDEBUG
  // Make sure the vars are numbered from `x[1]`, not `x[0]`, in the return values of `.DebugAsString()`.
  x[0u] = 0.0;
#endif

  for (size_t i = 0; i < 9; ++i) {
    x[i + 1] = 0.0;
  }

  value_t const v = (x[1] + x[2]) * (x[3] * (x[4] * (x[5] * (x[6] + x[7] + x[8] + x[9]))));

  EXPECT_EQ(8u, ExpressionTreeHeight(v));
  EXPECT_EQ(8u, UnitTestRecursiveExpressionTreeHeight(v));
  EXPECT_EQ("((x[1]+x[2])*(x[3]*(x[4]*(x[5]*(((x[6]+x[7])+x[8])+x[9])))))", v.DebugAsString());
  BalanceExpressionTree(v);
  EXPECT_EQ(5u, ExpressionTreeHeight(v));
  EXPECT_EQ(5u, UnitTestRecursiveExpressionTreeHeight(v));
  EXPECT_EQ("((((x[1]+x[2])*x[3])*x[4])*(x[5]*((x[6]+x[7])+(x[8]+x[9]))))", v.DebugAsString());
  BalanceExpressionTree(v);
  EXPECT_EQ(5u, ExpressionTreeHeight(v));
  EXPECT_EQ(5u, UnitTestRecursiveExpressionTreeHeight(v));
  EXPECT_EQ("((((x[1]+x[2])*x[3])*x[4])*(x[5]*((x[6]+x[7])+(x[8]+x[9]))))", v.DebugAsString());
}

TEST(OptimizationExpressionTreeBalancer, RebalanceWhatWasAddedLater) {
  using namespace current::expression;

  Vars::ThreadLocalContext vars_context;
#ifdef NDEBUG
  // Make sure the vars are numbered from `x[1]`, not `x[0]`, in the return values of `.DebugAsString()`.
  x[0u] = 0.0;
#endif

  for (size_t i = 0; i < 8; ++i) {
    x[i + 1] = 0.0;
  }

  value_t v = 0.0;
  for (size_t i = 1; i <= 4; ++i) {
    v += x[i];
  }

  EXPECT_EQ(4u, ExpressionTreeHeight(v));
  EXPECT_EQ(4u, UnitTestRecursiveExpressionTreeHeight(v));
  EXPECT_EQ("(((x[1]+x[2])+x[3])+x[4])", v.DebugAsString());

  BalanceExpressionTree(v);
  EXPECT_EQ(3u, ExpressionTreeHeight(v));
  EXPECT_EQ(3u, UnitTestRecursiveExpressionTreeHeight(v));
  EXPECT_EQ("((x[1]+x[2])+(x[3]+x[4]))", v.DebugAsString());

  for (size_t i = 5; i <= 8; ++i) {
    v = x[i] + v;
  }

  EXPECT_EQ(7u, ExpressionTreeHeight(v));
  EXPECT_EQ(7u, UnitTestRecursiveExpressionTreeHeight(v));
  EXPECT_EQ("(x[8]+(x[7]+(x[6]+(x[5]+((x[1]+x[2])+(x[3]+x[4]))))))", v.DebugAsString());

  BalanceExpressionTree(v);
  EXPECT_EQ(4u, ExpressionTreeHeight(v));
  EXPECT_EQ(4u, UnitTestRecursiveExpressionTreeHeight(v));
  EXPECT_EQ("(((x[8]+x[7])+(x[6]+x[5]))+((x[1]+x[2])+(x[3]+x[4])))", v.DebugAsString());
}

TEST(OptimizationExpressionTreeBalancer, BalancedStaysBalanced) {
  using namespace current::expression;

  Vars::ThreadLocalContext vars_context;
#ifdef NDEBUG
  // Make sure the vars are numbered from `x[1]`, not `x[0]`, in the return values of `.DebugAsString()`.
  x[0u] = 0.0;
#endif

  for (size_t i = 0; i < 7; ++i) {
    x[i + 1] = 0.0;
  }

  {
    value_t const v3_left = (x[1] + x[2]) + x[3];
    EXPECT_EQ(3u, ExpressionTreeHeight(v3_left));
    EXPECT_EQ("((x[1]+x[2])+x[3])", v3_left.DebugAsString());
    BalanceExpressionTree(v3_left);
    EXPECT_EQ(3u, ExpressionTreeHeight(v3_left));
    EXPECT_EQ("((x[1]+x[2])+x[3])", v3_left.DebugAsString());
  }

  {
    value_t const v3_right = x[1] * (x[2] * x[3]);
    EXPECT_EQ(3u, ExpressionTreeHeight(v3_right));
    EXPECT_EQ("(x[1]*(x[2]*x[3]))", v3_right.DebugAsString());
    BalanceExpressionTree(v3_right);
    EXPECT_EQ(3u, ExpressionTreeHeight(v3_right));
    EXPECT_EQ("(x[1]*(x[2]*x[3]))", v3_right.DebugAsString());
  }

  {
    // clang-format off
    value_t const v7_unconventionally_balanced = (((x[1]+x[2])+x[3])+((x[4]+x[5])+(x[6]+x[7])));
    std::string const its_text_representation = "(((x[1]+x[2])+x[3])+((x[4]+x[5])+(x[6]+x[7])))";
    // clang-format on
    EXPECT_EQ(4u, ExpressionTreeHeight(v7_unconventionally_balanced));
    EXPECT_EQ(its_text_representation, v7_unconventionally_balanced.DebugAsString());
    BalanceExpressionTree(v7_unconventionally_balanced);
    EXPECT_EQ(its_text_representation, v7_unconventionally_balanced.DebugAsString());
  }

  {
    value_t const v7_unbalanced = (((x[1] * x[2]) * x[3]) * x[4]) * (x[5] * (x[6] * x[7]));
    EXPECT_EQ(5u, ExpressionTreeHeight(v7_unbalanced));
    std::string const unbalanced_original_representation = v7_unbalanced.DebugAsString();
    EXPECT_EQ("((((x[1]*x[2])*x[3])*x[4])*(x[5]*(x[6]*x[7])))", unbalanced_original_representation);
    BalanceExpressionTree(v7_unbalanced);
    EXPECT_EQ(4u, ExpressionTreeHeight(v7_unbalanced));
    std::string const balanced_conventional_representation = v7_unbalanced.DebugAsString();
    EXPECT_EQ("(((x[1]*x[2])*(x[3]*x[4]))*((x[5]*x[6])*x[7]))", balanced_conventional_representation);
    EXPECT_NE(unbalanced_original_representation, balanced_conventional_representation);
    EXPECT_EQ(balanced_conventional_representation, v7_unbalanced.DebugAsString());
    BalanceExpressionTree(v7_unbalanced);
    EXPECT_EQ(balanced_conventional_representation, v7_unbalanced.DebugAsString());
  }
}

#define SMOKE_LO(count, perfect_height, s_before, s_after)                                    \
  TEST(OptimizationExpressionTreeBalancer, LowNodesCountWithRecursiveSanityCheck##count) {    \
    using namespace current::expression;                                                      \
    Vars::ThreadLocalContext vars_context;                                                    \
    x[0u] = 0.0;                                                                              \
    value_t v = 0.0;                                                                          \
    for (size_t i = 0u; i < static_cast<size_t>(count); ++i) {                                \
      x[i + 1u] = 0.0;                                                                        \
      v += x[i + 1u];                                                                         \
    }                                                                                         \
    EXPECT_EQ(static_cast<size_t>(count), ExpressionTreeHeight(v));                           \
    EXPECT_EQ(static_cast<size_t>(count), UnitTestRecursiveExpressionTreeHeight(v));          \
    value_t const v_before = v;                                                               \
    EXPECT_EQ(s_before, v_before.DebugAsString());                                            \
    BalanceExpressionTree(v);                                                                 \
    EXPECT_EQ(static_cast<size_t>(perfect_height), ExpressionTreeHeight(v));                  \
    EXPECT_EQ(static_cast<size_t>(perfect_height), UnitTestRecursiveExpressionTreeHeight(v)); \
    value_t const v_after = v;                                                                \
    EXPECT_EQ(s_after, v_after.DebugAsString());                                              \
  }

#define SMOKE_HI(count, perfect_height)                                                                              \
  TEST(OptimizationExpressionTreeBalancer, HighNodesCountWithNoRecursion##count) {                                   \
    using namespace current::expression;                                                                             \
    Vars::ThreadLocalContext vars_context;                                                                           \
    x[0u] = 0.0;                                                                                                     \
    value_t v = 0.0;                                                                                                 \
    for (size_t i = 0u; i < static_cast<size_t>(count); ++i) {                                                       \
      x[i + 1u] = 0.0;                                                                                               \
      v += x[i + 1u];                                                                                                \
    }                                                                                                                \
    EXPECT_EQ(static_cast<size_t>(count), ExpressionTreeHeight(v));                                                  \
    BalanceExpressionTree(v);                                                                                        \
    EXPECT_EQ(static_cast<size_t>(perfect_height), ExpressionTreeHeight(v));                                         \
    EXPECT_EQ(static_cast<size_t>(perfect_height), static_cast<size_t>(1.0 + ceil((log(count) / log(2.0)) - 1e-9))); \
  }

SMOKE_LO(2, 2, "(x[1]+x[2])", "(x[1]+x[2])");

SMOKE_LO(3, 3, "((x[1]+x[2])+x[3])", "((x[1]+x[2])+x[3])");
SMOKE_LO(4, 3, "(((x[1]+x[2])+x[3])+x[4])", "((x[1]+x[2])+(x[3]+x[4]))");
SMOKE_LO(5, 4, "((((x[1]+x[2])+x[3])+x[4])+x[5])", "(((x[1]+x[2])+x[3])+(x[4]+x[5]))");
SMOKE_LO(6, 4, "(((((x[1]+x[2])+x[3])+x[4])+x[5])+x[6])", "(((x[1]+x[2])+x[3])+((x[4]+x[5])+x[6]))");
SMOKE_LO(7, 4, "((((((x[1]+x[2])+x[3])+x[4])+x[5])+x[6])+x[7])", "(((x[1]+x[2])+(x[3]+x[4]))+((x[5]+x[6])+x[7]))");
SMOKE_LO(8,
         4,
         "(((((((x[1]+x[2])+x[3])+x[4])+x[5])+x[6])+x[7])+x[8])",
         "(((x[1]+x[2])+(x[3]+x[4]))+((x[5]+x[6])+(x[7]+x[8])))");

SMOKE_LO(9,
         5,
         "((((((((x[1]+x[2])+x[3])+x[4])+x[5])+x[6])+x[7])+x[8])+x[9])",
         "((((x[1]+x[2])+x[3])+(x[4]+x[5]))+((x[6]+x[7])+(x[8]+x[9])))");
SMOKE_LO(12,
         5,
         "(((((((((((x[1]+x[2])+x[3])+x[4])+x[5])+x[6])+x[7])+x[8])+x[9])+x[10])+x[11])+x[12])",
         "((((x[1]+x[2])+x[3])+((x[4]+x[5])+x[6]))+(((x[7]+x[8])+x[9])+((x[10]+x[11])+x[12])))");

// clang-format off
SMOKE_LO(
          16,
          5,
          "(((((((((((((((x[1]+x[2])+x[3])+x[4])+x[5])+x[6])+x[7])+x[8])+x[9])+x[10])+x[11])+x[12])+x[13])+x[14])+x[15])+x[16])",
          "((((x[1]+x[2])+(x[3]+x[4]))+((x[5]+x[6])+(x[7]+x[8])))+(((x[9]+x[10])+(x[11]+x[12]))+((x[13]+x[14])+(x[15]+x[16]))))");

SMOKE_LO(
          17,
          6,
          "((((((((((((((((x[1]+x[2])+x[3])+x[4])+x[5])+x[6])+x[7])+x[8])+x[9])+x[10])+x[11])+x[12])+x[13])+x[14])+x[15])+x[16])+x[17])",
          "(((((x[1]+x[2])+x[3])+(x[4]+x[5]))+((x[6]+x[7])+(x[8]+x[9])))+(((x[10]+x[11])+(x[12]+x[13]))+((x[14]+x[15])+(x[16]+x[17]))))");
// clang-format on

SMOKE_HI(16, 5);
SMOKE_HI(17, 6);

SMOKE_HI(127, 8);
SMOKE_HI(128, 8);
SMOKE_HI(129, 9);
SMOKE_HI(130, 9);

SMOKE_HI(511, 10);
SMOKE_HI(512, 10);
SMOKE_HI(513, 11);
SMOKE_HI(514, 11);

SMOKE_HI(1024, 11);
SMOKE_HI(1025, 12);

SMOKE_HI(4096, 13);
SMOKE_HI(4097, 14);

// The tests below would overflow the stack if a pure recursive implementation is used. -- D.K.
SMOKE_HI(16384, 15);
SMOKE_HI(16385, 16);
SMOKE_HI(65536, 17);
SMOKE_HI(65537, 18);
SMOKE_HI(262144, 19);
SMOKE_HI(262145, 20);

#undef SMOKE_LO
#undef SMOKE_HI
