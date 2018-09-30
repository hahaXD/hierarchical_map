#include <array>
#include <cinttypes>
#include <fstream>
#include <hierarchical_map/leaf_constraint_handler.h>
#include <hierarchical_map/map_cluster.h>
#include <iostream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
extern "C" {
#include <sdd/sddapi.h>
}

namespace {
using hierarchical_map::Edge;
using nlohmann::json;
using std::istringstream;
using std::map;
using std::max;
using std::min;
using std::pair;
using std::set;
using std::string;
using std::unordered_map;
using std::vector;

Vtree *
VtreeSwapVariables(Vtree *orig_vtree,
                   std::unordered_map<SddLiteral, SddLiteral> &variable_map) {
  std::vector<Vtree *> serialized_vtrees =
      vtree_util::SerializeVtree(orig_vtree);
  for (auto vit = serialized_vtrees.rbegin(); vit != serialized_vtrees.rend();
       ++vit) {
    Vtree *cur_node = *vit;
    Vtree *new_node = nullptr;
    if (sdd_vtree_is_leaf(cur_node)) {
      SddLiteral cur_leaf_variable = sdd_vtree_var(cur_node);
      assert(variable_map.find(cur_leaf_variable) != variable_map.end());
      new_node = new_leaf_vtree(variable_map[cur_leaf_variable]);
    } else {
      Vtree *left_child = sdd_vtree_left(cur_node);
      Vtree *right_child = sdd_vtree_right(cur_node);
      auto new_left_child = (Vtree *)sdd_vtree_data(left_child);
      auto new_right_child = (Vtree *)sdd_vtree_data(right_child);
      sdd_vtree_set_data(nullptr, left_child);
      sdd_vtree_set_data(nullptr, right_child);
      new_node = new_internal_vtree(new_left_child, new_right_child);
    }
    sdd_vtree_set_data((void *)new_node, cur_node);
  }
  auto result = (Vtree *)sdd_vtree_data(orig_vtree);
  sdd_vtree_set_data(nullptr, orig_vtree);
  set_vtree_properties(result);
  return result;
}

SddNode *ExactlyOne(const vector<SddLiteral> &variables,
                    SddManager *sdd_manager) {
  SddNode *result = sdd_manager_false(sdd_manager);
  for (SddLiteral i : variables) {
    SddNode *cur_term = sdd_manager_true(sdd_manager);
    for (SddLiteral j : variables) {
      if (i != j) {
        cur_term = sdd_conjoin(sdd_manager_literal(-j, sdd_manager), cur_term,
                               sdd_manager);
      } else {
        cur_term = sdd_conjoin(sdd_manager_literal(j, sdd_manager), cur_term,
                               sdd_manager);
      }
    }
    result = sdd_disjoin(cur_term, result, sdd_manager);
  }
  return result;
}

SddNode *NegativeTerm(const vector<SddLiteral> &variables,
                      SddManager *sdd_manager) {
  SddNode *result = sdd_manager_true(sdd_manager);
  for (SddLiteral i : variables) {
    result =
        sdd_conjoin(sdd_manager_literal(-i, sdd_manager), result, sdd_manager);
  }
  return result;
}

SddNode *CompleteZddChild(
    SddLiteral variable_index, const string &child,
    const unordered_map<uintmax_t, SddNode *> &conversion_map,
    const unordered_map<uintmax_t, SddLiteral> &decision_variable_map,
    const vector<SddNode *> &neg_zdd_literals, SddLiteral zdd_variable_size,
    SddManager *sdd_manager) {
  if (child == "T") {
    SddNode *skipped_negative_term = sdd_manager_true(sdd_manager);
    for (SddLiteral i = variable_index + 1; i <= zdd_variable_size; ++i) {
      skipped_negative_term =
          sdd_conjoin(skipped_negative_term, neg_zdd_literals[i], sdd_manager);
    }
    return skipped_negative_term;
  } else if (child == "B") {
    return sdd_manager_false(sdd_manager);
  } else {
    uintmax_t child_index = std::strtoumax(child.c_str(), nullptr, 10);
    auto child_variable_it = decision_variable_map.find(child_index);
    assert(child_variable_it != decision_variable_map.end());
    SddLiteral child_variable = child_variable_it->second;
    auto child_sdd_node_it = conversion_map.find(child_index);
    assert(child_sdd_node_it != conversion_map.end());
    SddNode *child_sdd_node = child_sdd_node_it->second;
    for (SddLiteral i = variable_index + 1; i < child_variable; ++i) {
      child_sdd_node =
          sdd_conjoin(child_sdd_node, neg_zdd_literals[i], sdd_manager);
    }
    return child_sdd_node;
  }
}

SddNode *ZddStringToSdd(const string &zdd_string,
                        const vector<vector<SddLiteral>> &zdd_to_sdd_map,
                        SddManager *sdd_manager) {
  vector<SddNode *> pos_zdd_indicator_to_sdd(zdd_to_sdd_map.size(), nullptr);
  vector<SddNode *> neg_zdd_indicator_to_sdd(zdd_to_sdd_map.size(), nullptr);
  for (SddLiteral l = 1; l < (SddLiteral)zdd_to_sdd_map.size(); ++l) {
    pos_zdd_indicator_to_sdd[l] = ExactlyOne(zdd_to_sdd_map[l], sdd_manager);
    neg_zdd_indicator_to_sdd[l] = NegativeTerm(zdd_to_sdd_map[l], sdd_manager);
  }
  istringstream f(zdd_string);
  string line;
  unordered_map<uintmax_t, SddNode *> conversion_map;
  unordered_map<uintmax_t, SddLiteral> decision_variable_map;
  SddNode *last_node = nullptr;
  SddLiteral last_decision_variable = 0;
  while (std::getline(f, line)) {
    if (line[0] == '.') {
      continue;
    } else if (line[0] == 'T') {
      // must be the only line
      SddLiteral variable_size = sdd_manager_var_count(sdd_manager);
      SddNode *cur_node = sdd_manager_true(sdd_manager);
      for (SddLiteral l = 1; l <= variable_size; ++l) {
        cur_node = sdd_conjoin(sdd_manager_literal(-l, sdd_manager), cur_node,
                               sdd_manager);
      }
      return cur_node;
    } else if (line[0] == 'B') {
      // must be the the only line
      return sdd_manager_false(sdd_manager);
    } else {
      istringstream cur_line_stream(line);
      uintmax_t node_index;
      SddLiteral variable_index;
      string low_child;
      string high_child;
      cur_line_stream >> node_index >> variable_index >> low_child >>
          high_child;
      decision_variable_map[node_index] = variable_index;
      SddNode *sdd_low_child = CompleteZddChild(
          variable_index, low_child, conversion_map, decision_variable_map,
          neg_zdd_indicator_to_sdd, zdd_to_sdd_map.size() - 1, sdd_manager);
      SddNode *sdd_high_child = CompleteZddChild(
          variable_index, high_child, conversion_map, decision_variable_map,
          neg_zdd_indicator_to_sdd, zdd_to_sdd_map.size() - 1, sdd_manager);
      SddNode *positive_element =
          sdd_conjoin(pos_zdd_indicator_to_sdd[variable_index], sdd_high_child,
                      sdd_manager);
      SddNode *negative_element = sdd_conjoin(
          neg_zdd_indicator_to_sdd[variable_index], sdd_low_child, sdd_manager);
      SddNode *cur_node =
          sdd_disjoin(positive_element, negative_element, sdd_manager);
      conversion_map[node_index] = cur_node;
      last_node = cur_node;
      last_decision_variable = variable_index;
    }
  }
  assert(last_node != nullptr);
  if (last_decision_variable != 1) {
    for (SddLiteral i = 1; i < last_decision_variable; ++i) {
      last_node =
          sdd_conjoin(neg_zdd_indicator_to_sdd[i], last_node, sdd_manager);
    }
  }

  return last_node;
}
string exec(const char *cmd) {
  std::array<char, 128> buffer;
  std::string result;
  std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
  if (!pipe)
    throw std::runtime_error("popen() failed!");
  while (!feof(pipe.get())) {
    if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
      result += buffer.data();
  }
  return result;
}

void LeftToRightVtreeLeafTraverse(vector<SddLiteral> *result, Vtree *root) {
  if (sdd_vtree_is_leaf(root)) {
    result->push_back(sdd_vtree_var(root));
  } else {
    LeftToRightVtreeLeafTraverse(result, sdd_vtree_left(root));
    LeftToRightVtreeLeafTraverse(result, sdd_vtree_right(root));
  }
}

json ConstructProblemSpecJson(
    const map<pair<NodeSize, NodeSize>, vector<Edge *>> &node_pair_to_edges,
    const set<NodeSize> &terminal_nodes,
    const set<pair<NodeSize, NodeSize>> &non_terminal_nodes) {
  std::vector<json> graph_vec;
  for (const auto &entry : node_pair_to_edges) {
    graph_vec.push_back(json::array({entry.first.first, entry.first.second}));
  }
  json graph(graph_vec);
  std::vector<json> terminal_nodes_vec;
  for (NodeSize entering_node : terminal_nodes) {
    terminal_nodes_vec.push_back(entering_node);
  }
  json terminal_nodes_json(terminal_nodes_vec);
  std::vector<json> non_terminal_nodes_vec;
  for (const auto &node_pair : non_terminal_nodes) {
    if (node_pair.first != node_pair.second) {
      non_terminal_nodes_vec.push_back(
          json::array({node_pair.first, node_pair.second}));
    }
  }
  json non_terminal_nodes_json(non_terminal_nodes_vec);
  return {{"graph", graph},
          {"terminal_nodes", terminal_nodes_json},
          {"non_terminal_nodes", non_terminal_nodes_json}};
}

} // namespace

