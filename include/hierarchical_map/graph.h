//
// Created by Jason Shen on 5/21/18.
//

#ifndef HIERARCHICAL_MAP_GRAPH_H
#define HIERARCHICAL_MAP_GRAPH_H

#include <hierarchical_map/edge.h>
#include <nlohmann/json.hpp>
#include <set>
#include <unordered_map>
#include <vector>
extern "C" {
#include <sdd/sddapi.h>
}

using json = nlohmann::json;

namespace hierarchical_map {
class Graph {
public:
  ~Graph();
  const std::vector<Edge *> &edges() const;
  static Graph *GraphFromJsonEdgeList(const json &edge_list);
  static std::unique_ptr<Graph>
  GraphFromEdgeList(std::vector<Edge *> edge_list);
  static Graph *GraphFromStolenEdgeList(std::vector<Edge *> edge_list);
  static std::unordered_map<NodeSize, std::vector<Edge *>>
  IncidenceMapFromEdgeList(const std::vector<Edge *> &edges);
  std::vector<Edge *> GreedyEdgeOrder() const;
  std::unordered_map<NodeSize, std::set<NodeSize>> AdjacencyMap() const;
  std::unordered_map<NodeSize, std::vector<Edge *>> IncidenceMap() const;
  std::map<std::pair<NodeSize, NodeSize>, std::vector<Edge *>>
  NodePairToEdges() const;
  std::unordered_map<NodeSize, EdgeSize> NodeDegrees() const;
  std::set<NodeSize> Vertices() const;
  NodeSize node_size() const { return node_size_; }
  json to_json() const;
  // Returns the matched edge with the same src and dst as specified by
  // input_json. If multiple such edges exist, returns the one sharing the same
  // name as specified by input_json, otherwise returns an arbitrary matching
  // edge.
  Edge *EdgeFromJson(const json &input_json);

private:
  explicit Graph(std::vector<Edge *> edges, bool stolen_edges);
  std::map<std::pair<NodeSize, NodeSize>, std::vector<Edge *>>
      node_pair_to_edges_;
  std::vector<Edge *> edges_;
  NodeSize node_size_;
  bool stolen_edges_;
};
} // namespace hierarchical_map

#endif // HIERARCHICAL_MAP_GRAPH_H
