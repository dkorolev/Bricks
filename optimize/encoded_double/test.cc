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

#include "../../3rdparty/gtest/gtest-main.h"

#include "../../bricks/strings/strings.h"

#include "../double.h"

inline void RunDoubleRepresentationTest(uint64_t uint64_value_hex,
                                        uint64_t uint64_value_bin,
                                        double double_value_approximate,
                                        bool compactifiable,
                                        double double_value_computed,
                                        char const* value_source) {
  // First, test that the very number is the same in different representations match.
  // This effectively validates that the system is following the IEEE 754 double-precision binary floating point format.
  EXPECT_EQ(uint64_value_hex, uint64_value_bin) << value_source;

  double const double_value_precise = *reinterpret_cast<double const*>(&uint64_value_hex);

  if (!std::isnan(double_value_precise)) {
    // Since two `nan`-s are not equal, `EXPECT_EQ`-s do not apply to them.
    EXPECT_EQ(*reinterpret_cast<double const*>(&uint64_value_hex), double_value_precise) << value_source;
#ifndef CURRENT_APPLE
    EXPECT_EQ(double_value_computed, double_value_precise) << value_source;
#else
    // Thanks Travis for catching this!
    EXPECT_NEAR(double_value_computed, double_value_precise, 1e-9) << value_source;
    if (!(double_value_computed == double_value_precise)) {
      std::cerr << "NOTE: On MacOS, `" << value_source << "` does not match the Linux-computed value exactly.\n";
    }
#endif
  }

  // Also, make sure the double number is really what it should be.
  // NOTE(dkorolev): The test condition is excessively strict, but it should work on *nix-style `printf()`-s.
  // NOTE(dkorolev): Must revisit, and likely remove, or at least substantially relax this test if the next line fails.
  EXPECT_EQ(current::ToString(double_value_precise), current::ToString(double_value_approximate)) << value_source;

  // Now, make sure the very `current/optimize/double.h` logic is being followed.
  EXPECT_EQ(compactifiable, current::expression::IsRegularDouble(double_value_precise));
  if (compactifiable) {
    uint64_t const u = current::expression::PackDouble(double_value_precise);
    EXPECT_TRUE(current::expression::IsUInt64PackedDouble(u));
    double const d = current::expression::UnpackDouble(u);
    if (!std::isnan(d)) {
      EXPECT_EQ(d, double_value_precise);
    }
  }

  // Lastly, `compactifiable` is true when bits { 3, 4 } counting from 1-based MSB are the same.
  // They are the top bits of the exponent in the IEEE 754 representation, excluding two sign bits.
  bool const truly_compactifiable = ((uint64_value_hex ^ (uint64_value_hex >> 1)) & (1ull << 60)) == 0;
  EXPECT_EQ(compactifiable, truly_compactifiable);

  // A substantially more verbose version of the above check, for readability purposes.
  uint64_t const kMagicDoubleBit1 = (1ull << 61);  // 3rd 1-based from the MSB, as the top bit is (1ull << 63).
  uint64_t const kMagicDoubleBit2 = (1ull << 60);  // 4th 1-based from the MSB.
  bool const magic_bit_one_set = (uint64_value_hex & kMagicDoubleBit1) != 0;
  bool const magic_bit_two_set = (uint64_value_hex & kMagicDoubleBit2) != 0;
  if (truly_compactifiable) {
    EXPECT_EQ(magic_bit_one_set, magic_bit_two_set);
  } else {
    EXPECT_NE(magic_bit_one_set, magic_bit_two_set);
  }
}

