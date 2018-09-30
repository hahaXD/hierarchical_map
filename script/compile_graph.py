from graphillion import GraphSet
import json
import tempfile

def ConvertEdgesToUniverse(edge_list):
    universe = []
    edge_pool = set()
    for e in edge_list:
        assert type(e) is list and len(e) == 2
        edge_tup = (min(e[0], e[1]), max(e[0], e[1]))
        assert edge_tup not in edge_pool
        edge_pool.add(edge_tup)
        universe.append(edge_tup)
    return universe

def NodesInEdges(edge_list):
    nodes = set()
    for e in edge_list:
        assert type(e) is list and len(e) == 2
        nodes.add(e[0])
        nodes.add(e[1])
    return nodes

def DumpToStr(paths):
    try:
        zdd_file = tempfile.TemporaryFile()
        paths.dump(zdd_file)
        zdd_file.seek(0)
        zdd_content = zdd_file.read()
    finally:
        zdd_file.close()
    return zdd_content

if __name__ == "__main__":
    import sys
    import itertools
    problem_spec_file = sys.argv[1]
    with open(problem_spec_file, "r") as fp:
        problem_spec = json.load(fp)
    edge_list = problem_spec["graph"]
    terminal_nodes = problem_spec["terminal_nodes"]
    non_terminal_nodes = problem_spec["non_terminal_nodes"]
    universe = ConvertEdgesToUniverse(edge_list)
    GraphSet.set_universe(universe)
    universe = GraphSet.universe()
    nodes = NodesInEdges(edge_list)
    terminal_path = {}
    non_terminal_path = {}
    path_cache = {}
    for i, j in itertools.combinations(nodes, 2):
        path_cache[(min(i,j), max(i,j))] = GraphSet.paths(i,j)
    internal_path = GraphSet()
    for path in path_cache.values():
        internal_path = internal_path.union(path)
    for node in terminal_nodes:
        cur_path = GraphSet()
        for other_node in nodes:
            if other_node == node:
                continue
            node_key = (min(node, other_node), max(node, other_node))
            cur_path = cur_path.union(path_cache[node_key])
        terminal_path[node] = cur_path
    result = {}
    result["internal_path"] = DumpToStr(internal_path)
    for node in terminal_nodes:
        result["terminal_%s" % node] = DumpToStr(terminal_path[node])
    for node_pair in non_terminal_nodes:
        min_node = min(node_pair[0], node_pair[1])
        max_node = max(node_pair[0], node_pair[1])
        result["non_terminal_%s_%s" % (min_node, max_node)] = DumpToStr(path_cache[(min_node, max_node)])
    result["universe"] = [[min(a), max(a)] for a in universe]
    print json.dumps(result)


