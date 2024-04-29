#pragma once

#include "../../bricks/time/chrono.h"

typedef std::vector<uint64_t> Clocks;

class VectorClock {
 protected:
  Clocks clock;
  uint32_t local_id;

 public:
  explicit VectorClock(uint32_t size, uint32_t node_id) {
    // Set local process id and cluster size
    local_id = node_id;
    clock.resize(size, 0);
  }

  explicit VectorClock(Clocks &v, uint32_t node_id) {
    // Constructor for existing clock, used for inserting new data
    local_id = node_id;
    clock = Clocks(v.begin(), v.end());
  }

  explicit VectorClock() {
    // Lamport clocks for size=1
    local_id = 0;
    clock.push_back(0);
  }

  void step() {
    // T[i] = T[i] + 1 for logical step
    clock[local_id]++;
  }

  Clocks &state() {
    // Returns current state for network transmission
    return clock;
  }

  static bool is_conflicting(Clocks &v1, Clocks &v2) {
    // default implementation
    // returns true if there are no conflicts for merging
    return !is_lte(v1, v2);
  }

  bool merge(Clocks &to_compare, std::function<bool(Clocks &v1, Clocks &v2)> validator) {
    // Do we need to generalize validation, or it's ok to always merge vectors?
    // It's valid for replication example, but maybe in some cases we should not merge
    bool is_data_conflicted = validator(clock, to_compare);
    for (size_t i = 0; i < clock.size(); i++) {
      clock[i] = std::max(clock[i], to_compare[i]);
    }
    step();
    return !is_data_conflicted;
  }

  bool merge(Clocks &to_compare) { return merge(to_compare, is_conflicting); }

  static bool is_same(Clocks &v1, Clocks &v2) {
    // Happens on exactly same moment
    // T=T' if T[i] = T'[i] for any i
    for (size_t i = 0; i < v1.size(); i++) {
      if (v1[i] != v2[i]) {
        return false;
      }
    }
    return true;
  }

  static bool is_lte(Clocks &v1, Clocks &v2) {
    // Happens early or at the same time
    // T <= T' if T[i] <= T'[i] for any i
    for (size_t i = 0; i < v1.size(); i++) {
      if (v1[i] > v2[i]) {
        return false;
      }
    }
    return true;
  }

  static bool is_early(Clocks &v1, Clocks &v2) {
    // v1 happens before v2
    // T < T' if T <= T' and T != T'
    return is_lte(v1, v2) && !is_same(v1, v2);
  }

  static bool is_parallel(Clocks &v1, Clocks &v2) {
    // v1 is not in sync with v2
    // T || T' if !(T <= T') and !(T' <= T)
    return !is_lte(v1, v2) && !is_lte(v2, v1);
  }
};

class StrictVectorClock : public VectorClock {
  using VectorClock::VectorClock;

 public:
  static bool is_conflicting(Clocks &v1, Clocks &v2) {
    // Check if v1 is in sync with v2 and v1 is strictly early then v2
    return !(!is_parallel(v1, v2) && is_early(v1, v2));
  }
  bool merge(Clocks &to_compare) { return VectorClock::merge(to_compare, is_conflicting); }
};