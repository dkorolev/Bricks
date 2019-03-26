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

#ifndef OPTIMIZE_DOUBLE_H
#define OPTIMIZE_DOUBLE_H

#include <cstdint>

namespace current {
namespace expression {

// For Current's optimizer purposes, `double` values are regular when the top two bits of their exponent are the same.
// This discards the values that are either `1e+many` or `1e+many`, leaving everything else free to use. The win is that
// the double values can be kept in the same 64-bit space as the "expression node indexes", which saves dramatically
// both on CPU and on RAM when operating with mathematical expressions. Gentle reminder: RAM is the major constraint.

// The MSB and the 2nd MSB of the representation of double are the sign of the very value and the sign of its exponent.
// The next two bits, `(1ull << 61) and `(1ull << 60)`, are the most signigifant bits of the exponent, and they either
// are both zero (for `1e+{small_number}`-s, ex. `2.5e10`), or both one (for `1e-{small_number}`-s, ex. `7.5e-10`).
//
// For the check on whether a given `uint64_t` is actually an encoded `double`, the logic is:
// - The ultimate is simply if bit `61` is set: `if (encoded_value & (1ull << 61)) { ... }`
// - The encoder just sets this bit.
// - The decoder clears this bit, unless bit 60 is set, as at the end of the day the bits 60 and 61 shouls be equal.

#ifndef NDEBUG
// This this header file dependencies-free, no `googletest`, etc. -- D.K.
inline void TriggerSegmentationFault() { *reinterpret_cast<volatile int*>(0) = 0; }
#endif

inline bool IsRegularDouble(double x) {
  uint64_t const u = *reinterpret_cast<uint64_t*>(&x);
  return ((u ^ (u >> 1)) & (1ull << 60)) == 0;
}

inline uint64_t PackDouble(double x) {
#ifndef NDEBUG
  if (!IsRegularDouble(x)) {
    TriggerSegmentationFault();
  }
#endif
  uint64_t const u = *reinterpret_cast<uint64_t*>(&x);
  return u | (1ull << 61);
}

// No casting to `bool` for performance reasons. -- D.K.
inline uint64_t IsUInt64PackedDouble(uint64_t u) { return u & (1ull << 61); }

inline double UnpackDouble(uint64_t u) {
#ifndef NDEBUG
  if (!IsUInt64PackedDouble(u)) {
    TriggerSegmentationFault();
  }
#endif
  if (u & (1ull << 60)) {
    return *reinterpret_cast<double*>(&u);
  } else {
    u ^= (1ull << 61);
    return *reinterpret_cast<double*>(&u);
  }
}

// TODO(dkorolev): Check in the large unit test that exposes the internals of the `double` representation?
// TODO(dkorolev): Introduce constants, at least for the values used by the differentiator, `0`, `1`, `2`, `4`, `0.5`?

}  // namespace current::expression
}  // namespace current

#endif  // OPTIMIZE_DOUBLE_H
