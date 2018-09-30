//
// Created by Jason Shen on 9/22/18.
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hierarchical_map/edge.h>
#include <hierarchical_map/map_cluster.h>
#include <hierarchical_map_test/test_util.h>
#include <memory>
#include <unordered_set>
#include <vector>

namespace {
using hierarchical_map::Edge;
using hierarchical_map::MapCluster;
using std::unordered_set;
using testing::ElementsAre;
using testing::WhenSorted;
MATCHER_P(EdgeListEq, expected_edges, "") {
  if (arg.size() != expected_edges.size()) {
    return false;
  }
  unordered_set<Edge *> expected_set;
  for (Edge *cur_edge : expected_edges) {
    expected_set.insert(cur_edge);
  }
  for (Edge *cur_edge : arg) {
    auto cur_edge_it = expected_set.find(cur_edge);
    if (cur_edge_it == expected_set.end()) {
      return false;
    }
    expected_set.erase(cur_edge_it);
  }
  return true;
}
void ExpectEdgeListEq(const std::vector<Edge *> &first,
                      const std::vector<Edge *> &second) {
  EXPECT_THAT(first, EdgeListEq(second));
}

Edge *GetEdgeBetweenNode(NodeSize node_a, NodeSize node_b,
                         const std::map<std::pair<NodeSize, NodeSize>,
                                        std::vector<Edge *>> &edge_map) {
  auto edge_it = edge_map.find(
      std::make_pair(std::min(node_a, node_b), std::max(node_a, node_b)));
  if (edge_it == edge_map.end()) {
    return nullptr;
  } else {
    return edge_it->second[0];
  }
}

std::pair<Edge *, Edge *> make_ordered_edge_pair(Edge *a, Edge *b) {
  return std::make_pair(std::min(a, b), std::max(a, b));
}

} // namespace

