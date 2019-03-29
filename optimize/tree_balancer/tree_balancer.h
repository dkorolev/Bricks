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

#ifndef OPTIMIZE_TREE_BALANCER_TREE_BALANCER_H
#define OPTIMIZE_TREE_BALANCER_TREE_BALANCER_H

#include <stack>

#include "../expression/expression.h"

namespace current {
namespace expression {

inline size_t ExpressionTreeHeight(ExpressionNodeIndex index,
                                   VarsContext const& vars_context = VarsManager::TLS().Active()) {
  std::stack<std::pair<size_t, size_t>> stack;
  size_t max_depth = 0u;

  auto const PushToStack = [&stack, &max_depth](ExpressionNodeIndex index, size_t current_depth) {
    max_depth = std::max(current_depth, max_depth);
    if (index.UncheckedIsSpecificallyNodeIndex()) {
      stack.emplace(index.UncheckedNodeIndex(), current_depth);
    }
  };

  PushToStack(index, 1u);

#if 0
    // There are no cycles, but the same node can be reached via different paths.
    // Do not re-visit the nodes unless the new path from the root is longer (thus increasing the depth).
    // NOTE(dkorolev): Disabled this as the performance is already O(nodes), and I'm focusing on fewer RAM allocations.
  std::vector<size_t> seen_at_depth(vars_context.NumberOfNodes());
#endif

  while (!stack.empty()) {
    size_t const current_index = stack.top().first;
    size_t const current_depth = stack.top().second;
    stack.pop();

#if 0
    // There are no cycles, but the same node can be reached via different paths.
    // Do not re-visit the nodes unless the new path from the root is longer (thus increasing the depth).
    // NOTE(dkorolev): Disabled this as the performance is already O(nodes), and I'm focusing on fewer RAM allocations.
    if (seen_at_depth[current_index] >= current_depth) {
      continue;
    }
    seen_at_depth[current_index] = current_depth;
#endif

    ExpressionNodeImpl const& node = vars_context[current_index];
    ExpressionNodeType const node_type = node.Type();
    if (IsOperationNode(node_type)) {
      PushToStack(node.LHSIndex(), current_depth + 1u);
      PushToStack(node.RHSIndex(), current_depth + 1u);
    } else if (IsFunctionNode(node_type)) {
      PushToStack(node.ArgumentIndex(), current_depth + 1u);
    } else {
#ifndef NDEBUG
      TriggerSegmentationFault();
#endif
    }
  }

  return max_depth;
}

// A collection of node indexes united by `+` or by `*`, for further rebalancing.
class NodesCluster {
 private:
  VarsContext& vars_context_;
  std::vector<size_t> nodes_;                // The indexes of the nodes that form the cluster of `+` or `*` nodes.
  std::vector<ExpressionNodeIndex> leaves_;  // The nodes that are the "leaves" of this cluster, left to right.
  size_t max_height_ = 0u;                   // The height of this expression subtree, to decide if it needs balancing.

#if 0
  // A canonical, recursive implementation, which would overflow the stack in the largest tests from `test.cc`.
  void DoBuild(ExpressionNodeIndex index, ExpressionNodeType desired_node_type, size_t height) {
    max_height_ = std::max(max_height_, height);
    if (!index.template Dispatch<bool>(
            [this, desired_node_type, height](size_t node_index) -> bool {
              ExpressionNodeImpl const& node = vars_context_[node_index];
              if (node.Type() == desired_node_type) {
                nodes_.push_back(node_index);
                DoBuild(node.LHSIndex(), desired_node_type, height + 1u);
                DoBuild(node.RHSIndex(), desired_node_type, height + 1u);
                return true;
              } else {
                return false;
              }
            },
            [](size_t) -> bool { return false; },
            [](double) -> bool { return false; },
            []() -> bool { return false; })) {
      leaves_.push_back(index);
    }
  }
#else
  // An implementation without recursion, that uses a manually allocated stack.
  void DoBuild(ExpressionNodeIndex starting_index, ExpressionNodeType desired_node_type, size_t starting_height) {
    // NOTE(dkorolev): Technically, we don't need the separate `stack` here, just the index in the `nodes_` array,
    // but then a separate array of their depths will be required. To avoid that separate array of depths the solution
    // is to keep the stack size (computed in O(1) CPU and O(0) RAM as the difference between two indexes) equal to the
    // height of the presently considered node, but _this_, in its turn, would require the use of the special bit,
    // because the "recursive" LHS and RHS calls would have to be "made" "sequentially", not pushed into the stack
    // at the same time when considering the parent node.
    // Thus, that logic, mildly more efficient in RAM, and, _maybe_, more effective overall would be unreadable.
    // Instead, go with the simple and bulletproof one with a single stack of pairs of { node, height }.
    std::stack<std::pair<ExpressionNodeIndex, size_t>> stack;
    stack.emplace(starting_index, starting_height);
    while (!stack.empty()) {
      ExpressionNodeIndex const index = stack.top().first;
      size_t const height = stack.top().second;
      stack.pop();
      max_height_ = std::max(max_height_, height);
      bool done = false;
      if (index.UncheckedIsSpecificallyNodeIndex()) {
        size_t const node_index = static_cast<size_t>(index.UncheckedNodeIndex());
        ExpressionNodeImpl const& node = vars_context_[node_index];
        if (node.Type() == desired_node_type) {
          nodes_.push_back(node_index);
          // NOTE(dkorolev): Essential to flip the order of LHS and RHS here!
          stack.emplace(node.RHSIndex(), height + 1u);
          stack.emplace(node.LHSIndex(), height + 1u);
          done = true;
        }
      }
      if (!done) {
        leaves_.push_back(index);
      }
    }
  }
#endif

