//
// Created by Jason Shen on 5/21/18.
//
#include <algorithm>
#include <hierarchical_map/graph.h>
#include <map>
#include <set>
#include <unordered_set>

namespace {
using nlohmann::json;
using std::unique_ptr;
using std::unordered_set;
using std::vector;
} // namespace

namespace hierarchical_map {
Graph *Graph::GraphFromJsonEdgeList(const json &edge_list) {
  assert(edge_list.is_array());
  std::vector<Edge *> constructed_edge_list;
  for (const json &cur_edge : edge_list) {
    assert(cur_edge.find("__Edge__") != cur_edge.end());
    constructed_edge_list.push_back(
        new Edge(cur_edge["name"], cur_edge["x"], cur_edge["y"]));
  }
  return new Graph(constructed_edge_list, true);
}

const std::vector<Edge *> &Graph::edges() const { return edges_; }
Graph::~Graph() {
  if (stolen_edges_) {
    for (Edge *e : edges_) {
      delete (e);
    }
  }
}
Graph::Graph(std::vector<Edge *> edges, bool stolen_edges)
    : edges_(std::move(edges)), stolen_edges_(stolen_edges) {
  std::set<NodeSize> used_node_indexes;
  for (Edge *cur_edge : edges_) {
    if (used_node_indexes.find(cur_edge->x_node_index()) ==
        used_node_indexes.end()) {
      used_node_indexes.insert(cur_edge->x_node_index());
    }
    if (used_node_indexes.find(cur_edge->y_node_index()) ==
        used_node_indexes.end()) {
      used_node_indexes.insert(cur_edge->y_node_index());
    }
  }
  node_size_ = (NodeSize)used_node_indexes.size();
}

std::unordered_map<NodeSize, std::set<NodeSize>> Graph::AdjacencyMap() const {
  std::unordered_map<NodeSize, std::set<NodeSize>> adjacency_map;
  for (Edge *cur_edge : edges_) {
    NodeSize x_index = cur_edge->x_node_index();
    NodeSize y_index = cur_edge->y_node_index();
    auto x_it = adjacency_map.find(x_index);
    auto y_it = adjacency_map.find(y_index);
    if (x_it == adjacency_map.end())
      x_it = adjacency_map.insert({x_index, {}}).first;
    if (y_it == adjacency_map.end())
      y_it = adjacency_map.insert({y_index, {}}).first;
    x_it->second.insert(y_index);
    y_it->second.insert(x_index);
  }
  return adjacency_map;
}

std::unordered_map<NodeSize, std::vector<Edge *>> Graph::IncidenceMap() const {
  return IncidenceMapFromEdgeList(edges_);
}

std::vector<Edge *> Graph::GreedyEdgeOrder() const {
  std::unordered_map<NodeSize, std::set<NodeSize>> adjacency_map =
      AdjacencyMap();
  std::map<std::pair<NodeSize, NodeSize>, std::vector<Edge *>>
      node_pair_to_edges = NodePairToEdges();
  std::unordered_map<NodeSize, EdgeSize>
      node_degrees_star; // ignore multi edge degree
  for (const auto &entry : adjacency_map) {
    node_degrees_star[entry.first] = entry.second.size();
  }
  std::set<NodeSize> vertices = Vertices();
  NodeSize u = *vertices.begin();
  std::set<NodeSize> visited_nodes;
  std::vector<Edge *> sorted_edges;
  std::vector<std::tuple<NodeSize, NodeSize, NodeSize>> heap;
  while (true) {
    visited_nodes.insert(u);
    const std::set<NodeSize> &neighbors = adjacency_map[u];
    for (NodeSize v : neighbors) {
      node_degrees_star[v] -= 1;
      if (visited_nodes.find(v) != visited_nodes.end()) {
        node_degrees_star[u] -= 1;
        std::pair<NodeSize, NodeSize> node_pair(std::min(u, v), std::max(u, v));
        auto node_pair_it = node_pair_to_edges.find(node_pair);
        assert(node_pair_it != node_pair_to_edges.end());
        for (Edge *edge_to_add : node_pair_it->second) {
          sorted_edges.push_back(edge_to_add);
        }
        if (node_degrees_star[v] > 0) {
          for (NodeSize w : adjacency_map[v]) {
            if (visited_nodes.find(w) == visited_nodes.end()) {
              heap.emplace_back(std::tuple<NodeSize, NodeSize, NodeSize>(
                  node_degrees_star[v], node_degrees_star[w], w));
              std::push_heap(
                  heap.begin(), heap.end(),
                  std::greater<std::tuple<NodeSize, NodeSize, NodeSize>>());
            }
          }
        }
      }
    }
    for (NodeSize v : neighbors) {
      if (visited_nodes.find(v) == visited_nodes.end()) {
        heap.emplace_back(std::tuple<NodeSize, NodeSize, NodeSize>(
            node_degrees_star[u], node_degrees_star[v], v));
        std::push_heap(
            heap.begin(), heap.end(),
            std::greater<std::tuple<NodeSize, NodeSize, NodeSize>>());
      }
    }
    if (visited_nodes.size() == vertices.size()) {
      break;
    }
    while (visited_nodes.find(u) != visited_nodes.end()) {
      if (heap.empty()) {
        for (NodeSize k : vertices) {
          if (visited_nodes.find(k) == visited_nodes.end()) {
            u = k;
            break;
          }
        }
      } else {
        const auto &cur_front = heap.front();
        u = std::get<2>(cur_front);
        std::pop_heap(heap.begin(), heap.end(),
                      std::greater<std::tuple<NodeSize, NodeSize, NodeSize>>());
        heap.pop_back();
      }
    }
  }
  assert(sorted_edges.size() == edges_.size());
  return sorted_edges;
}

std::set<NodeSize> Graph::Vertices() const {
  std::set<NodeSize> vertices;
  for (Edge *cur_edge : edges_) {
    vertices.insert(cur_edge->x_node_index());
    vertices.insert(cur_edge->y_node_index());
  }
  return vertices;
}

std::unordered_map<NodeSize, EdgeSize> Graph::NodeDegrees() const {
  std::unordered_map<NodeSize, EdgeSize> node_degrees;
  for (Edge *cur_edge : edges_) {
    NodeSize x_index = cur_edge->x_node_index();
    NodeSize y_index = cur_edge->y_node_index();
    auto x_it = node_degrees.find(x_index);
    auto y_it = node_degrees.find(y_index);
    if (x_it == node_degrees.end())
      x_it = node_degrees.insert({x_index, 0}).first;
    if (y_it == node_degrees.end())
      y_it = node_degrees.insert({y_index, 0}).first;
    x_it->second += 1;
    y_it->second += 1;
  }
  return node_degrees;
}

std::map<std::pair<NodeSize, NodeSize>, std::vector<Edge *>>
Graph::NodePairToEdges() const {
  std::map<std::pair<NodeSize, NodeSize>, std::vector<Edge *>> result;
  for (Edge *cur_edge : edges_) {
    auto node_pair = cur_edge->OrderedNodePair();
    auto node_pair_it = result.find(node_pair);
    if (node_pair_it == result.end())
      node_pair_it = result.insert({node_pair, std::vector<Edge *>()}).first;
    node_pair_it->second.push_back(cur_edge);
  }
  return result;
}

unique_ptr<Graph> Graph::GraphFromEdgeList(std::vector<Edge *> edge_list) {
  return unique_ptr<Graph>(new Graph(std::move(edge_list), false));
}

Graph *Graph::GraphFromStolenEdgeList(std::vector<Edge *> edge_list) {
  return new Graph(std::move(edge_list), true);
}

json Graph::to_json() const {
  std::vector<json> edges_as_json;
  for (Edge *cur_edge : edges_) {
    edges_as_json.push_back(cur_edge->to_json());
  }
  return json({{"__Graph__", true}, {"edges", edges_as_json}});
}

std::unordered_map<NodeSize, std::vector<Edge *>>
Graph::IncidenceMapFromEdgeList(const std::vector<Edge *> &edges) {
  std::unordered_map<NodeSize, std::vector<Edge *>> incidence_map;
  for (Edge *cur_edge : edges) {
    NodeSize x_index = cur_edge->x_node_index();
    NodeSize y_index = cur_edge->y_node_index();
    auto x_it = incidence_map.find(x_index);
    auto y_it = incidence_map.find(y_index);
    if (x_it == incidence_map.end())
      x_it = incidence_map.insert({x_index, {}}).first;
    if (y_it == incidence_map.end())
      y_it = incidence_map.insert({y_index, {}}).first;
    x_it->second.push_back(cur_edge);
    y_it->second.push_back(cur_edge);
  }
  return incidence_map;
}

Edge *Graph::EdgeFromJson(const json &input_json) {
  if (node_pair_to_edges_.size() == 0 && edges_.size() != 0) {
    node_pair_to_edges_ = NodePairToEdges();
  }
  const NodeSize first_node = input_json.find("x").value();
  const NodeSize second_node = input_json.find("y").value();
  std::string input_edge_name = input_json.find("name").value();
  std::pair<NodeSize, NodeSize> node_pair_key = std::make_pair(
      std::min(first_node, second_node), std::max(first_node, second_node));
  auto result_edges = node_pair_to_edges_.find(node_pair_key);
  assert(result_edges != node_pair_to_edges_.end());
  Edge *matched_edge = nullptr;
  for (Edge *cur_edge : result_edges->second) {
    if (cur_edge->edge_name() == input_edge_name) {
      matched_edge = cur_edge;
      break;
    }
  }
  return matched_edge != nullptr ? matched_edge : result_edges->second[0];
}
} // namespace hierarchical_map
