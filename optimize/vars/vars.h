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

// An internal error when using internal means to access vars by their indexes.
struct VarIndexOutOfBoundsException final : OptimizeException {};

// For flattened vars vector access, see the unit tests for more details.
struct VarsMapperException : OptimizeException {};
struct VarsMapperWrongVarException final : VarsMapperException {};
struct VarsMapperNodeNotVarException final : VarsMapperException {};
struct VarsMapperVarIsConstant final : VarsMapperException {};

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
  std::vector<size_t> const dense_index;
  json::Node const root;
  VarsMapperConfig(size_t total_leaves,
                   size_t total_nodes,
                   std::vector<size_t> dense_index,
                   std::vector<double> x0,
                   std::vector<std::string> name,
                   std::vector<bool> is_constant,
                   json::Node root)
      : total_leaves(total_leaves),
        total_nodes(total_nodes),
        x0(std::move(x0)),
        name(std::move(name)),
        is_constant(std::move(is_constant)),
        dense_index(std::move(dense_index)),
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
  virtual VarsMapperConfig ReindexVars() = 0;
  virtual VarsMapperConfig Freeze() = 0;
  virtual void Unfreeze() = 0;
  virtual uint32_t AllocateVar(std::string var_name) = 0;
  // TODO(dkorolev): Var finalized index stamping is something I'll need to refactor.
  virtual void MarkVarAsConstant(size_t var_internal_index) = 0;
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

class StringOrInt {
 private:
  std::string s;
  size_t i = static_cast<size_t>(-1);

 public:
  StringOrInt() = default;
  StringOrInt(std::string s) : s(std::move(s)) {}
  StringOrInt(size_t i) : i(i) {}

  operator bool() const { return !s.empty() || i != static_cast<size_t>(-1); }

  std::string AsString() const {
    CURRENT_ASSERT(s.empty() == (i != static_cast<size_t>(-1)));
    if (!s.empty()) {
      return JSON(s);
    } else {
      return current::ToString(i);
    }
  }
};

