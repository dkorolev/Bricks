#pragma once

#include "../../bricks/time/chrono.h"

class DiscreteClocks : public std::vector<uint64_t> {
 public:
  using vector<uint64_t>::vector;
  void inc(int i) { std::vector<uint64_t>::operator[](i)++; }
  void reset(uint32_t size) { std::vector<uint64_t>::resize(size, 0); }
};

class ContinuousClocks : public std::vector<std::chrono::microseconds> {
 public:
  using vector<std::chrono::microseconds>::vector;
  void inc(int i) { std::vector<std::chrono::microseconds>::operator[](i) = current::time::Now(); }

  void reset(uint32_t size) {
    auto now = current::time::Now();
    std::vector<std::chrono::microseconds>::resize(size, now);
  }
};

template <class CLOCK = DiscreteClocks>
class MergeStrategy {
 public:
  std::pair<bool, bool> merge(CLOCK &v1, CLOCK &v2, std::function<bool(CLOCK &v1, CLOCK &v2)> validator) {
    // Basic merge strategy:
    // 1. always merge (even in case of conflict)
    // 2. default conflict function check that v1 <= v2
    return std::pair<bool, bool>{!validator(v1, v2), true};
  }
  std::pair<bool, bool> merge(CLOCK &v1, CLOCK &v2) { return merge(v1, v2, is_conflicting); }
  static bool is_conflicting(CLOCK &v1, CLOCK &v2) {
    // basic implementation
    // returns true if there are no conflicts for merging
    return !is_lte(v1, v2);
  }

  static bool is_same(CLOCK &v1, CLOCK &v2) {
    // Happens on exactly same moment
    // T=T' if T[i] = T'[i] for any i
    for (size_t i = 0; i < v1.size(); i++) {
      if (v1[i] != v2[i]) {
        return false;
      }
    }
    return true;
  }

  static bool is_lte(CLOCK &v1, CLOCK &v2) {
    // Happens early or at the same time
    // T <= T' if T[i] <= T'[i] for any i
    for (size_t i = 0; i < v1.size(); i++) {
      if (v1[i] > v2[i]) {
        return false;
      }
    }
    return true;
  }

  static bool is_early(CLOCK &v1, CLOCK &v2) {
    // v1 happens before v2
    // T < T' if T <= T' and T != T'
    return is_lte(v1, v2) && !is_same(v1, v2);
  }

  static bool is_parallel(CLOCK &v1, CLOCK &v2) {
    // v1 is not in sync with v2
    // T || T' if !(T <= T') and !(T' <= T)
    return !is_lte(v1, v2) && !is_lte(v2, v1);
  }
};

template <class CLOCK = DiscreteClocks>
class StrictMergeStrategy : public MergeStrategy<CLOCK> {
 public:
  static bool is_conflicting(CLOCK &v1, CLOCK &v2) {
    // Check if v1 is in sync with v2 and v1 is strictly early then v2
    return !(!MergeStrategy<CLOCK>::is_parallel(v1, v2) && MergeStrategy<CLOCK>::is_early(v1, v2));
  }
  std::pair<bool, bool> merge(CLOCK &v1, CLOCK &v2) { return MergeStrategy<CLOCK>::merge(v1, v2, is_conflicting); }
};

template <class CLOCK = DiscreteClocks, template <typename> class STRATEGY = MergeStrategy>
class VectorClock {
 protected:
  CLOCK clock;
  STRATEGY<CLOCK> strategy;
  uint32_t local_id;

 public:
  explicit VectorClock(uint32_t size, uint32_t node_id) {
    // Set local process id and cluster size
    local_id = node_id;
    clock.reset(size);
  }

  explicit VectorClock(CLOCK &v, uint32_t node_id) {
    // Constructor for existing clock, used for inserting new data
    local_id = node_id;
    clock = CLOCK(v.begin(), v.end());
  }

  explicit VectorClock() {
    // Lamport clocks for size=1
    local_id = 0;
    clock.reset(1);
  }

  void step() {
    // T[i] = T[i] + 1 for logical step
    clock.inc(local_id);
  }

  CLOCK &state() {
    // Returns current state for network transmission
    return clock;
  }

  bool merge(CLOCK &to_compare) {
    auto merge_results = strategy.merge(clock, to_compare);
    if (merge_results.second) {
      for (size_t i = 0; i < clock.size(); i++) {
        clock[i] = std::max(clock[i], to_compare[i]);
      }
      step();
    }
    return merge_results.first;
  }
};
