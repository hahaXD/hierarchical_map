//
// Created by Jason Shen on 5/21/18.
//

#include <algorithm>
#include <chrono>
#include <fstream>
#include <hierarchical_map/graph.h>
#include <hierarchical_map/leaf_constraint_handler.h>
#include <hierarchical_map/map_network.h>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>

namespace {
using ms = std::chrono::milliseconds;
using get_time = std::chrono::steady_clock;
using json = nlohmann::json;
using std::map;
using std::max;
using std::min;
using std::pair;
using std::set;
using std::unordered_map;
using std::vector;
std::vector<std::string> TopologicalSort(const json &spec) {
  std::unordered_set<std::string> explored_clusters;
  std::vector<std::string> candidates;
  std::vector<std::string> result;
  for (json::const_iterator it = spec.cbegin(); it != spec.cend(); ++it) {
    std::string cluster_name = it.key();
    if (it.value().find("nodes") != it.value().end()) {
      result.emplace_back(cluster_name);
      explored_clusters.insert(cluster_name);
    } else {
      candidates.emplace_back(cluster_name);
    }
  }
  while (!candidates.empty()) {
    auto candidate_it = candidates.begin();
    while (candidate_it != candidates.end()) {
      assert(spec.find(*candidate_it) != spec.end());
      const json &cur_cluster_spec = spec[*candidate_it];
      std::string child_1_name = cur_cluster_spec["sub_clusters"][0];
      std::string child_2_name = cur_cluster_spec["sub_clusters"][1];
      if (explored_clusters.find(child_1_name) != explored_clusters.end() &&
          explored_clusters.find(child_2_name) != explored_clusters.end()) {
        result.push_back(*candidate_it);
        explored_clusters.insert(*candidate_it);
        candidate_it = candidates.erase(candidate_it);
      } else {
        ++candidate_it;
      }
    }
  }
  return result;
}

} // namespace

