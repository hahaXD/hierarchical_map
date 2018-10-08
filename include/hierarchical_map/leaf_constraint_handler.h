//
// Created by Jason Shen on 6/12/18.
//

#ifndef HIERARCHICAL_MAP_LEAF_CONSTRAINT_HANDLER_H
#define HIERARCHICAL_MAP_LEAF_CONSTRAINT_HANDLER_H
extern "C" {
#include <sdd/sddapi.h>
}
#include <hierarchical_map/edge.h>
#include <hierarchical_map/map_cluster.h>
#include <hierarchical_map/types.h>
#include <map>
#include <set>
#include <unordered_map>

namespace hierarchical_map {

class LeafConstraintHandler {
public:
  // Constructor borrows used_vtree
  static LeafConstraintHandler *GetSddLeafConstraintHandler(
      const std::unordered_map<Edge *, SddLiteral> *edge_variable_map,
      const std::unordered_map<MapCluster *, Vtree *> *local_vtree_per_cluster,
      const std::unordered_map<MapCluster *, std::set<NodeSize>>
          *terminal_nodes_per_cluster,
      const std::unordered_map<MapCluster *,
                               std::set<std::pair<NodeSize, NodeSize>>>
          *non_terminal_nodes_per_cluster);
  static LeafConstraintHandler *GetGraphillionSddLeafConstraintHandler(
      const std::unordered_map<Edge *, SddLiteral> &edge_variable_map,
      const std::unordered_map<SddLiteral, Edge *> &variable_to_edge_map,
      const std::unordered_map<MapCluster *, Vtree *> &local_vtree_per_cluster,
      const std::string &graphillion_script, const std::string &tmp_dir,
      int thread_num);
  virtual ~LeafConstraintHandler() = default;
  // init_constraints need to be called before GetNode. Return nullptr if the
  // leaf cluster cannot be found.
  virtual SddNode *internal_path_constraint(MapCluster *leaf_cluster) = 0;
  virtual const std::map<std::pair<NodeSize, NodeSize>, SddNode *> &
  non_terminal_path_constraint(MapCluster *leaf_cluster) = 0;
  virtual SddNode *empty_path_constraint(MapCluster *leaf_cluster) = 0;
  virtual const std::map<NodeSize, SddNode *> &
  terminal_path_constraint(MapCluster *leaf_cluster) = 0;
  virtual SddManager *sdd_manager(MapCluster *leaf_cluster) = 0;
};
} // namespace hierarchical_map
#endif // HIERARCHICAL_MAP_LEAF_CONSTRAINT_HANDLER_H
