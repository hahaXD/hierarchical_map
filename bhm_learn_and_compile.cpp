//
// Created by Jason Shen on 5/21/18.
//

#include <fstream>
#include <hierarchical_map/map_network.h>
#include <iostream>
#include <nlohmann/json.hpp>
extern "C" {
#include <sdd/sddapi.h>
}

namespace {
using hierarchical_map::Edge;
using hierarchical_map::Graph;
using hierarchical_map::MapNetwork;

std::vector<std::vector<Edge *>> ReadRoutesFromFile(const std::string &filename,
                                                    Graph *graph) {
  std::ifstream in_fd(filename.c_str());
  json route_spec;
  in_fd >> route_spec;
  std::vector<std::vector<Edge *>> routes;
  // iterate the array
  for (json::iterator it = route_spec.begin(); it != route_spec.end(); ++it) {
    auto route_entry = it->find("edges");
    assert(route_entry != it->end());
    std::vector<Edge *> cur_route;
    for (json::iterator edge_it = route_entry.value().begin();
         edge_it != route_entry.value().end(); ++edge_it) {
      Edge *target_edge = graph->EdgeFromJson(*edge_it);
      cur_route.push_back(target_edge);
    }
    routes.push_back(std::move(cur_route));
  }
  return routes;
}

void SaveEdgeVariableMapToJsonFile(
    const std::unordered_map<Edge *, SddLiteral> &edge_variable_map,
    const std::string &json_filename) {
  json result;
  for (const auto &edge_index_pair : edge_variable_map) {
    result[edge_index_pair.second] = edge_index_pair.first->to_json();
  }
  std::ofstream o(json_filename.c_str());
  o << std::setw(4) << result << std::endl;
}
} // namespace

int main(int argc, const char *argv[]) {
  if (argc <= 8) {
    std::cout << "Usage: map_filename graph_hopper_script tmp_dir thread_num "
                 "psdd_filename vtree_filename edge_variable_map_filename "
                 "training_routes_filename"
              << std::endl;
    exit(1);
  }
  std::string map_filename(argv[1]);
  std::string graphillion_script(argv[2]);
  std::string tmp_dir(argv[3]);
  int thread_num = atoi(argv[4]);
  std::string psdd_filename(argv[5]);
  std::string vtree_filename(argv[6]);
  std::string edge_variable_map_filename(argv[7]);
  std::string training_routes_filename(argv[8]);
  std::cout << "Running map file : " << map_filename << std::endl;
  std::cout << "Graph hopper script : " << graphillion_script << std::endl;
  std::cout << "Tmp dir : " << tmp_dir << std::endl;
  std::cout << "Thread Num : " << thread_num << std::endl;
  std::cout << "Psdd filename : " << psdd_filename << std::endl;
  std::cout << "Vtree filename : " << vtree_filename << std::endl;
  std::cout << "Edge variable map filename : " << edge_variable_map_filename
            << std::endl;
  std::cout << "Training routes filename : " << training_routes_filename
            << std::endl;

  MapNetwork *network =
      MapNetwork::MapNetworkFromJsonSpecFile(map_filename.c_str());
  auto training_routes =
      ReadRoutesFromFile(training_routes_filename, network->graph());
  network->LearnWithRoutes(graphillion_script, tmp_dir, thread_num,
                           training_routes);
  auto result = network->InferenceByCompilation();
  psdd_node_util::WritePsddToFile(result.first, psdd_filename.c_str());
  sdd_vtree_save(vtree_filename.c_str(), result.second->vtree());
  SaveEdgeVariableMapToJsonFile(network->edge_variable_map(),
                                edge_variable_map_filename);
  return 0;
}
