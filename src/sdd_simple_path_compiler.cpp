extern "C" {
#include <sdd/sddapi.h>
}

#include <algorithm>
#include <hierarchical_map/edge.h>
#include <hierarchical_map/graph.h>
#include <hierarchical_map/sdd_simple_path_compiler.h>
#include <map>
#include <psdd/psdd_node.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using hierarchical_map::Edge;
using hierarchical_map::Graph;
using std::map;
using std::pair;
using std::vector;
struct VtreeData {
  Graph *graph_;
  vector<bool> edges_;
  vector<bool> frontier;
  vector<bool> inner_nodes;
  explicit VtreeData(Graph *graph) : graph_(graph) {
    edges_.resize(graph->edges().size() + 1, false);
    frontier.resize((size_t)graph_->node_size() + 1, false);
    inner_nodes.resize((size_t)graph_->node_size() + 1, false);
  }
  void operator|=(const VtreeData &other) {
    auto num_edges = graph_->edges().size();
    for (int i = 1; i <= (int)num_edges; i++)
      edges_[i] = (edges_[i] | other.edges_[i]);
  }
  void set_frontier() {
    auto num_nodes = graph_->node_size();
    auto num_edges = graph_->edges().size();
    const auto &graph_edges = graph_->edges();
    vector<bool> g1((size_t)num_nodes + 1, false);
    vector<bool> g2((size_t)num_nodes + 1, false);
    for (auto i = 1; i <= (int)num_edges; i++) {
      if (edges_[i]) {
        g1[graph_edges[i - 1]->x_node_index()] =
            g1[graph_edges[i - 1]->y_node_index()] = 1;
        inner_nodes[graph_edges[i - 1]->x_node_index()] =
            inner_nodes[graph_edges[i - 1]->y_node_index()] = 1;
      } else
        g2[graph_edges[i - 1]->x_node_index()] =
            g2[graph_edges[i - 1]->y_node_index()] = 1;
    }
    for (auto i = 1; i <= num_nodes; i++) {
      frontier[i] = g1[i] && g2[i];
    }
  }
};

struct Node {
  vector<SddLiteral> m; // matching
  int term;             // 1 is true, 2 is false, -1 is empty
  SddLiteral
      label; // +/- edge number (i.e. -3 means -C, 2 means +B in final SDD)
  vector<pair<Node *, Node *>> children;
  bool processed;
  Node(Graph *graph, NodeSize source_node, NodeSize target_node) {
    auto num_nodes = (size_t)graph->node_size();
    m.resize(num_nodes + 1);
    for (int i = 1; i <= static_cast<int>(num_nodes); i++)
      m[i] = i;
    m[source_node] = -1 * target_node, m[target_node] = -1 * source_node;
    term = 0;
    label = 0;
    processed = false;
  }
  Node(const Node *cp) {
    m = cp->m;
    term = cp->term;
    label = cp->label;
    children = cp->children;
    processed = false;
  }
};

bool is_one_term(const Node *r) { return r->term == 1; }

bool is_zero_term(const Node *r) { return r->term == 2; }

bool is_empty_term(const Node *r) { return r->term == -1; }

/*
SddSize count_sdd(Vtree *vtree, Node *r) {
if (is_zero_term(r))
  return 0;
if (is_one_term(r))
  return 1;
if (is_empty_term(r))
  return 0;
if (r->label != 0)
  return 1;

SddSize count = 0;

Vtree *lt = sdd_vtree_left(vtree);
Vtree *rt = sdd_vtree_right(vtree);

for (SddSize i = 0; i < r->children.size(); i++) {
  Node *left = r->children[i].first;
  Node *right = r->children[i].second;

  SddSize lc = count_sdd(lt, left);
  SddSize rc = count_sdd(rt, right);

  count += lc * rc;
}
return count;
}
*/

bool finished(Node *s) {
  auto num_nodes = s->m.size() - 1;
  for (auto i = 1; i <= static_cast<int>(num_nodes); i++)
    if (s->m[i] != 0 && s->m[i] != i)
      return false;
  return true;
}

inline SddLiteral sgn(SddLiteral x) { return (x > 0) - (x < 0); }

bool isShannon(Vtree *vtree) {
  return sdd_vtree_is_leaf(sdd_vtree_left(vtree)) ||
         sdd_vtree_is_leaf(sdd_vtree_right(vtree));
}

