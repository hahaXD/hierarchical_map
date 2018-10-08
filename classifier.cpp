//
// Created by Jason Shen on 5/21/18.
//

#include <cassert>
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
} // namespace

int main(int argc, const char *argv[]) {
  if (argc <= 6) {
    std::cout << "Usage: map_filename graphillion_script tmp_dir thread_num "
                 "training_routes testing_routes"
              << std::endl;
    exit(1);
  }
  std::string map_filename(argv[1]);
  std::string graphillion_script(argv[2]);
  std::string tmp_dir(argv[3]);
  int thread_num = atoi(argv[4]);
  std::string training_routes_filename(argv[5]);
  std::string testing_routes_filename(argv[6]);
  std::cout << "Running map file : " << map_filename << std::endl;
  std::cout << "Graphillion script : " << graphillion_script << std::endl;
  std::cout << "Tmp dir : " << tmp_dir << std::endl;
  std::cout << "Thread Num : " << thread_num << std::endl;
  std::cout << "Training routes filename : " << training_routes_filename
            << std::endl;
  std::cout << "Testing routes filename : " << testing_routes_filename
            << std::endl;
  MapNetwork *network =
      MapNetwork::MapNetworkFromJsonSpecFile(map_filename.c_str());
  auto training_routes =
      ReadRoutesFromFile(training_routes_filename, network->graph());
  auto testing_routes =
      ReadRoutesFromFile(testing_routes_filename, network->graph());
  network->LearnWithRoutes(graphillion_script, tmp_dir, thread_num,
                           training_routes);
  std::cout << "Inference Result:" << std::endl;
  size_t example_index = 0;
  for (const auto &cur_test_route : testing_routes) {
    Probability cur_result = network->Evaluate({cur_test_route});
    std::cout << example_index << ":" << cur_result.parameter() << "\n";
    ++example_index;
  }
  std::cout << "End" << std::endl;
  delete network;
  return 0;
}
