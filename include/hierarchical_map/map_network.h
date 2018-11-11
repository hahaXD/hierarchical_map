//
// Created by Jason Shen on 5/21/18.
//

#ifndef HIERARCHICAL_MAP_MAP_NETWORK_H
#define HIERARCHICAL_MAP_MAP_NETWORK_H
#include "map_cluster.h"
#include <psdd/psdd_node.h>

namespace hierarchical_map {
class MapNetwork {
public:
  explicit MapNetwork(std::vector<MapCluster *> clusters, Graph *graph);
  ~MapNetwork() {
    delete (graph_);
    for (MapCluster *cur_cluster : clusters_) {
      delete (cur_cluster);
    }
  }
  static MapNetwork *MapNetworkFromJsonSpecFile(const char *filename);
  static MapNetwork *MapNetworkFromJsonSpec(const json &json_spec);
  MapCluster *root_cluster() const;
  std::unordered_map<std::string, MapCluster *> GetMapClustersByName() const;
  Graph *graph() { return graph_; }
  std::vector<MapCluster *> clusters() const { return clusters_; }

  // Returns a map whose value cluster contains the key edge as the internal
  // edge
  std::unordered_map<Edge *, MapCluster *> EdgeClusterMap() const;
  std::pair<PsddNode *, PsddManager *>
  CompileConstraint(const std::string &graphillion_script,
                    const std::string &tmp_dir, int thread_num);

  // Returns the mapping between edge and the edge index
  const std::unordered_map<Edge *, SddLiteral> &edge_variable_map() const {
    return edge_variable_map_;
  }

  // Compiler methods
  void
  CompileLeafClustersUsingGraphillion(const std::string &graphillion_script,
                                      const std::string &tmp_dir,
                                      int thread_num);
  // Set up Psdd Manager states.
  void SetupPsddManager();
  // Learning methods.
  void LearnWithRoutes(const std::string &graphillion_script,
                       const std::string &tmp_dir, int thread_num,
                       const std::vector<std::vector<Edge *>> &routes);

  // Inference methods.
  Probability Evaluate(const std::vector<std::vector<Edge *>> &target_routes);
  std::pair<PsddNode *, PsddManager *> InferenceByCompilation();

private:
  std::vector<MapCluster *> clusters_;
  Graph *graph_;
  LeafConstraintHandler *leaf_constraint_handler_;
  // PSDD managers
  std::unordered_map<MapCluster *, SddLiteral> cluster_variable_map_;
  std::unordered_map<Edge *, SddLiteral> edge_variable_map_;
  std::unordered_map<MapCluster *, Vtree *> local_vtree_per_cluster_;
  PsddManager *psdd_manager_;
};
} // namespace hierarchical_map
#endif // HIERARCHICAL_MAP_MAP_NETWORK_H