struct equal_node {
  bool operator()(const Node *n1, const Node *n2) const {
    if (is_one_term(n1) != is_one_term(n2))
      return false;
    if (is_zero_term(n1) != is_zero_term(n2))
      return false;
    if (is_empty_term(n1) != is_empty_term(n2))
      return false;
    if (n1->label != n2->label)
      return false;
    auto num_nodes = n1->m.size() - 1;
    for (auto i = 1; i <= static_cast<int>(num_nodes); i++)
      if (n1->m[i] != n2->m[i])
        return false;
    return true;
  }
};
struct hash_node {
  size_t operator()(const Node *node) const {
    size_t res = 0;
    auto num_nodes = node->m.size() - 1;
    for (auto i = 1; i <= static_cast<int>(num_nodes); i++)
      res ^= node->m[i] + 0x9e3779b9 + (res << 6) + (res >> 2);
    return res;
  }
};

class SddSimplePathCompilerImplement : public SddSimplePathCompiler {
public:
  ~SddSimplePathCompilerImplement() override {
    auto vtree_nodes = vtree_util::SerializeVtree(vtree_);
    for (Vtree *cur_vtree : vtree_nodes) {
      auto cur_vtree_data = (VtreeData *)sdd_vtree_data(cur_vtree);
      delete (cur_vtree_data);
    }
    for (Node *cur_node : constructed_nodes_) {
      delete (cur_node);
    }
    sdd_vtree_free(vtree_);
  }
  SddSimplePathCompilerImplement(const vector<Edge *> &edges,
                                 NodeSize source_node, NodeSize target_node,
                                 SddManager *manager)
      : manager_(manager) {
    std::vector<Edge *> new_edges(edges.size(), nullptr);
    std::unordered_map<NodeSize, NodeSize> node_map;
    NodeSize node_size = 0;
    EdgeSize edge_index = 0;
    for (Edge *cur_edge : edges) {
      auto x_it = node_map.find(cur_edge->x_node_index());
      auto y_it = node_map.find(cur_edge->y_node_index());
      if (x_it == node_map.end()) {
        x_it = node_map.insert({cur_edge->x_node_index(), ++node_size}).first;
      }
      if (y_it == node_map.end()) {
        y_it = node_map.insert({cur_edge->y_node_index(), ++node_size}).first;
      }
      new_edges[edge_index++] =
          new Edge(cur_edge->edge_name(), x_it->second, y_it->second);
    }
    graph_ = Graph::GraphFromStolenEdgeList(std::move(new_edges));
    vtree_ = copy_vtree(sdd_manager_vtree(manager_));
    auto source_node_it = node_map.find(source_node);
    auto target_node_it = node_map.find(target_node);
    assert(source_node_it != node_map.end() &&
           target_node_it != node_map.end());
    source_node_ = source_node_it->second;
    target_node_ = target_node_it->second;
    one_term_ = GetNode(graph_, source_node_, target_node_);
    zero_term_ = GetNode(graph_, source_node_, target_node_);
    one_term_->term = 1;
    zero_term_->term = 2;
    one_term_->m[source_node_] = one_term_->m[target_node_] = 0;
  }
  SddManager *sdd_manager() { return manager_; }
  SddNode *Compile() override {
    init_vtree_frontier(vtree_);
    map<Vtree *, vector<Node *>> Z;
    Z[vtree_] = vector<Node *>(1, rootNode());
    construct(vtree_, Z);
    return dfs(vtree_, Z[vtree_][0]);
  }
  Node *GetNode(Node *other) {
    Node *new_node = new Node(other);
    constructed_nodes_.push_back(new_node);
    return new_node;
  }
  Node *GetNode(Graph *graph, NodeSize source_node, NodeSize target_node) {
    Node *new_node = new Node(graph, source_node, target_node);
    constructed_nodes_.push_back(new_node);
    return new_node;
  }
  NodeSize source_node() const { return source_node_; }
  NodeSize target_node() const { return target_node_; }
  Graph *graph() const { return graph_; }
  Node *zero_term() { return zero_term_; }
  Node *one_term() { return one_term_; }

private:
  Vtree *vtree_;
  SddManager *manager_;
  Graph *graph_;
  NodeSize source_node_;
  NodeSize target_node_;
  std::vector<Node *> constructed_nodes_;
  Node *zero_term_;
  Node *one_term_;
  Node *rootNode() {
    Node *ret = GetNode(graph_, source_node_, target_node_);
    return ret;
  }
  SddNode *dfs(Vtree *vtree, Node *r) {
    if (is_zero_term(r))
      return sdd_manager_false(manager_);
    if (is_one_term(r))
      return sdd_manager_true(manager_);
    if (is_empty_term(r))
      return sdd_manager_true(manager_);
    if (r->label != 0) {
      if (r->label > 0)
        return sdd_manager_literal(r->label, manager_);
      else
        return sdd_negate(sdd_manager_literal(-1 * r->label, manager_),
                          manager_);
    }

    Vtree *lt = sdd_vtree_left(vtree);
    Vtree *rt = sdd_vtree_right(vtree);

    SddNode *alpha = sdd_manager_false(manager_);
    SddNode *beta;

    for (auto i = 0; i < static_cast<int>(r->children.size()); i++) {
      Node *left = r->children[i].first;
      Node *right = r->children[i].second;

      SddNode *sl = dfs(lt, left);
      SddNode *sr = dfs(rt, right);

      beta = sdd_conjoin(sl, sr, manager_);
      alpha = sdd_disjoin(alpha, beta, manager_);
    }
    return alpha;
  }

