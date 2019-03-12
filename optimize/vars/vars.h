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

// When the variables tree is attempted to be changed after being frozen via `.Freeze()`.
// I.e., `c["foo"] = 1.0; vars_context.Freeze(); c["bar"] = 2.0;`.
struct VarsFrozenException final : OptimizeException {};

// To make sure `Freeze()` is only called on the unfrozen vars tree, and `Unfreeze()` is only called on the frozen one.
struct VarsAlreadyFrozenException final : OptimizeException {};
struct VarsNotFrozenException final : OptimizeException {};

// For flattened vars vector access, see the unit tests for more details.
struct VarsMapperException : OptimizeException {};
struct VarsMapperWrongVarException : VarsMapperException {};
struct VarsMapperNodeNotVarException : VarsMapperException {};
struct VarsMapperVarIsConstant : VarsMapperException {};

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
  CURRENT_FIELD(i, Optional<uint32_t>);  // The in-dense-vector, post-`Freeze()` index, in the DFS order. For JIT.
  CURRENT_FIELD(x, Optional<double>);    // The value, of a starting point, or of a constant.
  CURRENT_FIELD(c, Optional<bool>);      // Set to `true` if this "variable" is a constant, `null` otherwise.
  CURRENT_CONSTRUCTOR(X)
  (Optional<double> x, bool is_constant, uint32_t q, uint32_t optional_i = static_cast<uint32_t>(-1))
      : q(q),
        i(optional_i == static_cast<uint32_t>(-1) ? nullptr : Optional<uint32_t>(optional_i)),
        x(x),
        c(is_constant ? Optional<bool>(true) : nullptr) {}
};
CURRENT_STRUCT(V) { CURRENT_FIELD(z, std::vector<Node>); };
CURRENT_STRUCT(I) { CURRENT_FIELD(z, (std::map<uint32_t, Node>)); };
CURRENT_STRUCT(S) { CURRENT_FIELD(z, (std::map<std::string, Node>)); };
}  // namespace current::expression::json

// The information about the variables set, as well as their initial values and which are the constants.
struct VarsMapperConfig final {
  size_t const total_leaves;  // The number of variables, including the constant (`x[...].SetConstant(...)`) ones.
  size_t const total_nodes;   // The number of expression nodes.
  std::vector<double> const x0;
  std::vector<std::string> const name;
  std::vector<bool> const is_constant;
  json::Node const root;
  VarsMapperConfig(size_t total_leaves,
                   size_t total_nodes,
                   std::vector<double> x0,
                   std::vector<std::string> name,
                   std::vector<bool> is_constant,
                   json::Node root)
      : total_leaves(total_leaves),
        total_nodes(total_nodes),
        x0(std::move(x0)),
        name(std::move(name)),
        is_constant(std::move(is_constant)),
        root(std::move(root)) {}
};

class VarsMapper final {
 private:
  VarsMapperConfig const config_;
  std::vector<double> value_;

  class AccessorNode final {
   private:
    std::vector<double>& value_;
    json::Node const& node_;

   public:
    AccessorNode(std::vector<double>& value, json::Node const& node) : value_(value), node_(node) {}

    AccessorNode operator[](size_t i) const {
      if (Exists<json::V>(node_)) {
        auto const& v = Value<json::V>(node_);
        if (i < v.z.size()) {
          return AccessorNode(value_, v.z[i]);
        }
      } else if (Exists<json::I>(node_)) {
        auto const& v = Value<json::I>(node_);
        auto const cit = v.z.find(i);
        if (cit != v.z.end()) {
          return AccessorNode(value_, cit->second);
        }
      }
      CURRENT_THROW(VarsMapperWrongVarException());
    }

    AccessorNode operator[](std::string const& s) const {
      if (Exists<json::S>(node_)) {
        auto const& v = Value<json::S>(node_);
        auto const cit = v.z.find(s);
        if (cit != v.z.end()) {
          return AccessorNode(value_, cit->second);
        }
      }
      CURRENT_THROW(VarsMapperWrongVarException());
    }

    AccessorNode operator[](char const* s) const {
      if (Exists<json::S>(node_)) {
        auto const& v = Value<json::S>(node_);
        auto const cit = v.z.find(s);
        if (cit != v.z.end()) {
          return AccessorNode(value_, cit->second);
        }
      }
      CURRENT_THROW(VarsMapperWrongVarException());
    }

    operator double() const {
      if (Exists<json::X>(node_)) {
        auto const& v = Value<json::X>(node_);
        if (Exists(v.i)) {
          return value_[Value(v.i)];
        }
      }
      CURRENT_THROW(VarsMapperNodeNotVarException());
    }

