import sys
import json
import subprocess,shlex
import simple_graph
import logging
import math

Route = simple_graph.Route
Edge = simple_graph.Edge
class Haversine:
    '''
    use the haversine class to calculate the distance between
    two lon/lat coordnate pairs.
    output distance available in kilometers, meters, miles, and feet.
    example usage: Haversine([lon1,lat1],[lon2,lat2]).feet
    '''
    def __init__(self,coord1,coord2):
        lon1,lat1=coord1
        lon2,lat2=coord2
        R=6371000                               # radius of Earth in meters
        phi_1=math.radians(lat1)
        phi_2=math.radians(lat2)

        delta_phi=math.radians(lat2-lat1)
        delta_lambda=math.radians(lon2-lon1)

        a=math.sin(delta_phi/2.0)**2+\
           math.cos(phi_1)*math.cos(phi_2)*\
           math.sin(delta_lambda/2.0)**2
        c=2*math.atan2(math.sqrt(a),math.sqrt(1-a))
        self.meters=R*c                         # output distance in meters
        self.km=self.meters/1000.0              # output distance in kilometers
        self.miles=self.meters*0.000621371      # output distance in miles
        self.feet=self.miles*5280               # output distance in feet

# D(predicted_route, actual_route)
class DistanceMetricsCalculator:
    def __init__(self, actual_route, predicted_route, node_locations):
        self.actual_route = actual_route
        self.predicted_route = predicted_route
        self.node_locations = node_locations

    @staticmethod
    def route_length(route, node_locations):
        route_length = sum([Haversine(node_locations[a.x], node_locations[a.y]).feet for a in route.edges])
        return route_length

    def get_trip_length_distance(self):
        return DistanceMetricsCalculator.route_length(self.predicted_route, self.node_locations) - DistanceMetricsCalculator.route_length(self.actual_route, self.node_locations)

    def get_trip_length_distance_normalized(self):
        return self.get_trip_length_distance() / DistanceMetricsCalculator.route_length(self.actual_route, self.node_locations)

    @staticmethod
    def hausdorff_distance_between(route_1, route_2, node_locations):
        sequence_1 = route_1.node_sequence()
        sequence_2 = route_2.node_sequence()
        inf_distances = []
        for node_1 in sequence_1:
            inf_distances.append(min([Haversine(node_locations[node_1], node_locations[i]).feet for i in sequence_2]))
        sup_1 = max(inf_distances)
        inf_distances = []
        for node_2 in sequence_2:
            inf_distances.append(min([Haversine(node_locations[node_2], node_locations[i]).feet for i in sequence_1]))
        sup_2 = max(inf_distances)
        return max(sup_1, sup_2)

    def get_hausdorff_distance(self):
        return DistanceMetricsCalculator.hausdorff_distance_between(self.predicted_route, self.actual_route, self.node_locations)

    def get_hausdorff_distance_normalized(self):
        return self.get_hausdorff_distance() / DistanceMetricsCalculator.route_length(self.actual_route, self.node_locations)

    @staticmethod
    def dsn_between(route_1, route_2):
        route_1_edges = set(route_1.edges)
        route_2_edges = set(route_2.edges)
        common = route_1_edges.intersection(route_2_edges)
        mis_match = 0
        for i in route_1_edges:
            if i not in common:
                mis_match += 1
        for i in route_2_edges:
            if i not in common:
                mis_match += 1
        total_len = len(route_1_edges) + len(route_2_edges)
        return float(mis_match) / total_len

    def get_dsn(self):
        return DistanceMetricsCalculator.dsn_between(self.predicted_route, self.actual_route)

def parse_routes (route_filename):
    with open(route_filename, "r") as fp:
        json_routes = json.load(fp)
    routes = [Route.from_json(r) for r in json_routes]
    return routes

def exactly_one_cnf(variables):
    cnf = []
    var_len = len(variables)
    for i in range(0, var_len):
        for j in range(i+1, var_len):
            cnf.append([-variables[i], -variables[j]])
    cnf.append([a for a in variables])
    return cnf

def generate_cnf_from_route_contraint(src_node, dst_node, used_edges, node_to_edge_indexes, edge_to_index, cnf_filename):
    neighboring_src_edges_indexes = node_to_edge_indexes[src_node]
    neighboring_dst_edges_indexes = node_to_edge_indexes[dst_node]
    cnf = []
    cnf.extend(exactly_one_cnf(neighboring_src_edges_indexes))
    cnf.extend(exactly_one_cnf(neighboring_dst_edges_indexes))
    cnf.extend([[edge_to_index[e]] for e in used_edges])
    with open (cnf_filename, "w") as fp:
        for clause in cnf:
            fp.write(" ".join([str(e) for e in clause]))
            fp.write(" 0\n")

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

