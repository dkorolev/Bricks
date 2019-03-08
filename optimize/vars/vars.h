/*******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Dmitry "Dima" Korolev <dmitry.korolev@gmail.com>
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

#include "../../bricks/exception.h"

#include "../../typesystem/serialization/json.h"
#include "../../typesystem/struct.h"

#include "../../bricks/util/singleton.h"

namespace current {
namespace expression {

struct VarsManagementException final : current::Exception {
  using Exception::Exception;
};

class VarsContext;

class VarsManager final {
 private:
  VarsContext* active_context_ = nullptr;

 public:
  static VarsManager& TLS() { return ThreadLocalSingleton<VarsManager>(); }
  VarsContext*& Active() { return active_context_; }
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
  CURRENT_FIELD(x, double);
  CURRENT_CONSTRUCTOR(X)(double x = 0.0) : x(x) {}
};
CURRENT_STRUCT(V) { CURRENT_FIELD(z, std::vector<Node>); };
CURRENT_STRUCT(I) { CURRENT_FIELD(z, (std::map<uint32_t, Node>)); };
CURRENT_STRUCT(S) { CURRENT_FIELD(z, (std::map<std::string, Node>)); };
}  // namespace current::expression::json

struct VarNode {
  enum class Type { Unset, Vector, IntMap, StringMap, Value };

  Type type = Type::Unset;
  std::vector<VarNode> children_vector;                // `type == Vector`.
  std::map<size_t, VarNode> children_int_map;          // `type == IntMap`.
  std::map<std::string, VarNode> children_string_map;  // `type == StringMap`.
  double value;                                        // `type == Value`.

  void DenseDoubleVector(size_t dim) {
    if (!dim || dim > static_cast<size_t>(1e6)) {
      // NOTE(dkorolev): The `1M` size cutoff is somewhat arbitrary here, but I honestly don't believe
      //                 this optimization code should be used to optimize an 1M++-variables model.
      CURRENT_THROW(VarsManagementException("Attempted to create a dense vector of the wrong size."));
    }
    if (type == Type::Unset) {
      type = Type::Vector;
      children_vector.resize(dim);
    } else if (!(type == Type::Vector && children_vector.size() == dim)) {
      CURRENT_THROW(VarsManagementException("Attempted to create a dense vector in a node that can not be one."));
    }
  }

  VarNode& operator[](size_t i) {
    if (type == Type::Vector) {
      if (i < children_vector.size()) {
        return children_vector[i];
      } else {
        CURRENT_THROW(VarsManagementException("Out of bounds for dense variables node."));
      }
    }
    if (type == Type::Unset) {
      type = Type::IntMap;
    }
    if (type != Type::IntMap) {
      CURRENT_THROW(VarsManagementException("Attempted to access non-dense and non-int-map variables node by index."));
    }
    return children_int_map[i];
  }

  VarNode& operator[](std::string const& s) {
    if (type == Type::Unset) {
      type = Type::StringMap;
    }
    if (type != Type::StringMap) {
      CURRENT_THROW(VarsManagementException("Attempted to access non-string-map variables node by string."));
    }
    return children_string_map[s];
  }

  void operator=(double x) {
    if (type != Type::Unset) {
      CURRENT_THROW(VarsManagementException("Attempted to assign a value to an already assigned node."));
    }
    type = Type::Value;
    value = x;
  }

  json::Node Dump() const {
    if (type == Type::Vector) {
      json::V dense;
      dense.z.reserve(children_vector.size());
      for (VarNode const& c : children_vector) {
        dense.z.push_back(c.Dump());
      }
      return dense;
    } else if (type == Type::IntMap) {
      json::I sparse_by_int;
      for (std::pair<size_t, VarNode> const& e : children_int_map) {
        sparse_by_int.z[static_cast<uint32_t>(e.first)] = e.second.Dump();
      }
      return sparse_by_int;
    } else if (type == Type::StringMap) {
      json::S sparse_by_string;
      for (std::pair<std::string, VarNode> const& e : children_string_map) {
        sparse_by_string.z[e.first] = e.second.Dump();
      }
      return sparse_by_string;
    } else if (type == Type::Value) {
      return json::X(value);
    } else if (type == Type::Unset) {
      return json::U();
    } else {
      CURRENT_THROW(VarsManagementException("Attempted to Dump() an invalid var node."));
    }
  }
};

class VarsContext final {
 private:
  VarNode root_;

 public:
  VarsContext() {
    VarsManager& tls = VarsManager::TLS();
    if (tls.Active()) {
      CURRENT_THROW(VarsManagementException("Attempted to create nested context for variables."));
    }
    tls.Active() = this;
  }
  ~VarsContext() {
    VarsManager& tls = VarsManager::TLS();
    if (!tls.Active() || tls.Active() != this) {
      std::cerr << "Internal error when deleting variables context." << std::endl;
      std::exit(-1);
    }
    tls.Active() = nullptr;
  }
  VarNode& RootNode() {
    VarsManager& tls = VarsManager::TLS();
    if (!tls.Active() || tls.Active() != this) {
      CURRENT_THROW(VarsManagementException("Vars context is required to access them."));
    }
    return root_;
  }
};

struct VarsAccessor final {
  void DenseDoubleVector(size_t dim) { VarsManager::TLS().Active()->RootNode().DenseDoubleVector(dim); }
  VarNode& operator[](size_t i) { return VarsManager::TLS().Active()->RootNode()[i]; }
  VarNode& operator[](std::string const& s) { return VarsManager::TLS().Active()->RootNode()[s]; }
  json::Node Dump() const { return VarsManager::TLS().Active()->RootNode().Dump(); }
};

static struct VarsAccessor c;  // `c` for Constants.
// TODO(dkorolev): `x` for Variables, and `s` for Starting point.

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_VARS_VARS_H
