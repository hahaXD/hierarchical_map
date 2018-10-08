//
// Created by Jason Shen on 5/21/18.
//

#include <hierarchical_map/map_network.h>
#include <iostream>
extern "C" {
#include <sdd/sddapi.h>
}

namespace {
using hierarchical_map::MapNetwork;
}

int main(int argc, const char *argv[]) {
  if (argc <= 6) {
    std::cout << "Usage: map_filename graph_hopper_script tmp_dir thread_num "
                 "sdd_filename vtree_filename"
              << std::endl;
    exit(1);
  }
  std::string map_filename(argv[1]);
  std::string graph_hopper_script(argv[2]);
  std::string tmp_dir(argv[3]);
  int thread_num = atoi(argv[4]);
  std::string sdd_filename(argv[5]);
  std::string vtree_filename(argv[6]);
  std::cout << "Running map file : " << map_filename << std::endl;
  std::cout << "Graph hopper script : " << graph_hopper_script << std::endl;
  std::cout << "Tmp dir : " << tmp_dir << std::endl;
  std::cout << "Thread Num : " << thread_num << std::endl;
  std::cout << "Sdd filename : " << sdd_filename << std::endl;
  std::cout << "Vtree filename : " << vtree_filename << std::endl;
  MapNetwork *network =
      MapNetwork::MapNetworkFromJsonSpecFile(map_filename.c_str());
  auto result =
      network->CompileConstraint(graph_hopper_script, tmp_dir, thread_num);
  std::cout << "Model Count"
            << psdd_node_util::ModelCount(
                   psdd_node_util::SerializePsddNodes(result.first))
                   .get_str(10)
            << std::endl;
  psdd_node_util::WritePsddToFile(result.first, sdd_filename.c_str());
  sdd_vtree_save(vtree_filename.c_str(), result.second->vtree());
  return 0;
}
