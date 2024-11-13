// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#ifndef MESH_PROCESSING_LIBHH_GRAPHOP_H_
#define MESH_PROCESSING_LIBHH_GRAPHOP_H_

#include "libHh/Array.h"
#include "libHh/Geometry.h"
#include "libHh/Graph.h"
#include "libHh/Pqueue.h"
#include "libHh/Queue.h"
#include "libHh/RangeOp.h"  // fill()
#include "libHh/Set.h"
#include "libHh/Spatial.h"
#include "libHh/Stat.h"
#include "libHh/UnionFind.h"

namespace hh {

// Add edges to a graph to make it symmetric.
template <typename T> void graph_symmetric_closure(Graph<T>& g) {
  for (const T& v1 : g.vertices())
    for (const T& v2 : g.edges(v1))
      if (!g.contains(v2, v1)) g.enter(v2, v1);
}

// Given a graph (possibly directed), return vertices in order of increasing graph distance from vs.
// (Vertex vs itself is returned on first invocation of next().)
template <typename T, typename Func_dist = float (&)(const T& v1, const T& v2)> class Dijkstra : noncopyable {
 public:
  explicit Dijkstra(const Graph<T>* g, T vs, Func_dist fdist = Func_dist{}) : _g(*assertx(g)), _fdist(fdist) {
    _pq.enter(vs, 0.f);
  }
  bool done() { return _pq.empty(); }
  T next(float& dis) {
    assertx(!_pq.empty());
    float dmin = _pq.min_priority();
    T vmin = _pq.remove_min();
    _set.enter(vmin);
    for (const T& v : _g.edges(vmin)) {
      if (_set.contains(v)) continue;
      float pnd = dmin + _fdist(vmin, v);  // possibly smaller distance
      _pq.enter_update_if_smaller(v, pnd);
    }
    dis = dmin;
    return vmin;
  }

 private:
  const Graph<T>& _g;
  Func_dist _fdist;
  HPqueue<T> _pq;
  Set<T> _set;
};

// Template deduction guide:
template <typename T, typename Func_dist> Dijkstra(const Graph<T>* g, T vs, Func_dist fdist) -> Dijkstra<T, Func_dist>;

// *** Kruskal MST

template <typename T> struct MstResult {
  Graph<T> tree;
  bool is_connected;
};

// Returns [gnew, is_connected] where gnew is the minimum spanning tree of undirectedg under the cost metric fdist.
// Implementation: Kruskal's algorithm, O(e log(e))  (Prim's algorithm is recommended when e=~n^2, see below.)
template <typename T, typename Func = float(const T&, const T&)>
auto graph_mst(const Graph<T>& undirectedg, Func fdist) {
  MstResult<T> result;
  Graph<T>& gnew = result.tree;
  for (const T& v : undirectedg.vertices()) gnew.enter(v);
  int nv = 0, nebefore = 0;
  struct tedge {
    T v1, v2;
    float w;
  };
  Array<tedge> tedges;
  for (const T& v1 : undirectedg.vertices()) {
    nv++;
    for (const T& v2 : undirectedg.edges(v1)) {
      if (v1 < v2) continue;
      nebefore++;
      tedges.push(tedge{v1, v2, fdist(v1, v2)});
    }
  }
  const auto by_increasing_weight = [](const tedge& a, const tedge& b) { return a.w < b.w; };
  sort(tedges, by_increasing_weight);
  UnionFind<T> uf;
  int neconsidered = 0, neadded = 0;
  for (const tedge& t : tedges) {
    neconsidered++;
    T v1 = t.v1, v2 = t.v2;
    if (!uf.unify(v1, v2)) continue;
    gnew.enter_undirected(v1, v2);
    neadded++;
    if (neadded == nv - 1) break;
  }
  showf("graph_mst: %d vertices, %d/%d edges considered, %d output\n", nv, neconsidered, nebefore, neadded);
  result.is_connected = neadded == nv - 1;
  return result;
}

// *** Prim MST

// Returns a undirected graph that is the minimum spanning tree of the full graph
//  between the num points, where the cost metric between two points v1 and v2 is fdist(v1, v2).
// Implementation: Prim's algorithm, complexity O(n^2)!
template <typename Func = float(int, int)> Graph<int> graph_mst(int num, Func fdist) {
  assertx(num > 0);
  const float k_inf = 1e30f;
  Array<float> lowcost(num);
  Array<int> closest(num);
  Graph<int> gnew;
  for_int(i, num) gnew.enter(i);
  for_intL(i, 1, num) {
    lowcost[i] = fdist(0, i);
    closest[i] = 0;
  }
  for_intL(i, 1, num) {
    const int offset = 1;
    int minj = arg_min(lowcost.slice(offset, num)) + offset;
    float minf = lowcost[minj];
    assertx(minf < k_inf);
    gnew.enter_undirected(minj, closest[minj]);
    lowcost[minj] = k_inf;
    for_intL(j, 1, num) {
      if (lowcost[j] == k_inf) continue;
      float pnd = fdist(minj, j);
      if (pnd < lowcost[j]) {
        lowcost[j] = pnd;
        closest[j] = minj;
      }
    }
  }
  return gnew;
}

// *** graph_quick_emst

// Try to build the EMST of the num points pa using all edges with length less than thresh.
// Uses modified Prim's, where only edges of length<thresh are considered.
// Uses a HPqueue because it can no longer afford to find min in O(n) time.
// Returns an empty graph if not connected.
inline Graph<int> try_emst(float thresh, CArrayView<Point> pa, const PointSpatial<int>& sp) {
  Graph<int> gnew;
  Array<bool> inset(pa.num(), false);  // vertices already added to mst
  Array<int> closest(pa.num());        // for !inset[i], closest inset[] so far
  for_int(i, pa.num()) gnew.enter(i);
  HPqueue<int> pq;
  pq.enter(0, 0.f);
  while (!pq.empty()) {
    int i = pq.remove_min();
    ASSERTXX(pa.ok(i) && !inset[i]);
    if (i) gnew.enter_undirected(i, closest[i]);
    inset[i] = true;
    SpatialSearch<int> ss(&sp, pa[i]);
    for (;;) {
      if (ss.done()) break;
      const auto [j, d2] = ss.next();
      if (d2 > square(thresh)) break;
      if (inset[j]) continue;
      if (pq.enter_update_if_smaller(j, d2)) closest[j] = i;
    }
  }
  int nfound = 0;
  for_int(i, pa.num()) {
    if (inset[i]) nfound++;
  }
  if (nfound != pa.num()) gnew.clear();
  return gnew;
}

// Same as graph_mst() but works specifically on an array of points and tries
// to do it more quickly by making use of a spatial data structure.
// Implementation: Prim's MST on series of subgraphs.
inline Graph<int> graph_quick_emst(CArrayView<Point> pa, const PointSpatial<int>& sp) {
  Graph<int> gnew;
  const float initf = .02f;
  int n = 0;
  for (float f = initf;; f *= 1.6f) {
    n++;
    gnew = try_emst(f, pa, sp);
    if (!gnew.empty()) break;
    assertx(f < 1.f);
  }
  showf("GraphQuickEmst: had to do %d approximate Emst's\n", n);
  return gnew;
}

// Return statistics about graph edge lengths.  If undirected, edges stats are duplicated.
template <typename T, typename Func = float(const T&, const T&)> Stat graph_edge_stats(const Graph<T>& g, Func fdist) {
  Stat stat;
  for (const T& v1 : g.vertices())
    for (const T& v2 : g.edges(v1)) stat.enter(fdist(v1, v2));
  return stat;
}

// Returns a newly allocated directed graph that connects each vertex to its
// kcl closest neighbors (based on Euclidean distance).
// Consider applying graph_symmetric_closure() !
inline Graph<int> graph_euclidean_k_closest(CArrayView<Point> pa, int kcl, const PointSpatial<int>& sp) {
  Graph<int> gnew;
  for_int(i, pa.num()) gnew.enter(i);
  for_int(i, pa.num()) {
    SpatialSearch<int> ss(&sp, pa[i]);
    for_int(nn, kcl + 1) {
      const int j = ss.next().id;
      if (j == i) continue;
      gnew.enter(i, j);
    }
  }
  return gnew;
}

// *** GraphComponent

// Access each connected component of a graph.
// next() returns a representative vertex of each component.
template <typename T> class GraphComponent : noncopyable {
 public:
  explicit GraphComponent(const Graph<T>* g) : _g(*assertx(g)) {
    auto&& r = _g.vertices();
    _vcur = r.begin();
    _vend = r.end();
  }
  explicit operator bool() const { return _vcur != _vend; }
  T operator()() const { return *_vcur; }
  void next() {
    Queue<T> queue;
    _set.enter(*_vcur);
    queue.enqueue(*_vcur);
    while (!queue.empty()) {
      T v = queue.dequeue();
      for (const T& v2 : _g.edges(v))
        if (_set.add(v2)) queue.enqueue(v2);
    }
    for (++_vcur; _vcur != _vend; ++_vcur)
      if (!_set.contains(*_vcur)) break;
  }

 private:
  const Graph<T>& _g;
  typename Graph<T>::vertex_iterator _vcur;
  typename Graph<T>::vertex_iterator _vend;
  Set<T> _set;
};

template <typename T> int graph_num_components(const Graph<T>& g) {
  int n = 0;
  for (GraphComponent<T> gc(&g); gc; gc.next()) n++;
  return n;
}

}  // namespace hh

#endif  // MESH_PROCESSING_LIBHH_GRAPHOP_H_