enum class VarNodeType { Unset, Vector, IntMap, StringMap, Value };
struct VarNode {
  VarNode const* parent = nullptr;  // To reconstruct the "full name" of the var going up.
  StringOrInt key;                  // The of this very var node, string or int.
  VarNodeType type = VarNodeType::Unset;
  std::vector<VarNode> children_vector;                  // `type == Vector`.
  std::map<size_t, VarNode> children_int_map;            // `type == IntMap`.
  std::map<std::string, VarNode> children_string_map;    // `type == StringMap`.
  Optional<double> value;                                // `type == Value`, unset if introduced w/o assignment.
  bool is_constant = false;                              // `type == Value`.
  size_t internal_var_index;                             // `type == Value`.
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
      for (size_t i = 0; i < dim; ++i) {
        children_vector[i].parent = this;
        children_vector[i].key = StringOrInt(i);
      }
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
    VarNode& result = children_int_map[i];
    if (!result.parent) {
      result.parent = this;
      result.key = StringOrInt(i);
    }
    return result;
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
    VarNode& result = children_string_map[s];
    if (!result.parent) {
      result.parent = this;
      result.key = StringOrInt(s);
    }
    return result;
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
    internal_var_index = VarsManager::TLS().ActiveViaInterface().AllocateVar(FullVarName());
  }

  void SetConstant() {
    if (VarsManager::TLS().ActiveViaInterface().IsFrozen()) {
      CURRENT_THROW(VarsFrozenException());
    }
    if (type != VarNodeType::Value) {
      CURRENT_THROW(VarNodeTypeMismatchException());
    }
    is_constant = true;
    VarsManager::TLS().ActiveViaInterface().MarkVarAsConstant(internal_var_index);
  }

  void SetConstant(double x) {
    operator=(x);
    SetConstant();
  }

  std::string FullVarName() const {
    std::vector<VarNode const*> path;
    VarNode const* node = this;
    do {
      path.push_back(node);
      node = node->parent;
      CURRENT_ASSERT(node);  // The very root node has no key, so the `while` loop would terminate. But it exists.
    } while (node->key);

    std::ostringstream os;
    os << 'x';
    for (auto crit = path.rbegin(); crit != path.rend(); ++crit) {
      os << '[' << (*crit)->key.AsString() << ']';
    }

    if (finalized_index != static_cast<uint32_t>(-1)) {
      os << '{' << finalized_index << '}';
    }

    return os.str();
  }

  size_t InternalVarIndex() const {
    if (type != VarNodeType::Value) {
      CURRENT_THROW(VarIsNotLeafException());
    }
    return internal_var_index;
  }

  struct FrozenVariablesSetBeingPopulated {
    std::vector<double> x0;
    std::vector<std::string> name;
    std::vector<bool> is_constant;
    std::vector<size_t> dense_index;

    explicit FrozenVariablesSetBeingPopulated(size_t leaves_allocated)
        : dense_index(leaves_allocated, static_cast<size_t>(-1)) {}
  };
  void DSFStampDenseIndexesForJIT(FrozenVariablesSetBeingPopulated& state) {
    if (type == VarNodeType::Vector) {
      for (size_t i = 0; i < children_vector.size(); ++i) {
        children_vector[i].DSFStampDenseIndexesForJIT(state);
      }
    } else if (type == VarNodeType::IntMap) {
      for (auto& e : children_int_map) {
        e.second.DSFStampDenseIndexesForJIT(state);
      }
    } else if (type == VarNodeType::StringMap) {
      for (auto& e : children_string_map) {
        e.second.DSFStampDenseIndexesForJIT(state);
      }
    } else if (type == VarNodeType::Value) {
      finalized_index = state.x0.size();
      state.dense_index[internal_var_index] = state.x0.size();
      state.x0.push_back(Exists(value) ? Value(value) : 0.0);  // TODO(dkorolev): Collect `uninitialized` as well?
      state.name.push_back(FullVarName());
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
      return json::X(value, is_constant, static_cast<uint32_t>(internal_var_index), finalized_index);
    } else if (type == VarNodeType::Unset) {
      return json::U();
    } else {
      CURRENT_THROW(VarsManagementException("Attempted to `InternalDebugDump()` an invalid var node."));
    }
  }
};

class VarsContext final : public VarsContextInterface {
 private:
  VarNode root_;
  bool frozen_ = false;
  std::vector<uint8_t> allocated_var_is_constant_;  // This is a small `vector<bool>` that is `push_back()`-into a lot.
  std::vector<std::string> var_name_;               // `var index => name`, w/ or w/o dense index, whether it's stamped.
  std::vector<size_t> dense_index_;                 // `var index => node index` mapping, or `static_cast<size_t>(-1)`.
  std::vector<size_t> dense_reverse_index_;         // `node index => var index` mapping, always valid.
  std::vector<ExpressionNodeImpl> expression_nodes_;

 public:
  VarsContext() { VarsManager::TLS().SetActive(this, this); }
  ~VarsContext() { VarsManager::TLS().ClearActive(this, this); }

  size_t NumberOfVars() const { return dense_index_.size(); }
  size_t NumberOfNodes() const { return expression_nodes_.size(); }

  std::string const& VarNameByOriginalIndex(size_t i) const { return var_name_[dense_reverse_index_[i]]; }

  VarNode& RootNode() {
    VarsManager::TLS().ConfirmActive(this, this);
    return root_;
  }

  bool IsFrozen() const override {
    VarsManager::TLS().ConfirmActive(this, this);
    return frozen_;
  }

