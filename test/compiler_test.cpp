//
// Created by Jason Shen on 6/6/18.
//
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <hierarchical_map/edge.h>
#include <hierarchical_map/graph.h>
#include <hierarchical_map/map_network.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include <psdd/psdd_manager.h>
#include <unordered_map>
extern "C" {
#include <sdd/sddapi.h>
}

#ifndef STR
#define STR(x) VAR(x)
#endif
#ifndef VAR
#define VAR(x) #x
#endif
using nlohmann::json;
namespace {
using hierarchical_map::MapNetwork;
json GenerateSmall3LayerSpec() {
  json spec;
  spec["node_size"] = 8;
  spec["edge_size"] = 16;
  std::vector<json> edges;
  for (auto i = 0; i < 16; i += 2) {
    auto src_node = i / 2;
    auto dst_node = (i / 2 + 1) % 8;
    edges.push_back({{"__Edge__", true},
                     {"x", src_node},
                     {"y", dst_node},
                     {"name", std::to_string(i)}});
    edges.push_back({{"__Edge__", true},
                     {"x", src_node},
                     {"y", dst_node},
                     {"name", std::to_string(i + 1)}});
  }
  spec["edges"] = json(edges);
  json cluster;
  cluster["root"] = {{"sub_clusters", {"root0", "root1"}}};
  cluster["root0"] = {{"sub_clusters", {"root00", "root01"}}};
  cluster["root1"] = {{"sub_clusters", {"root10", "root11"}}};
  cluster["root00"] = {{"nodes", {0, 1}}};
  cluster["root01"] = {{"nodes", {2, 3}}};
  cluster["root10"] = {{"nodes", {4, 5}}};
  cluster["root11"] = {{"nodes", {6, 7}}};
  spec["clusters"] = cluster;
  return spec;
}
} // namespace

TEST(BINARY_HIERARCHICAL_MAP_COMPILER_TEST, SANITY_CHECK_TEST) {
  std::string src_directory(STR(SRC_DIRECTORY));
  auto json_spec = GenerateSmall3LayerSpec();
  MapNetwork *network = MapNetwork::MapNetworkFromJsonSpec(json_spec);
  auto result = network->CompileConstraint(
      src_directory + "/script/compile_graph.py", "/tmp", 4);
  PsddNode *psdd_node = result.first;
  EXPECT_EQ(
      psdd_node_util::ModelCount(psdd_node_util::SerializePsddNodes(psdd_node))
          .get_str(10),
      "944");
  delete (network);
  delete (result.second);
}
