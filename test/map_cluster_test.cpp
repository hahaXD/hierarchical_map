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

#ifndef STR
#define STR(x) VAR(x)
#endif
#ifndef VAR
#define VAR(x) #x
#endif

namespace {
using hierarchical_map::Edge;
using hierarchical_map::InternalClusterParameterPerCase;
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

MATCHER_P(InternalClusterParameterPerCaseEq, target_parameter, "") {
  constexpr double kDiffMax = 0.0001;
  for (const auto &lr_cut_edge_param_pair :
       target_parameter.lr_cut_edges_freq) {
    auto cur_arg_it = arg.lr_cut_edges_freq.find(lr_cut_edge_param_pair.first);
    if (cur_arg_it == arg.lr_cut_edges_freq.end()) {
      return false;
    }
    if (std::abs(cur_arg_it->second.parameter() -
                 lr_cut_edge_param_pair.second.parameter()) > kDiffMax) {
      return false;
    }
  }
  if (std::abs(arg.zero_lr_cut_edge_freq.parameter() -
               target_parameter.zero_lr_cut_edge_freq.parameter()) > kDiffMax) {
    return false;
  }
  if (std::abs(arg.left_internal_freq.parameter() -
               target_parameter.left_internal_freq.parameter()) > kDiffMax) {
    return false;
  }
  if (std::abs(arg.right_internal_freq.parameter() -
               target_parameter.right_internal_freq.parameter()) > kDiffMax) {
    return false;
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

void test_singleton_distribution(
    PsddNode *root_node, SddLiteral variable_index,
    const std::pair<PsddParameter, PsddParameter> &dist) {
  std::bitset<MAX_VAR> variables;
  std::bitset<MAX_VAR> instantiation;
  variables.set(variable_index);
  instantiation.reset(variable_index);
  EXPECT_THAT(
      psdd_node_util::Evaluate(variables, instantiation, root_node).parameter(),
      ::testing::DoubleNear(dist.first.parameter(), 0.0001));
  instantiation.set(variable_index);
  EXPECT_THAT(
      psdd_node_util::Evaluate(variables, instantiation, root_node).parameter(),
      ::testing::DoubleNear(dist.second.parameter(), 0.0001));
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
  auto learning_state =
      lu_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_EQ(learning_state.param_learning_state_internal.routes_in_leaf_cluster
                .size(),
            static_cast<size_t>(0));
  EXPECT_EQ(learning_state.param_learning_state_non_terminal.size(),
            static_cast<size_t>(1));
  auto expected_external_edge_pair =
      make_ordered_edge_pair(GetEdgeBetweenNode(1, 2, node_pair_to_edges),
                             GetEdgeBetweenNode(9, 5, node_pair_to_edges));
  EXPECT_THAT(
      learning_state
          .param_learning_state_non_terminal[expected_external_edge_pair]
          .routes_in_leaf_cluster,
      ElementsAre(std::vector<Edge *>(
          1, GetEdgeBetweenNode(1, 5, node_pair_to_edges))));
  EXPECT_TRUE(learning_state.param_learning_state_terminal.empty());
  // Checks ll_cluster
  learning_state = ll_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_THAT(learning_state
                  .param_learning_state_terminal[GetEdgeBetweenNode(
                      5, 9, node_pair_to_edges)]
                  .routes_in_leaf_cluster,
              ElementsAre(std::vector<Edge *>(
                  1, GetEdgeBetweenNode(8, 9, node_pair_to_edges))));
  // Checks left_cluster
  learning_state = left_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_EQ(learning_state
                .param_learning_state_terminal[GetEdgeBetweenNode(
                    1, 2, node_pair_to_edges)]
                .lr_cut_edges_freq.size(),
            static_cast<size_t>(1));
  EXPECT_EQ(
      learning_state
          .param_learning_state_terminal[GetEdgeBetweenNode(1, 2,
                                                            node_pair_to_edges)]
          .lr_cut_edges_freq[GetEdgeBetweenNode(5, 9, node_pair_to_edges)],
      1);
  // Checks root_cluster
  learning_state = root_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_EQ(
      learning_state.param_learning_state_internal.lr_cut_edges_freq.size(),
      static_cast<size_t>(1));
  EXPECT_EQ(
      learning_state.param_learning_state_internal
          .lr_cut_edges_freq[GetEdgeBetweenNode(1, 2, node_pair_to_edges)],
      1);
  EXPECT_EQ(learning_state.param_learning_state_internal.left_internal_freq, 0);
  EXPECT_EQ(learning_state.param_learning_state_internal.right_internal_freq,
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
  auto learning_state =
      root_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_TRUE(
      learning_state.param_learning_state_internal.lr_cut_edges_freq.empty());
  EXPECT_EQ(learning_state.param_learning_state_internal.left_internal_freq, 1);
  EXPECT_EQ(learning_state.param_learning_state_internal.right_internal_freq,
            0);

  // Checks left_cluster
  learning_state = left_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_EQ(
      learning_state.param_learning_state_internal.lr_cut_edges_freq.size(),
      static_cast<size_t>(2));
  EXPECT_EQ(
      learning_state.param_learning_state_internal
          .lr_cut_edges_freq[GetEdgeBetweenNode(4, 8, node_pair_to_edges)],
      1);
  EXPECT_EQ(
      learning_state.param_learning_state_internal
          .lr_cut_edges_freq[GetEdgeBetweenNode(5, 9, node_pair_to_edges)],
      1);

  // Checks lu_cluster
  learning_state = lu_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_EQ(learning_state.param_learning_state_non_terminal.size(),
            static_cast<size_t>(1));
  auto expected_external_edge_pair =
      make_ordered_edge_pair(GetEdgeBetweenNode(4, 8, node_pair_to_edges),
                             GetEdgeBetweenNode(5, 9, node_pair_to_edges));
  EXPECT_THAT(
      learning_state
          .param_learning_state_non_terminal[expected_external_edge_pair]
          .routes_in_leaf_cluster,
      ElementsAre(std::vector<Edge *>(
          1, GetEdgeBetweenNode(4, 5, node_pair_to_edges))));
  EXPECT_TRUE(learning_state.param_learning_state_terminal.empty());
  // Checks ll_cluster
  learning_state = ll_cluster->UpdateParameterLearningStateSimple({route_a});
  EXPECT_EQ(learning_state.param_learning_state_terminal.size(),
            static_cast<size_t>(2));
  EXPECT_THAT(learning_state
                  .param_learning_state_terminal[GetEdgeBetweenNode(
                      4, 8, node_pair_to_edges)]
                  .routes_in_leaf_cluster,
              ElementsAre(std::vector<Edge *>()));
  EXPECT_THAT(learning_state
                  .param_learning_state_terminal[GetEdgeBetweenNode(
                      5, 9, node_pair_to_edges)]
                  .routes_in_leaf_cluster,
              ElementsAre(std::vector<Edge *>()));
}

TEST(MAP_CLUSTER_SPLIT_ROUTE_TEST,
     InternalClusterTerminalCaseNonTerminalSameChild) {
  std::string src_directory(STR(SRC_DIRECTORY));
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
  // Create training data with 2-1-5-6
  auto node_pair_to_edges = network_graph->NodePairToEdges();
  std::vector<Edge *> route_1 = {GetEdgeBetweenNode(1, 2, node_pair_to_edges),
                                 GetEdgeBetweenNode(1, 5, node_pair_to_edges),
                                 GetEdgeBetweenNode(5, 6, node_pair_to_edges)};
  std::vector<std::vector<Edge *>> training_routes = {route_1};
  test_network->LearnWithRoutes(src_directory + "/script/compile_graph.py",
                                "/tmp", 4, training_routes);
  auto map_clusters_by_name = test_network->GetMapClustersByName();
  InternalClusterParameterPerCase target_parameter;
  // Test non terminal parameters in left cluster
  Edge *edge_1_2 = GetEdgeBetweenNode(1, 2, node_pair_to_edges);
  Edge *edge_5_6 = GetEdgeBetweenNode(5, 6, node_pair_to_edges);
  target_parameter.zero_lr_cut_edge_freq =
      PsddParameter::CreateFromDecimal(1.0);
  EXPECT_THAT(map_clusters_by_name["left"]
                  ->internal_cluster_non_terminal_parameter()
                  .find(make_ordered_edge_pair(edge_1_2, edge_5_6))
                  ->second,
              InternalClusterParameterPerCaseEq(target_parameter));
}

TEST(MAP_CLUSTER_SPLIT_ROUTE_TEST, LearnParametersTest) {
  std::string src_directory(STR(SRC_DIRECTORY));
  auto test_network =
      hierarchical_map::testing::CreateSimple3LayerNetworkLinearLeaf(2);
  // Graph structure
  // 0 - 1 - 2 - 3
  // |   |   |   |
  // 4 - 5 - 6 - 7
  Graph *network_graph = test_network->graph();
  // Create training data with 0-4-5-1
  auto node_pair_to_edges = network_graph->NodePairToEdges();
  std::vector<Edge *> route_1 = {GetEdgeBetweenNode(0, 4, node_pair_to_edges),
                                 GetEdgeBetweenNode(4, 5, node_pair_to_edges),
                                 GetEdgeBetweenNode(5, 1, node_pair_to_edges)};
  std::vector<std::vector<Edge *>> training_routes = {route_1};
  test_network->LearnWithRoutes(src_directory + "/script/compile_graph.py",
                                "/tmp", 4, training_routes);
  auto map_clusters_by_name = test_network->GetMapClustersByName();
  InternalClusterParameterPerCase target_parameter;
  // Test internal parameters in root cluster
  for (Edge *cur_lr_cut_edge : map_clusters_by_name["total"]->lr_cut_edges()) {
    target_parameter.lr_cut_edges_freq[cur_lr_cut_edge] =
        PsddParameter::CreateFromDecimal(1.0 / 5);
  }
  target_parameter.left_internal_freq =
      PsddParameter::CreateFromDecimal(2.0 / 5);
  target_parameter.right_internal_freq =
      PsddParameter::CreateFromDecimal(1.0 / 5);
  EXPECT_THAT(
      map_clusters_by_name["total"]->internal_cluster_internal_parameter(),
      InternalClusterParameterPerCaseEq(target_parameter));
  // Test terminal parameters in left cluster
  target_parameter = InternalClusterParameterPerCase();
  target_parameter.zero_lr_cut_edge_freq =
      PsddParameter::CreateFromDecimal(1.0 / 3);
  target_parameter
      .lr_cut_edges_freq[GetEdgeBetweenNode(0, 4, node_pair_to_edges)] =
      PsddParameter::CreateFromDecimal(1.0 / 3);
  target_parameter
      .lr_cut_edges_freq[GetEdgeBetweenNode(1, 5, node_pair_to_edges)] =
      PsddParameter::CreateFromDecimal(1.0 / 3);
  // With external edge (1,2)
  auto cur_terminal_case_it =
      map_clusters_by_name["left"]->internal_cluster_terminal_parameter().find(
          GetEdgeBetweenNode(1, 2, node_pair_to_edges));
  ASSERT_NE(cur_terminal_case_it, map_clusters_by_name["left"]
                                      ->internal_cluster_terminal_parameter()
                                      .end());
  EXPECT_THAT(cur_terminal_case_it->second,
              InternalClusterParameterPerCaseEq(target_parameter));
  // With external edge (5,6)
  cur_terminal_case_it =
      map_clusters_by_name["left"]->internal_cluster_terminal_parameter().find(
          GetEdgeBetweenNode(5, 6, node_pair_to_edges));
  ASSERT_NE(cur_terminal_case_it, map_clusters_by_name["left"]
                                      ->internal_cluster_terminal_parameter()
                                      .end());
  EXPECT_THAT(cur_terminal_case_it->second,
              InternalClusterParameterPerCaseEq(target_parameter));
  // Test non terminal parameters in left cluster with external edges (1,2)
  // (5,6)
  auto cur_non_terminal_case_it =
      map_clusters_by_name["left"]
          ->internal_cluster_non_terminal_parameter()
          .find(make_ordered_edge_pair(
              GetEdgeBetweenNode(1, 2, node_pair_to_edges),
              GetEdgeBetweenNode(5, 6, node_pair_to_edges)));
  ASSERT_NE(cur_non_terminal_case_it,
            map_clusters_by_name["left"]
                ->internal_cluster_non_terminal_parameter()
                .end());
  target_parameter = InternalClusterParameterPerCase();
  target_parameter
      .lr_cut_edges_freq[GetEdgeBetweenNode(0, 4, node_pair_to_edges)] =
      PsddParameter::CreateFromDecimal(1.0 / 2);
  target_parameter
      .lr_cut_edges_freq[GetEdgeBetweenNode(1, 5, node_pair_to_edges)] =
      PsddParameter::CreateFromDecimal(1.0 / 2);
  EXPECT_THAT(cur_non_terminal_case_it->second,
              InternalClusterParameterPerCaseEq(target_parameter));
  // Test internal parameters in left cluster
  target_parameter = InternalClusterParameterPerCase();
  for (Edge *cur_edge : map_clusters_by_name["left"]->lr_cut_edges()) {
    target_parameter.lr_cut_edges_freq[cur_edge] =
        PsddParameter::CreateFromDecimal(2.0 / 6);
  }
  target_parameter.left_internal_freq =
      PsddParameter::CreateFromDecimal(1.0 / 6);
  target_parameter.right_internal_freq =
      PsddParameter::CreateFromDecimal(1.0 / 6);
  EXPECT_THAT(
      map_clusters_by_name["left"]->internal_cluster_internal_parameter(),
      InternalClusterParameterPerCaseEq(target_parameter));
  // Test internal parameter in ll cluster
  SddLiteral edge_index =
      map_clusters_by_name["ll"]
          ->edge_variable_map()
          ->find(GetEdgeBetweenNode(4, 5, node_pair_to_edges))
          ->second;
  test_singleton_distribution(
      map_clusters_by_name["ll"]->leaf_cluster_internal_path_distribution(),
      edge_index,
      std::make_pair(PsddParameter::CreateFromDecimal(0.0),
                     PsddParameter::CreateFromDecimal(1.0)));
  // Test terminal parameters in ll
  // with external_edge (0,4)
  Edge *edge_0_4 = GetEdgeBetweenNode(0, 4, node_pair_to_edges);
  Edge *edge_1_5 = GetEdgeBetweenNode(1, 5, node_pair_to_edges);
  Edge *edge_5_6 = GetEdgeBetweenNode(6, 5, node_pair_to_edges);
  test_singleton_distribution(
      map_clusters_by_name["ll"]
          ->leaf_cluster_terminal_path_distribution()
          .find(edge_0_4)
          ->second,
      edge_index,
      std::make_pair(PsddParameter::CreateFromDecimal(1.0 / 2),
                     PsddParameter::CreateFromDecimal(1.0 / 2)));
  // with external_edge (1,5)
  test_singleton_distribution(
      map_clusters_by_name["ll"]
          ->leaf_cluster_terminal_path_distribution()
          .find(edge_1_5)
          ->second,
      edge_index,
      std::make_pair(PsddParameter::CreateFromDecimal(1.0 / 2),
                     PsddParameter::CreateFromDecimal(1.0 / 2)));
  // with external_edge (5,6)
  test_singleton_distribution(
      map_clusters_by_name["ll"]
          ->leaf_cluster_terminal_path_distribution()
          .find(edge_5_6)
          ->second,
      edge_index,
      std::make_pair(PsddParameter::CreateFromDecimal(1.0 / 2),
                     PsddParameter::CreateFromDecimal(1.0 / 2)));
  // with non_terminal_edge (0,4) (1,5)
  test_singleton_distribution(
      map_clusters_by_name["ll"]
          ->leaf_cluster_non_terminal_path_distribution()
          .find(make_ordered_edge_pair(edge_0_4, edge_1_5))
          ->second,
      edge_index,
      std::make_pair(PsddParameter::CreateFromDecimal(0.0),
                     PsddParameter::CreateFromDecimal(1.0)));
  // with non_terminal_edge (0,4) (5,6)
  test_singleton_distribution(
      map_clusters_by_name["ll"]
          ->leaf_cluster_non_terminal_path_distribution()
          .find(make_ordered_edge_pair(edge_0_4, edge_5_6))
          ->second,
      edge_index,
      std::make_pair(PsddParameter::CreateFromDecimal(0.0),
                     PsddParameter::CreateFromDecimal(1.0)));
  // with non_terminal_edge (1,5) (5,6)
  test_singleton_distribution(
      map_clusters_by_name["ll"]
          ->leaf_cluster_non_terminal_path_distribution()
          .find(make_ordered_edge_pair(edge_1_5, edge_5_6))
          ->second,
      edge_index,
      std::make_pair(PsddParameter::CreateFromDecimal(1.0),
                     PsddParameter::CreateFromDecimal(0.0)));
  // Test terminal parameter in lu with terminal_edge (0,4)
  edge_index = map_clusters_by_name["lu"]
                   ->edge_variable_map()
                   ->find(GetEdgeBetweenNode(0, 1, node_pair_to_edges))
                   ->second;
  test_singleton_distribution(
      map_clusters_by_name["lu"]
          ->leaf_cluster_terminal_path_distribution()
          .find(edge_0_4)
          ->second,
      edge_index,
      std::make_pair(PsddParameter::CreateFromDecimal(2.0 / 3),
                     PsddParameter::CreateFromDecimal(1.0 / 3)));
  // Test terminal parameter in lu with terminal_edge (1,5)
  test_singleton_distribution(
      map_clusters_by_name["lu"]
          ->leaf_cluster_terminal_path_distribution()
          .find(edge_1_5)
          ->second,
      edge_index,
      std::make_pair(PsddParameter::CreateFromDecimal(2.0 / 3),
                     PsddParameter::CreateFromDecimal(1.0 / 3)));
}

TEST(MAP_CLUSTER_SPLIT_ROUTE_TEST, InferenceTest) {
  std::string src_directory(STR(SRC_DIRECTORY));
  auto test_network =
      hierarchical_map::testing::CreateSimple3LayerNetworkLinearLeaf(2);
  // Graph structure
  // 0 - 1 - 2 - 3
  // |   |   |   |
  // 4 - 5 - 6 - 7
  Graph *network_graph = test_network->graph();
  // Create training data with 0-4-5-1
  auto node_pair_to_edges = network_graph->NodePairToEdges();
  std::vector<Edge *> route_1 = {GetEdgeBetweenNode(0, 4, node_pair_to_edges),
                                 GetEdgeBetweenNode(4, 5, node_pair_to_edges),
                                 GetEdgeBetweenNode(5, 1, node_pair_to_edges)};
  std::vector<std::vector<Edge *>> training_routes = {route_1};
  test_network->LearnWithRoutes(src_directory + "/script/compile_graph.py",
                                "/tmp", 4, training_routes);
  Probability learning_pr = test_network->Evaluate({route_1});
  Probability expected_pr = PsddParameter::CreateFromDecimal(
      2.0 / 5 * 1.0 / 3 * 1.0 / 3 * 2.0 / 3 * 2.0 / 3);
  EXPECT_THAT(learning_pr.parameter(),
              ::testing::DoubleNear(expected_pr.parameter(), 0.0001));
}
} // namespace hierarchical_map
