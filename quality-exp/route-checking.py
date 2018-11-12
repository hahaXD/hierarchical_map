import sys
import json
import subprocess,shlex
import simple_graph
import logging
import math

Route = simple_graph.Route
Edge = simple_graph.Edge

def parse_routes (route_filename):
    with open(route_filename, "r") as fp:
        json_routes = json.load(fp)
    routes = [Route.from_json(r) for r in json_routes]
    return routes

def get_mpe_route_from_output(mpe_output, index_to_edge):
    lines = mpe_output.split("\n")
    mpe_used_edges = None
    for line in lines:
        if line.find("MPE result") != -1:
            line = line.strip()
            mpe_used_edges = []
            raw_result = line.split("=")[1].split(",")
            for tok in raw_result:
                if tok.strip() == "":
                    continue
                var_index = int(tok.split(":")[0])
                value = int(tok.split(":")[1])
                if value > 0:
                    if var_index in index_to_edge:
                        mpe_used_edges.append(index_to_edge[var_index])
    if mpe_used_edges is None:
        return None
    return Route.get_route_from_edge_list(mpe_used_edges)

if __name__ == "__main__" :
    if len(sys.argv) <= 3:
        print ("quality_exp.py <network_prefix> <psdd_binary> <all_route_filename>")
        sys.exit(1)
    network_file_prefix = sys.argv[1]
    psdd_binary = sys.argv[2]
    all_route_filename = sys.argv[3]
    psdd_filename = "%s.psdd" % network_file_prefix
    vtree_filename = "%s.vtree" % network_file_prefix
    variable_map_filename = "%s_variable_map.json" % network_file_prefix
    valid_routes_filename = "%s_number_of_valid_routes.txt" % network_file_prefix
    cnf_filename = "%s.cnf" % network_file_prefix
    print ("psdd_filename : %s" % psdd_filename)
    print ("vtree_filename : %s" % vtree_filename)
    print ("variable_map_filename : %s" % variable_map_filename)
    print ("valid_routes_filename : %s" % valid_routes_filename)
    total_routes = parse_routes(all_route_filename)[:9300]
    edge_to_index = {}
    index_to_edge = {}
    node_to_neighboring_edge_indexes = {}
    summary = {}
    total_valid_routes = 0
    route_infos = []
    logger = logging.getLogger()
    with open (variable_map_filename, "r") as fp:
        variable_map_raw = json.load(fp)
        for variable_index_str in variable_map_raw:
            cur_edge = Edge.load_from_json(variable_map_raw[variable_index_str])
            edge_to_index[cur_edge] = int(variable_index_str)
            index_to_edge[int(variable_index_str)] = cur_edge
            node_to_neighboring_edge_indexes.setdefault(cur_edge.x, []).append(int(variable_index_str))
            node_to_neighboring_edge_indexes.setdefault(cur_edge.y, []).append(int(variable_index_str))
    for cur_route in total_routes:
        edge_indexes = [edge_to_index[e] for e in cur_route.edges]
        with open (cnf_filename, "w") as fp:
            for edge_index in edge_indexes:
                fp.write("%s 0\n" % edge_index)
        cmd = "%s --mpe_query --cnf_evid %s %s %s" % (psdd_binary, cnf_filename, psdd_filename, vtree_filename)
        logger.debug("Running command : %s" % cmd)
        output = subprocess.check_output(shlex.split(cmd))
        mpe_route = get_mpe_route_from_output(output, index_to_edge)
        cur_route_json = cur_route.as_json() 
        if mpe_route is not None:
            total_valid_routes += 1
            cur_route_json["valid"] = 1
        else:
            cur_route_json["valid"] = 0
        route_infos.append(cur_route_json)
    summary["total_valid_routes"] = total_valid_routes
    summary["route_infos"] = route_infos
    with open(valid_routes_filename, "w") as fp:
        json.dump(summary, fp, indent=2)


