#include <hierarchical_map/map_cluster.h>
#include <hierarchical_map/map_network.h>
#include <hierarchical_map/types.h>
#include <memory>

namespace hierarchical_map {
namespace testing {
std::unique_ptr<MapNetwork>
CreateSimple3LayerNetwork(NodeSize width_per_cluster);
} // namespace testing
} // namespace hierarchical_map
