import random
import simple_graph
import json
import subprocess,shlex
import sys

Route = simple_graph.Route
Edge = simple_graph.Edge

training_size = 32768
testing_size = 4096
graphillion_script = "../script/compile_graph.py"

def parse_classifier_output(output):
    ll = output.split("\n")
    start = False
    result = []
    for line in ll:
        if "Inference Result:" in line:
            start = True
        if start:
            if ":" in line:
                result.append(float(line.split(":")[1]))
    return result

def parse_routes (route_filename):
    with open(route_filename, "r") as fp:
        json_routes = json.load(fp)
    routes = [Route.from_json(r) for r in json_routes]
    return routes

def single_iteration (hierarchical_map_filename, google_routes, cab_routes, test_prefix, classifier_binary):
    random.shuffle(google_routes)
    random.shuffle(cab_routes)
    google_training_routes = google_routes[:training_size]
    google_testing_routes = google_routes[training_size: training_size + testing_size]
    cab_training_routes = cab_routes[:training_size]
    cab_testing_routes = cab_routes[training_size : training_size + testing_size]
    google_training_routes_filename = "%s_google_%s_training.json" % (test_prefix, training_size)
    google_testing_routes_filename = "%s_google_%s_testing.json" % (test_prefix, testing_size)
    cab_training_routes_filename = "%s_cab_%s_training.json" % (test_prefix, training_size)
    cab_testing_routes_filename = "%s_cab_%s_testing.json" % (test_prefix, testing_size)
    tmp_prefix = "/tmp"
    with open (google_training_routes_filename, "w") as fp:
        json.dump([r.as_json() for r in google_training_routes], fp, indent=2)
    with open (google_testing_routes_filename, "w") as fp:
        json.dump([r.as_json() for r in google_testing_routes], fp, indent=2)
    with open (cab_training_routes_filename, "w") as fp:
        json.dump([r.as_json() for r in cab_training_routes], fp, indent=2)
    with open (cab_testing_routes_filename, "w") as fp:
        json.dump([r.as_json() for r in cab_testing_routes], fp, indent=2)
    google_google_cmd = "%s %s %s %s 4 %s %s" % (classifier_binary, hierarchical_map_filename, graphillion_script, tmp_prefix, google_training_routes_filename, google_testing_routes_filename)
    google_google_output = subprocess.check_output(shlex.split(google_google_cmd))
    google_google_scores = parse_classifier_output(google_google_output)
    print google_google_scores

if __name__ == "__main__":
    if len(sys.argv) <= 5:
        print ("Usage <map file> <google route> <cab route> <classifier binary> <test_prefix>")
        sys.exit(1)
    hierarchical_map_filename = sys.argv[1]
    google_route_filename = sys.argv[2]
    cab_route_filename = sys.argv[3]
    classifier_binary = sys.argv[4]
    test_prefix = sys.argv[5]
    google_routes = parse_routes(google_route_filename)
    cab_routes = parse_routes(cab_route_filename)
    for i in range(0, 1):
        single_iteration(hierarchical_map_filename, google_routes, cab_routes, "%s_%s" % (test_prefix, i), classifier_binary)

