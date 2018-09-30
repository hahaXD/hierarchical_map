//
// Created by Jason Shen on 5/21/18.
//

#include <algorithm>
#include <hierarchical_map/edge.h>

namespace {
using std::unordered_set;
}
namespace hierarchical_map {

Edge::Edge(std::string edge_name, NodeSize x_node_index, NodeSize y_node_index)
    : edge_name_(std::move(edge_name)),
      x_node_index_(std::min(x_node_index, y_node_index)),
      y_node_index_(std::max(x_node_index, y_node_index)) {}

NodeSize Edge::x_node_index() const { return x_node_index_; }

NodeSize Edge::y_node_index() const { return y_node_index_; }

const std::string &Edge::edge_name() const { return edge_name_; }
std::pair<NodeSize, NodeSize> Edge::OrderedNodePair() const {
  return {x_node_index_, y_node_index_};
}
json Edge::to_json() const {
  return {{"__Edge__", true},
          {"x", x_node_index_},
          {"y", y_node_index_},
          {"name", edge_name_}};
}
NodeSize Edge::OtherEnd(NodeSize one_end) const {
  if (one_end == x_node_index_) {
    return y_node_index_;
  } else {
    assert(y_node_index_ == one_end);
    return x_node_index_;
  }
}

unordered_set<NodeSize>
Edge::ReleventEndPoints(const unordered_set<NodeSize> &nodes) const {
  unordered_set<NodeSize> relevent_end_points;
  if (nodes.find(x_node_index_) != nodes.end()) {
    relevent_end_points.insert(x_node_index_);
  }
  if (nodes.find(y_node_index_) != nodes.end()) {
    relevent_end_points.insert(y_node_index_);
  }
  return relevent_end_points;
}

} // namespace hierarchical_map
