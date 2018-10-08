//
// Created by Jason Shen on 5/21/18.
//

#include <algorithm>
#include <cassert>
#include <hierarchical_map/leaf_constraint_handler.h>
#include <hierarchical_map/map_cluster.h>
#include <htd/main.hpp>
#include <psdd/psdd_manager.h>
#include <psdd/psdd_node.h>
#include <tuple>
#include <unordered_map>
extern "C" {
#include <sdd/sddapi.h>
}
namespace {
using hierarchical_map::Edge;
using std::map;
using std::max;
using std::min;
using std::pair;
using std::set;
using std::unique_ptr;
using std::unordered_map;
using std::unordered_set;
using std::vector;

std::unordered_map<int32_t, BatchedPsddValue> GetDataMapFromRoute(
    const std::vector<std::vector<Edge *>> &routes,
    const std::unordered_set<Edge *> &internal_edges,
    const std::unordered_map<Edge *, SddLiteral> &edge_variable_map) {
  std::unordered_map<int32_t, BatchedPsddValue> training_data_map;
  for (const auto &single_route : routes) {
    std::unordered_set<Edge *> used_edges(single_route.begin(),
                                          single_route.end());
    for (Edge *cur_internal_edge : internal_edges) {
      if (used_edges.find(cur_internal_edge) == used_edges.end()) {
        training_data_map[edge_variable_map.find(cur_internal_edge)->second]
            .push_back(false);
      } else {
        training_data_map[edge_variable_map.find(cur_internal_edge)->second]
            .push_back(true);
      }
    }
  }
  return training_data_map;
}

std::pair<Edge *, Edge *> make_edge_pair(Edge *edge_a, Edge *edge_b) {
  return std::make_pair(std::min(edge_a, edge_b), std::max(edge_a, edge_b));
}

PsddNode *NegativeTerm(PsddManager *psdd_manager, Vtree *local_vtree) {
  if (sdd_vtree_is_leaf(local_vtree)) {
    return psdd_manager->GetPsddLiteralNode(-sdd_vtree_var(local_vtree), 0);
  } else {
    PsddNode *single_prime =
        NegativeTerm(psdd_manager, sdd_vtree_left(local_vtree));
    PsddNode *single_sub =
        NegativeTerm(psdd_manager, sdd_vtree_right(local_vtree));
    PsddNode *result = psdd_manager->GetConformedPsddDecisionNode(
        {single_prime}, {single_sub}, {PsddParameter::CreateFromDecimal(1)}, 0);
    return result;
  }
}

// Return a negative term if positive_literal does not exist in local_vtree.
// Otherwise, it set the positive_literal true in the term.
PsddNode *OneHopTerm(PsddManager *psdd_manager, Vtree *local_vtree,
                     SddLiteral positive_literal) {
  if (sdd_vtree_is_leaf(local_vtree)) {
    if (sdd_vtree_var(local_vtree) == positive_literal) {
      return psdd_manager->GetPsddLiteralNode(sdd_vtree_var(local_vtree), 0);
    } else {
      return psdd_manager->GetPsddLiteralNode(
          -(SddLiteral)sdd_vtree_var(local_vtree), 0);
    }
  } else {
    PsddNode *single_prime =
        OneHopTerm(psdd_manager, sdd_vtree_left(local_vtree), positive_literal);
    PsddNode *single_sub = OneHopTerm(
        psdd_manager, sdd_vtree_right(local_vtree), positive_literal);
    PsddNode *result = psdd_manager->GetConformedPsddDecisionNode(
        {single_prime}, {single_sub}, {PsddParameter::CreateFromDecimal(1)}, 0);
    return result;
  }
}

Vtree *GenerateRLVtree(const std::vector<SddLiteral> &ordered_variable_indexes,
                       size_t index_offset) {
  if (index_offset == (ordered_variable_indexes.size() - 1)) {
    return new_leaf_vtree(ordered_variable_indexes[index_offset]);
  } else {
    Vtree *shannon_node =
        new_leaf_vtree(ordered_variable_indexes[index_offset]);
    Vtree *right_child =
        GenerateRLVtree(ordered_variable_indexes, index_offset + 1);
    return new_internal_vtree(shannon_node, right_child);
  }
}
} // namespace

