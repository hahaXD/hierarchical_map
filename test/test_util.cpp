#include <hierarchical_map/edge.h>
#include <hierarchical_map_test/test_util.h>
#include <unordered_map>

namespace hierarchical_map {
namespace testing {
std::unique_ptr<MapNetwork>
CreateSimple3LayerNetwork(NodeSize width_per_cluster) {
  // Graph
  // 0                1                 2 3 4 5 6 ..
  // node_per_cluster 1+node_per_cluster ...
  std::vector<Edge *> edges;
  NodeSize map_width = 2 * width_per_cluster;
  EdgeSize edge_index = 0;
  for (NodeSize i = 0; i < map_width; ++i) {
    for (NodeSize j = 0; j < map_width; ++j) {
      NodeSize cur_node_index = i * map_width + j;
      if (i != map_width - 1) {
        NodeSize next_node_index = (i + 1) * map_width + j;
        Edge *new_edge = new Edge(std::to_string(edge_index), cur_node_index,
                                  next_node_index);
        ++edge_index;
        edges.push_back(new_edge);
      }
      if (j != map_width - 1) {
        NodeSize next_node_index = cur_node_index + 1;
        Edge *new_edge = new Edge(std::to_string(edge_index), cur_node_index,
                                  next_node_index);
        ++edge_index;
        edges.push_back(new_edge);
      }
    }
  }
  Graph *graph = Graph::GraphFromStolenEdgeList(std::move(edges));
  std::vector<MapCluster *> clusters;
  std::unordered_set<NodeSize> lu_nodes;
  for (NodeSize i = 0; i < width_per_cluster; ++i) {
    for (NodeSize j = 0; j < width_per_cluster; ++j) {
      lu_nodes.insert(i * map_width + j);
    }
  }
  std::unordered_set<NodeSize> ll_nodes;
  for (NodeSize i = width_per_cluster; i < 2 * width_per_cluster; ++i) {
    for (NodeSize j = 0; j < width_per_cluster; ++j) {
      ll_nodes.insert(i * map_width + j);
    }
  }
  std::unordered_set<NodeSize> ru_nodes;
  for (NodeSize i = 0; i < width_per_cluster; ++i) {
    for (NodeSize j = width_per_cluster; j < 2 * width_per_cluster; ++j) {
      ru_nodes.insert(i * map_width + j);
    }
  }
  std::unordered_set<NodeSize> rl_nodes;
  for (NodeSize i = width_per_cluster; i < 2 * width_per_cluster; ++i) {
    for (NodeSize j = width_per_cluster; j < 2 * width_per_cluster; ++j) {
      rl_nodes.insert(i * map_width + j);
    }
  }
  std::unordered_set<NodeSize> left_nodes(lu_nodes.begin(), lu_nodes.end());
  left_nodes.insert(ll_nodes.begin(), ll_nodes.end());

  std::unordered_set<NodeSize> right_nodes(ru_nodes.begin(), ru_nodes.end());
  right_nodes.insert(rl_nodes.begin(), rl_nodes.end());

  std::unordered_set<NodeSize> total_nodes(left_nodes.begin(),
                                           left_nodes.end());
  total_nodes.insert(right_nodes.begin(), right_nodes.end());
  MapCluster *lu_cluster =
      new MapCluster(0, "lu", std::move(lu_nodes), nullptr, nullptr);
  clusters.push_back(lu_cluster);
  MapCluster *ll_cluster =
      new MapCluster(1, "ll", std::move(ll_nodes), nullptr, nullptr);
  clusters.push_back(ll_cluster);
  MapCluster *ru_cluster =
      new MapCluster(2, "ru", std::move(ru_nodes), nullptr, nullptr);
  clusters.push_back(ru_cluster);
  MapCluster *rl_cluster =
      new MapCluster(3, "rl", std::move(rl_nodes), nullptr, nullptr);
  clusters.push_back(rl_cluster);
  MapCluster *left_cluster =
      new MapCluster(4, "left", std::move(left_nodes), lu_cluster, ll_cluster);
  clusters.push_back(left_cluster);
  MapCluster *right_cluster = new MapCluster(5, "right", std::move(right_nodes),
                                             ru_cluster, rl_cluster);
  clusters.push_back(right_cluster);
  MapCluster *total_cluster = new MapCluster(6, "total", std::move(total_nodes),
                                             left_cluster, right_cluster);
  clusters.push_back(total_cluster);
  total_cluster->SetInternalExternalEdgesFromEdgeList(graph->edges());
  auto map_network = std::make_unique<MapNetwork>(std::move(clusters), graph);
  return map_network;
}
} // namespace testing
} // namespace hierarchical_map