namespace hierarchical_map {

class GraphillionLeafConstraintHandler : public LeafConstraintHandler {
public:
  GraphillionLeafConstraintHandler(
      const std::unordered_map<Edge *, SddLiteral> *edge_variable_map,
      const std::unordered_map<SddLiteral, Edge *> *variable_to_edge_map,
      const std::unordered_map<MapCluster *, Vtree *> *local_vtree_per_cluster,
      const std::unordered_map<MapCluster *, std::set<NodeSize>>
          *terminal_nodes_per_cluster,
      const std::unordered_map<MapCluster *,
                               std::set<std::pair<NodeSize, NodeSize>>>
          *non_terminal_nodes_per_cluster,
      const string &graphillion_script, const string &tmp_dir, int thread_num);
  ~GraphillionLeafConstraintHandler() override {
    for (const auto &manager_entry : sdd_manager_) {
      sdd_manager_free(manager_entry.second);
    }
  }
  SddNode *internal_path_constraint(MapCluster *leaf_cluster) override {
    auto leaf_cluster_it = internal_path_constraint_.find(leaf_cluster);
    assert(leaf_cluster_it != internal_path_constraint_.end());
    return leaf_cluster_it->second;
  }
  SddNode *empty_path_constraint(MapCluster *leaf_cluster) override {
    auto leaf_cluster_it = empty_path_constraint_.find(leaf_cluster);
    assert(leaf_cluster_it != empty_path_constraint_.end());
    return leaf_cluster_it->second;
  }
  const map<NodeSize, SddNode *> &
  terminal_path_constraint(MapCluster *leaf_cluster) override {
    auto leaf_cluster_it = terminal_path_constraint_.find(leaf_cluster);
    assert(leaf_cluster_it != terminal_path_constraint_.end());
    return leaf_cluster_it->second;
  }
  const map<pair<NodeSize, NodeSize>, SddNode *> &
  non_terminal_path_constraint(MapCluster *leaf_cluster) override {
    assert(leaf_cluster->left_child() == nullptr &&
           leaf_cluster->right_child() == nullptr);
    auto leaf_cluster_it = non_terminal_path_constraint_.find(leaf_cluster);
    assert(leaf_cluster_it != non_terminal_path_constraint_.end());
    return leaf_cluster_it->second;
  }
  SddManager *sdd_manager(MapCluster *leaf_cluster) override {
    auto leaf_cluster_it = sdd_manager_.find(leaf_cluster);
    assert(leaf_cluster_it != sdd_manager_.end());
    return leaf_cluster_it->second;
  }

private:
  unordered_map<MapCluster *, SddNode *> internal_path_constraint_;
  unordered_map<MapCluster *, SddNode *> empty_path_constraint_;
  unordered_map<MapCluster *, map<NodeSize, SddNode *>>
      terminal_path_constraint_;
  unordered_map<MapCluster *, map<pair<NodeSize, NodeSize>, SddNode *>>
      non_terminal_path_constraint_;
  unordered_map<MapCluster *, SddManager *> sdd_manager_;
};

GraphillionLeafConstraintHandler::GraphillionLeafConstraintHandler(
    const std::unordered_map<Edge *, SddLiteral> *edge_variable_map,
    const std::unordered_map<SddLiteral, Edge *> *variable_to_edge_map,
    const std::unordered_map<MapCluster *, Vtree *> *local_vtree_per_cluster,
    const std::unordered_map<MapCluster *, std::set<NodeSize>>
        *terminal_nodes_per_cluster,
    const std::unordered_map<MapCluster *,
                             std::set<std::pair<NodeSize, NodeSize>>>
        *non_terminal_nodes_per_cluster,
    const string &graphillion_script, const string &tmp_dir, int thread_num) {
  for (const auto &local_vtree_entry : *local_vtree_per_cluster) {
    MapCluster *target_cluster = local_vtree_entry.first;
    if (target_cluster->left_child() != nullptr) {
      assert(target_cluster->right_child() != nullptr);
      continue; // only handles leaf
    }
    Vtree *local_vtree = local_vtree_entry.second;
    vector<SddLiteral> sequenced_literals;
    LeftToRightVtreeLeafTraverse(&sequenced_literals, local_vtree);
    unordered_map<SddLiteral, SddLiteral> psdd_index_to_sdd_index;
    SddLiteral sdd_index = 1;
    for (SddLiteral cur_psdd_index : sequenced_literals) {
      psdd_index_to_sdd_index[cur_psdd_index] = sdd_index++;
    }
    Vtree *sdd_vtree = VtreeSwapVariables(local_vtree, psdd_index_to_sdd_index);
    SddManager *sdd_manager = sdd_manager_new(sdd_vtree);
    sdd_manager_auto_gc_and_minimize_off(sdd_manager);
    sdd_manager_[target_cluster] = sdd_manager;
    sdd_vtree_free(sdd_vtree);
    auto terminal_nodes_it = terminal_nodes_per_cluster->find(target_cluster);
    auto non_terminal_nodes_it =
        non_terminal_nodes_per_cluster->find(target_cluster);
    assert(terminal_nodes_it != terminal_nodes_per_cluster->end() &&
           non_terminal_nodes_it != non_terminal_nodes_per_cluster->end());
    map<pair<NodeSize, NodeSize>, vector<Edge *>> node_pair_to_edges;
    for (SddLiteral cur_lit : sequenced_literals) {
      auto cur_lit_it = variable_to_edge_map->find(cur_lit);
      assert(cur_lit_it != variable_to_edge_map->end());
      Edge *cur_edge = cur_lit_it->second;
      pair<NodeSize, NodeSize> cur_pair(cur_edge->x_node_index(),
                                        cur_edge->y_node_index());
      auto node_pair_it = node_pair_to_edges.find(cur_pair);
      if (node_pair_it == node_pair_to_edges.end()) {
        node_pair_to_edges[cur_pair] = {cur_edge};
      } else {
        node_pair_it->second.push_back(cur_edge);
      }
    }
    json problem_spec =
        ConstructProblemSpecJson(node_pair_to_edges, terminal_nodes_it->second,
                                 non_terminal_nodes_it->second);
    string problem_spec_filename =
        tmp_dir + "/" + target_cluster->cluster_name() + ".json";
    std::ofstream problem_spec_fp(problem_spec_filename);
    problem_spec_fp << problem_spec << std::endl;
    problem_spec_fp.close();
    string cmd = "OMP_NUM_THREADS=" + std::to_string(thread_num) + " python " +
                 graphillion_script + " " + problem_spec_filename;
    string result = exec(cmd.c_str());
    json response = json::parse(result);
    auto universe = response["universe"];
    vector<vector<SddLiteral>> zdd_to_sdd_indexes;
    zdd_to_sdd_indexes.push_back({});
    for (const auto &node_pair : universe) {
      NodeSize first_node = node_pair[0];
      NodeSize second_node = node_pair[1];
      pair<NodeSize, NodeSize> node_pair_key(first_node, second_node);
      const auto &edges = node_pair_to_edges[node_pair_key];
      vector<SddLiteral> sdd_indexes;
      for (Edge *cur_edge : edges) {
        auto cur_edge_it = edge_variable_map->find(cur_edge);
        assert(cur_edge_it != edge_variable_map->end());
        SddLiteral cur_edge_psdd_index = cur_edge_it->second;
        auto psdd_index_it = psdd_index_to_sdd_index.find(cur_edge_psdd_index);
        assert(psdd_index_it != psdd_index_to_sdd_index.end());
        sdd_indexes.push_back(psdd_index_it->second);
      }
      zdd_to_sdd_indexes.emplace_back(sdd_indexes);
    }
    // internal path constraint
    string internal_path_zdd_string = response["internal_path"];
    internal_path_constraint_[target_cluster] = ZddStringToSdd(
        internal_path_zdd_string, zdd_to_sdd_indexes, sdd_manager);
    // empty path constraint
    vector<SddLiteral> sdd_literals;
    for (SddLiteral i = 1; i <= sdd_manager_var_count(sdd_manager); ++i) {
      sdd_literals.push_back(i);
    }
    empty_path_constraint_[target_cluster] =
        NegativeTerm(sdd_literals, sdd_manager);
    // terminal path constraint
    terminal_path_constraint_[target_cluster] = {};
    for (NodeSize terminal_node : terminal_nodes_it->second) {
      string cur_terminal_zdd_string =
          response["terminal_" + std::to_string(terminal_node)];
      SddNode *cur_terminal_constraint = ZddStringToSdd(
          cur_terminal_zdd_string, zdd_to_sdd_indexes, sdd_manager);
      cur_terminal_constraint =
          sdd_disjoin(cur_terminal_constraint,
                      empty_path_constraint_[target_cluster], sdd_manager);
      terminal_path_constraint_[target_cluster][terminal_node] =
          cur_terminal_constraint;
    }
    // non terminal path constraint
    non_terminal_path_constraint_[target_cluster] = {};
    for (const auto &non_terminal_node_pair : non_terminal_nodes_it->second) {
      if (non_terminal_node_pair.first == non_terminal_node_pair.second) {
        non_terminal_path_constraint_[target_cluster][non_terminal_node_pair] =
            empty_path_constraint_[target_cluster];
      } else {
        string cur_non_terminal_zdd_string =
            response["non_terminal_" +
                     std::to_string(non_terminal_node_pair.first) + "_" +
                     std::to_string(non_terminal_node_pair.second)];
        non_terminal_path_constraint_[target_cluster][non_terminal_node_pair] =
            ZddStringToSdd(cur_non_terminal_zdd_string, zdd_to_sdd_indexes,
                           sdd_manager);
      }
    }
  }
}

LeafConstraintHandler *
LeafConstraintHandler::GetGraphillionSddLeafConstraintHandler(
    const std::unordered_map<Edge *, SddLiteral> *edge_variable_map,
    const std::unordered_map<SddLiteral, Edge *> *variable_to_edge_map,
    const std::unordered_map<MapCluster *, Vtree *> *local_vtree_per_cluster,
    const std::unordered_map<MapCluster *, std::set<NodeSize>>
        *terminal_nodes_per_cluster,
    const std::unordered_map<MapCluster *,
                             std::set<std::pair<NodeSize, NodeSize>>>
        *non_terminal_nodes_per_cluster,
    const std::string &graphillion_script, const std::string &tmp_dir,
    int thread_num) {
  return new GraphillionLeafConstraintHandler(
      edge_variable_map, variable_to_edge_map, local_vtree_per_cluster,
      terminal_nodes_per_cluster, non_terminal_nodes_per_cluster,
      graphillion_script, tmp_dir, thread_num);
}
} // namespace hierarchical_map