  // This call is OK to be recursive, as its depth is O(log(nodes)) in this cluster.
  void DoRecursiveRebalance(size_t node_begin, size_t node_end, size_t leaf_begin, size_t leaf_end) {
#ifndef NDEBUG
    if (!(node_end > node_begin)) {
      TriggerSegmentationFault();
    }
    if (!(leaf_end > leaf_begin)) {
      TriggerSegmentationFault();
    }
    if (!((node_end - node_begin) + 1u == (leaf_end - leaf_begin))) {
      TriggerSegmentationFault();
    }
#endif
    // Never called on an empty set of nodes.
    if (node_end == node_begin + 1u) {
      // One node, to leaves.
      vars_context_.MutableNodeByIndex(nodes_[node_begin]).InitLHSRHS(leaves_[leaf_begin], leaves_[leaf_begin + 1u]);
    } else if (node_end == node_begin + 2u) {
      // Two nodes, three leaves. Represent them left-to-right, as the "natural" order would.
      vars_context_.MutableNodeByIndex(nodes_[node_begin])
          .InitLHSRHS(ExpressionNodeIndex::FromNodeIndex(nodes_[node_begin + 1u]), leaves_[leaf_end - 1u]);
      DoRecursiveRebalance(node_begin + 1u, node_end, leaf_begin, leaf_end - 1u);
    } else {
      // Three or more nodes, four or more leaves.
      size_t const leaf_midpoint = (leaf_begin + leaf_end + 1u) / 2u;
      size_t const node_midpoint = node_end - (leaf_end - leaf_midpoint) + 1;
      vars_context_.MutableNodeByIndex(nodes_[node_begin])
          .InitLHSRHS(ExpressionNodeIndex::FromNodeIndex(nodes_[node_begin + 1u]),
                      ExpressionNodeIndex::FromNodeIndex(nodes_[node_midpoint]));
      DoRecursiveRebalance(node_begin + 1u, node_midpoint, leaf_begin, leaf_midpoint);
      DoRecursiveRebalance(node_midpoint, node_end, leaf_midpoint, leaf_end);
    }
  }

 public:
  NodesCluster(VarsContext& vars_context) : vars_context_(vars_context) {}

  void Build(ExpressionNodeIndex index, ExpressionNodeType desired_node_type) {
    nodes_.clear();
    leaves_.clear();
#ifndef NDEBUG
    if (!IsOperationNode(desired_node_type)) {
      TriggerSegmentationFault();
    }
#endif
    DoBuild(index, desired_node_type, 1u);
  }

  bool NeedsRebalancing() const {
    // Needs rebalancing if the actual height of this subtree of the expression tree exceeds the perfect one.
    // The perfectly attainable height is (1 + log_2(number of leaves)).
    return max_height_ > static_cast<size_t>(1.0 + ceil((log(leaves_.size()) / log(2.0)) - 1e-9));
  }

  std::vector<ExpressionNodeIndex> const& Leaves() const { return leaves_; }

#ifndef NDEBUG
  void AssertFirstNodeIs(size_t first_node) {
    if (!(!nodes_.empty() && nodes_.front() == first_node)) {
      TriggerSegmentationFault();
    }
  }
#endif

  void Rebalance() {
#ifndef NDEBUG
    if (!(leaves_.size() == nodes_.size() + 1u)) {
      TriggerSegmentationFault();
    }
#endif
    DoRecursiveRebalance(0u, nodes_.size(), 0u, leaves_.size());
  }
};

inline void BalanceExpressionTree(ExpressionNodeIndex index, VarsContext& vars_context = VarsManager::TLS().Active()) {
  std::stack<size_t> stack;

  auto const PushToStack = [&stack](ExpressionNodeIndex index) {
    if (index.UncheckedIsSpecificallyNodeIndex()) {
      stack.push(index.UncheckedNodeIndex());
    }
  };

  PushToStack(index);

  while (!stack.empty()) {
    size_t const current_index = stack.top();
    stack.pop();

    ExpressionNodeImpl const& node = vars_context[current_index];
    ExpressionNodeType const node_type = node.Type();
    if (node_type == ExpressionNodeType::Operation_add || node_type == ExpressionNodeType::Operation_mul) {
      // Assemble the cluster. Invariant: It will always be M nodes and (M+1) leaves.
      NodesCluster cluster(vars_context);
      cluster.Build(ExpressionNodeIndex::FromNodeIndex(current_index), node_type);
      if (cluster.NeedsRebalancing()) {
#ifndef NDEBUG
        cluster.AssertFirstNodeIs(current_index);
#endif
        cluster.Rebalance();
        for (ExpressionNodeIndex const i : cluster.Leaves()) {
          PushToStack(i);
        }
      } else {
        PushToStack(node.LHSIndex());
        PushToStack(node.RHSIndex());
      }
    } else if (IsOperationNode(node_type)) {
      PushToStack(node.LHSIndex());
      PushToStack(node.RHSIndex());
    } else if (IsFunctionNode(node_type)) {
      PushToStack(node.ArgumentIndex());
    } else {
#ifndef NDEBUG
      TriggerSegmentationFault();
#endif
    }
  }
}

}  // namespace current::expression
}  // namespace current

#endif  // #ifndef OPTIMIZE_TREE_BALANCER_TREE_BALANCER_H
