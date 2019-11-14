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

def parse_variable_map_filename (filename):
    edge_to_index = {}
    index_to_edge = {}
    with open (filename, "r") as fp:
        variable_map_raw = json.load(fp)
        for variable_index_str in variable_map_raw:
            cur_edge = Edge.load_from_json(variable_map_raw[variable_index_str])
            edge_to_index[cur_edge] = int(variable_index_str)
            index_to_edge[int(variable_index_str)] = cur_edge
    return edge_to_index, index_to_edge

if __name__ == "__main__":
    route_filename = sys.argv[1]
    variable_map_filename = sys.argv[2]
    output_filename = sys.argv[3]
    routes = parse_routes(route_filename)
    edge_to_index, index_to_edge = parse_variable_map_filename(variable_map_filename)
    total_record = []
    for cur_route in routes:
        cur_record = []
        for cur_edge in cur_route.edges:
            cur_record.append(edge_to_index[cur_edge])
        total_record.append(cur_record)
    data_record = {}
    data_record["data"] = total_record
    data_record["mask"] = index_to_edge.keys()
    with open(output_filename+".sparse" , "w") as fp:
        json.dump(data_record, fp)

