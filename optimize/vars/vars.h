/*******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * *******************************************************************************/

#ifndef OPTIMIZE_VARS_VARS_H
#define OPTIMIZE_VARS_VARS_H

#include <map>
#include <string>
#include <vector>

#include "../base.h"

#include "../../typesystem/serialization/json.h"
#include "../../typesystem/struct.h"

#include "../../bricks/util/singleton.h"

namespace current {
namespace expression {

struct VarsManagementException final : OptimizeException {
  using OptimizeException::OptimizeException;
};

// When the "weakly typed" tree of possibly multidimensional var nodes is used in the way other than it was initialized.
// I.e.:
//   c["foo"]["bar"] = 42;
//   c["foo"][0] throws.
//   c["foo"]["bar"]["baz"] throws.
//   c[42] throws.
struct VarNodeTypeMismatchException final : OptimizeException {};

// When the "internal leaf allocation index" is requested for the var path that is not a leaf.
// I.e.:
//   c["test"]["passed"] = 42;
//   c["whatever"] = 0;
//   c["test"] + c["whatever"] throws, because `["test"]` is a node, not a leaf, in the vars tree of `c[]`.
struct VarIsNotLeafException final : OptimizeException {};

// When the value is attempted to be re-assigned. I.e.: `c["foo"] = 1; c["foo"] = 2;`.
struct VarNodeReassignmentAttemptException final : OptimizeException {};

// When the variables tree is attempted to be changed after being `.Finalize()`-s.
// I.e., `c["foo"] = 1.0; vars_context.Finalize(); c["bar"] = 2.0;`.
struct VarsTreeFinalizedException final : OptimizeException {};

class VarsContextInterface {
 public:
  ~VarsContextInterface() = default;
  virtual bool IsFinalized() const = 0;
  virtual void Finalize() = 0;
  virtual uint32_t AllocateVar() = 0;
};

class VarsContext;  // : public VarsContextInterface

class VarsManager final {
 private:
  VarsContext* active_context_ = nullptr;
  VarsContextInterface* active_context_interface_ = nullptr;

 public:
  static VarsManager& TLS() { return ThreadLocalSingleton<VarsManager>(); }
  VarsContext& Active() const {
    if (!active_context_) {
      CURRENT_THROW(VarsManagementException("The variables context is required."));
    }
    return *active_context_;
  }
  void SetActive(VarsContext* ptr1, VarsContextInterface* ptr2) {
    if (active_context_) {
      CURRENT_THROW(VarsManagementException("Attempted to create a nested variables context."));
    }
    active_context_ = ptr1;
    active_context_interface_ = ptr2;
  }
  void ConfirmActive(VarsContext const* ptr1, VarsContextInterface const* ptr2) const {
    if (!(active_context_ == ptr1 && active_context_interface_ == ptr2)) {
      CURRENT_THROW(VarsManagementException("Mismatch of active `current::expression::VarsContext`."));
    }
  }
  void ClearActive(VarsContext const* ptr1, VarsContextInterface const* ptr2) {
    if (!(active_context_ == ptr1 && active_context_interface_ == ptr2)) {
      std::cerr << "Internal error when deleting variables context." << std::endl;
      std::exit(-1);
    }
    active_context_ = nullptr;
    active_context_interface_ = nullptr;
  }
  VarsContextInterface& ActiveViaInterface() { return *active_context_interface_; }
};

namespace json {
// Short names to save on space in these JSONs.
// They are not really meant for human eyes consumption, just for checkpointing and for the unit tests. -- D.K.
CURRENT_FORWARD_DECLARE_STRUCT(U);  // "Unset".
CURRENT_FORWARD_DECLARE_STRUCT(V);  // "Vector".
CURRENT_FORWARD_DECLARE_STRUCT(I);  // "IntMap".
CURRENT_FORWARD_DECLARE_STRUCT(S);  // "StringMap".
CURRENT_FORWARD_DECLARE_STRUCT(X);  // "Value".
CURRENT_VARIANT(Node, U, V, I, S, X);
CURRENT_STRUCT(U){};
CURRENT_STRUCT(X) {
  CURRENT_FIELD(q, uint32_t);            // The internal index, in the order of defining the variables.
  CURRENT_FIELD(i, Optional<uint32_t>);  // The in-dense-vector, post-`Finalize()` index, in the DFS order.
  CURRENT_FIELD(x, double);
  CURRENT_CONSTRUCTOR(X)
  (double x, uint32_t q, uint32_t optional_i = static_cast<uint32_t>(-1))
      : q(q), i(optional_i == static_cast<uint32_t>(-1) ? nullptr : Optional<uint32_t>(optional_i)), x(x) {}
};
CURRENT_STRUCT(V) { CURRENT_FIELD(z, std::vector<Node>); };
CURRENT_STRUCT(I) { CURRENT_FIELD(z, (std::map<uint32_t, Node>)); };
CURRENT_STRUCT(S) { CURRENT_FIELD(z, (std::map<std::string, Node>)); };
}  // namespace current::expression::json

enum class VarNodeType { Unset, Vector, IntMap, StringMap, Value };
struct VarNode {
  VarNodeType type = VarNodeType::Unset;
  std::vector<VarNode> children_vector;                  // `type == Vector`.
  std::map<size_t, VarNode> children_int_map;            // `type == IntMap`.
  std::map<std::string, VarNode> children_string_map;    // `type == StringMap`.
  double value;                                          // `type == Value`.
  size_t internal_leaf_index;                            // `type == Value`.
  uint32_t finalized_index = static_cast<uint32_t>(-1);  // `type == Value`.