    double& Ref(bool allow_modifying_constants = false) const {
      if (Exists<json::X>(node_)) {
        auto const& v = Value<json::X>(node_);
        if (Exists(v.i)) {
          if (!allow_modifying_constants && Exists(v.c) && Value(v.c)) {
            CURRENT_THROW(VarsMapperVarIsConstant());
          }
          return value_[Value(v.i)];
        }
      }
      CURRENT_THROW(VarsMapperNodeNotVarException());
    }

    operator double&() const { return Ref(); }

    void operator=(double x) const { (operator double&()) = x; }

    double& RefEvenForAConstant() const { return Ref(true); }

    void SetConstantValue(double x) const { RefEvenForAConstant() = x; }
  };

  AccessorNode const root_;

 public:
  std::vector<double> const& x;  // `x` is a const reference `value_`, for easy read-only access to dense vars.
  explicit VarsMapper(VarsMapperConfig config)
      : config_(config), value_(config.x0), root_(value_, config_.root), x(value_) {}
  AccessorNode operator[](size_t i) const { return root_[i]; }
  AccessorNode operator[](std::string const& s) const { return root_[s]; }
};

class VarsContextInterface {
 public:
  ~VarsContextInterface() = default;
  virtual bool IsFrozen() const = 0;
  virtual VarsMapperConfig Freeze() = 0;
  virtual void Unfreeze() = 0;
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

enum class VarNodeType { Unset, Vector, IntMap, StringMap, Value };
struct VarNode {
  VarNodeType type = VarNodeType::Unset;
  std::vector<VarNode> children_vector;                  // `type == Vector`.
  std::map<size_t, VarNode> children_int_map;            // `type == IntMap`.
  std::map<std::string, VarNode> children_string_map;    // `type == StringMap`.
  Optional<double> value;                                // `type == Value`, unset if introduced w/o assignment.
  bool is_constant = false;                              // `type == Value`.
  size_t internal_leaf_index;                            // `type == Value`.
  uint32_t finalized_index = static_cast<uint32_t>(-1);  // `type == Value`.