#define CURRENT_DOUBLE_REPRESENTATION_TEST(hex, ...) \
  TEST(OptimizationDoubleRepresentation, Hex_##hex) { RunDoubleRepresentationTest(hex, __VA_ARGS__); }
#include "double_values.inl"
#undef CURRENT_DOUBLE_REPRESENTATION_TEST

// NOTE(dkorolev): This truly is a useless test, as the expression being compiled and/or optimized is NaN-free ;-)
TEST(OptimizationDoubleRepresentation, NanIsDoubleForOptimizePurposes) {
  double const v = std::numeric_limits<double>::quiet_NaN();
  uint64_t const u = *reinterpret_cast<uint64_t const*>(&v);
  RunDoubleRepresentationTest(u, u, v, true, v, "nan");
}

TEST(OptimizationDoubleRepresentation, DoublesUpTo1ePositive77AreRegular) {
  // Positive numbers with positive exponents.
  EXPECT_TRUE(current::expression::IsRegularDouble(+1e+1));
  EXPECT_TRUE(current::expression::IsRegularDouble(+1e+10));
  EXPECT_TRUE(current::expression::IsRegularDouble(+1e+50));
  EXPECT_TRUE(current::expression::IsRegularDouble(+1e+75));
  EXPECT_TRUE(current::expression::IsRegularDouble(+1e+76));
  EXPECT_TRUE(current::expression::IsRegularDouble(+1e+77));
  EXPECT_FALSE(current::expression::IsRegularDouble(+1e+78));
  EXPECT_FALSE(current::expression::IsRegularDouble(+1e+79));
  EXPECT_FALSE(current::expression::IsRegularDouble(+1e+80));
  EXPECT_FALSE(current::expression::IsRegularDouble(+1e+100));

  // Negative numbers with positive exponents.
  EXPECT_TRUE(current::expression::IsRegularDouble(-1e+1));
  EXPECT_TRUE(current::expression::IsRegularDouble(-1e+10));
  EXPECT_TRUE(current::expression::IsRegularDouble(-1e+50));
  EXPECT_TRUE(current::expression::IsRegularDouble(-1e+75));
  EXPECT_TRUE(current::expression::IsRegularDouble(-1e+76));
  EXPECT_TRUE(current::expression::IsRegularDouble(-1e+77));
  EXPECT_FALSE(current::expression::IsRegularDouble(-1e+78));
  EXPECT_FALSE(current::expression::IsRegularDouble(-1e+79));
  EXPECT_FALSE(current::expression::IsRegularDouble(-1e+80));
  EXPECT_FALSE(current::expression::IsRegularDouble(-1e+100));
}

TEST(OptimizationDoubleRepresentation, DoublesUpTo1eNegative76AreRegular) {
  // Positive numbers with negative exponents.
  EXPECT_TRUE(current::expression::IsRegularDouble(+1e-1));
  EXPECT_TRUE(current::expression::IsRegularDouble(+1e-10));
  EXPECT_TRUE(current::expression::IsRegularDouble(+1e-50));
  EXPECT_TRUE(current::expression::IsRegularDouble(+1e-75));
  EXPECT_TRUE(current::expression::IsRegularDouble(+1e-76));
  EXPECT_FALSE(current::expression::IsRegularDouble(+1e-77));
  EXPECT_FALSE(current::expression::IsRegularDouble(+1e-78));
  EXPECT_FALSE(current::expression::IsRegularDouble(+1e-79));
  EXPECT_FALSE(current::expression::IsRegularDouble(+1e-80));
  EXPECT_FALSE(current::expression::IsRegularDouble(+1e-100));

  // Negative numbers with negative exponents.
  EXPECT_TRUE(current::expression::IsRegularDouble(-1e-1));
  EXPECT_TRUE(current::expression::IsRegularDouble(-1e-10));
  EXPECT_TRUE(current::expression::IsRegularDouble(-1e-50));
  EXPECT_TRUE(current::expression::IsRegularDouble(-1e-75));
  EXPECT_TRUE(current::expression::IsRegularDouble(-1e-76));
  EXPECT_FALSE(current::expression::IsRegularDouble(-1e-77));
  EXPECT_FALSE(current::expression::IsRegularDouble(-1e-78));
  EXPECT_FALSE(current::expression::IsRegularDouble(-1e-79));
  EXPECT_FALSE(current::expression::IsRegularDouble(-1e-80));
  EXPECT_FALSE(current::expression::IsRegularDouble(-1e-100));
}
