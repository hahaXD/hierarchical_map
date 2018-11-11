import math
import re
import logging

class Edge(object):
    def __init__(self,x,y,name=""):
        if cmp(x,y) > 0: x,y = y,x
        self.x = x
        self.y = y
        self.name = str(name)
        self.nodes = set([x,y])

    @staticmethod
    def from_json(dct):
        if "__Edge__" in dct:
            assert "x" in dct and "y" in dct and "name" in dct
            return Edge.load_from_json(dct)
        else:
            return dct
    @staticmethod
    def load_from_json(edge_json):
        return Edge(edge_json["x"], edge_json["y"], str(edge_json["name"]))

    @staticmethod
    def load_from_str(edge_str):
        assert edge_str[0] == "e"
        p = re.match(r"e([^ ]*) \(([^\(\),]*),([^\(\),]*)\)", edge_str)
        assert p.group(0) == edge_str
        return Edge(int(p.group(2)), int(p.group(3)), p.group(1))

    def other_end(self, input_end):
        return self.OtherEnd(input_end)

    def OtherEnd(self, input_end):
        if self.x == input_end:
            return self.y
        else:
            return self.x

    def as_json(self):
        return {"__Edge__": True, "x": self.x, "y": self.y, "name": self.name}

    def as_tuple(self):
        return (self.x,self.y)

    def __repr__(self):
        return "e%s (%s,%s)" % (str(self.name),str(self.x),str(self.y))

    def __cmp__(self,other):
        return cmp(self.name,other.name)

    def __eq__ (self, other):
        return (self.x, self.y, self.name) == (other.x, other.y, other.name)

    def __hash__(self):
        return hash((self.x, self.y, self.name))


# route edges are sequenced
class Route:
    def __init__(self, edges):
        self.edges = edges

    def get_route_between(self, src_node, dst_node):
        node_to_edges = {}
        for e in self.edges:
            node_to_edges.setdefault(e.x, []).append(e)
            node_to_edges.setdefault(e.y, []).append(e)
        open_list = [(src_node, [])]
        while len(open_list) > 0:
            top_state = open_list[0]
            frontier_node = top_state[0]
            cur_path = top_state[1]
            open_list = open_list[1:]
            neighboring_edges = node_to_edges[top_state[0]]
            for e in neighboring_edges:
                if len(cur_path) != 0 and e == cur_path[-1]:
                    continue
                next_frontier = e.other_end(frontier_node)
                open_list.append((next_frontier, cur_path+[e]))
                if next_frontier == dst_node:
                    return Route.get_route_from_edge_list(cur_path+[e])
        return Route([])
    @staticmethod
    def get_route_from_edge_list (edge_list):
        if len(edge_list) == 0:
            return Route([])
        logger = logging.getLogger()
        logger.debug("Convert edges %s to route" % edge_list)
        node_to_edges = {}
        for edge in edge_list:
            node_to_edges.setdefault(edge.x, []).append(edge)
            node_to_edges.setdefault(edge.y, []).append(edge)
        sequenced_edges = []
        end_points = []
        for node in node_to_edges:
            if len(node_to_edges[node]) == 1:
                end_points.append(node)
        assert len(end_points) == 2
        sequenced_edges.append(node_to_edges[end_points[0]][0])
        cur_node = sequenced_edges[-1].other_end(end_points[0])
        while len(node_to_edges[cur_node]) > 1:
            assert node_to_edges[cur_node][0] == sequenced_edges[-1] or node_to_edges[cur_node][-1] == sequenced_edges[-1]
            edge_to_add = node_to_edges[cur_node][0] if node_to_edges[cur_node][1] == sequenced_edges[-1] else node_to_edges[cur_node][1]
            cur_node = edge_to_add.other_end(cur_node)
            sequenced_edges.append(edge_to_add)
        return Route(sequenced_edges)

    def src_node (self):
        if len(self.edges) == 0:
            return None
        if len(self.edges) == 1:
            return self.edges[0].x
        else:
            p = self.edges[0].nodes.difference(self.edges[1].nodes)
            return p.pop()

    def dst_node (self):
        if len(self.edges) == 0:
            return None
        if len(self.edges) == 1:
            return self.edges[0].y
        else:
            p = self.edges[-1].nodes.difference(self.edges[-2].nodes)
            return p.pop()

    def node_sequence(self):
        if len(self.edges) == 0:
            return None
        if len(self.edges) == 1:
            return [self.edges[0].x, self.edges[0].y]
        node_seq = []
        node_seq.append(self.src_node())
        for i in range(0, len(self.edges) -1):
            p = self.edges[i].nodes.intersection(self.edges[i+1].nodes)
            node_seq.append(p.pop())
        node_seq.append(self.dst_node())
        return node_seq

    def as_json(self):
        return {"__Route__": True, "edges":[e.as_json() for e in self.edges]}

    @staticmethod
    def from_json(dct):
        if "__Route__" in dct:
            assert "edges" in dct and type(dct["edges"]) is list
            edges = [Edge.from_json(e) for e in dct["edges"]]
            assert all([type(e) is Edge for e in edges])
            return Route.get_route_from_edge_list(edges)
        return dct

class SimpleGraph:
    def __init__(self,nodes,edges):
        self.nodes = nodes
        self.edges = edges
        self.print_all = False
        #self.graph_bounds = None

    def bounds(self):
        #if self.graph_bounds is not None: return self.graph_bounds

        lats = [ self.nodes[node][0] for node in self.nodes ]
        lons = [ self.nodes[node][1] for node in self.nodes ]

        rnd_up = lambda x: SimpleGraph.my_round(x,up=True)
        rnd_dn = lambda x: SimpleGraph.my_round(x,up=False)
        min_lat,max_lat = rnd_dn(min(lats)),rnd_up(max(lats))
        min_lon,max_lon = rnd_dn(min(lons)),rnd_up(max(lons))
        self.graph_bounds = (min_lat,max_lat,min_lon,max_lon)
        return self.graph_bounds

    @staticmethod    
    def my_round(number,up=True,digits=2):
        mult = 10.0**digits
        roundf = math.ceil if up else math.floor
        number = roundf(number * mult) / mult
        return number

    def __repr__(self):
        st = []

        min_lat,max_lat,min_lon,max_lon = self.bounds()
        st.append( "c BOUNDS:" )
        st.append( "c         %.2f" % max_lat )
        st.append( "c %.2f       %.2f" % (min_lon,max_lon) )
        st.append( "c         %.2f" % min_lat )

        if self.print_all is False: return "\n".join(st)

        st.append( "c NODES (%d):" % len(self.nodes) )
        for node in sorted(self.nodes.keys()):
            loc = self.nodes[node]
            st.append( "%d %.5f %.5f" % (node,loc[0],loc[1]) )

        st.append( "c EDGES (%d):" % len(self.edges) )
        for edge in self.edges:
            x,y,name = self.edges[edge]
            st.append( "%d %d %s" % (x,y,name) )

        return "\n".join(st)

def zero():
    return 0

def fix_sgraph(sgraph): #AC: should be fixed earlier?
    for index in sgraph.edges.keys():
        x,y,name = sgraph.edges[index]
        if x == y:
            sgraph.edges.pop(index)