def mpe_prediction_per_route(test_route, edge_to_index, index_to_edge, node_to_edge_indexes, tmp_filename_prefix, inference_binary, learned_psdd_filename, learned_vtree_filename):
    logger = logging.getLogger()
    route_length = len(test_route.edges)
    cur_evidence_len = route_length / 2
    #evidence edges is sorted.
    evidence_edges = test_route.edges[: cur_evidence_len]
    actual_remaining_route = Route.get_route_from_edge_list(test_route.edges[cur_evidence_len:])
    remaining_src_node = actual_remaining_route.src_node()
    remaining_dst_node = actual_remaining_route.dst_node()
    predicted_remaining_route = None
    cnf_filename = "%s_evid.cnf" % tmp_filename_prefix
    while len(evidence_edges) > 0:
        generate_cnf_from_route_contraint(test_route.src_node(), test_route.dst_node(), evidence_edges, node_to_edge_indexes, edge_to_index, cnf_filename)
        cmd = "%s --mpe_query --cnf_evid %s %s %s" % (inference_binary, cnf_filename, learned_psdd_filename, learned_vtree_filename)
        logger.debug("Running command : %s" % cmd)
        output = subprocess.check_output(shlex.split(cmd))
        mpe_route = get_mpe_route_from_output(output, index_to_edge)
        if mpe_route is not None:
            predicted_remaining_route = mpe_route.get_route_between(remaining_src_node, remaining_dst_node)
            break;
        else:
            evidence_edges = evidence_edges[1:]
    if predicted_remaining_route is None:
        generate_cnf_from_route_contraint(remaining_src_node, remaining_dst_node, [], node_to_edge_indexes, edge_to_index, cnf_filename)
        cmd = "%s --mpe_query --cnf_evid %s %s %s" % (inference_binary, cnf_filename, learned_psdd_filename, learned_vtree_filename)
        logger.debug("Running command : %s" % cmd)
        output = subprocess.check_output(shlex.split(cmd))
        mpe_route = get_mpe_route_from_output(output, index_to_edge)
        assert mpe_route is not None
        predicted_remaining_route = mpe_route.get_route_between(remaining_src_node, remaining_dst_node)
    return actual_remaining_route, predicted_remaining_route

if __name__ == "__main__" :
    if len(sys.argv) <= 3:
        print ("quality_exp.py <hierarchical_map_filename> <network_prefix> <psdd_binary>")
        sys.exit(1)
    bhm_network_filename = sys.argv[1]
    network_file_prefix = sys.argv[2]
    psdd_binary = sys.argv[3]
    psdd_filename = "%s.psdd" % network_file_prefix
    vtree_filename = "%s.vtree" % network_file_prefix
    test_route_filename = "%s_test_routes.json" % network_file_prefix
    variable_map_filename = "%s_variable_map.json" % network_file_prefix
    distance_result_filename = "%s_distance_result.json" % network_file_prefix
    print ("bhm_network_filename : %s" % bhm_network_filename)
    print ("psdd_filename : %s" % psdd_filename)
    print ("vtree_filename : %s" % vtree_filename)
    print ("test_route_filename : %s" % test_route_filename)
    print ("variable_map_filename : %s" % variable_map_filename)
    print ("distance_result_filename : %s" % distance_result_filename)
    with open(bhm_network_filename, "r") as fp:
        bhm_network = json.load(fp)
    test_routes = parse_routes(test_route_filename)
    edge_to_index = {}
    index_to_edge = {}
    node_to_neighboring_edge_indexes = {}
    with open (variable_map_filename, "r") as fp:
        variable_map_raw = json.load(fp)
        for variable_index_str in variable_map_raw:
            cur_edge = Edge.load_from_json(variable_map_raw[variable_index_str])
            edge_to_index[cur_edge] = int(variable_index_str)
            index_to_edge[int(variable_index_str)] = cur_edge
            node_to_neighboring_edge_indexes.setdefault(cur_edge.x, []).append(int(variable_index_str))
            node_to_neighboring_edge_indexes.setdefault(cur_edge.y, []).append(int(variable_index_str))
    dsns = []
    tl = []
    hd = []
    for test_route in test_routes:
        actual_remaining_route, predicted_remaining_route = mpe_prediction_per_route(test_route, edge_to_index, index_to_edge, node_to_neighboring_edge_indexes, network_file_prefix, psdd_binary, psdd_filename, vtree_filename)
        distance_calculator = DistanceMetricsCalculator (actual_remaining_route, predicted_remaining_route, bhm_network["nodes_location"])
        dsns.append(distance_calculator.get_dsn())
        tl.append(distance_calculator.get_trip_length_distance_normalized())
        hd.append(distance_calculator.get_hausdorff_distance_normalized())
    result = {"dsn" : dsns, "trip_length_distance" : tl, "hausdorff_distance" : hd}
    with open(distance_result_filename, "w") as fp:
        json.dump(result, fp, indent=2)