namespace hierarchical_map {

MapNetwork::MapNetwork(std::vector<MapCluster *> clusters, Graph *graph)
    : clusters_(std::move(clusters)), graph_(graph) {}
MapNetwork *MapNetwork::MapNetworkFromJsonSpecFile(const char *filename) {
  std::ifstream in_fd(filename);
  json network_spec;
  in_fd >> network_spec;
  return MapNetworkFromJsonSpec(network_spec);
}
MapNetwork *MapNetwork::MapNetworkFromJsonSpec(const json &network_spec) {
  assert(network_spec.find("edges") != network_spec.end());
  Graph *new_graph = Graph::GraphFromJsonEdgeList(network_spec["edges"]);
  assert(network_spec.find("clusters") != network_spec.end());
  auto topological_cluster_names = TopologicalSort(network_spec["clusters"]);
  ClusterSize cluster_index = 0;
  std::unordered_map<std::string, MapCluster *> cluster_construction_cache;
  std::vector<MapCluster *> clusters;
  for (const auto &cluster_name : topological_cluster_names) {
    MapCluster *new_cluster = MapCluster::MapClusterFromNetworkJson(
        network_spec["clusters"][cluster_name], cluster_index++, cluster_name,
        cluster_construction_cache);
    cluster_construction_cache[cluster_name] = new_cluster;
    clusters.push_back(new_cluster);
  }
  const std::vector<Edge *> &edges_in_map = new_graph->edges();
  clusters.back()->SetInternalExternalEdgesFromEdgeList(edges_in_map);
  return new MapNetwork(std::move(clusters), new_graph);
}
MapCluster *MapNetwork::root_cluster() const { return clusters_.back(); }

unordered_map<Edge *, MapCluster *> MapNetwork::EdgeClusterMap() const {
  unordered_map<Edge *, MapCluster *> result;
  for (MapCluster *cur_cluster : clusters_) {
    for (Edge *cur_internal_edge : cur_cluster->internal_edges()) {
      result[cur_internal_edge] = cur_cluster;
    }
  }
  return result;
}

pair<PsddNode *, PsddManager *> MapNetwork::CompileConstraint() const {
  size_t cluster_size = clusters_.size();
  unordered_map<MapCluster *, SddLiteral> cluster_variable_map;
  unordered_map<Edge *, SddLiteral> edge_variable_map;
  SddLiteral index = 1;
  MapCluster *root = clusters_.back();
  for (MapCluster *cur_cluster : clusters_) {
    if (cur_cluster != root) {
      cluster_variable_map[cur_cluster] = index++;
    }
    for (Edge *cur_internal_edge : cur_cluster->internal_edges()) {
      edge_variable_map[cur_internal_edge] = index++;
    }
  }
  // constructing a vtree for psdd manager
  // sub_vtree_map stores the tmp local vtree for each map cluster. This local
  // vtree contains all the variables inside this cluster.
  unordered_map<MapCluster *, Vtree *> sub_vtree_map;
  unordered_map<MapCluster *, Vtree *> vtree_construction_cache;
  for (size_t i = 0; i < cluster_size; ++i) {
    MapCluster *cur_cluster = clusters_[i];
    Vtree *tmp_local_vtree = cur_cluster->GenerateLocalVtree(
        &cluster_variable_map, &edge_variable_map);
    sub_vtree_map[cur_cluster] = tmp_local_vtree;
    if (cur_cluster->left_child() != nullptr) {
      Vtree *decomposable_vtree_node = new_internal_vtree(
          vtree_construction_cache[cur_cluster->left_child()],
          vtree_construction_cache[cur_cluster->right_child()]);
      Vtree *vtree_state_at_cur_cluster =
          new_internal_vtree(tmp_local_vtree, decomposable_vtree_node);
      vtree_construction_cache[cur_cluster] = vtree_state_at_cur_cluster;
    } else {
      vtree_construction_cache[cur_cluster] = tmp_local_vtree;
    }
  }
  Vtree *global_vtree = vtree_construction_cache[root];
  set_vtree_properties(global_vtree);
  PsddManager *psdd_manager =
      PsddManager::GetPsddManagerFromVtree(global_vtree);
  // Manager vtree is a copy of global_vtree
  Vtree *manager_vtree = psdd_manager->vtree();
  vector<Vtree *> serialized_global_vtree =
      vtree_util::SerializeVtree(global_vtree);
  vector<Vtree *> serialized_manager_vtree =
      vtree_util::SerializeVtree(manager_vtree);
  assert(serialized_global_vtree.size() == serialized_manager_vtree.size());
  auto serialized_vtree_size = serialized_global_vtree.size();
  for (size_t i = 0; i < serialized_vtree_size; ++i) {
    Vtree *cur_global_vtree_node = serialized_global_vtree[i];
    sdd_vtree_set_data((void *)serialized_manager_vtree[i],
                       cur_global_vtree_node);
  }
  unordered_map<MapCluster *, Vtree *> local_vtree_per_cluster;
  for (const auto &sub_vtree_pair : sub_vtree_map) {
    MapCluster *cur_cluster = sub_vtree_pair.first;
    Vtree *tmp_local_vtree = sub_vtree_pair.second;
    local_vtree_per_cluster[cur_cluster] =
        (Vtree *)sdd_vtree_data(tmp_local_vtree);
  }
  // free the global vtree, and all the vtree reference is from manager_vtree
  sdd_vtree_free(global_vtree);
  unordered_map<Edge *, MapCluster *> edge_cluster_map = EdgeClusterMap();
  unordered_map<SddLiteral, Edge *> variable_to_edge_map;
  for (const auto &entry : edge_variable_map) {
    variable_to_edge_map[entry.second] = entry.first;
  }
  unordered_map<MapCluster *, set<NodeSize>> terminal_entering_points =
      ConstructEntryPointsForTerminalPath();
  unordered_map<MapCluster *, set<pair<NodeSize, NodeSize>>>
      non_terminal_entering_points =
          ConstructEntryPointsForNonTerminalPath(edge_cluster_map);
  LeafConstraintHandler *leaf_constraint_handler =
      LeafConstraintHandler::GetGraphillionSddLeafConstraintHandler(
          &edge_variable_map, &variable_to_edge_map, &local_vtree_per_cluster,
          &terminal_entering_points, &non_terminal_entering_points,
          graphillion_script_, graphillion_tmp_dir_, graphillion_thread_num_);
  // start timer
  auto compilation_start_time = get_time::now();
  for (MapCluster *cur_cluster : clusters_) {
    cur_cluster->InitConstraint(&cluster_variable_map, &edge_variable_map,
                                &edge_cluster_map, psdd_manager,
                                local_vtree_per_cluster[cur_cluster],
                                leaf_constraint_handler);
  }
  auto compilation_end_time = get_time::now();
  std::cout << "Compilation time : "
            << std::chrono::duration_cast<ms>(compilation_end_time -
                                              compilation_start_time)
                   .count()
            << " ms " << std::endl;
  return pair<PsddNode *, PsddManager *>(root->internal_path_constraint(),
                                         psdd_manager);
}

void MapNetwork::SetGraphillionCompiler(std::string graphillion_script,
                                        std::string tmp_dir, int thread_num) {
  graphillion_script_ = std::move(graphillion_script);
  graphillion_tmp_dir_ = std::move(tmp_dir);
  graphillion_thread_num_ = thread_num;
}

unordered_map<MapCluster *, set<NodeSize>>
MapNetwork::ConstructEntryPointsForTerminalPath() const {
  unordered_map<MapCluster *, set<NodeSize>> result;
  for (MapCluster *cur_cluster : clusters_) {
    set<NodeSize> cur_entering_points;
    for (const auto &external_entry : cur_cluster->external_edges()) {
      if (cur_entering_points.find(external_entry.second) ==
          cur_entering_points.end()) {
        cur_entering_points.insert(external_entry.second);
      }
    }
    result[cur_cluster] = cur_entering_points;
  }
  return result;
}

unordered_map<MapCluster *, set<pair<NodeSize, NodeSize>>>
MapNetwork::ConstructEntryPointsForNonTerminalPath(
    const std::unordered_map<Edge *, MapCluster *> &edge_cluster_map) const {
  unordered_map<MapCluster *, set<pair<NodeSize, NodeSize>>> result;
  for (MapCluster *cur_cluster : clusters_) {
    set<pair<NodeSize, NodeSize>> cur_entering_points;
    const auto &cur_external_edges = cur_cluster->external_edges();
    for (auto i_it = cur_external_edges.begin();
         i_it != cur_external_edges.end(); ++i_it) {
      auto j_it = i_it;
      std::advance(j_it, 1);
      NodeSize first_entering_node = i_it->second;
      Edge *first_external_edge = i_it->first;
      auto first_edge_cluster_it = edge_cluster_map.find(first_external_edge);
      for (; j_it != cur_external_edges.end(); ++j_it) {
        NodeSize second_entering_node = j_it->second;
        Edge *second_external_edge = j_it->first;
        auto second_edge_cluster_it =
            edge_cluster_map.find(second_external_edge);
        if (first_edge_cluster_it->second == second_edge_cluster_it->second) {
          continue;
        }
        pair<NodeSize, NodeSize> node_pair(
            min(first_entering_node, second_entering_node),
            max(first_entering_node, second_entering_node));
        if (cur_entering_points.find(node_pair) == cur_entering_points.end()) {
          cur_entering_points.insert(node_pair);
        }
      }
    }
    result[cur_cluster] = cur_entering_points;
  }
  return result;
}
} // namespace hierarchical_map
