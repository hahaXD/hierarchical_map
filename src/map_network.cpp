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
    : clusters_(std::move(clusters)), graph_(graph),
      leaf_constraint_handler_(nullptr), psdd_manager_(nullptr) {}

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
    for (Edge *cur_internal_edge : cur_cluster->lr_cut_edges()) {
      result[cur_internal_edge] = cur_cluster;
    }
  }
  return result;
}

pair<PsddNode *, PsddManager *>
MapNetwork::CompileConstraint(const std::string &graphillion_script,
                              const std::string &tmp_dir, int thread_num) {
  unordered_map<Edge *, MapCluster *> edge_cluster_map = EdgeClusterMap();
  SetupPsddManager();
  CompileLeafClustersUsingGraphillion(graphillion_script, tmp_dir, thread_num);
  // start timer
  auto compilation_start_time = get_time::now();
  for (MapCluster *cur_cluster : clusters_) {
    cur_cluster->InitConstraint(
        &cluster_variable_map_, &edge_variable_map_, psdd_manager_,
        local_vtree_per_cluster_[cur_cluster], leaf_constraint_handler_);
  }
  auto compilation_end_time = get_time::now();
  std::cout << "Compilation time : "
            << std::chrono::duration_cast<ms>(compilation_end_time -
                                              compilation_start_time)
                   .count()
            << " ms " << std::endl;
  MapCluster *root = clusters_.back();
  return pair<PsddNode *, PsddManager *>(root->internal_path_constraint(),
                                         psdd_manager_);
}

void MapNetwork::SetupPsddManager() {
  cluster_variable_map_.clear();
  edge_variable_map_.clear();
  local_vtree_per_cluster_.clear();
  if (psdd_manager_ != nullptr)
    delete (psdd_manager_);
  SddLiteral index = 1;
  MapCluster *root = clusters_.back();
  for (MapCluster *cur_cluster : clusters_) {
    if (cur_cluster != root) {
      cluster_variable_map_[cur_cluster] = index++;
    }
    for (Edge *cur_internal_edge : cur_cluster->lr_cut_edges()) {
      edge_variable_map_[cur_internal_edge] = index++;
    }
  }
  // constructing a vtree for psdd manager
  // sub_vtree_map stores the tmp local vtree for each map cluster. This local
  // vtree contains all the variables inside this cluster.
  unordered_map<MapCluster *, Vtree *> sub_vtree_map;
  unordered_map<MapCluster *, Vtree *> vtree_construction_cache;
  for (MapCluster *cur_cluster : clusters_) {
    Vtree *tmp_local_vtree = cur_cluster->GenerateLocalVtree(
        &cluster_variable_map_, &edge_variable_map_);
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
  psdd_manager_ = PsddManager::GetPsddManagerFromVtree(global_vtree);
  // Manager vtree is a copy of global_vtree
  Vtree *manager_vtree = psdd_manager_->vtree();
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
  for (const auto &sub_vtree_pair : sub_vtree_map) {
    MapCluster *cur_cluster = sub_vtree_pair.first;
    Vtree *tmp_local_vtree = sub_vtree_pair.second;
    local_vtree_per_cluster_[cur_cluster] =
        (Vtree *)sdd_vtree_data(tmp_local_vtree);
  }
  // free the global vtree, and all the vtree reference is from manager_vtree
  sdd_vtree_free(global_vtree);
}

void MapNetwork::CompileLeafClustersUsingGraphillion(
    const std::string &graphillion_script, const std::string &tmp_dir,
    int thread_num) {
  unordered_map<SddLiteral, Edge *> variable_to_edge_map;
  for (const auto &entry : edge_variable_map_) {
    variable_to_edge_map[entry.second] = entry.first;
  }
  if (leaf_constraint_handler_ != nullptr) {
    delete (leaf_constraint_handler_);
  }
  leaf_constraint_handler_ =
      LeafConstraintHandler::GetGraphillionSddLeafConstraintHandler(
          edge_variable_map_, variable_to_edge_map, local_vtree_per_cluster_,
          graphillion_script, tmp_dir, thread_num);
}
void MapNetwork::LearnWithRoutes(
    const std::string &graphillion_script, const std::string &tmp_dir,
    int thread_num, const std::vector<std::vector<Edge *>> &routes) {
  SetupPsddManager();
  CompileLeafClustersUsingGraphillion(graphillion_script, tmp_dir, thread_num);
  PsddParameter laplacian_alpha = PsddParameter::CreateFromDecimal(1.0);
  for (MapCluster *cur_cluster : clusters_) {
    if (cur_cluster->left_child() == nullptr) {
      // Only compile constraint in the leaf regions.
      cur_cluster->InitConstraint(
          &cluster_variable_map_, &edge_variable_map_, psdd_manager_,
          local_vtree_per_cluster_[cur_cluster], leaf_constraint_handler_);
      cur_cluster->LearnParameters(routes, psdd_manager_, laplacian_alpha);
    } else {
      cur_cluster->LearnParameters(routes, psdd_manager_, laplacian_alpha);
    }
  }
}

Probability
MapNetwork::Evaluate(const std::vector<std::vector<Edge *>> &target_routes) {
  Probability score = Probability::CreateFromDecimal(1.0);
  for (MapCluster *cur_cluster : clusters_) {
    score *= cur_cluster->Evaluate(target_routes);
  }
  return score;
}

std::unordered_map<std::string, MapCluster *>
MapNetwork::GetMapClustersByName() const {
  std::unordered_map<std::string, MapCluster *> map_clusters_by_name;
  for (MapCluster *cur_cluster : clusters_) {
    map_clusters_by_name[cur_cluster->cluster_name()] = cur_cluster;
  }
  return map_clusters_by_name;
}

std::pair<PsddNode *, PsddManager *> MapNetwork::InferenceByCompilation() {
  unordered_map<Edge *, MapCluster *> edge_cluster_map = EdgeClusterMap();
  // start timer
  auto compilation_start_time = get_time::now();
  for (MapCluster *cur_cluster : clusters_) {
    if (cur_cluster->left_child() != nullptr) {
      cur_cluster->InitConstraint(
          &cluster_variable_map_, &edge_variable_map_, psdd_manager_,
          local_vtree_per_cluster_[cur_cluster], leaf_constraint_handler_);
    }
  }
  auto compilation_end_time = get_time::now();
  std::cout << "Compilation time : "
            << std::chrono::duration_cast<ms>(compilation_end_time -
                                              compilation_start_time)
                   .count()
            << " ms " << std::endl;
  MapCluster *root = clusters_.back();
  return std::make_pair(root->internal_path_constraint(), psdd_manager_);
}
} // namespace hierarchical_map
