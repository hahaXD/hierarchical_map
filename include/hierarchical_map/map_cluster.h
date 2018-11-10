//
// Created by Jason Shen on 5/21/18.
//

#ifndef HIERARCHICAL_MAP_MAP_CLUSTER_H
#define HIERARCHICAL_MAP_MAP_CLUSTER_H

#include <hierarchical_map/graph.h>
#include <hierarchical_map/types.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <psdd/psdd_manager.h>
#include <unordered_map>
#include <unordered_set>

namespace hierarchical_map {
class LeafConstraintHandler;

// Learning states given a particular way of the entering the region, and we
// used this state to estimate parameters. Different subset of fields are used
// for different contexts. For internal cluster:
//   If two external edges are used, a.k.a. non_terminal_path,
//     both lr_cut_edges_freq_ and zero_lr_cut_edge_freq are used.
//   If only one external edge is used, a.k.a. terminal_path,
//     both lr_cut_edges_freq_ and zero_lr_cut_edge_freq_ is used.
//   If no external edge is used,
//     all of lr_cut_edges_freq_, left_internal_freq_ and right_internal_freq_
//     are used.
// For leaf cluster:
//   All cases:
//     routes_in_leaf_cluster_

struct ParameterLearningStatePerCase {
  std::unordered_map<Edge *, double> lr_cut_edges_freq;
  double zero_lr_cut_edge_freq = 0;
  double left_internal_freq = 0;
  double right_internal_freq = 0;
  std::vector<std::vector<Edge *>> routes_in_leaf_cluster;
};

struct ParameterLearningState {
  std::map<std::pair<Edge *, Edge *>, ParameterLearningStatePerCase>
      param_learning_state_non_terminal;
  std::unordered_map<Edge *, ParameterLearningStatePerCase>
      param_learning_state_terminal;
  ParameterLearningStatePerCase param_learning_state_internal;
};

struct InternalClusterParameterPerCase {
  std::unordered_map<Edge *, PsddParameter> lr_cut_edges_freq;
  PsddParameter zero_lr_cut_edge_freq = PsddParameter::CreateFromDecimal(0);
  PsddParameter left_internal_freq = PsddParameter::CreateFromDecimal(0);
  PsddParameter right_internal_freq = PsddParameter::CreateFromDecimal(0);
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
  // Recursively sets the internal and external edges given the set of edges.
  // Call root.SetInternalExternalEdgesFromEdgeList(...) will set the internal
  // and external edges for every clusters in the network.
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
  const std::unordered_set<Edge *> &lr_cut_edges() const {
    return lr_cut_edges_;
  }

  // Returns the set of internal nodes that external edges of the current
  // cluster connects.
  std::set<NodeSize> EntryPoints() const;

  // Returns the set of entry node pairs that can form non terminal paths. If a
  // pair of external edges of this cluster does not belong to the lr-cut-edges
  // of some cluster in the network, their corresponding entry points form a
  // pair in the result.
  std::set<std::pair<NodeSize, NodeSize>>
  EntryPointPairsForNonTerminalPaths() const;

  const std::string &cluster_name() { return cluster_name_; }
  void InitConstraint(
      const std::unordered_map<MapCluster *, SddLiteral> *cluster_variable_map,
      const std::unordered_map<Edge *, SddLiteral> *edge_variable_map,
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
  // Learn parameters inside this cluster. If this cluster is a leaf cluster,
  // its conditional constraints must be compiled before calling this method.
  void LearnParameters(const std::vector<std::vector<Edge *>> &routes,
                       PsddManager *manager, PsddParameter alpha);
  // Update the learning states for this cluster, and the learning states will
  // be used for parameter estimation.
  ParameterLearningState UpdateParameterLearningStateSimple(
      const std::vector<std::vector<Edge *>> &routes) const;

  Probability Evaluate(const std::vector<std::vector<Edge *>> &routes);

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

  const InternalClusterParameterPerCase &
  internal_cluster_internal_parameter() const {
    return internal_parameter_;
  }
  const std::map<std::pair<Edge *, Edge *>, InternalClusterParameterPerCase> &
  internal_cluster_non_terminal_parameter() const {
    return non_terminal_parameter_;
  }
  const std::map<Edge *, InternalClusterParameterPerCase> &
  internal_cluster_terminal_parameter() const {
    return terminal_parameter_;
  }
  PsddNode *leaf_cluster_internal_path_distribution() const {
    return leaf_internal_path_distribution_;
  }

  const std::map<Edge *, PsddNode *> &
  leaf_cluster_terminal_path_distribution() const {
    return leaf_terminal_path_distribution_;
  }

  const std::map<std::pair<Edge *, Edge *>, PsddNode *> &
  leaf_cluster_non_terminal_path_distribution() const {
    return leaf_non_terminal_path_distribution_;
  }

  const std::unordered_map<Edge *, SddLiteral> *edge_variable_map() const {
    return edge_variable_map_;
  }

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
  std::unordered_set<Edge *> lr_cut_edges_;
  // compilation members
  const std::unordered_map<MapCluster *, SddLiteral> *cluster_variable_map_;
  const std::unordered_map<Edge *, SddLiteral> *edge_variable_map_;
  Vtree *local_vtree_;
  PsddManager *pm_;
  // compilation results for internal clusters
  PsddNode *internal_path_constraint_;
  PsddNode *empty_path_constraint_;
  std::map<Edge *, PsddNode *> terminal_path_constraint_;
  std::map<std::pair<Edge *, Edge *>, PsddNode *> non_terminal_path_constraint_;

  // parameters for internal clusters.
  InternalClusterParameterPerCase internal_parameter_;
  std::map<std::pair<Edge *, Edge *>, InternalClusterParameterPerCase>
      non_terminal_parameter_;
  std::map<Edge *, InternalClusterParameterPerCase> terminal_parameter_;
  // parameters for leaf clusters.
  PsddNode *leaf_internal_path_distribution_;
  std::map<Edge *, PsddNode *> leaf_terminal_path_distribution_;
  std::map<std::pair<Edge *, Edge *>, PsddNode *>
      leaf_non_terminal_path_distribution_;
  // flags
  // indicates whether the cluster has been learned from data yet.
  bool learned_;
};

} // namespace hierarchical_map
#endif // HIERARCHICAL_MAP_MAP_CLUSTER_H