  void init_vtree_frontier(Vtree *root) {
    VtreeData *vd = new VtreeData(graph_);
    vd->edges_[sdd_vtree_var(root)] = true;
    Vtree *left = sdd_vtree_left(root);
    Vtree *right = sdd_vtree_right(root);
    if (left) {
      init_vtree_frontier(left);
      VtreeData *lvd = (VtreeData *)sdd_vtree_data(left);
      *vd |= *lvd;
    }
    if (right) {
      init_vtree_frontier(right);
      VtreeData *rvd = (VtreeData *)sdd_vtree_data(right);
      *vd |= *rvd;
    }
    vd->set_frontier(); // IMPORTANT! or else internal Node of vtreedata is
                        // inconsistent
    sdd_vtree_set_data(vd, root);
  }
  Node *shannonChild(Vtree *vtree, Node *z, bool guess) {
    Node *nz = GetNode(z);
    SddLiteral x = sdd_vtree_var(sdd_vtree_left(vtree));
    if (!sdd_vtree_is_leaf(sdd_vtree_left(vtree))) // right child is the leaf
      x = sdd_vtree_var(sdd_vtree_right(vtree));
    const auto &graph_edges = graph()->edges();
    auto ua = graph_edges[x - 1]->x_node_index(),
         ub = graph_edges[x - 1]->y_node_index(); // edges are 0 indexed while
                                                  // sdd are 1 indexed

    if (guess) {
      if (!nz->m[ua] || !nz->m[ub]) {
        return zero_term();
      }
      if (nz->m[ua] == ub && nz->m[ub] == ua) {
        return zero_term();
      }
      if (nz->m[ua] < 0 && nz->m[ub] < 0 && nz->m[ua] != -1 * ua &&
          nz->m[ub] != -1 * ub) { // a and b are reserved
        if (nz->m[ua] == -1 * ub &&
            nz->m[ub] == -1 * ua) { // check if reserved for each other
          nz->m[ua] = nz->m[ub] = 0;
          if (finished(nz)) {
            goto label;
            return nz;
          }
        } else
          return zero_term();
      } else {
        if (nz->m[ua] == -1 * ua)
          nz->m[ua] = ua;
        if (nz->m[ub] == -1 * ub)
          nz->m[ub] = ub;
        auto ta = nz->m[ua], tb = nz->m[ub];
        auto sa = sgn(nz->m[ua]), sb = sgn(nz->m[ub]);
        auto ss = (sa == -1 || sb == -1) ? -1 : 1;
        nz->m[ua] = nz->m[ub] = 0;
        nz->m[(size_t)sa * ta] = ss * sb * tb;
        nz->m[(size_t)sb * tb] = ss * sa * ta;
        if (finished(nz)) {
          goto label;
          return nz;
        }
      }
    }

  label:
    if (!sdd_vtree_is_leaf(sdd_vtree_left(vtree))) // right child is the leaf
      return nz;

    //
    // line 19-21 in paper
    //
    VtreeData *vd = (VtreeData *)sdd_vtree_data(vtree);
    VtreeData *rvd = (VtreeData *)sdd_vtree_data(sdd_vtree_right(vtree));
    auto num_nodes = graph()->node_size();
    for (auto i = 1; i <= num_nodes; i++) {
      // set difference: F(v) \ F(vr)
      if (!(rvd->frontier[i]) && (vd->frontier[i])) {
        if (nz->m[i] != 0 && nz->m[i] != i)
          return zero_term();
        nz->m[i] = 0;
      }
    }

    if (!sdd_vtree_is_leaf(sdd_vtree_right(vtree)))
      return nz;

    SddLiteral y = sdd_vtree_var(sdd_vtree_right(vtree));

    if (finished(nz)) {
      Node *justY = GetNode(graph(), source_node(), target_node());
      justY->label = -1 * y;
      justY->m[source_node()] = justY->m[target_node()] = 0;
      return justY;
    }
    ua = graph_edges[y - 1]->x_node_index(),
    ub = graph_edges[y - 1]->y_node_index();

    if (nz->m[ua] == -1 * ub && nz->m[ub] == -1 * ua) {
      nz->m[ua] = nz->m[ub] = 0;
      if (finished(nz)) {
        Node *justY = GetNode(graph(), source_node(), target_node());
        justY->label = y;
        justY->m[source_node()] = justY->m[target_node()] = 0;
        return justY;
      }
    }
    return zero_term();
  }
  void construct(Vtree *vtree, std::map<Vtree *, vector<Node *>> &Z) {
    std::unordered_map<Node *, Node *, hash_node, equal_node> mml, mmr;
    Vtree *vtl = sdd_vtree_left(vtree);
    Vtree *vtr = sdd_vtree_right(vtree);
    vector<Node *> vz = Z[vtree]; // check initialization
    if (isShannon(vtree)) {
      for (auto i = 0; i < static_cast<int>(vz.size()); i++) {
        Node *z = vz[i];
        if (z->processed)
          continue;

        Node *mf = shannonChild(vtree, z, false);
        Node *mt = shannonChild(vtree, z, true);

        SddLiteral x = sdd_vtree_var(vtl);

        if (!sdd_vtree_is_leaf(vtl)) // right child is the leaf
          x = sdd_vtree_var(vtr);
        if (!is_zero_term(mf)) {
          Node *justX = GetNode(graph_, source_node_, target_node_);
          justX->label = -1 * x;
          if (!sdd_vtree_is_leaf(vtl))
            z->children.push_back({mf, justX});
          else
            z->children.push_back({justX, mf});
        }
        if (!is_zero_term(mt)) {
          Node *justX = GetNode(graph_, source_node_, target_node_);
          justX->label = x;
          if (!sdd_vtree_is_leaf(vtl))
            z->children.push_back({mt, justX});
          else
            z->children.push_back({justX, mt});
        }

        for (auto i = 0; i < static_cast<int>(z->children.size()); i++) {
          if (mml.find(z->children[i].first) == mml.end())
            mml[z->children[i].first] = z->children[i].first;
          if (mmr.find(z->children[i].second) == mmr.end())
            mmr[z->children[i].second] = z->children[i].second;
          z->children[i].first = mml[z->children[i].first];
          z->children[i].second = mmr[z->children[i].second];
          Z[vtl].push_back(z->children[i].first);
          Z[vtr].push_back(z->children[i].second);
        }

        z->processed = true;
      }
      assert(sdd_vtree_is_leaf(vtl) || sdd_vtree_is_leaf(vtr));
      if (!sdd_vtree_is_leaf(vtl))
        construct(vtl, Z);
      if (!sdd_vtree_is_leaf(vtr))
        construct(vtr, Z);
    } else {
      assert(false);
    }
  }
};
} // namespace

SddSimplePathCompiler *SddSimplePathCompiler::GetSddSimplePathCompiler(
    const std::vector<Edge *> &edges, NodeSize src_node, NodeSize dst_node,
    SddManager *sdd_manager) {
  return new SddSimplePathCompilerImplement(edges, src_node, dst_node,
                                            sdd_manager);
}
