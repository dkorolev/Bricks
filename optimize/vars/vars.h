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

// TODO(dkorolev): Enable some `NonStrict()` mode for liberal variables introduction.

#include <iostream>
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
//   x["foo"]["bar"] = 42;
//   x["foo"][0] throws.
//   x["foo"]["bar"]["baz"] throws.
//   x[42] throws.
struct VarNodeTypeMismatchException final : OptimizeException {};

// When the "variable index" is requested for the path that does not yet lead to a variable.
// I.e.:
//   x["test"]["passed"] = 42;
//   x["whatever"] = 0;
//   x["whatever"] + x["test"] throws, because `x["test"]` is a node, not a leaf, in the variables tree.
struct VarIsNotLeafException final : OptimizeException {};

// When the value is attempted to be re-assigned. I.e.: `x["foo"] = 1; x["foo"] = 2;`.
struct VarNodeReassignmentAttemptException final : OptimizeException {};

// When the variables tree is attempted to be changed after `GetVarsMapperConfig()` was called.
// I.e., `x["foo"] = 1.0; auto const config = x.GetVarsMapperConfig(); x["bar"] = 2.0;`.
//
// This is done to make sure the gradient is guaranteed to be of the right dimensionality.
// No new variables can be added after the first call to `GetVarsMapperConfig()`,
//
// NOTE(dkorolev): This exception is even being thrown in `NDEBUG` mode, because
// the check for it is lightweight, and the possible conesquences are just way too bad.
struct NoNewVarsCanBeAddedException final : OptimizeException {};

// Same for nodes for the "frozen" vars context.
struct NoNewNodesCanBeAddedException final : OptimizeException {};

#ifndef NDEBUG
struct VarIndexOutOfBoundsException final : OptimizeException {};
#endif

// For flattened vars vector access, see the unit tests for more details.
struct VarsMapperException : OptimizeException {};
struct VarsMapperWrongVarException final : VarsMapperException {};
struct VarsMapperNodeNotVarException final : VarsMapperException {};
struct VarsMapperVarIsConstant final : VarsMapperException {};
struct VarsMapperMovePointDimensionsMismatchException final : VarsMapperException {};
struct VarsMapperMovePointUnexpectedLambda final : VarsMapperException {};

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
  CURRENT_FIELD(i, uint32_t);          // The 0-based index of this variable, in the order of introduction.
  CURRENT_FIELD(x, Optional<double>);  // The value, of this variable at a starting point, or if it's a constant.
  CURRENT_FIELD(c, Optional<bool>);    // Set to `true` if this "variable" is a constant, `null` otherwise.
  CURRENT_CONSTRUCTOR(X)
  (uint32_t i, Optional<double> x, bool is_constant) : i(i), x(x), c(is_constant ? Optional<bool>(true) : nullptr) {}
};
CURRENT_STRUCT(V) { CURRENT_FIELD(z, std::vector<Node>); };
CURRENT_STRUCT(I) { CURRENT_FIELD(z, (std::map<uint32_t, Node>)); };
CURRENT_STRUCT(S) { CURRENT_FIELD(z, (std::map<std::string, Node>)); };
}  // namespace current::expression::json

class InternalVarsContext;

// The information about the variables set, as well as their initial values and which are the constants.
class InternalVarsConfig final {
 private:
  size_t const number_of_variables_;  // The number of vars, including the constant (`x[...].SetConstant(...)`) ones.
  size_t const number_of_nodes_;
#ifndef NDEBUG
  InternalVarsContext const* vars_context_;
#endif
  std::vector<double> const x0_;
  std::vector<std::string> const name_;
  std::vector<bool> const is_constant_;
  json::Node const root_;

 public:
  InternalVarsConfig(size_t number_of_variables,
                     size_t number_of_nodes,
#ifndef NDEBUG
                     InternalVarsContext const* vars_context,
#endif

                     std::vector<double> x0,
                     std::vector<std::string> name,
                     std::vector<bool> is_constant,
                     json::Node root)
      : number_of_variables_(number_of_variables),
        number_of_nodes_(number_of_nodes),
#ifndef NDEBUG
        vars_context_(vars_context),
#endif