namespace hierarchical_map {

MapCluster *MapCluster::MapClusterFromNetworkJson(
    const json &json_spec, ClusterSize cluster_index,
    const std::string &cluster_name,
    const std::unordered_map<std::string, MapCluster *> &constructed_clusters) {
  if (json_spec.find("nodes") != json_spec.end()) {
    // This is a leaf node
    std::unordered_set<NodeSize> nodes;
    for (auto cur_node : json_spec["nodes"]) {
      NodeSize cur_node_index = cur_node;
      nodes.insert(cur_node_index);
    }
    return new MapCluster(cluster_index, cluster_name, nodes, nullptr, nullptr);
  } else {
    // This is an internal node
    assert(json_spec.find("sub_clusters") != json_spec.end());
    assert(json_spec["sub_clusters"].is_array());
    assert(json_spec["sub_clusters"].size() == 2);
    std::string left_child_cluster = json_spec["sub_clusters"][0];
    std::string right_child_cluster = json_spec["sub_clusters"][1];
    auto left_child_it = constructed_clusters.find(left_child_cluster);
    auto right_child_it = constructed_clusters.find(right_child_cluster);
    assert(left_child_it != constructed_clusters.end());
    assert(right_child_it != constructed_clusters.end());
    std::unordered_set<NodeSize> node_indexes_at_cur_region;
    const std::unordered_set<NodeSize> &left_nodes =
        left_child_it->second->nodes();
    const std::unordered_set<NodeSize> &right_nodes =
        right_child_it->second->nodes();
    for (NodeSize i : left_nodes) {
      if (node_indexes_at_cur_region.find(i) ==
          node_indexes_at_cur_region.end()) {
        node_indexes_at_cur_region.insert(i);
      }
    }
    for (NodeSize i : right_nodes) {
      if (node_indexes_at_cur_region.find(i) ==
          node_indexes_at_cur_region.end()) {
        node_indexes_at_cur_region.insert(i);
      }
    }
    return new MapCluster(cluster_index, cluster_name,
                          node_indexes_at_cur_region, left_child_it->second,
                          right_child_it->second);
  }
}
void MapCluster::SetInternalExternalEdgesFromEdgeList(
    const std::vector<Edge *> &edges) {
  std::unordered_set<Edge *> internal_edges;
  std::unordered_map<Edge *, NodeSize> external_edges;
  if (left_child_ == nullptr) {
    // leaf node
    for (Edge *cur_edge : edges) {
      auto x_it = nodes_.find(cur_edge->x_node_index());
      auto y_it = nodes_.find(cur_edge->y_node_index());
      if (x_it != nodes_.end() && y_it != nodes_.end()) {
        internal_edges.insert(cur_edge);
      } else if (x_it != nodes_.end()) {
        external_edges[cur_edge] = *x_it;
      } else if (y_it != nodes_.end()) {
        external_edges[cur_edge] = *y_it;
      }
    }
  } else {
    std::vector<Edge *> left_child_edge_candidates;
    std::vector<Edge *> right_child_edge_candidates;
    const auto &left_child_nodes = left_child_->nodes();
    const auto &right_child_nodes = right_child_->nodes();
    for (Edge *cur_edge : edges) {
      auto left_x_it = left_child_nodes.find(cur_edge->x_node_index());
      auto left_y_it = left_child_nodes.find(cur_edge->y_node_index());
      auto right_x_it = right_child_nodes.find(cur_edge->x_node_index());
      auto right_y_it = right_child_nodes.find(cur_edge->y_node_index());
      if (left_x_it != left_child_nodes.end()) {
        if (left_y_it != left_child_nodes.end()) {
          left_child_edge_candidates.push_back(cur_edge);
        } else if (right_y_it != right_child_nodes.end()) {
          left_child_edge_candidates.push_back(cur_edge);
          right_child_edge_candidates.push_back(cur_edge);
          internal_edges.insert(cur_edge);
        } else {
          left_child_edge_candidates.push_back(cur_edge);
          external_edges[cur_edge] = 0;
        }
      } else if (right_x_it != right_child_nodes.end()) {
        if (left_y_it != left_child_nodes.end()) {
          left_child_edge_candidates.push_back(cur_edge);
          right_child_edge_candidates.push_back(cur_edge);
          internal_edges.insert(cur_edge);
        } else if (right_y_it != right_child_nodes.end()) {
          right_child_edge_candidates.push_back(cur_edge);
        } else {
          right_child_edge_candidates.push_back(cur_edge);
          external_edges[cur_edge] = 1;
        }
      } else {
        if (left_y_it != left_child_nodes.end()) {
          left_child_edge_candidates.push_back(cur_edge);
          external_edges[cur_edge] = 0;
        } else if (right_y_it != right_child_nodes.end()) {
          right_child_edge_candidates.push_back(cur_edge);
          external_edges[cur_edge] = 1;
        } else {
          continue;
        }
      }
    }
    left_child_->SetInternalExternalEdgesFromEdgeList(
        left_child_edge_candidates);
    right_child_->SetInternalExternalEdgesFromEdgeList(
        right_child_edge_candidates);
  }
  lr_cut_edges_ = std::move(internal_edges);
  external_edges_ = std::move(external_edges);
}

MapCluster::MapCluster(ClusterSize cluster_index, std::string cluster_name,
                       std::unordered_set<NodeSize> nodes,
                       MapCluster *left_child, MapCluster *right_child)
    : cluster_index_(cluster_index), cluster_name_(std::move(cluster_name)),
      nodes_(std::move(nodes)), left_child_(left_child),
      right_child_(right_child), external_edges_(), lr_cut_edges_(),
      leaf_internal_path_distribution_(nullptr) {}
const std::unordered_set<NodeSize> &MapCluster::nodes() const { return nodes_; }

Vtree *MapCluster::GenerateLocalVtree(
    const std::unordered_map<MapCluster *, SddLiteral> *cluster_variable_map,
    const std::unordered_map<Edge *, SddLiteral> *edge_variable_map) const {
  if (left_child_ == nullptr) {
    vector<Edge *> internal_edges(lr_cut_edges_.begin(), lr_cut_edges_.end());
    auto leaf_region_graph =
        Graph::GraphFromEdgeList(std::move(internal_edges));
    auto edge_order = leaf_region_graph->GreedyEdgeOrder();
    vector<SddLiteral> ordered_variable_indexes;
    for (Edge *cur_edge : edge_order) {
      ordered_variable_indexes.push_back(
          edge_variable_map->find(cur_edge)->second);
    }
    return GenerateRLVtree(ordered_variable_indexes, 0);
    // return GenerateVtreeUsingMinFillOfPrimalGraph(lr_cut_edges_,
    //                                              edge_variable_map);
  } else {
    std::vector<SddLiteral> sdd_literals_cur_cluster;
    auto left_cluster_it = cluster_variable_map->find(left_child_);
    auto right_cluster_it = cluster_variable_map->find(right_child_);
    assert(left_cluster_it != cluster_variable_map->end());
    assert(right_cluster_it != cluster_variable_map->end());
    sdd_literals_cur_cluster.push_back(left_cluster_it->second);
    sdd_literals_cur_cluster.push_back(right_cluster_it->second);
    for (Edge *cur_internal_edge : lr_cut_edges_) {
      auto internal_edge_it = edge_variable_map->find(cur_internal_edge);
      assert(internal_edge_it != edge_variable_map->end());
      sdd_literals_cur_cluster.push_back(internal_edge_it->second);
    }
    return GenerateRLVtree(sdd_literals_cur_cluster, 0);
  }
}

Vtree *MapCluster::GenerateVtreeUsingMinFillOfPrimalGraph(
    const unordered_set<Edge *> &edges,
    const unordered_map<Edge *, SddLiteral> *edge_variable_map) {
  std::unique_ptr<htd::LibraryInstance> manager(
      htd::createManagementInstance(htd::Id::FIRST));
  htd::MultiHypergraph htd_graph(manager.get());
  unordered_map<NodeSize, htd::vertex_t> node_map;
  for (Edge *cur_edge : edges) {
    auto x_it = node_map.find(cur_edge->x_node_index());
    auto y_it = node_map.find(cur_edge->y_node_index());
    if (x_it == node_map.end()) {
      x_it = node_map.insert({cur_edge->x_node_index(), node_map.size() + 1})
                 .first;
      htd_graph.addVertex();
    }
    if (y_it == node_map.end()) {
      y_it = node_map.insert({cur_edge->y_node_index(), node_map.size() + 1})
                 .first;
      htd_graph.addVertex();
    }
    htd_graph.addEdge(x_it->second, y_it->second);
  }
  htd::MinFillOrderingAlgorithm minfill_manager(manager.get());
  auto ordering = minfill_manager.computeOrdering(htd_graph);
  vector<NodeSize> optimal_elim_order;
  for (auto var_index : ordering->sequence()) {
    optimal_elim_order.push_back((NodeSize)var_index);
  }
  delete (ordering);
  typedef pair<Edge *, SddLiteral> edge_literal_pair;
  vector<set<edge_literal_pair>> node_to_edge_literal_pair(
      (size_t)node_map.size() + 1);
  for (Edge *cur_edge : edges) {
    auto cur_edge_it = edge_variable_map->find(cur_edge);
    assert(cur_edge_it != edge_variable_map->end());
    edge_literal_pair cur_pair(cur_edge, cur_edge_it->second);
    auto x_it = node_map.find(cur_edge->x_node_index());
    auto y_it = node_map.find(cur_edge->y_node_index());
    assert(x_it != node_map.end() && y_it != node_map.end());
    node_to_edge_literal_pair[x_it->second].insert(cur_pair);
    node_to_edge_literal_pair[y_it->second].insert(cur_pair);
  }
  vector<NodeSize> node_index_to_order_index((size_t)node_map.size() + 1);
  for (NodeSize i = 0; i < (NodeSize)optimal_elim_order.size(); ++i) {
    NodeSize node_index = optimal_elim_order[i];
    node_index_to_order_index[node_index] = i;
  }
  vector<vector<pair<Vtree *, set<NodeSize>>>> buckets(
      optimal_elim_order.size() + 1);
  for (NodeSize i = 0; i < (NodeSize)optimal_elim_order.size(); ++i) {
    NodeSize cur_node_index = optimal_elim_order[i];
    const auto &neighboring_edges = node_to_edge_literal_pair[cur_node_index];
    for (const auto &cur_neighbor_edge : neighboring_edges) {
      Edge *cur_edge = cur_neighbor_edge.first;
      auto x_it = node_map.find(cur_edge->x_node_index());
      auto y_it = node_map.find(cur_edge->y_node_index());
      assert(x_it != node_map.end() && y_it != node_map.end());
      NodeSize other_end_index = ((NodeSize)x_it->second == cur_node_index)
                                     ? y_it->second
                                     : x_it->second;
      if (node_index_to_order_index[cur_node_index] <
          node_index_to_order_index[other_end_index]) {
        buckets[i].emplace_back(std::make_pair<Vtree *, set<NodeSize>>(
            new_leaf_vtree(cur_neighbor_edge.second),
            set<NodeSize>({other_end_index, cur_node_index})));
      }
    }
  }
  // Start Eliminating. The set contains the set of nodes which has not yet
  // conditioned.
  for (NodeSize i = 0; i < (NodeSize)optimal_elim_order.size(); ++i) {
    const auto &cur_bucket = buckets[i];
    if (cur_bucket.empty()) {
      continue;
    } else {
      Vtree *next_vtree = cur_bucket[0].first;
      set<NodeSize> next_node_set = cur_bucket[0].second;
      for (NodeSize j = 1; j < (NodeSize)cur_bucket.size(); ++j) {
        next_vtree = new_internal_vtree(cur_bucket[j].first, next_vtree);
        const auto &cur_external_node_set = cur_bucket[j].second;
        for (NodeSize cur_external_node : cur_external_node_set) {
          if (next_node_set.find(cur_external_node) == next_node_set.end()) {
            next_node_set.insert(cur_external_node);
          }
        }
      }
      auto cur_node_it = next_node_set.find(optimal_elim_order[i]);
      assert(cur_node_it != next_node_set.end());
      next_node_set.erase(cur_node_it);
      NodeSize next_bucket_index = (NodeSize)optimal_elim_order.size();
      for (NodeSize next_node_index : next_node_set) {
        if (node_index_to_order_index[next_node_index] < next_bucket_index) {
          next_bucket_index = node_index_to_order_index[next_node_index];
        }
      }
      buckets[next_bucket_index].emplace_back(
          std::make_pair(next_vtree, next_node_set));
    }
  }
  assert(!buckets[optimal_elim_order.size()].empty());
  const auto &last_bucket = buckets[optimal_elim_order.size()];
  Vtree *result_vtree = last_bucket[0].first;
  for (NodeSize i = 1; i < (NodeSize)last_bucket.size(); ++i) {
    result_vtree = new_internal_vtree(last_bucket[i].first, result_vtree);
  }
  set_vtree_properties(result_vtree);
  return result_vtree;
}
void MapCluster::InitConstraint(
    const std::unordered_map<MapCluster *, SddLiteral> *cluster_variable_map,
    const std::unordered_map<Edge *, SddLiteral> *edge_variable_map,
    PsddManager *manager, Vtree *local_vtree,
    LeafConstraintHandler *leaf_constraint_handler) {
  cluster_variable_map_ = cluster_variable_map;
  edge_variable_map_ = edge_variable_map;
  pm_ = manager;
  local_vtree_ = local_vtree;
  if (left_child_ == nullptr) {
    // Load results from leaf_constraint_handler to compilation result fields
    SddManager *sdd_manager_for_leaf =
        leaf_constraint_handler->sdd_manager(this);
    Vtree *vtree_in_sdd_manager = sdd_manager_vtree(sdd_manager_for_leaf);
    // empty_path
    empty_path_constraint_ =
        pm_->FromSdd(leaf_constraint_handler->empty_path_constraint(this),
                     vtree_in_sdd_manager, 0, local_vtree_);
    internal_path_constraint_ =
        pm_->FromSdd(leaf_constraint_handler->internal_path_constraint(this),
                     vtree_in_sdd_manager, 0, local_vtree_);
    const std::map<NodeSize, SddNode *> &terminal_result =
        leaf_constraint_handler->terminal_path_constraint(this);
    const map<pair<NodeSize, NodeSize>, SddNode *> &non_terminal_result =
        leaf_constraint_handler->non_terminal_path_constraint(this);
    for (const auto &cur_external_edge_entry : external_edges_) {
      auto entering_node = cur_external_edge_entry.second;
      auto entering_node_it = terminal_result.find(entering_node);
      assert(entering_node_it != terminal_result.end());
      terminal_path_constraint_[cur_external_edge_entry.first] = pm_->FromSdd(
          entering_node_it->second, vtree_in_sdd_manager, 0, local_vtree_);
    }
    for (auto i_it = external_edges_.begin(); i_it != external_edges_.end();
         ++i_it) {
      auto j_it = i_it;
      std::advance(j_it, 1);
      NodeSize first_entering_node = i_it->second;
      Edge *first_external_edge = i_it->first;
      for (; j_it != external_edges_.end(); ++j_it) {
        NodeSize second_entering_node = j_it->second;
        Edge *second_external_edge = j_it->first;
        pair<NodeSize, NodeSize> node_cache(
            min(first_entering_node, second_entering_node),
            max(first_entering_node, second_entering_node));
        pair<Edge *, Edge *> edge_cache(
            min(first_external_edge, second_external_edge),
            max(first_external_edge, second_external_edge));
        auto node_cache_it = non_terminal_result.find(node_cache);
        assert(node_cache_it != non_terminal_result.end());
        SddNode *cur_non_terminal_constraint = node_cache_it->second;
        non_terminal_path_constraint_[edge_cache] = pm_->FromSdd(
            cur_non_terminal_constraint, vtree_in_sdd_manager, 0, local_vtree_);
      }
    }
  } else {
    // Construct PSDD nodes for these internal cases.
    // construct empty path
    empty_path_constraint_ = ConstructEmptyPathConstraintForInternalCluster();
    internal_path_constraint_ =
        ConstructInternalPathConstraintForInternalCluster();
    for (const auto &external_edge_entry : external_edges_) {
      terminal_path_constraint_[external_edge_entry.first] =
          ConstructTerminalPathConstraintForInternalCluster(
              external_edge_entry.first);
    }
    for (auto i_it = external_edges_.begin(); i_it != external_edges_.end();
         ++i_it) {
      auto j_it = i_it;
      std::advance(j_it, 1);
      Edge *first_external_edge = i_it->first;
      for (; j_it != external_edges_.end(); ++j_it) {
        Edge *second_external_edge = j_it->first;
        PsddNode *cur_non_terminal_node =
            ConstructNonTerminalPathConstraintForInternalCluster(
                first_external_edge, second_external_edge);
        pair<Edge *, Edge *> edge_key(
            min(first_external_edge, second_external_edge),
            max(first_external_edge, second_external_edge));
        non_terminal_path_constraint_[edge_key] = cur_non_terminal_node;
      }
    }
  }
}

PsddNode *MapCluster::internal_path_constraint() const {
  return internal_path_constraint_;
}

PsddNode *MapCluster::terminal_path_constraint(Edge *entering_edge) const {
  auto entering_edge_it = terminal_path_constraint_.find(entering_edge);
  assert(entering_edge_it != terminal_path_constraint_.end());
  return entering_edge_it->second;
}

PsddNode *
MapCluster::non_terminal_path_constraint(Edge *entering_edge_1,
                                         Edge *entering_edge_2) const {
  std::pair<Edge *, Edge *> key(std::min(entering_edge_1, entering_edge_2),
                                std::max(entering_edge_1, entering_edge_2));
  auto key_it = non_terminal_path_constraint_.find(key);
  assert(key_it != non_terminal_path_constraint_.end());
  return key_it->second;
}

PsddNode *MapCluster::empty_path_constraint() const {
  return empty_path_constraint_;
}

PsddNode *MapCluster::ConstructEmptyPathConstraintForInternalCluster() const {
  assert(left_child_ != nullptr && right_child_ != nullptr);
  PsddNode *child_prime = left_child_->empty_path_constraint();
  PsddNode *child_sub = right_child_->empty_path_constraint();
  PsddNode *sub = pm_->GetConformedPsddDecisionNode(
      {child_prime}, {child_sub}, {PsddParameter::CreateFromDecimal(1)}, 0);
  PsddNode *prime = NegativeTerm(pm_, local_vtree_);
  return pm_->GetConformedPsddDecisionNode(
      {prime}, {sub}, {PsddParameter::CreateFromDecimal(1)}, 0);
}
PsddNode *
MapCluster::ConstructInternalPathConstraintForInternalCluster() const {
  // left only
  auto left_cluster_variable_it = cluster_variable_map_->find(left_child_);
  assert(left_cluster_variable_it != cluster_variable_map_->end());
  auto right_cluster_variable_it = cluster_variable_map_->find(right_child_);
  assert(right_cluster_variable_it != cluster_variable_map_->end());
  vector<PsddNode *> primes;
  vector<PsddNode *> subs;
  vector<PsddParameter> params;
  PsddNode *left_only_prime =
      OneHopTerm(pm_, local_vtree_, left_cluster_variable_it->second);
  PsddNode *left_only_child_prime = left_child_->internal_path_constraint();
  PsddNode *left_only_child_sub = right_child_->empty_path_constraint();
  PsddNode *left_only_sub = pm_->GetConformedPsddDecisionNode(
      {left_only_child_prime}, {left_only_child_sub},
      {PsddParameter::CreateFromDecimal(1)}, 0);
  primes.push_back(left_only_prime);
  subs.push_back(left_only_sub);
  params.push_back(PsddParameter::CreateFromDecimal(1));
  // right only
  PsddNode *right_only_prime =
      OneHopTerm(pm_, local_vtree_, right_cluster_variable_it->second);
  PsddNode *right_only_child_prime = left_child_->empty_path_constraint();
  PsddNode *right_only_child_sub = right_child_->internal_path_constraint();
  PsddNode *right_only_sub = pm_->GetConformedPsddDecisionNode(
      {right_only_child_prime}, {right_only_child_sub},
      {PsddParameter::CreateFromDecimal(1)}, 0);
  primes.push_back(right_only_prime);
  subs.push_back(right_only_sub);
  params.push_back(PsddParameter::CreateFromDecimal(1));
  // including exactly one of the internal edge
  for (Edge *cur_internal_edge : lr_cut_edges_) {
    auto cur_internal_edge_it = edge_variable_map_->find(cur_internal_edge);
    assert(cur_internal_edge_it != edge_variable_map_->end());
    PsddNode *cur_prime =
        OneHopTerm(pm_, local_vtree_, cur_internal_edge_it->second);
    PsddNode *cur_sub_child_prime =
        left_child_->terminal_path_constraint(cur_internal_edge);
    PsddNode *cur_sub_child_sub =
        right_child_->terminal_path_constraint(cur_internal_edge);
    PsddNode *cur_sub = pm_->GetConformedPsddDecisionNode(
        {cur_sub_child_prime}, {cur_sub_child_sub},
        {PsddParameter::CreateFromDecimal(1)}, 0);
    primes.push_back(cur_prime);
    subs.push_back(cur_sub);
    params.push_back(PsddParameter::CreateFromDecimal(1));
  }
  PsddNode *result_node =
      pm_->GetConformedPsddDecisionNode(primes, subs, params, 0);
  return result_node;
}
PsddNode *MapCluster::ConstructTerminalPathConstraintForInternalCluster(
    Edge *edge) const {
  auto external_edge_it = external_edges_.find(edge);
  assert(external_edge_it != external_edges_.end());
  vector<PsddNode *> primes;
  vector<PsddNode *> subs;
  vector<PsddParameter> params;
  if (external_edge_it->second == 0) {
    // the external edge enters the leaf region
    // case one none of the internal edges are used
    PsddNode *first_prime = NegativeTerm(pm_, local_vtree_);
    PsddNode *first_sub_child_prime =
        left_child_->terminal_path_constraint(edge);
    PsddNode *first_sub_child_sub = right_child_->empty_path_constraint();
    PsddNode *first_sub = pm_->GetConformedPsddDecisionNode(
        {first_sub_child_prime}, {first_sub_child_sub},
        {PsddParameter::CreateFromDecimal(1)}, 0);
    primes.push_back(first_prime);
    subs.push_back(first_sub);
    params.push_back(PsddParameter::CreateFromDecimal(1));
    // case two exactly one of the internal edges used
    for (Edge *cur_internal_edge : lr_cut_edges_) {
      auto cur_internal_edge_it = edge_variable_map_->find(cur_internal_edge);
      assert(cur_internal_edge_it != edge_variable_map_->end());
      PsddNode *cur_prime =
          OneHopTerm(pm_, local_vtree_, cur_internal_edge_it->second);
      PsddNode *cur_sub_child_prime =
          left_child_->non_terminal_path_constraint(edge, cur_internal_edge);
      PsddNode *cur_sub_child_sub =
          right_child_->terminal_path_constraint(cur_internal_edge);
      PsddNode *cur_sub = pm_->GetConformedPsddDecisionNode(
          {cur_sub_child_prime}, {cur_sub_child_sub},
          {PsddParameter::CreateFromDecimal(1)}, 0);
      primes.push_back(cur_prime);
      subs.push_back(cur_sub);
      params.push_back(PsddParameter::CreateFromDecimal(1));
    }
  } else {
    assert(external_edge_it->second == 1);
    // the external edge enters the right region
    // case one none of the internal edges are used
    PsddNode *first_prime = NegativeTerm(pm_, local_vtree_);
    PsddNode *first_sub_child_prime = left_child_->empty_path_constraint();
    PsddNode *first_sub_child_sub =
        right_child_->terminal_path_constraint(edge);
    PsddNode *first_sub = pm_->GetConformedPsddDecisionNode(
        {first_sub_child_prime}, {first_sub_child_sub},
        {PsddParameter::CreateFromDecimal(1)}, 0);
    primes.push_back(first_prime);
    subs.push_back(first_sub);
    params.push_back(PsddParameter::CreateFromDecimal(1));
    // case two exactly one of the internal edges used
    for (Edge *cur_internal_edge : lr_cut_edges_) {
      auto cur_internal_edge_it = edge_variable_map_->find(cur_internal_edge);
      assert(cur_internal_edge_it != edge_variable_map_->end());
      PsddNode *cur_prime =
          OneHopTerm(pm_, local_vtree_, cur_internal_edge_it->second);
      PsddNode *cur_sub_child_prime =
          left_child_->terminal_path_constraint(cur_internal_edge);
      PsddNode *cur_sub_child_sub =
          right_child_->non_terminal_path_constraint(edge, cur_internal_edge);
      PsddNode *cur_sub = pm_->GetConformedPsddDecisionNode(
          {cur_sub_child_prime}, {cur_sub_child_sub},
          {PsddParameter::CreateFromDecimal(1)}, 0);
      primes.push_back(cur_prime);
      subs.push_back(cur_sub);
      params.push_back(PsddParameter::CreateFromDecimal(1));
    }
  }
  PsddNode *result = pm_->GetConformedPsddDecisionNode(primes, subs, params, 0);
  return result;
}

PsddNode *MapCluster::ConstructNonTerminalPathConstraintForInternalCluster(
    Edge *edge_1, Edge *edge_2) const {
  assert(left_child_ != nullptr && right_child_ != nullptr);
  auto edge_1_it = external_edges_.find(edge_1);
  auto edge_2_it = external_edges_.find(edge_2);
  assert(edge_1_it != external_edges_.end() &&
         edge_2_it != external_edges_.end());
  vector<PsddNode *> primes;
  vector<PsddNode *> subs;
  vector<PsddParameter> params;
  if (edge_1_it->second == 0 && edge_2_it->second == 0) {
    PsddNode *prime = NegativeTerm(pm_, local_vtree_);
    PsddNode *sub_child_prime =
        left_child_->non_terminal_path_constraint(edge_1, edge_2);
    PsddNode *sub_child_sub = right_child_->empty_path_constraint();
    PsddNode *sub = pm_->GetConformedPsddDecisionNode(
        {sub_child_prime}, {sub_child_sub},
        {PsddParameter::CreateFromDecimal(1)}, 0);
    primes.push_back(prime);
    subs.push_back(sub);
    params.push_back(PsddParameter::CreateFromDecimal(1));
  } else if (edge_1_it->second == 0 && edge_2_it->second == 1) {
    for (Edge *cur_internal_edge : lr_cut_edges_) {
      auto cur_internal_edge_it = edge_variable_map_->find(cur_internal_edge);
      assert(cur_internal_edge_it != edge_variable_map_->end());
      PsddNode *cur_prime =
          OneHopTerm(pm_, local_vtree_, cur_internal_edge_it->second);
      PsddNode *cur_sub_child_prime =
          left_child_->non_terminal_path_constraint(edge_1, cur_internal_edge);
      PsddNode *cur_sub_child_sub =
          right_child_->non_terminal_path_constraint(edge_2, cur_internal_edge);
      PsddNode *cur_sub = pm_->GetConformedPsddDecisionNode(
          {cur_sub_child_prime}, {cur_sub_child_sub},
          {PsddParameter::CreateFromDecimal(1)}, 0);
      primes.push_back(cur_prime);
      subs.push_back(cur_sub);
      params.push_back(PsddParameter::CreateFromDecimal(1));
    }
  } else if (edge_1_it->second == 1 && edge_2_it->second == 0) {
    for (Edge *cur_internal_edge : lr_cut_edges_) {
      auto cur_internal_edge_it = edge_variable_map_->find(cur_internal_edge);
      assert(cur_internal_edge_it != edge_variable_map_->end());
      PsddNode *cur_prime =
          OneHopTerm(pm_, local_vtree_, cur_internal_edge_it->second);
      PsddNode *cur_sub_child_prime =
          left_child_->non_terminal_path_constraint(edge_2, cur_internal_edge);
      PsddNode *cur_sub_child_sub =
          right_child_->non_terminal_path_constraint(edge_1, cur_internal_edge);
      PsddNode *cur_sub = pm_->GetConformedPsddDecisionNode(
          {cur_sub_child_prime}, {cur_sub_child_sub},
          {PsddParameter::CreateFromDecimal(1)}, 0);
      primes.push_back(cur_prime);
      subs.push_back(cur_sub);
      params.push_back(PsddParameter::CreateFromDecimal(1));
    }
  } else {
    assert(edge_1_it->second == 1 && edge_2_it->second == 1);
    PsddNode *prime = NegativeTerm(pm_, local_vtree_);
    PsddNode *sub_child_prime = left_child_->empty_path_constraint();
    PsddNode *sub_child_sub =
        right_child_->non_terminal_path_constraint(edge_1, edge_2);
    PsddNode *sub = pm_->GetConformedPsddDecisionNode(
        {sub_child_prime}, {sub_child_sub},
        {PsddParameter::CreateFromDecimal(1)}, 0);
    primes.push_back(prime);
    subs.push_back(sub);
    params.push_back(PsddParameter::CreateFromDecimal(1));
  }
  assert(primes.size() == subs.size() && primes.size() == params.size());
  PsddNode *result = pm_->GetConformedPsddDecisionNode(primes, subs, params, 0);
  return result;
}

vector<SlicedRouteComponent>
MapCluster::SliceRoute(const vector<Edge *> &edges) const {
  vector<Edge *> internal_edges;
  unordered_map<NodeSize, vector<Edge *>> external_edges;
  // unused_external_edges is to store the external edges that has not been
  // added to the SlicedRouteComponent yet.
  std::unordered_set<Edge *> unused_external_edges;
  for (Edge *cur_edge : edges) {
    const auto cur_relevent_nodes = cur_edge->ReleventEndPoints(nodes_);
    if (cur_relevent_nodes.size() == 2) {
      internal_edges.push_back(cur_edge);
      continue;
    }
    if (cur_relevent_nodes.size() == 1) {
      auto external_edges_it = external_edges.find(*cur_relevent_nodes.begin());
      if (external_edges_it == external_edges.end()) {
        external_edges[*cur_relevent_nodes.begin()] = {cur_edge};
      } else {
        external_edges_it->second.push_back(cur_edge);
      }
      unused_external_edges.insert(cur_edge);
      continue;
    }
  }
  std::vector<SlicedRouteComponent> sliced_components;
  auto incident_map = Graph::IncidenceMapFromEdgeList(internal_edges);
  // Boundary nodes for each maximal connected simple routes with internal
  // edges.
  std::vector<NodeSize> boundary_nodes;
  for (const auto &cur_incident_entry : incident_map) {
    if (cur_incident_entry.second.size() == 1) {
      boundary_nodes.push_back(cur_incident_entry.first);
    }
  }
  // Iterate over all boundary nodes.
  std::unordered_set<NodeSize> explored_boundary_nodes;
  for (const NodeSize cur_boundary_node : boundary_nodes) {
    if (explored_boundary_nodes.find(cur_boundary_node) !=
        explored_boundary_nodes.end()) {
      continue;
    }
    explored_boundary_nodes.insert(cur_boundary_node);
    std::vector<Edge *> path_ordered_internal_edges;
    std::vector<Edge *> path_external_edges;
    auto external_edges_it = external_edges.find(cur_boundary_node);
    if (external_edges_it != external_edges.end()) {
      assert(external_edges_it->second.size() == 1);
      path_external_edges.push_back(external_edges_it->second[0]);
      unused_external_edges.erase(external_edges_it->second[0]);
    }
    Edge *last_edge = incident_map[cur_boundary_node][0];
    path_ordered_internal_edges.push_back(last_edge);
    NodeSize next_node = last_edge->OtherEnd(cur_boundary_node);
    while (incident_map[next_node].size() != 1) {
      const auto &neighboring_edges = incident_map[next_node];
      last_edge = neighboring_edges[0] == last_edge ? neighboring_edges[1]
                                                    : neighboring_edges[0];
      path_ordered_internal_edges.push_back(last_edge);
      next_node = last_edge->OtherEnd(next_node);
    }
    // Next node is another boundary_node
    explored_boundary_nodes.insert(next_node);
    external_edges_it = external_edges.find(next_node);
    if (external_edges_it != external_edges.end()) {
      assert(external_edges_it->second.size() == 1);
      path_external_edges.push_back(external_edges_it->second[0]);
      unused_external_edges.erase(external_edges_it->second[0]);
      if (path_external_edges.size() == 1) {
        // Reverse the path_ordered_internal_edges if we have only one external
        // edge that is connected at the end of the internal path.
        std::reverse(path_ordered_internal_edges.begin(),
                     path_ordered_internal_edges.end());
      }
    }
    SlicedRouteComponent sliced_component(
        std::move(path_ordered_internal_edges), std::move(path_external_edges));
    sliced_components.push_back(std::move(sliced_component));
  }
  // Add sliced components which are single external edges
  for (const auto &cur_external_edge_entry : external_edges) {
    if (cur_external_edge_entry.second.size() == 2) {
      // Two external edges forms a simple paths.
      Edge *first_external_edge = cur_external_edge_entry.second[0];
      Edge *second_external_edge = cur_external_edge_entry.second[1];
      assert(unused_external_edges.find(first_external_edge) !=
                 unused_external_edges.end() &&
             unused_external_edges.find(second_external_edge) !=
                 unused_external_edges.end());
      unused_external_edges.erase(first_external_edge);
      unused_external_edges.erase(second_external_edge);
      SlicedRouteComponent sliced_component({}, cur_external_edge_entry.second);
      sliced_components.push_back(std::move(sliced_component));
    } else {
      assert(cur_external_edge_entry.second.size() == 1);
      Edge *cur_external_edge = cur_external_edge_entry.second[0];
      if (unused_external_edges.find(cur_external_edge) !=
          unused_external_edges.end()) {
        unused_external_edges.erase(cur_external_edge);
        SlicedRouteComponent sliced_component({},
                                              cur_external_edge_entry.second);
        sliced_components.push_back(std::move(sliced_component));
      }
    }
  }
  assert(unused_external_edges.empty());
  return sliced_components;
}

// Learning methods
ParameterLearningState MapCluster::UpdateParameterLearningStateSimple(
    const std::vector<std::vector<Edge *>> &routes) const {
  ParameterLearningState learning_state;
  if (left_child_ == nullptr) {
    // Leaf region
    for (const auto &cur_route : routes) {
      std::vector<SlicedRouteComponent> sliced_components =
          SliceRoute(cur_route);
      for (const auto &cur_slice : sliced_components) {
        if (cur_slice.external_edges.size() == 0) {
          // Adds the route into the internal feature state
          learning_state.param_learning_state_internal.routes_in_leaf_cluster
              .push_back(std::move(cur_slice.internal_edges));
          continue;
        }
        if (cur_slice.external_edges.size() == 1) {
          // Adds the route into the terminal feature state
          Edge *external_edge = cur_slice.external_edges[0];
          learning_state.param_learning_state_terminal[external_edge]
              .routes_in_leaf_cluster.push_back(
                  std::move(cur_slice.internal_edges));
          continue;
        }
        // Adds the route into the non terminal feature state
        assert(cur_slice.external_edges.size() == 2);
        Edge *external_edge_a = cur_slice.external_edges[0];
        Edge *external_edge_b = cur_slice.external_edges[1];
        auto pair_key = make_edge_pair(external_edge_a, external_edge_b);
        learning_state.param_learning_state_non_terminal[pair_key]
            .routes_in_leaf_cluster.push_back(
                std::move(cur_slice.internal_edges));
      }
    }
  } else {
    // Internal region
    for (const auto &cur_route : routes) {
      std::vector<SlicedRouteComponent> sliced_components =
          SliceRoute(cur_route);
      for (const auto &cur_slice : sliced_components) {
        if (cur_slice.external_edges.size() == 0) {
          std::vector<Edge *> used_lr_cut_edges;
          for (Edge *cur_edge : cur_slice.internal_edges) {
            if (lr_cut_edges_.find(cur_edge) != lr_cut_edges_.end()) {
              used_lr_cut_edges.push_back(cur_edge);
            }
          }
          if (used_lr_cut_edges.size() == 0) {
            if (cur_slice.internal_edges.size() != 0) {
              if (left_child_->nodes().find(
                      cur_slice.internal_edges[0]->x_node_index()) !=
                  left_child_->nodes().end()) {
                learning_state.param_learning_state_internal
                    .left_internal_freq += 1;
              } else {
                learning_state.param_learning_state_internal
                    .right_internal_freq += 1;
              }
            }
          } else {
            for (Edge *cur_edge : used_lr_cut_edges) {
              learning_state.param_learning_state_internal
                  .lr_cut_edges_freq[cur_edge] += 1;
            }
          }
        } else if (cur_slice.external_edges.size() == 1) {
          Edge *cur_external_edge = cur_slice.external_edges[0];
          int lr_cut_edges_size = cur_slice.internal_edges.size();
          int i = 0;
          while (i < lr_cut_edges_size) {
            Edge *cur_internal_edge = cur_slice.internal_edges[i];
            if (lr_cut_edges_.find(cur_internal_edge) != lr_cut_edges_.end()) {
              learning_state.param_learning_state_terminal[cur_external_edge]
                  .lr_cut_edges_freq[cur_internal_edge]++;
              break;
            }
            ++i;
          }
          if (i == lr_cut_edges_size) {
            // The external edge is not connected with any lr-cut-edge of this
            // cluster
            learning_state.param_learning_state_terminal[cur_external_edge]
                .zero_lr_cut_edge_freq++;
          }
          // Creates internal route features for the remaining lr-cut-edges in
          // the slice
          for (; i < lr_cut_edges_size; ++i) {
            Edge *cur_internal_edge = cur_slice.internal_edges[i];
            if (lr_cut_edges_.find(cur_internal_edge) != lr_cut_edges_.end()) {
              learning_state.param_learning_state_internal
                  .lr_cut_edges_freq[cur_internal_edge]++;
            }
          }
        } else {
          assert(cur_slice.external_edges.size() == 2);
          std::vector<Edge *> lr_cut_edges;
          for (Edge *cur_edge : cur_slice.internal_edges) {
            if (lr_cut_edges_.find(cur_edge) != lr_cut_edges_.end()) {
              lr_cut_edges.push_back(cur_edge);
            }
          }
          if (lr_cut_edges.size() == 0) {
            auto external_edge_pair = make_edge_pair(
                cur_slice.external_edges[0], cur_slice.external_edges[1]);
            learning_state.param_learning_state_non_terminal[external_edge_pair]
                .zero_lr_cut_edge_freq++;
          } else if (lr_cut_edges.size() == 1) {
            auto external_edge_pair = make_edge_pair(
                cur_slice.external_edges[0], cur_slice.external_edges[1]);
            learning_state.param_learning_state_non_terminal[external_edge_pair]
                .lr_cut_edges_freq[lr_cut_edges[0]]++;
          } else {
            // We create:
            // a terminal case (external_edge[0], internal_edge[0])
            // a terminal case (external_edge[1], internal_edge[k])
            // internal cases where each is internal_edge[i] having 0 < i < k.
            learning_state
                .param_learning_state_terminal[cur_slice.external_edges[0]]
                .lr_cut_edges_freq[lr_cut_edges[0]]++;
            learning_state
                .param_learning_state_terminal[cur_slice.external_edges[1]]
                .lr_cut_edges_freq[lr_cut_edges.back()]++;
            int lr_cut_edge_size = lr_cut_edges.size();
            for (int i = 1; i < lr_cut_edge_size - 1; ++i) {
              learning_state.param_learning_state_internal
                  .lr_cut_edges_freq[lr_cut_edges[i]]++;
            }
          }
        }
      }
    }
  }
  return learning_state;
}

void MapCluster::LearnParameters(const std::vector<std::vector<Edge *>> &routes,
                                 PsddManager *manager, PsddParameter alpha) {
  auto learning_state = UpdateParameterLearningStateSimple(routes);
  if (left_child_ != nullptr) {
    // Learn parameters in internal cluster
    // Internal route parameter
    PsddParameter internal_total = PsddParameter::CreateFromDecimal(0);
    internal_parameter_.left_internal_freq =
        PsddParameter::CreateFromDecimal(
            learning_state.param_learning_state_internal.left_internal_freq) +
        alpha;
    internal_total += internal_parameter_.left_internal_freq;
    internal_parameter_.right_internal_freq =
        alpha +
        PsddParameter::CreateFromDecimal(
            learning_state.param_learning_state_internal.right_internal_freq);
    internal_total += internal_parameter_.right_internal_freq;
    for (Edge *cur_lr_cut_edge : lr_cut_edges_) {
      const PsddParameter cur_param =
          alpha + PsddParameter::CreateFromDecimal(
                      learning_state.param_learning_state_internal
                          .lr_cut_edges_freq[cur_lr_cut_edge]);
      internal_parameter_.lr_cut_edges_freq[cur_lr_cut_edge] = cur_param;
      internal_total += cur_param;
    }
    internal_parameter_.left_internal_freq /= internal_total;
    internal_parameter_.right_internal_freq /= internal_total;
    for (Edge *cur_lr_cut_edge : lr_cut_edges_) {
      internal_parameter_.lr_cut_edges_freq[cur_lr_cut_edge] /= internal_total;
    }
    // Terminal route parameter
    for (auto external_edge_pair : external_edges_) {
      Edge *cur_external_edge = external_edge_pair.first;
      std::unordered_map<Edge *, InternalClusterParameterPerCase>::iterator
          cur_param_it;
      InternalClusterParameterPerCase cur_terminal_parameter;
      auto cur_case_it =
          learning_state.param_learning_state_terminal.find(cur_external_edge);
      if (cur_case_it == learning_state.param_learning_state_terminal.end()) {
        auto case_size = lr_cut_edges_.size() + 1;
        PsddParameter uniform_parameter =
            PsddParameter::CreateFromDecimal(1.0 / case_size);
        cur_terminal_parameter.zero_lr_cut_edge_freq = uniform_parameter;
        for (Edge *cur_edge : lr_cut_edges_) {
          cur_terminal_parameter.lr_cut_edges_freq[cur_edge] =
              uniform_parameter;
        }
      } else {
        PsddParameter cur_terminal_total = PsddParameter::CreateFromDecimal(0);
        cur_terminal_parameter.zero_lr_cut_edge_freq =
            PsddParameter::CreateFromDecimal(
                cur_case_it->second.zero_lr_cut_edge_freq) +
            alpha;
        cur_terminal_total += cur_terminal_parameter.zero_lr_cut_edge_freq;
        for (Edge *cur_edge : lr_cut_edges_) {
          PsddParameter cur_weight =
              PsddParameter::CreateFromDecimal(
                  cur_case_it->second.lr_cut_edges_freq[cur_edge]) +
              alpha;
          cur_terminal_parameter.lr_cut_edges_freq[cur_edge] = cur_weight;
          cur_terminal_total += cur_weight;
        }
        cur_terminal_parameter.zero_lr_cut_edge_freq /= cur_terminal_total;
        for (Edge *cur_edge : lr_cut_edges_) {
          cur_terminal_parameter.lr_cut_edges_freq[cur_edge] /=
              cur_terminal_total;
        }
      }
      terminal_parameter_[cur_external_edge] =
          std::move(cur_terminal_parameter);
    }
    // Nonterminal route parameter
    for (auto i_it = external_edges_.begin(); i_it != external_edges_.end();
         ++i_it) {
      auto j_it = i_it;
      std::advance(j_it, 1);
      NodeSize first_entering_node = i_it->second;
      Edge *first_external_edge = i_it->first;
      for (; j_it != external_edges_.end(); ++j_it) {
        NodeSize second_entering_node = j_it->second;
        Edge *second_external_edge = j_it->first;
        auto edge_pair =
            make_edge_pair(first_external_edge, second_external_edge);
        InternalClusterParameterPerCase cur_non_terminal_parameter;
        if (first_entering_node == second_entering_node) {
          cur_non_terminal_parameter.zero_lr_cut_edge_freq =
              PsddParameter::CreateFromDecimal(1.0);
        } else {
          auto cur_learning_state_it =
              learning_state.param_learning_state_non_terminal.find(edge_pair);
          if (cur_learning_state_it ==
              learning_state.param_learning_state_non_terminal.end()) {
            // Set a uniform parameters of all lr_cut_edges.
            auto lr_cut_edge_size = lr_cut_edges_.size();
            PsddParameter uniform_pr =
                PsddParameter::CreateFromDecimal(1.0 / lr_cut_edge_size);
            for (Edge *cur_edge : lr_cut_edges_) {
              cur_non_terminal_parameter.lr_cut_edges_freq[cur_edge] =
                  uniform_pr;
            }
          } else {
            PsddParameter cur_non_terminal_total =
                PsddParameter::CreateFromDecimal(0);
            for (Edge *cur_edge : lr_cut_edges_) {
              PsddParameter cur_weight = PsddParameter::CreateFromDecimal(
                                             cur_learning_state_it->second
                                                 .lr_cut_edges_freq[cur_edge]) +
                                         alpha;
              cur_non_terminal_parameter.lr_cut_edges_freq[cur_edge] =
                  cur_weight;
              cur_non_terminal_total += cur_weight;
            }
            for (Edge *cur_edge : lr_cut_edges_) {
              cur_non_terminal_parameter.lr_cut_edges_freq[cur_edge] /=
                  cur_non_terminal_total;
            }
          }
        }
        non_terminal_parameter_[edge_pair] = cur_non_terminal_parameter;
      }
    }
  } else {
    // Leaf cluster
    // Internal routes
    assert(internal_path_constraint_ != nullptr);
    std::unordered_map<int32_t, BatchedPsddValue> internal_training_values =
        GetDataMapFromRoute(
            learning_state.param_learning_state_internal.routes_in_leaf_cluster,
            lr_cut_edges_, *edge_variable_map_);
    leaf_internal_path_distribution_ = manager->LearnPsddParameters(
        internal_path_constraint_, internal_training_values,
        learning_state.param_learning_state_internal.routes_in_leaf_cluster
            .size(),
        alpha, /*flag_index=*/1);
    // Terminal route parameter
    for (auto external_edge_pair : external_edges_) {
      Edge *cur_external_edge = external_edge_pair.first;
      std::unordered_map<int32_t, BatchedPsddValue> terminal_training_values;
      size_t data_size = 0;
      auto cur_case_it =
          learning_state.param_learning_state_terminal.find(cur_external_edge);
      if (cur_case_it != learning_state.param_learning_state_terminal.end()) {
        terminal_training_values =
            GetDataMapFromRoute(cur_case_it->second.routes_in_leaf_cluster,
                                lr_cut_edges_, *edge_variable_map_);
        data_size = cur_case_it->second.routes_in_leaf_cluster.size();
      }
      PsddNode *cur_terminal_trained_psdd = manager->LearnPsddParameters(
          terminal_path_constraint_[cur_external_edge],
          terminal_training_values, data_size, alpha, /*flag_index=*/1);
      leaf_terminal_path_distribution_[cur_external_edge] =
          cur_terminal_trained_psdd;
    }
    // Nonterminal route parameter
    for (auto i_it = external_edges_.begin(); i_it != external_edges_.end();
         ++i_it) {
      auto j_it = i_it;
      std::advance(j_it, 1);
      Edge *first_external_edge = i_it->first;
      for (; j_it != external_edges_.end(); ++j_it) {
        Edge *second_external_edge = j_it->first;
        auto edge_pair =
            make_edge_pair(first_external_edge, second_external_edge);
        std::unordered_map<int32_t, BatchedPsddValue>
            non_terminal_training_values;
        size_t data_size = 0;
        auto cur_learning_state_it =
            learning_state.param_learning_state_non_terminal.find(edge_pair);
        if (cur_learning_state_it !=
            learning_state.param_learning_state_non_terminal.end()) {
          non_terminal_training_values = GetDataMapFromRoute(
              cur_learning_state_it->second.routes_in_leaf_cluster,
              lr_cut_edges_, *edge_variable_map_);
          data_size =
              cur_learning_state_it->second.routes_in_leaf_cluster.size();
        }
        PsddNode *cur_non_terminal_trained_psdd = manager->LearnPsddParameters(
            non_terminal_path_constraint_[edge_pair],
            non_terminal_training_values, data_size, alpha, /*flag_index=*/1);
        leaf_non_terminal_path_distribution_[edge_pair] =
            cur_non_terminal_trained_psdd;
      }
    }
  }
}

std::set<NodeSize> MapCluster::EntryPoints() const {
  std::set<NodeSize> entry_points;
  for (const auto &external_entry : external_edges_) {
    if (entry_points.find(external_entry.second) == entry_points.end()) {
      entry_points.insert(external_entry.second);
    }
  }
  return entry_points;
}

std::set<std::pair<NodeSize, NodeSize>>
MapCluster::EntryPointPairsForNonTerminalPaths() const {
  std::set<std::pair<NodeSize, NodeSize>> entering_points;
  for (auto i_it = external_edges_.begin(); i_it != external_edges_.end();
       ++i_it) {
    auto j_it = i_it;
    std::advance(j_it, 1);
    NodeSize first_entering_node = i_it->second;
    for (; j_it != external_edges_.end(); ++j_it) {
      NodeSize second_entering_node = j_it->second;
      std::pair<NodeSize, NodeSize> node_pair(
          std::min(first_entering_node, second_entering_node),
          std::max(first_entering_node, second_entering_node));
      if (entering_points.find(node_pair) == entering_points.end()) {
        entering_points.insert(node_pair);
      }
    }
  }
  return entering_points;
}

Probability
MapCluster::Evaluate(const std::vector<std::vector<Edge *>> &routes) {
  auto learning_state = UpdateParameterLearningStateSimple(routes);
  double log_pr = 0;
  if (left_child_ != nullptr) {
    // internal cases
    const auto &internal_states = learning_state.param_learning_state_internal;
    log_pr += internal_states.left_internal_freq *
              internal_parameter_.left_internal_freq.parameter();
    log_pr += internal_states.right_internal_freq *
              internal_parameter_.right_internal_freq.parameter();
    for (const auto &edge_freq_pair : internal_states.lr_cut_edges_freq) {
      log_pr += edge_freq_pair.second *
                internal_parameter_.lr_cut_edges_freq[edge_freq_pair.first]
                    .parameter();
    }
    // terminal case
    const auto &terminal_states = learning_state.param_learning_state_terminal;
    for (const auto &external_edge_and_state_pair : terminal_states) {
      const auto &cur_param =
          terminal_parameter_[external_edge_and_state_pair.first];
      log_pr += external_edge_and_state_pair.second.zero_lr_cut_edge_freq *
                cur_param.zero_lr_cut_edge_freq.parameter();
      for (const auto &lr_cut_edge_and_state_pair :
           external_edge_and_state_pair.second.lr_cut_edges_freq) {
        log_pr +=
            lr_cut_edge_and_state_pair.second *
            cur_param.lr_cut_edges_freq.find(lr_cut_edge_and_state_pair.first)
                ->second.parameter();
      }
    }
    // non terminal case
    const auto &non_terminal_states =
        learning_state.param_learning_state_non_terminal;
    for (const auto &external_edge_pair_and_state_pair : non_terminal_states) {
      const auto &cur_param =
          non_terminal_parameter_[external_edge_pair_and_state_pair.first];
      if (cur_param.zero_lr_cut_edge_freq ==
          PsddParameter::CreateFromDecimal(0)) {
        // Two external edges enter different regions
        for (const auto &lr_cut_edge_and_state_pair :
             external_edge_pair_and_state_pair.second.lr_cut_edges_freq) {
          log_pr += lr_cut_edge_and_state_pair.second *
                    cur_param.lr_cut_edges_freq
                        .find(lr_cut_edge_and_state_pair.first)
                        ->second.parameter();
        }
      } else {
        // Two external edges enter the same region
        log_pr +=
            external_edge_pair_and_state_pair.second.zero_lr_cut_edge_freq *
            cur_param.zero_lr_cut_edge_freq.parameter();
      }
    }
    return Probability::CreateFromLog(log_pr);
  } else {
    // Leaf region
    // internal routes
    double log_pr = 0;
    const auto &internal_state = learning_state.param_learning_state_internal;
    std::bitset<MAX_VAR> variable_mask;
    for (Edge *cur_edge : lr_cut_edges_) {
      variable_mask.set(edge_variable_map_->find(cur_edge)->second);
    }
    for (const auto &cur_route : internal_state.routes_in_leaf_cluster) {
      std::bitset<MAX_VAR> instantiation;
      for (Edge *cur_edge : cur_route) {
        instantiation.set(edge_variable_map_->find(cur_edge)->second);
      }
      auto cur_pr = psdd_node_util::Evaluate(variable_mask, instantiation,
                                             leaf_internal_path_distribution_);
      log_pr += cur_pr.parameter();
    }
    // terminal routes
    const auto &terminal_states = learning_state.param_learning_state_terminal;
    for (const auto &external_edge_and_state_pair : terminal_states) {
      PsddNode *cur_leaf_param =
          leaf_terminal_path_distribution_[external_edge_and_state_pair.first];
      for (const auto &cur_route :
           external_edge_and_state_pair.second.routes_in_leaf_cluster) {
        std::bitset<MAX_VAR> instantiation;
        for (Edge *cur_edge : cur_route) {
          instantiation.set(edge_variable_map_->find(cur_edge)->second);
        }
        auto cur_pr = psdd_node_util::Evaluate(variable_mask, instantiation,
                                               cur_leaf_param);
        log_pr += cur_pr.parameter();
      }
    }
    // non terminal case
    const auto &non_terminal_states =
        learning_state.param_learning_state_non_terminal;
    for (const auto &external_edge_pair_and_state_pair : non_terminal_states) {
      PsddNode *cur_leaf_param =
          leaf_non_terminal_path_distribution_[external_edge_pair_and_state_pair
                                                   .first];
      for (const auto &cur_route :
           external_edge_pair_and_state_pair.second.routes_in_leaf_cluster) {
        std::bitset<MAX_VAR> instantiation;
        for (Edge *cur_edge : cur_route) {
          instantiation.set(edge_variable_map_->find(cur_edge)->second);
        }
        auto cur_pr = psdd_node_util::Evaluate(variable_mask, instantiation,
                                               cur_leaf_param);
        log_pr += cur_pr.parameter();
      }
    }
    return Probability::CreateFromLog(log_pr);
  }
}

} // namespace hierarchical_map