  VarsMapperConfig ReindexVars() override {
    VarsManager::TLS().ConfirmActive(this, this);
    size_t const vars_count = allocated_var_is_constant_.size();
    VarNode::FrozenVariablesSetBeingPopulated state(vars_count);
    root_.DSFStampDenseIndexesForJIT(state);
    if (state.dense_index.size() != vars_count || state.x0.size() != vars_count || state.name.size() != vars_count ||
        state.is_constant.size() != vars_count) {
      CURRENT_THROW(VarsManagementException("Internal error: invariant failure during `ReindexVars()`."));
    }
    var_name_ = state.name;
    dense_index_ = state.dense_index;
    dense_reverse_index_.resize(vars_count);
    for (size_t i = 0; i < vars_count; ++i) {
      dense_reverse_index_[dense_index_[i]] = i;
    }
    return VarsMapperConfig(
        vars_count, expression_nodes_.size(), dense_index_, state.x0, state.name, state.is_constant, root_.DoDump());
  }

  VarsMapperConfig Freeze() override {
    if (frozen_) {
      CURRENT_THROW(VarsAlreadyFrozenException());
    } else {
      frozen_ = true;
      return ReindexVars();
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

  uint32_t AllocateVar(std::string var_name) override {
    VarsManager::TLS().ConfirmActive(this, this);
    if (frozen_) {
      CURRENT_THROW(VarsManagementException("Attempted to `AllocateVar()` after the vars context is frozen."));
    } else {
      uint32_t const newly_allocated_var_index = static_cast<uint32_t>(allocated_var_is_constant_.size());
      allocated_var_is_constant_.push_back(false);
      var_name_.push_back(std::move(var_name));
      dense_index_.push_back(static_cast<size_t>(-1));
      dense_reverse_index_.push_back(newly_allocated_var_index);
      return newly_allocated_var_index;
    }
  }

  void MarkVarAsConstant(size_t var_internal_index) override {
    VarsManager::TLS().ConfirmActive(this, this);
    if (frozen_) {
      CURRENT_THROW(VarsManagementException("Attempted to `MarkVarAsConstant()` after the vars context is frozen."));
    }
    if (!(var_internal_index < allocated_var_is_constant_.size())) {
      CURRENT_THROW(VarIndexOutOfBoundsException());
    }
    allocated_var_is_constant_[var_internal_index] = true;
  }

  double LeafDerivativeZeroOrOne(size_t var_internal_index, size_t derivative_per_finalized_var_index) const {
    VarsManager::TLS().ConfirmActive(this, this);
    if (frozen_) {
      CURRENT_THROW(
          VarsManagementException("Attempted to `LeafDerivativeZeroOrOne()` when the vars context is frozen."));
    }
    if (!(var_internal_index < dense_index_.size())) {
      CURRENT_THROW(VarIndexOutOfBoundsException());
    }
    if (dense_index_[var_internal_index] == static_cast<size_t>(-1)) {
      CURRENT_THROW(
          VarsManagementException("Attempted to `LeafDerivativeZeroOrOne()` on unidexed vars, run `ReindexVars()`."));
    }
    if (dense_index_.size() != allocated_var_is_constant_.size()) {
      CURRENT_THROW(VarsManagementException("Internal error: invariant failure during `LeafDerivativeZeroOrOne()`."));
    }
    return (dense_index_[var_internal_index] == derivative_per_finalized_var_index &&
            !allocated_var_is_constant_[var_internal_index])
               ? 1.0
               : 0.0;
  }

  template <typename... ARGS>
  expression_node_index_t EmplaceExpressionNode(ARGS&&... args) {
    VarsManager::TLS().ConfirmActive(this, this);
    if (frozen_) {
      CURRENT_THROW(
          VarsManagementException("Attempted to `EmplaceExpressionNode()` after the vars context is frozen."));
    }
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
  json::Node InternalDebugDump() const { return VarsManager::TLS().Active().RootNode().DoDump(); }
};

// Let the user who is `using namespace current::expression` access the vars directly, without any syntactic sugar.
static struct VarsAccessor x;

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_VARS_VARS_H
