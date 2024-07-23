/*******************************************************************************
The MIT License (MIT)

Copyright (c) 2014 Andrei Drozdov <sulverus@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
`copies of the Software, and to permit persons to whom the Software is
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
#pragma once

#include "../../bricks/time/chrono.h"

template <typename T>
class ActsAsVector {
  // Implementation of a container with Vector-like interface.
 protected:
  std::vector<T> storage;

 public:
  explicit ActsAsVector() { storage = std::vector<T>(); };
  explicit ActsAsVector(size_t count) { storage = std::vector<T>(count); };
  explicit ActsAsVector(ActsAsVector& other) { storage = std::vector<T>(other); };
  explicit ActsAsVector(const ActsAsVector& other) { storage = std::vector<T>(other.storage); };
  ActsAsVector& operator=(ActsAsVector const& other) = default;
  ActsAsVector& operator=(ActsAsVector&& other) = default;
  ~ActsAsVector() = default;
  ActsAsVector(std::initializer_list<T> init) { storage = std::vector<T>(init); };

  T& at(size_t pos) { return storage.at(pos); };
  const T& at(size_t pos) const { return storage.at(pos); };
  T& operator[](size_t pos) { return storage[pos]; };
  const T& operator[](size_t pos) const { return storage[pos]; };
  T* data() { return storage.data(); };
  const T* data() const { return storage.data(); };

  typename std::vector<T>::iterator begin() { return storage.begin(); };
  typename std::vector<T>::const_iterator begin() const { return storage.begin(); };
  typename std::vector<T>::iterator end() { return storage.end(); };
  typename std::vector<T>::const_iterator end() const { return storage.end(); };

  bool empty() const { return storage.empty(); };
  size_t size() const { return storage.size(); };
  void resize(size_t count) { storage.resize(count); };
  void resize(size_t count, const T& value) { storage.resize(count, value); };
  void reserve(size_t new_cap) { storage.reserve(new_cap); };
  void push_back(const T& value) { storage.push_back(value); };
  void push_back(T&& value) { storage.push_back(value); };
};

class DiscreteClocks : public ActsAsVector<uint64_t> {
 public:
  using ActsAsVector<uint64_t>::ActsAsVector;
  uint64_t ToStringAt(int i) { return storage[i]; }
  void Increment(int i) { storage[i]++; }
  void Reset(uint32_t size) { storage.resize(size, 0); }
};

class ContinuousClocks : public ActsAsVector<std::chrono::microseconds> {
 public:
  using ActsAsVector<std::chrono::microseconds>::ActsAsVector;
  void Increment(int i) { storage[i] = current::time::Now(); }
  uint64_t ToStringAt(int i) { return storage[i].count(); }

  void Reset(uint32_t size) {
    auto now = current::time::Now();
    storage.resize(size, now);
  }
};

struct MergeResult {
  bool is_valid_state;
  bool should_mutate_clock;
};

template <class CLOCK = DiscreteClocks>
class MergeStrategy {
 public:
  static struct MergeResult Merge(CLOCK& v1, CLOCK& v2, std::function<bool(CLOCK& v1, CLOCK& v2)> validator) {
    // Basic merge strategy:
    // 1. always merge (even in case of conflict)
    // 2. default conflict function check that v1 <= v2.
    return MergeResult{!validator(v1, v2), true};
  }
  static struct MergeResult Merge(CLOCK& v1, CLOCK& v2) { return Merge(v1, v2, IsConflicting); }
  static bool IsConflicting(CLOCK& v1, CLOCK& v2) {
    // basic implementation,
    // returns true if there are no conflicts for merging.
    return !IsLte(v1, v2);
  }

  static bool IsSame(CLOCK& v1, CLOCK& v2) {
    // Happens at exactly same moment,
    // T=T' if T[i] = T'[i] for any i.
    for (size_t i = 0; i < v1.size(); ++i) {
      if (v1[i] != v2[i]) {
        return false;
      }
    }
    return true;
  }

  static bool IsLte(CLOCK& v1, CLOCK& v2) {
    // Happens early or at the same time,
    // T <= T' if T[i] <= T'[i] for any i.
    for (size_t i = 0; i < v1.size(); ++i) {
      if (v1[i] > v2[i]) {
        return false;
      }
    }
    return true;
  }

  static bool IsEarly(CLOCK& v1, CLOCK& v2) {
    // v1 happens before v2,
    // T < T' if T <= T' and T != T'.
    return IsLte(v1, v2) && !IsSame(v1, v2);
  }

  static bool IsParallel(CLOCK& v1, CLOCK& v2) {
    // v1 is not in sync with v2,
    // T || T' if !(T <= T') and !(T' <= T).
    return !IsLte(v1, v2) && !IsLte(v2, v1);
  }
};

template <class CLOCK = DiscreteClocks>
class StrictMergeStrategy : public MergeStrategy<CLOCK> {
 public:
  static bool IsConflicting(CLOCK& v1, CLOCK& v2) {
    // Check if v1 is in sync with v2 and v1 is strictly early then v2.
    return !(!MergeStrategy<CLOCK>::IsParallel(v1, v2) && MergeStrategy<CLOCK>::IsEarly(v1, v2));
  }
  static struct MergeResult Merge(CLOCK& v1, CLOCK& v2) { return MergeStrategy<CLOCK>::Merge(v1, v2, IsConflicting); }
};

template <class CLOCK = DiscreteClocks, template <typename> class STRATEGY = MergeStrategy>
class VectorClock {
 protected:
  CLOCK clock_;
  STRATEGY<CLOCK> strategy_;
  uint32_t local_id_;

 public:
  explicit VectorClock(uint32_t size, uint32_t node_id) {
    // Set local process id and cluster size.
    local_id_ = node_id;
    clock_.reset(size);
  }

  explicit VectorClock(const CLOCK& v, uint32_t node_id) {
    // Constructor for existing clock, used for inserting new data.
    local_id_ = node_id;
    clock_ = CLOCK(v);
  }

  explicit VectorClock() {
    // Lamport clocks for size=1.
    local_id_ = 0;
    clock_.Reset(1);
  }

  std::string ToString() {
    // Returns string representation of the object
    std::ostringstream out_str;
    out_str << "VCLOCK ID=" << local_id_ << ": [";
    for (size_t i = 0; i < clock_.size(); ++i) {
      out_str << clock_.ToStringAt(i);
      if (i < clock_.size() - 1) {
        out_str << ", ";
      }
    }
    out_str << "]";
    return out_str.str();
  }

  void Step() {
    // T[i] = T[i] + 1 for logical step.
    clock_.Increment(local_id_);
  }

  const CLOCK& State() {
    // Returns current state for network transmission.
    return clock_;
  }

  bool AdvanceTo(CLOCK& to_compare) {
    auto merge_results = strategy_.Merge(clock_, to_compare);
    if (merge_results.should_mutate_clock) {
      for (size_t i = 0; i < clock_.size(); ++i) {
        clock_[i] = std::max(clock_[i], to_compare[i]);
      }
      Step();
    }
    return merge_results.is_valid_state;
  }
};