  void DenseDoubleVector(size_t dim) {
    if (VarsManager::TLS().ActiveViaInterface().IsFinalized()) {
      CURRENT_THROW(VarsTreeFinalizedException());
    }
    if (!dim || dim > static_cast<size_t>(1e6)) {
      // NOTE(dkorolev): The `1M` size cutoff is somewhat arbitrary here, but I honestly don't believe
      //                 this optimization code should be used to optimize an 1M++-variables model.
      CURRENT_THROW(VarsManagementException("Attempted to create a dense vector of the wrong size."));
    }
    if (type == VarNodeType::Unset) {
      type = VarNodeType::Vector;
      children_vector.resize(dim);
    } else if (!(type == VarNodeType::Vector && children_vector.size() == dim)) {
      CURRENT_THROW(VarNodeTypeMismatchException());
    }
  }

  VarNode& operator[](size_t i) {
    if (VarsManager::TLS().ActiveViaInterface().IsFinalized()) {
      if (type == VarNodeType::Vector && i < children_vector.size()) {
        return children_vector[i];
      } else if (type == VarNodeType::IntMap) {
        auto const cit = children_int_map.find(i);
        if (cit != children_int_map.end()) {
          return cit->second;
        }
      }
      CURRENT_THROW(VarsTreeFinalizedException());
    }
    if (type == VarNodeType::Vector) {
      if (i < children_vector.size()) {
        return children_vector[i];
      } else {
        CURRENT_THROW(VarsManagementException("Out of bounds for the dense variables node."));
      }
    }
    if (type == VarNodeType::Unset) {
      type = VarNodeType::IntMap;
    }
    if (type != VarNodeType::IntMap) {
      CURRENT_THROW(VarNodeTypeMismatchException());
    }
    return children_int_map[i];
  }

  VarNode& operator[](std::string const& s) {
    if (VarsManager::TLS().ActiveViaInterface().IsFinalized()) {
      if (type == VarNodeType::StringMap) {
        auto const cit = children_string_map.find(s);
        if (cit != children_string_map.end()) {
          return cit->second;
        }
      }
      CURRENT_THROW(VarsTreeFinalizedException());
    }
    if (type == VarNodeType::Unset) {
      type = VarNodeType::StringMap;
    }
    if (type != VarNodeType::StringMap) {
      CURRENT_THROW(VarNodeTypeMismatchException());
    }
    return children_string_map[s];
  }

  void operator=(double x) {
    if (VarsManager::TLS().ActiveViaInterface().IsFinalized()) {
      CURRENT_THROW(VarsTreeFinalizedException());
    }
    if (type != VarNodeType::Unset) {
      if (type == VarNodeType::Value) {
        if (value == x) {
          return;  // A valid no-op. -- D.K.
        } else {
          CURRENT_THROW(VarNodeReassignmentAttemptException());
        }
      } else {
        CURRENT_THROW(VarNodeTypeMismatchException());
      }
    }
    type = VarNodeType::Value;
    value = x;
    internal_leaf_index = VarsManager::TLS().ActiveViaInterface().AllocateVar();
  }