        x0_(std::move(x0)),
        name_(std::move(name)),
        is_constant_(std::move(is_constant)),
        root_(std::move(root)) {
  }

#ifndef NDEBUG
  bool DebugConfigMatchesContext(InternalVarsContext const* candidate_vars_context,
                                 size_t candidate_number_of_variables) const {
    return candidate_vars_context == vars_context_ && candidate_number_of_variables == number_of_variables_;
  }
#endif

  std::vector<double> const& StartingPoint() const { return x0_; }
  size_t NumberOfVars() const { return number_of_variables_; }
  size_t NumberOfNodes() const { return number_of_nodes_; }
  std::vector<std::string> const& VarNames() const { return name_; }
  std::string const& operator[](size_t var_index) const {
#ifndef NDEBUG
    if (!(var_index < name_.size())) {
      TriggerSegmentationFault();
    }
#endif
    return name_[var_index];
  }
  std::vector<bool> const& VarIsConstant() const { return is_constant_; }
  json::Node const& Root() const { return root_; }
};

class InternalVarsContextInterface {
 public:
  ~InternalVarsContextInterface() = default;

  // After the call to `DoGetConfig()`, the expression is "frozen", and no new vars or nodes can be added.
  virtual InternalVarsConfig const& DoGetConfig() = 0;
  virtual bool IsFrozen() const = 0;

#ifndef NDEBUG
  virtual uint32_t AllocateNewVarInDebugMode(std::string var_name) = 0;
#else
  virtual uint32_t AllocateNewVar() = 0;
#endif
  virtual void MarkVarAsConstant(size_t var_index) = 0;
};
// Forward declaration for the default constuctor of `Vars` below.
inline InternalVarsContextInterface& InternalTLSInterface();

// To also be implemented for `value_t` in `../expression/expression.h`; the `value_t` type is unknown to `vars/`.
inline ExpressionNodeIndex ExpressionNodeIndexFromExpressionNodeOrValue(ExpressionNodeIndex index) { return index; }

class Vars final {
 private:
  InternalVarsConfig const& config_;
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

    operator double() const { return Ref(); }

    void operator=(double x) const { Ref() = x; }

    double& RefEvenForAConstant() const { return Ref(true); }

    void SetConstantValue(double x) const { RefEvenForAConstant() = x; }
  };

  AccessorNode const root_;

 public:
  using ThreadLocalContext = InternalVarsContext;
  using Config = InternalVarsConfig;

  // `x` is a const reference `value_`, for easy read-only access to the vars. NOTE(dkorolev): Possibly reshuffled vars!
  std::vector<double> const& x = value_;

  explicit Vars(InternalVarsConfig const& config = InternalTLSInterface().DoGetConfig())
      : config_(config), value_(config.StartingPoint()), root_(value_, config_.Root()) {}

  AccessorNode operator[](size_t i) const { return root_[i]; }
  AccessorNode operator[](std::string const& s) const { return root_[s]; }

  // This method is `template`-d to accept both `std::vector<ExpressionNodeIndex>` and `std::vector<value_t>`.
  // The latter type, `value_t`, is not introduced when building `vars.h`.
  template <typename T>
  void MovePoint(double const* ram, T const& direction, double step_size) {
    if (direction.size() != value_.size()) {
      CURRENT_THROW(VarsMapperMovePointDimensionsMismatchException());
    }
    std::vector<double> new_value(value_);
    for (size_t i = 0; i < direction.size(); ++i) {
      ExpressionNodeIndexFromExpressionNodeOrValue(direction[i])
          .CheckedDispatch([&](size_t node_index) { new_value[i] += ram[node_index] * step_size; },
                           [&](size_t var_index) { new_value[i] += value_[var_index] * step_size; },
                           [&](double x) { new_value[i] += x * step_size; },
                           []() { CURRENT_THROW(VarsMapperMovePointUnexpectedLambda()); });
    }
    value_ = std::move(new_value);
  }
};

