#pragma once

#include "../../bricks/time/chrono.h"

typedef std::vector<std::chrono::microseconds> Clocks;

class VectorClock {
 protected:
  Clocks clock;
  uint32_t local_id;

 public:
  explicit VectorClock(uint32_t size, uint32_t node_id) {
    // Set local process id and cluster size
    local_id = node_id;
    clock.resize(size, current::time::Now());
  }
  explicit VectorClock() {
    // Lamport clocks for size=1
    VectorClock(1, 0);
  }

  void step() {
    // T[i] = T[i] + 1 for logical step
    clock[local_id] = current::time::Now();
  }

  Clocks &state() {
    // Returns current state for network transmission
    return clock;
  }

  static bool is_conflicting(Clocks &v1, Clocks &v2) {
    // default implementation
    // returns true if there are no conflicts for merging
    return is_lte(v1, v2);
  }

  bool merge(Clocks &to_compare, bool force, std::function<bool(Clocks &v1, Clocks &v2)> validator) {
    // Merges vector clock if there is no conflicts
    // force flag is used for inserts (from other nodes)
    if (!force && validator(clock, to_compare)) {
      return false;
    }
    for (size_t i = 0; i < clock.size(); i++) {
      clock[i] == max(clock[i], to_compare[i]);
    }
    step();
    return true;
  }

  bool merge(Clocks &to_compare, bool force) { return merge(to_compare, force, is_conflicting); }

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
    return !is_parallel(v1, v2) && is_early(v1, v2);
  }
};