  size_t InternalLeafIndex() const {
    if (type != VarNodeType::Value) {
      CURRENT_THROW(VarIsNotLeafException());
    }
    return internal_leaf_index;
  }

  void DFSToStampFinalizedIndexes(uint32_t& next_finalized_index) {
    if (type == VarNodeType::Vector) {
      for (VarNode& c : children_vector) {
        c.DFSToStampFinalizedIndexes(next_finalized_index);
      }
    } else if (type == VarNodeType::IntMap) {
      for (auto& e : children_int_map) {
        e.second.DFSToStampFinalizedIndexes(next_finalized_index);
      }
    } else if (type == VarNodeType::StringMap) {
      for (auto& e : children_string_map) {
        e.second.DFSToStampFinalizedIndexes(next_finalized_index);
      }
    } else if (type == VarNodeType::Value) {
      finalized_index = next_finalized_index;
      ++next_finalized_index;
    } else if (type != VarNodeType::Unset) {
      CURRENT_THROW(VarsManagementException("Attempted to `DFSToStampFinalizedIndexes()` on an invalid var node."));
    }
  }

  json::Node DoDump() const {
    if (type == VarNodeType::Vector) {
      json::V dense;
      dense.z.reserve(children_vector.size());
      for (VarNode const& c : children_vector) {
        dense.z.push_back(c.DoDump());
      }
      return dense;
    } else if (type == VarNodeType::IntMap) {
      json::I sparse_by_int;
      for (std::pair<size_t, VarNode> const& e : children_int_map) {
        sparse_by_int.z[static_cast<uint32_t>(e.first)] = e.second.DoDump();
      }
      return sparse_by_int;
    } else if (type == VarNodeType::StringMap) {
      json::S sparse_by_string;
      for (std::pair<std::string, VarNode> const& e : children_string_map) {
        sparse_by_string.z[e.first] = e.second.DoDump();
      }
      return sparse_by_string;
    } else if (type == VarNodeType::Value) {
      return json::X(value, static_cast<uint32_t>(internal_leaf_index), finalized_index);
    } else if (type == VarNodeType::Unset) {
      return json::U();
    } else {
      CURRENT_THROW(VarsManagementException("Attempted to Dump() an invalid var node."));
    }
  }
};

class VarsContext final : public VarsContextInterface {
 private:
  VarNode root_;
  bool finalized_ = false;
  size_t leaves_allocated_ = 0;

 public:
  VarsContext() { VarsManager::TLS().SetActive(this, this); }
  ~VarsContext() { VarsManager::TLS().ClearActive(this, this); }
  VarNode& RootNode() {
    VarsManager::TLS().ConfirmActive(this, this);
    return root_;
  }
  bool IsFinalized() const override {
    VarsManager::TLS().ConfirmActive(this, this);
    return finalized_;
  }
  void Finalize() override {
    VarsManager::TLS().ConfirmActive(this, this);
    if (!finalized_) {
      finalized_ = true;
      uint32_t finalized_leaf_next_index = 0;
      root_.DFSToStampFinalizedIndexes(finalized_leaf_next_index);
      if (static_cast<size_t>(finalized_leaf_next_index) != leaves_allocated_) {
        CURRENT_THROW(VarsManagementException("Internal error: leaf count mismatch."));
      }
    }
  }
  uint32_t AllocateVar() override {
    VarsManager::TLS().ConfirmActive(this, this);
    if (finalized_) {
      CURRENT_THROW(VarsManagementException("Attempted to `AllocateVar()` after the vars context is `Finalize()`-ed."));
    } else {
      return leaves_allocated_++;
    }
  }
};

struct VarsAccessor final {
  void DenseDoubleVector(size_t dim) { VarsManager::TLS().Active().RootNode().DenseDoubleVector(dim); }
  VarNode& operator[](size_t i) { return VarsManager::TLS().Active().RootNode()[i]; }
  VarNode& operator[](std::string const& s) { return VarsManager::TLS().Active().RootNode()[s]; }
  void Finalize() const { return VarsManager::TLS().Active().Finalize(); }
  json::Node Dump() const { return VarsManager::TLS().Active().RootNode().DoDump(); }
};

static struct VarsAccessor x;  // Let the user who is `using namespace current::expression` access vars directly.

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_VARS_VARS_H