namespace hierarchical_map {
TEST(MAP_CLUSTER_SPLIT_ROUTE_TEST, SIMPLE_TEST) {
  unordered_set<NodeSize> nodes;
  nodes.insert(0);
  nodes.insert(1);
  nodes.insert(2);
  nodes.insert(3);
  nodes.insert(4);
  MapCluster cluster(1, "1", nodes, nullptr, nullptr);
  auto e0 = std::make_unique<Edge>("0", 0, 5);
  auto e1 = std::make_unique<Edge>("1", 5, 6);
  auto e2 = std::make_unique<Edge>("2", 6, 7);
  auto e3 = std::make_unique<Edge>("3", 7, 1);
  auto e4 = std::make_unique<Edge>("4", 1, 8);
  auto e5 = std::make_unique<Edge>("5", 8, 2);
  auto e6 = std::make_unique<Edge>("6", 2, 3);
  auto e7 = std::make_unique<Edge>("7", 3, 4);
  auto e8 = std::make_unique<Edge>("8", 4, 10);
  std::vector<Edge *> edges = {e0.get(), e1.get(), e2.get(), e3.get(), e4.get(),
                               e5.get(), e6.get(), e7.get(), e8.get()};
  auto components = cluster.SliceRoute(edges);
  EXPECT_TRUE(3 == components.size());
  ExpectEdgeListEq(components[0].internal_edges, {e6.get(), e7.get()});
  ExpectEdgeListEq(components[0].external_edges, {e5.get(), e8.get()});
  EXPECT_TRUE(components[1].internal_edges.size() == 0);
  ExpectEdgeListEq(components[1].external_edges, {e3.get(), e4.get()});
  EXPECT_TRUE(components[2].internal_edges.size() == 0);
  EXPECT_THAT(components[2].external_edges, ElementsAre(e0.get()));
}

TEST(MAP_CLUSTER_SPLIT_ROUTE_TEST, LEARNING_STATE_TEST) {
  auto test_network = hierarchical_map::testing::CreateSimple3LayerNetwork(2);
  // Graph structure
  // 0 - 1 - 2 - 3
  // |   |   |   |
  // 4 - 5 - 6 - 7
  // |   |   |   |
  // 8 - 9 -10 -11
  // |   |   |   |
  // 12 -13 -14 -15
  Graph *network_graph = test_network->graph();
  auto node_pair_to_edges = network_graph->NodePairToEdges();
  // Creates routes 8-9-5-1-2-6-10-14
  std::vector<Edge *> route_a = {
      GetEdgeBetweenNode(8, 9, node_pair_to_edges),
      GetEdgeBetweenNode(9, 5, node_pair_to_edges),
      GetEdgeBetweenNode(5, 1, node_pair_to_edges),
      GetEdgeBetweenNode(1, 2, node_pair_to_edges),
      GetEdgeBetweenNode(2, 6, node_pair_to_edges),
      GetEdgeBetweenNode(6, 10, node_pair_to_edges),
      GetEdgeBetweenNode(10, 14, node_pair_to_edges)};
  // Test learning state for leaf cluster
  const std::vector<MapCluster *> clusters = test_network->clusters();
  MapCluster *lu_cluster = clusters[0];
  MapCluster *ll_cluster = clusters[1];
  MapCluster *left_cluster = clusters[4];
  MapCluster *root_cluster = clusters.back();
  // Checks lu_cluster
  lu_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_EQ(
      lu_cluster->param_learning_state_internal_.routes_in_leaf_cluster_.size(),
      static_cast<size_t>(0));
  EXPECT_EQ(lu_cluster->param_learning_state_non_terminal_.size(),
            static_cast<size_t>(1));
  auto expected_external_edge_pair =
      make_ordered_edge_pair(GetEdgeBetweenNode(1, 2, node_pair_to_edges),
                             GetEdgeBetweenNode(9, 5, node_pair_to_edges));
  EXPECT_THAT(
      lu_cluster
          ->param_learning_state_non_terminal_[expected_external_edge_pair]
          .routes_in_leaf_cluster_,
      ElementsAre(std::vector<Edge *>(
          1, GetEdgeBetweenNode(1, 5, node_pair_to_edges))));
  EXPECT_TRUE(lu_cluster->param_learning_state_terminal_.empty());
  // Checks ll_cluster
  ll_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_THAT(ll_cluster
                  ->param_learning_state_terminal_[GetEdgeBetweenNode(
                      5, 9, node_pair_to_edges)]
                  .routes_in_leaf_cluster_,
              ElementsAre(std::vector<Edge *>(
                  1, GetEdgeBetweenNode(8, 9, node_pair_to_edges))));
  // Checks left_cluster
  left_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_EQ(left_cluster
                ->param_learning_state_terminal_[GetEdgeBetweenNode(
                    1, 2, node_pair_to_edges)]
                .lr_cut_edges_freq_.size(),
            static_cast<size_t>(1));
  EXPECT_EQ(
      left_cluster
          ->param_learning_state_terminal_[GetEdgeBetweenNode(
              1, 2, node_pair_to_edges)]
          .lr_cut_edges_freq_[GetEdgeBetweenNode(5, 9, node_pair_to_edges)],
      1);
  // Checks root_cluster
  root_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_EQ(
      root_cluster->param_learning_state_internal_.lr_cut_edges_freq_.size(),
      static_cast<size_t>(1));
  EXPECT_EQ(
      root_cluster->param_learning_state_internal_
          .lr_cut_edges_freq_[GetEdgeBetweenNode(1, 2, node_pair_to_edges)],
      1);
  EXPECT_EQ(root_cluster->param_learning_state_internal_.left_internal_freq_,
            0);
  EXPECT_EQ(root_cluster->param_learning_state_internal_.right_internal_freq_,
            0);
}

TEST(MAP_CLUSTER_SPLIT_ROUTE_TEST, LEARNING_STATE_TEST_WITH_ILLEGAL_ROUTE) {
  auto test_network = hierarchical_map::testing::CreateSimple3LayerNetwork(2);
  // Graph structure
  // 0 - 1 - 2 - 3
  // |   |   |   |
  // 4 - 5 - 6 - 7
  // |   |   |   |
  // 8 - 9 -10 -11
  // |   |   |   |
  // 12 -13 -14 -15
  Graph *network_graph = test_network->graph();
  auto node_pair_to_edges = network_graph->NodePairToEdges();
  // Creates routes 8-4-5-9
  std::vector<Edge *> route_a = {GetEdgeBetweenNode(8, 4, node_pair_to_edges),
                                 GetEdgeBetweenNode(4, 5, node_pair_to_edges),
                                 GetEdgeBetweenNode(5, 9, node_pair_to_edges)};
  const std::vector<MapCluster *> clusters = test_network->clusters();
  MapCluster *lu_cluster = clusters[0];
  MapCluster *ll_cluster = clusters[1];
  MapCluster *left_cluster = clusters[4];
  MapCluster *root_cluster = clusters.back();
  // Checks root_cluster
  root_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_TRUE(
      root_cluster->param_learning_state_internal_.lr_cut_edges_freq_.empty());
  EXPECT_EQ(root_cluster->param_learning_state_internal_.left_internal_freq_,
            1);
  EXPECT_EQ(root_cluster->param_learning_state_internal_.right_internal_freq_,
            0);

  // Checks left_cluster
  left_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_EQ(
      left_cluster->param_learning_state_internal_.lr_cut_edges_freq_.size(),
      static_cast<size_t>(2));
  EXPECT_EQ(
      left_cluster->param_learning_state_internal_
          .lr_cut_edges_freq_[GetEdgeBetweenNode(4, 8, node_pair_to_edges)],
      1);
  EXPECT_EQ(
      left_cluster->param_learning_state_internal_
          .lr_cut_edges_freq_[GetEdgeBetweenNode(5, 9, node_pair_to_edges)],
      1);

  // Checks lu_cluster
  lu_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_EQ(lu_cluster->param_learning_state_non_terminal_.size(),
            static_cast<size_t>(1));
  auto expected_external_edge_pair =
      make_ordered_edge_pair(GetEdgeBetweenNode(4, 8, node_pair_to_edges),
                             GetEdgeBetweenNode(5, 9, node_pair_to_edges));
  EXPECT_THAT(
      lu_cluster
          ->param_learning_state_non_terminal_[expected_external_edge_pair]
          .routes_in_leaf_cluster_,
      ElementsAre(std::vector<Edge *>(
          1, GetEdgeBetweenNode(4, 5, node_pair_to_edges))));
  EXPECT_TRUE(lu_cluster->param_learning_state_terminal_.empty());
  // Checks ll_cluster
  ll_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_EQ(ll_cluster->param_learning_state_terminal_.size(),
            static_cast<size_t>(2));
  EXPECT_THAT(ll_cluster
                  ->param_learning_state_terminal_[GetEdgeBetweenNode(
                      4, 8, node_pair_to_edges)]
                  .routes_in_leaf_cluster_,
              ElementsAre(std::vector<Edge *>()));
  EXPECT_THAT(ll_cluster
                  ->param_learning_state_terminal_[GetEdgeBetweenNode(
                      5, 9, node_pair_to_edges)]
                  .routes_in_leaf_cluster_,
              ElementsAre(std::vector<Edge *>()));
}

} // namespace hierarchical_map