class VarsManager final {
 private:
  InternalVarsContext* active_context_ = nullptr;
  InternalVarsContextInterface* active_context_interface_ = nullptr;

 public:
  static VarsManager& StaticInternalTLS() { return ThreadLocalSingleton<VarsManager>(); }
  InternalVarsContext& ActiveContext() const {
    if (!active_context_) {
      CURRENT_THROW(VarsManagementException("The variables context is required."));
    }
    return *active_context_;
  }
  void SetActive(InternalVarsContext* ptr1, InternalVarsContextInterface* ptr2) {
    if (active_context_) {
      CURRENT_THROW(VarsManagementException("Attempted to create a nested variables context."));
    }
    active_context_ = ptr1;
    active_context_interface_ = ptr2;
  }
  void ConfirmActive(InternalVarsContext const* ptr1, InternalVarsContextInterface const* ptr2) const {
    if (!(active_context_ == ptr1 && active_context_interface_ == ptr2)) {
      CURRENT_THROW(VarsManagementException("Mismatch of active `current::expression::InternalVarsContext`."));
    }
  }
  void ClearActive(InternalVarsContext const* ptr1, InternalVarsContextInterface const* ptr2) {
    if (!(active_context_ == ptr1 && active_context_interface_ == ptr2)) {
      std::cerr << "Internal error when deleting variables context." << std::endl;
#ifndef NDEBUG
      TriggerSegmentationFault();
#else
      std::exit(-1);
#endif
    }
    active_context_ = nullptr;
    active_context_interface_ = nullptr;
  }
  InternalVarsContextInterface& ActiveContextViaInterface() { return *active_context_interface_; }
};

inline InternalVarsContextInterface& InternalTLSInterface() {
  return VarsManager::StaticInternalTLS().ActiveContextViaInterface();
}

class StringOrInt {
 private:
  Optional<std::string> string_;  // NOTE(dkorolev): An empty string is a valid variable or variable node name.
  size_t size_t_ = static_cast<size_t>(-1);

 public:
  StringOrInt() = default;
  StringOrInt(std::string string_) : string_(std::move(string_)) {}
  StringOrInt(size_t size_t_) : size_t_(size_t_) {}

  operator bool() const { return Exists(string_) || size_t_ != static_cast<size_t>(-1); }

  std::string AsString() const {
    CURRENT_ASSERT(!Exists(string_) == (size_t_ != static_cast<size_t>(-1)));
    if (Exists(string_)) {
      return JSON(string_);
    } else {
      return current::ToString(size_t_);
    }
  }
};

enum class VarNodeType { Unset, Vector, IntMap, StringMap, Value };
struct VarNode {
  VarNode const* parent = nullptr;  // To reconstruct the "full name" of the var going up.
  StringOrInt key;                  // The of this very var node, string or int.
  VarNodeType type = VarNodeType::Unset;
  std::vector<VarNode> children_vector;                // `type == Vector`.
  std::map<size_t, VarNode> children_int_map;          // `type == IntMap`.
  std::map<std::string, VarNode> children_string_map;  // `type == StringMap`.
  Optional<double> value;                              // `type == Value`, unset if introduced w/o assignment.
  bool is_constant = false;                            // `type == Value`.
  size_t var_index;                                    // `type == Value`.

