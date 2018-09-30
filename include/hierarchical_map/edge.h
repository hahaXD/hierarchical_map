//
// Created by Jason Shen on 5/21/18.
//

#ifndef HIERARCHICAL_MAP_EDGE_H
#define HIERARCHICAL_MAP_EDGE_H

#include <cstdint>
#include <hierarchical_map/types.h>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_set>

using nlohmann::json;
namespace hierarchical_map {
class Edge {
public:
  Edge(std::string edge_name, NodeSize x_node_index, NodeSize y_node_index);
  NodeSize x_node_index() const;
  NodeSize y_node_index() const;
  NodeSize OtherEnd(NodeSize one_end) const;
  const std::string &edge_name() const;
  std::pair<NodeSize, NodeSize> OrderedNodePair() const;
  json to_json() const;
  // Returns the set of end points of the edge that is contained in the
  // node_set.
  std::unordered_set<NodeSize>
  ReleventEndPoints(const std::unordered_set<NodeSize> &nodes) const;

private:
  std::string edge_name_;
  NodeSize x_node_index_;
  NodeSize y_node_index_;
};

} // namespace hierarchical_map
#endif // HIERARCHICAL_MAP_EDGE_H