  void DenseDoubleVector(size_t dim) {
    if (VarsManager::TLS().ActiveViaInterface().IsFrozen()) {
      CURRENT_THROW(VarsFrozenException());
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
    if (VarsManager::TLS().ActiveViaInterface().IsFrozen()) {
      if (type == VarNodeType::Vector && i < children_vector.size()) {
        return children_vector[i];
      } else if (type == VarNodeType::IntMap) {
        auto const cit = children_int_map.find(i);
        if (cit != children_int_map.end()) {
          return cit->second;
        }
      }
      CURRENT_THROW(VarsFrozenException());
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
    if (VarsManager::TLS().ActiveViaInterface().IsFrozen()) {
      if (type == VarNodeType::StringMap) {
        auto const cit = children_string_map.find(s);
        if (cit != children_string_map.end()) {
          return cit->second;
        }
      }
      CURRENT_THROW(VarsFrozenException());
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
    if (VarsManager::TLS().ActiveViaInterface().IsFrozen()) {
      CURRENT_THROW(VarsFrozenException());
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

  void SetConstant() {
    if (VarsManager::TLS().ActiveViaInterface().IsFrozen()) {
      CURRENT_THROW(VarsFrozenException());
    }
    if (type != VarNodeType::Value) {
      CURRENT_THROW(VarNodeTypeMismatchException());
    }
    is_constant = true;
  }

  void SetConstant(double x) {
    operator=(x);
    SetConstant();
  }

  size_t InternalLeafIndex() const {
    if (type != VarNodeType::Value) {
      CURRENT_THROW(VarIsNotLeafException());
    }
    return internal_leaf_index;
  }

  struct FrozenVariablesSetBeingPopulated {
    std::vector<double> x0;
    std::vector<std::string> name;
    std::vector<bool> is_constant;
    std::string name_so_far = "x";  // Will keep adding the removing scoped `[%d]` or `["%s]` here.
  };
  void DSFStampDenseIndexesForJIT(FrozenVariablesSetBeingPopulated& state) {
    if (type == VarNodeType::Vector) {
      size_t const name_so_far_length = state.name_so_far.length();
      for (size_t i = 0; i < children_vector.size(); ++i) {
        state.name_so_far += "[" + current::ToString(i) + "]";
        children_vector[i].DSFStampDenseIndexesForJIT(state);
        state.name_so_far.resize(name_so_far_length);
      }
    } else if (type == VarNodeType::IntMap) {
      size_t const name_so_far_length = state.name_so_far.length();
      for (auto& e : children_int_map) {
        state.name_so_far += "[" + current::ToString(e.first) + "]";
        e.second.DSFStampDenseIndexesForJIT(state);
        state.name_so_far.resize(name_so_far_length);
      }
    } else if (type == VarNodeType::StringMap) {
      size_t const name_so_far_length = state.name_so_far.length();
      for (auto& e : children_string_map) {
        state.name_so_far += "[" + JSON(e.first) + "]";
        e.second.DSFStampDenseIndexesForJIT(state);
        state.name_so_far.resize(name_so_far_length);
      }
    } else if (type == VarNodeType::Value) {
      finalized_index = state.x0.size();
      state.x0.push_back(Exists(value) ? Value(value) : 0.0);  // TODO(dkorolev): Collect `uninitialized` as well?
      state.name.push_back(state.name_so_far);
      state.is_constant.push_back(is_constant);
    } else if (type != VarNodeType::Unset) {
      CURRENT_THROW(VarsManagementException("Attempted to `DSFStampDenseIndexesForJIT()` on an invalid var node."));
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
      return json::X(value, is_constant, static_cast<uint32_t>(internal_leaf_index), finalized_index);
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
  bool frozen_ = false;
  size_t leaves_allocated_ = 0;
  std::vector<ExpressionNodeImpl> expression_nodes_;

 public:
  VarsContext() { VarsManager::TLS().SetActive(this, this); }
  ~VarsContext() { VarsManager::TLS().ClearActive(this, this); }
  VarNode& RootNode() {
    VarsManager::TLS().ConfirmActive(this, this);
    return root_;
  }
  bool IsFrozen() const override {
    VarsManager::TLS().ConfirmActive(this, this);
    return frozen_;
  }
  VarsMapperConfig Freeze() override {
    VarsManager::TLS().ConfirmActive(this, this);
    if (frozen_) {
      CURRENT_THROW(VarsAlreadyFrozenException());
    } else {
      frozen_ = true;
      VarNode::FrozenVariablesSetBeingPopulated state;
      root_.DSFStampDenseIndexesForJIT(state);
      if (state.x0.size() != leaves_allocated_ || state.name.size() != leaves_allocated_ ||
          state.is_constant.size() != leaves_allocated_ || state.name_so_far != "x") {
        CURRENT_THROW(VarsManagementException("Internal error: invariant failure during `Freeze()`."));
      }
      return VarsMapperConfig(
          leaves_allocated_, expression_nodes_.size(), state.x0, state.name, state.is_constant, root_.DoDump());
    }
  }
  void Unfreeze() override {
    VarsManager::TLS().ConfirmActive(this, this);
    if (!frozen_) {
      CURRENT_THROW(VarsNotFrozenException());
    } else {
      frozen_ = false;
    }
  }
  uint32_t AllocateVar() override {
    VarsManager::TLS().ConfirmActive(this, this);
    if (frozen_) {
      CURRENT_THROW(VarsManagementException("Attempted to `AllocateVar()` after the vars context is frozen."));
    } else {
      return leaves_allocated_++;
    }
  }
  template <typename... ARGS>
  expression_node_index_t EmplaceExpressionNode(ARGS&&... args) {
    expression_node_index_t const new_node_index = static_cast<expression_node_index_t>(expression_nodes_.size());
    expression_nodes_.emplace_back(std::forward<ARGS>(args)...);
    return new_node_index;
  }
  ExpressionNodeImpl const& operator[](expression_node_index_t expression_node_index) const {
    return expression_nodes_[expression_node_index];
  }
};

struct VarsAccessor final {
  void DenseDoubleVector(size_t dim) { VarsManager::TLS().Active().RootNode().DenseDoubleVector(dim); }
  VarNode& operator[](size_t i) { return VarsManager::TLS().Active().RootNode()[i]; }
  VarNode& operator[](std::string const& s) { return VarsManager::TLS().Active().RootNode()[s]; }
  VarsMapperConfig Freeze() const { return VarsManager::TLS().Active().Freeze(); }
  void Unfreeze() const { VarsManager::TLS().Active().Unfreeze(); }
  json::Node Dump() const { return VarsManager::TLS().Active().RootNode().DoDump(); }
};

// Let the user who is `using namespace current::expression` access the vars directly, without any syntactic sugar.
static struct VarsAccessor x;

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_VARS_VARS_H