  void DenseDoubleVector(size_t dim) {
    if (InternalTLSInterface().IsFrozen()) {
      CURRENT_THROW(NoNewVarsCanBeAddedException());
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
    if (InternalTLSInterface().IsFrozen()) {
      if (type == VarNodeType::Vector && i < children_vector.size()) {
        return children_vector[i];
      } else if (type == VarNodeType::IntMap) {
        auto const cit = children_int_map.find(i);
        if (cit != children_int_map.end()) {
          return cit->second;
        }
      }
      CURRENT_THROW(NoNewVarsCanBeAddedException());
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
    if (InternalTLSInterface().IsFrozen()) {
      if (type == VarNodeType::StringMap) {
        auto const cit = children_string_map.find(s);
        if (cit != children_string_map.end()) {
          return cit->second;
        }
      }
      CURRENT_THROW(NoNewVarsCanBeAddedException());
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
    if (InternalTLSInterface().IsFrozen()) {
      CURRENT_THROW(NoNewVarsCanBeAddedException());
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
#ifndef NDEBUG
    var_index = InternalTLSInterface().AllocateNewVarInDebugMode(FullVarName());
#else
    var_index = InternalTLSInterface().AllocateNewVar();
#endif
  }

  void SetConstant() {
    if (InternalTLSInterface().IsFrozen()) {
      CURRENT_THROW(NoNewVarsCanBeAddedException());
    }
#ifndef NDEBUG
    if (type != VarNodeType::Value) {
      CURRENT_THROW(VarNodeTypeMismatchException());
    }
#endif
    is_constant = true;
    InternalTLSInterface().MarkVarAsConstant(var_index);
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
      CURRENT_ASSERT(node);  // The very root node exists but has no key, so the `while` loop would terminate. -- D.K.
    } while (node->key);

    std::ostringstream os;
    os << 'x';
    for (auto crit = path.rbegin(); crit != path.rend(); ++crit) {
      os << '[' << (*crit)->key.AsString() << ']';
    }

    return os.str();
  }

  size_t VarIndex() const {
    if (type != VarNodeType::Value) {
      CURRENT_THROW(VarIsNotLeafException());
    }
    return var_index;
  }

  struct FrozenVariablesSetBeingPopulated {
    size_t const size;

#ifndef NDEBUG
    std::vector<bool> initialized;
#endif

    std::vector<double> x0;
    std::vector<std::string> name;
    std::vector<bool> is_constant;
    explicit FrozenVariablesSetBeingPopulated(size_t size)
        : size(size),
#ifndef NDEBUG
          initialized(size),
#endif
          x0(size),
          name(size),
          is_constant(size) {
    }

#ifndef NDEBUG
    ~FrozenVariablesSetBeingPopulated() {
      if (initialized != std::vector<bool>(size, true)) {
        TriggerSegmentationFault();
      }
    }
#endif
  };
  void DSFStampDenseIndexesForJIT(FrozenVariablesSetBeingPopulated& state) {
    if (type == VarNodeType::Vector) {
      for (size_t i = 0u; i < children_vector.size(); ++i) {
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
#ifndef NDEBUG
      if (!(var_index < state.size)) {
        TriggerSegmentationFault();
      }
#endif
      state.x0[var_index] = Exists(value) ? Value(value) : 0.0;  // TODO(dkorolev): Collect `uninitialized` as well?
      state.name[var_index] = FullVarName();
      state.is_constant[var_index] = is_constant;
#ifndef NDEBUG
      state.initialized[var_index] = true;
#endif
    } else if (type != VarNodeType::Unset) {
      CURRENT_THROW(VarsManagementException("Attempted to `DSFStampDenseIndexesForJIT()` on an invalid var node."));
    }
  }

  json::Node ConstructTree() const {
    if (type == VarNodeType::Vector) {
      json::V dense;
      dense.z.reserve(children_vector.size());
      for (VarNode const& c : children_vector) {
        dense.z.push_back(c.ConstructTree());
      }
      return dense;
    } else if (type == VarNodeType::IntMap) {
      json::I sparse_by_int;
      for (std::pair<size_t, VarNode> const& e : children_int_map) {
        sparse_by_int.z[static_cast<uint32_t>(e.first)] = e.second.ConstructTree();
      }
      return sparse_by_int;
    } else if (type == VarNodeType::StringMap) {
      json::S sparse_by_string;
      for (std::pair<std::string, VarNode> const& e : children_string_map) {
        sparse_by_string.z[e.first] = e.second.ConstructTree();
      }
      return sparse_by_string;
    } else if (type == VarNodeType::Value) {
      return json::X(var_index, value, is_constant);
    } else if (type == VarNodeType::Unset) {
      return json::U();
    } else {
      CURRENT_THROW(VarsManagementException("Attempted to `InternalDebugDump()` an invalid var node."));
    }
  }
};

class InternalVarsContext final : public InternalVarsContextInterface {
 private:
  VarNode root_;

#ifndef NDEBUG
  std::vector<std::string> allocated_var_debug_name_;
#endif

  std::vector<bool> allocated_var_is_constant_;
  std::vector<ExpressionNodeImpl> expression_nodes_;

  // Initialized on the first call to `DoGetConfig()`, and no new vars or nodes can be added after that call.
  std::unique_ptr<InternalVarsConfig> vars_mapper_config_;

#ifndef NDEBUG
  void ConfirmSelfActiveIfInDebugMode() const { VarsManager::StaticInternalTLS().ConfirmActive(this, this); }
#endif

 public:
  InternalVarsContext() { VarsManager::StaticInternalTLS().SetActive(this, this); }
  ~InternalVarsContext() { VarsManager::StaticInternalTLS().ClearActive(this, this); }

  size_t NumberOfVars() const { return allocated_var_is_constant_.size(); }
  size_t NumberOfNodes() const { return expression_nodes_.size(); }

#ifndef NDEBUG
  std::string const& DebugVarNameByIndex(size_t i) const {
    if (!(i < allocated_var_debug_name_.size())) {
      TriggerSegmentationFault();
    }
    return allocated_var_debug_name_[i];
  }
  std::string VarName(size_t i) const { return DebugVarNameByIndex(i); }
#else
  // NOTE(dkorolev): Important to keep in mind that in `NDEBUG` mode the variable names are just `x[0]` and onwards.
  // The exported `InternalVarsConfig` will have all the fully-specified variable names collected,
  // but the calls to `DebugAsString()` would return erroneus results unless the very variables were introduced
  // specifically in the order of `x[0]`, `x[1]`, etc.
  std::string VarName(size_t i) const { return "x[" + current::ToString(i) + ']'; }
#endif

  VarNode& RootNode() {
#ifndef NDEBUG
    ConfirmSelfActiveIfInDebugMode();
#endif
    return root_;
  }

  bool IsFrozen() const override {
#ifndef NDEBUG
    ConfirmSelfActiveIfInDebugMode();
#endif
    return vars_mapper_config_ != nullptr;
  }

  // After the call to `DoGetConfig()`, the expression is "frozen", and no new vars or nodes can be added.
  InternalVarsConfig const& DoGetConfig() override {
#ifndef NDEBUG
    ConfirmSelfActiveIfInDebugMode();
#endif
    if (!vars_mapper_config_) {
      size_t const vars_count = NumberOfVars();
      VarNode::FrozenVariablesSetBeingPopulated state(vars_count);
      root_.DSFStampDenseIndexesForJIT(state);
      if (state.name.size() != vars_count || state.x0.size() != vars_count || state.is_constant.size() != vars_count) {
#ifndef NDEBUG
        TriggerSegmentationFault();
#else
        CURRENT_THROW(VarsManagementException("Internal error: invariant failure during `GetVarsMapperConfig()`."));
#endif
      }
      vars_mapper_config_ = std::make_unique<InternalVarsConfig>(vars_count,
                                                                 NumberOfNodes(),
#ifndef NDEBUG
                                                                 this,
#endif
                                                                 state.x0,
                                                                 state.name,
                                                                 state.is_constant,
                                                                 root_.ConstructTree());
    }
    return *vars_mapper_config_;
  }

#ifndef NDEBUG
  uint32_t AllocateNewVarInDebugMode(std::string var_name) override {
    ConfirmSelfActiveIfInDebugMode();
    uint32_t const newly_allocated_var_index = static_cast<uint32_t>(allocated_var_is_constant_.size());
    allocated_var_is_constant_.push_back(false);
    allocated_var_debug_name_.push_back(std::move(var_name));
    return newly_allocated_var_index;
  }
#else
  uint32_t AllocateNewVar() override {
    uint32_t const newly_allocated_var_index = static_cast<uint32_t>(allocated_var_is_constant_.size());
    allocated_var_is_constant_.push_back(false);
    return newly_allocated_var_index;
  }
#endif

  void MarkVarAsConstant(size_t var_index) override {
#ifndef NDEBUG
    ConfirmSelfActiveIfInDebugMode();
    if (!(var_index < allocated_var_is_constant_.size())) {
      CURRENT_THROW(VarIndexOutOfBoundsException());
    }
#endif
    allocated_var_is_constant_[var_index] = true;
  }

  bool IsVarNotConstant(size_t var_index) const {
#ifndef NDEBUG
    ConfirmSelfActiveIfInDebugMode();
    if (!(var_index < allocated_var_is_constant_.size())) {
      CURRENT_THROW(VarIndexOutOfBoundsException());
    }
#endif
    return !allocated_var_is_constant_[var_index];
  }

  bool IsVarTheNonConstantOneBeingDifferentiatedBy(size_t var_index, size_t derivative_per_var_index) const {
#ifndef NDEBUG
    ConfirmSelfActiveIfInDebugMode();
    if (!(var_index < allocated_var_is_constant_.size())) {
      CURRENT_THROW(VarIndexOutOfBoundsException());
    }
#endif
    return (var_index == derivative_per_var_index && !allocated_var_is_constant_[var_index]);
  }

  template <typename... ARGS>
  size_t DoEmplace(ARGS&&... args) {
#ifndef NDEBUG
    ConfirmSelfActiveIfInDebugMode();
#endif
    if (IsFrozen()) {
      CURRENT_THROW(NoNewNodesCanBeAddedException());
    }
    size_t const new_node_index = expression_nodes_.size();
    expression_nodes_.emplace_back(std::forward<ARGS>(args)...);
    return new_node_index;
  }

  ExpressionNodeImpl const& operator[](size_t expression_node_index) const {
#ifndef NDEBUG
    if (!(expression_node_index < expression_nodes_.size())) {
      TriggerSegmentationFault();
    }
    expression_nodes_[expression_node_index].AssertValid();
#endif
    return expression_nodes_[expression_node_index];
  }

  ExpressionNodeImpl& MutableNodeByIndex(size_t expression_node_index) {
#ifndef NDEBUG
    if (!(expression_node_index < expression_nodes_.size())) {
      TriggerSegmentationFault();
    }
    expression_nodes_[expression_node_index].AssertValid();
#endif
    return expression_nodes_[expression_node_index];
  }
};

inline InternalVarsContext& InternalTLS() { return VarsManager::StaticInternalTLS().ActiveContext(); }

struct VarsAccessor final {
  // Vars creators and accessors.
  VarNode& operator[](size_t i) { return InternalTLS().RootNode()[i]; }
  VarNode& operator[](std::string const& s) { return InternalTLS().RootNode()[s]; }
  void DenseDoubleVector(size_t dim) { InternalTLS().RootNode().DenseDoubleVector(dim); }

  // After the call to `GetConfig()`, the expression is "frozen", and no new vars or nodes can be added.
  InternalVarsConfig GetConfig() const { return InternalTLS().DoGetConfig(); }

  // Used by the unit test only.
  json::Node UnitTestDump() const { return InternalTLS().RootNode().ConstructTree(); }
};

// Let the user who is `using namespace current::expression` access the vars directly, without any syntactic sugar.
static VarsAccessor x;

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_VARS_VARS_H
