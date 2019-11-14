import simple_graph
import json

Route = simple_graph.Route
Edge = simple_graph.Edge


def parse_routes (route_filename):
    with open(route_filename, "r") as fp:
        json_routes = json.load(fp)
    routes = [Route.from_json(r) for r in json_routes]
    return routes

if __name__ == "__main__":
    import sys
    route_filename = sys.argv[1]
    routes = parse_routes(route_filename)
    routes = routes[:9300]
    for cur_route in routes:
        node_seq = cur_route.node_sequence()
        node_set = set()
        for node_idx in node_seq:
            if (node_idx in node_set):
                print "BUG"
            else:
                node_set.add(node_idx)



