//
// Created by Jason Shen on 5/21/18.
//

#ifndef HIERARCHICAL_MAP_MAP_CLUSTER_H
#define HIERARCHICAL_MAP_MAP_CLUSTER_H

#include <gtest/gtest_prod.h>
#include <hierarchical_map/graph.h>
#include <hierarchical_map/types.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <psdd/psdd_manager.h>
#include <unordered_map>
#include <unordered_set>

namespace hierarchical_map {
class LeafConstraintHandler;

// Learning states, and we used this state to estimate
// parameters. Different subset of fields are used for different contexts.
// For internal cluster:
//   If two external edges are used, a.k.a. non_terminal_path,
//     lr_cut_edges_freq_ is used.
//   If only one external edge is used, a.k.a. terminal_path,
//     both lr_cut_edges_freq_ and zero_lr_cut_edge_freq_ is used.
//   If no external edge is used,
//     all of lr_cut_edges_freq_, left_internal_freq_ and right_internal_freq_
//     are used.
// For leaf cluster:
//   All cases:
//     routes_in_leaf_cluster_

struct ParameterLearningState {
  std::unordered_map<Edge *, double> lr_cut_edges_freq_;
  double zero_lr_cut_edge_freq_ = 0;
  double left_internal_freq_ = 0;
  double right_internal_freq_ = 0;
  std::vector<std::vector<Edge *>> routes_in_leaf_cluster_;
};

struct SlicedRouteComponent {
  SlicedRouteComponent(std::vector<Edge *> arg_internal_edges,
                       std::vector<Edge *> arg_external_edges)
      : internal_edges(std::move(arg_internal_edges)),
        external_edges(std::move(arg_external_edges)) {}
  std::vector<Edge *> internal_edges;
  std::vector<Edge *> external_edges;
};

class MapCluster {
public:
  MapCluster(ClusterSize cluster_index, std::string cluster_name,
             std::unordered_set<NodeSize> nodes, MapCluster *left_child,
             MapCluster *right_child);
  void SetInternalExternalEdgesFromEdgeList(const std::vector<Edge *> &edges);
  static MapCluster *
  MapClusterFromNetworkJson(const json &json_spec, ClusterSize cluster_index,
                            const std::string &cluster_name,
                            const std::unordered_map<std::string, MapCluster *>
                                &constructed_clusters);
  const std::unordered_set<NodeSize> &nodes() const;
  MapCluster *left_child() { return left_child_; }
  MapCluster *right_child() { return right_child_; }
  const std::unordered_map<Edge *, NodeSize> &external_edges() const {
    return external_edges_;
  }
  const std::unordered_set<Edge *> &internal_edges() const {
    return internal_edges_;
  }
  const std::string &cluster_name() { return cluster_name_; }
  void InitConstraint(
      const std::unordered_map<MapCluster *, SddLiteral> *cluster_variable_map,
      const std::unordered_map<Edge *, SddLiteral> *edge_variable_map,
      const std::unordered_map<Edge *, MapCluster *> *edge_cluster_map,
      PsddManager *manager, Vtree *local_vtree,
      LeafConstraintHandler *leaf_constraint_handler);
  Vtree *GenerateLocalVtree(
      const std::unordered_map<MapCluster *, SddLiteral> *cluster_variable_map,
      const std::unordered_map<Edge *, SddLiteral> *edge_variable_map) const;
  static Vtree *GenerateVtreeUsingMinFillOfPrimalGraph(
      const std::unordered_set<Edge *> &edges,
      const std::unordered_map<Edge *, SddLiteral> *edge_variable_map);
  PsddNode *empty_path_constraint() const;
  PsddNode *internal_path_constraint() const;
  PsddNode *terminal_path_constraint(Edge *entering_edge) const;
  PsddNode *non_terminal_path_constraint(Edge *entering_edge_1,
                                         Edge *entering_edge_2) const;
  // Learning methods
  void UpdateParameterLearningStateSimple(
      const std::vector<std::vector<Edge *>> &routes);
  void NormalizeLearningState();

  // Slice routes. In each route slice, the internal edges and external edges
  // are ordered.
  // If there is no external edge, the order of internal edges
  // corresponds to any traverse order of the route;
  // If there is one external edge, the order of internal edges corresponds to
  // external_edge[0] internal_edge[0] internal_edge[1] ...;
  // If there are two external edges, the order of internal edges corresponds to
  // external_edge[0] internal_edge[0] internal_edge[1] ... internal_edge[k]
  // external_edge[1].
  std::vector<SlicedRouteComponent>
  SliceRoute(const std::vector<Edge *> &edges) const;

protected:
  PsddNode *ConstructEmptyPathConstraintForInternalCluster() const;
  PsddNode *ConstructInternalPathConstraintForInternalCluster() const;
  PsddNode *ConstructTerminalPathConstraintForInternalCluster(Edge *edge) const;
  PsddNode *
  ConstructNonTerminalPathConstraintForInternalCluster(Edge *edge_1,
                                                       Edge *edge_2) const;
  ClusterSize cluster_index_;
  std::string cluster_name_;
  std::unordered_set<NodeSize> nodes_;
  MapCluster *left_child_;
  MapCluster *right_child_;
  std::unordered_map<Edge *, NodeSize> external_edges_;
  std::unordered_set<Edge *> internal_edges_;
  // compilation members
  const std::unordered_map<MapCluster *, SddLiteral> *cluster_variable_map_;
  const std::unordered_map<Edge *, SddLiteral> *edge_variable_map_;
  const std::unordered_map<Edge *, MapCluster *> *edge_cluster_map_;
  Vtree *local_vtree_;
  PsddManager *pm_;
  // compilation results for internal clusters
  PsddNode *internal_path_constraint_;
  PsddNode *empty_path_constraint_;
  std::map<Edge *, PsddNode *> terminal_path_constraint_;
  std::map<std::pair<Edge *, Edge *>, PsddNode *> non_terminal_path_constraint_;

  // Learning states.
  std::map<std::pair<Edge *, Edge *>, ParameterLearningState>
      param_learning_state_non_terminal_;
  std::unordered_map<Edge *, ParameterLearningState>
      param_learning_state_terminal_;
  ParameterLearningState param_learning_state_internal_;

  // Friend tests
  FRIEND_TEST(MAP_CLUSTER_SPLIT_ROUTE_TEST, LEARNING_STATE_TEST);
  FRIEND_TEST(MAP_CLUSTER_SPLIT_ROUTE_TEST,
              LEARNING_STATE_TEST_WITH_ILLEGAL_ROUTE);
};

} // namespace hierarchical_map
#endif // HIERARCHICAL_MAP_MAP_CLUSTER_H
