/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2014 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>

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
#include "../../bricks/time/chrono.h"
#include "vector_clock.h"

TEST(VectorClock, SmokeTest) {
  auto v = VectorClock();
  v.step();

  // test merge from future - should return false
  auto data = Clocks();
  data.push_back(0);
  EXPECT_EQ(false, v.merge(data));
  EXPECT_EQ(v.state().size(), (size_t)(1));

  // test lte (t==t')
  auto v2 = VectorClock(data, data.size());
  EXPECT_EQ(true, v2.merge(data));
}

TEST(VectorClock, Merge) {
  auto base_time = current::time::Now();
  Clocks c1 = {1, 2};
  auto v = VectorClock(c1, 0);

  // Merge correct update
  Clocks c2 = {2, 3};
  // each element is greater - ok to merge
  EXPECT_EQ(true, v.merge(c2, false, 3));
  auto cur_state = v.state();
  // local time should be updated after merge
  EXPECT_GT(cur_state[0], c2[0]);
  // merged time should be equal c2[1]
  EXPECT_EQ(cur_state[1], c2[1]);

  // Merge incorrect update after previous update
  c2 = {1, 2};
  // can't merge T > T'
  EXPECT_EQ(false, v.merge(c2));
  // invalid data, merged vector
  EXPECT_EQ(v.state()[0], cur_state[0] + 1);
  EXPECT_EQ(v.state()[1], cur_state[1]);

  // Merge partially equals using lte validation
  v = VectorClock(c1, 0);
  c2 = {1, 3};
  // 0 is equeal, 1 is greater - ok to merge
  EXPECT_EQ(true, v.merge(c2, false, 3));
  cur_state = v.state();
  // local time should be updated after merge
  EXPECT_GT(cur_state[0], c2[0] + 1);
  // merged time should be equal c2[1]
  EXPECT_EQ(c2[1], cur_state[1]);

  // Merge partially incorrect
  v = VectorClock(c1, 0);
  cur_state = v.state();
  c2 = {1, 0};
  EXPECT_EQ(false, v.merge(c2));
  // invalid data, merged vector
  EXPECT_EQ(v.state()[0], cur_state[0] + 1);
  EXPECT_EQ(v.state()[1], cur_state[1]);
}

TEST(VectorClock, CustomValidator) {
  auto base_time = current::time::Now();
  Clocks c1 = {1, 2};
  auto v = VectorClock(c1, 0);

  Clocks c2 = {0, 1};
  // Check custom validation lambda - merge all events
  EXPECT_EQ(true,
            v.merge(
                c2, [](Clocks&, Clocks&) { return false; }, false, 3));
  auto cur_state = v.state();
  // local time should be updated after merge
  EXPECT_GT(cur_state[0], c1[0] + 1);
  // merged time should be equal to max(t[1], t'[1]) = base + 2
  EXPECT_EQ(c2[1] + 1, cur_state[1]);
}

TEST(VectorClock, StrictMerge) {
  auto base_time = current::time::Now();
  Clocks c1 = {1, 2};
  auto v = StrictVectorClock(c1, 0);

  // Merge correct update
  Clocks c2 = {2, 3};
  // each element is greater - ok to merge
  EXPECT_EQ(true, v.merge(c2, false, 4));
  auto cur_state = v.state();
  // local time should be updated after merge
  EXPECT_GT(cur_state[0], c2[1]);
  // merged time should be equal c2[1]
  EXPECT_EQ(c2[1], cur_state[1]);

  // Merge equals using strict validation
  c1 = {10, 20};
  v = StrictVectorClock(c1, 0);
  cur_state = v.state();
  c2 = {10, 20};
  EXPECT_EQ(false, v.merge(c2));
  EXPECT_EQ(v.state()[0], cur_state[0] + 1);
  EXPECT_EQ(v.state()[1], cur_state[1]);

  // Merge partially equals
  v = StrictVectorClock(c1, 0);
  cur_state = v.state();
  c2 = {1, 20};
  // 0 is equeal, 1 is greater - not ok to merge
  EXPECT_EQ(false, v.merge(c2));
  EXPECT_EQ(v.state()[0], cur_state[0] + 1);
  EXPECT_EQ(v.state()[1], cur_state[1]);

  // Merge incorrect
  v = StrictVectorClock(c1, 0);
  cur_state = v.state();
  c2 = {0, 1};
  EXPECT_EQ(false, v.merge(c2));
  // internal state was not changed
  EXPECT_EQ(v.state()[0], cur_state[0] + 1);
  EXPECT_EQ(v.state()[1], cur_state[1]);
}