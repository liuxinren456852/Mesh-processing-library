// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#ifndef MESH_PROCESSING_LIBHH_SPATIAL_H_
#define MESH_PROCESSING_LIBHH_SPATIAL_H_

#include "libHh/Array.h"
#include "libHh/Bbox.h"
#include "libHh/Geometry.h"
#include "libHh/Map.h"
#include "libHh/Pqueue.h"
#include "libHh/Queue.h"
#include "libHh/Set.h"
#include "libHh/Stat.h"
#include "libHh/Univ.h"
#include "libHh/Vec.h"

namespace hh {

namespace details {
class BSpatialSearch;
}

// Spatial data structure for efficient queries like "closest_elements" or "find_elements_intersecting_ray".
class Spatial : noncopyable {            // abstract class
  static constexpr int k_max_gn = 1023;  // 10 bits per coordinate
 public:
  explicit Spatial(int gn) : _gn(gn) {
    assertx(_gn <= k_max_gn);
    _gni = 1.f / float(_gn);
  }
  virtual ~Spatial() {}  // not = default because gcc "looser throw specified" in derived
  virtual void clear() = 0;

 protected:
  friend details::BSpatialSearch;
  int _gn;     // grid size
  float _gni;  // 1.f / _gn
  //
  using Ind = Vec3<int>;
  int inbounds(int i) const { return i >= 0 && i < _gn; }
  int indices_inbounds(const Ind& ci) const { return inbounds(ci[0]) && inbounds(ci[1]) && inbounds(ci[2]); }
  int index_from_float(float fd) const;
  float float_from_index(int i) const { return i * _gni; }
  Ind indices_from_point(const Point& p) const {
    Ind ci;
    for_int(c, 3) ci[c] = index_from_float(p[c]);
    return ci;
  }
  Point point_from_indices(const Ind& ci) const {
    Point p;
    for_int(c, 3) p[c] = float_from_index(ci[c]);
    return p;
  }
  Bbox<float, 3> bbox_of_indices(const Ind& ci) const;
  int encode(const Ind& ci) const { return (ci[0] << 20) | (ci[1] << 10) | ci[2]; }  // k_max_gn implied here
  Ind decode(int en) const;

  // for BSpatialSearch:
  // Add elements from cell ci to priority queue with priority equal to distance from pcenter squared.
  // May use set to avoid duplication.
  virtual void add_cell(const Ind& ci, Pqueue<Univ>& pq, const Point& pcenter, Set<Univ>& set) const = 0;

  // Refine distance estimate of first entry in pq (optional)
  virtual void pq_refine(Pqueue<Univ>& pq, const Point& pcenter) const { dummy_use(pq, pcenter); }

  virtual Univ pq_id(Univ pqe) const = 0;  // given pq entry, return id
};

namespace details {

class BPointSpatial : public Spatial {
 public:
  explicit BPointSpatial(int gn) : Spatial(gn) {}
  ~BPointSpatial() override { BPointSpatial::clear(); }
  void clear() override;
  // id != 0
  void enter(Univ id, const Point* pp);   // note: pp not copied, no ownership taken
  void remove(Univ id, const Point* pp);  // must exist, else die
  void shrink_to_fit();                   // often just fragments memory

 private:
  void add_cell(const Ind& ci, Pqueue<Univ>& pq, const Point& pcenter, Set<Univ>& set) const override;
  Univ pq_id(Univ pqe) const override;
  struct Node {
    Univ id;
    const Point* p;
  };
  Map<int, Array<Node>> _map;  // encoded cube index -> Array
};

}  // namespace details

// Spatial data structure for point elements.
template <typename T> class PointSpatial : public details::BPointSpatial {
 public:
  explicit PointSpatial(int gn) : BPointSpatial(gn) {}
  void enter(T id, const Point* pp) { BPointSpatial::enter(Conv<T>::e(id), pp); }
  void remove(T id, const Point* pp) { BPointSpatial::remove(Conv<T>::e(id), pp); }
};

// Spatial data structure for point elements indexed by an integer.
class IPointSpatial : public Spatial {
 public:
  explicit IPointSpatial(int gn, CArrayView<Point> arp);
  ~IPointSpatial() override { clear(); }
  void clear() override;

 private:
  void add_cell(const Ind& ci, Pqueue<Univ>& pq, const Point& pcenter, Set<Univ>& set) const override;
  Univ pq_id(Univ pqe) const override;

  const Point* _pp;
  Map<int, Array<int>> _map;  // encoded cube index -> Array of point indices
};

// Spatial data structure for more general objects.
template <typename Approx2 = float(const Point& p, Univ id), typename Exact2 = float(const Point& p, Univ id)>
class ObjectSpatial : public Spatial {
 public:
  explicit ObjectSpatial(int gn) : Spatial(gn) {}
  ~ObjectSpatial() override { ObjectSpatial::clear(); }
  void clear() override {
    for (auto& cell : _map.values()) HH_SSTAT(Sospcelln, cell.num());
  }
  // id != 0
  // Enter an object that comes with a containment function: the function returns true if the object lies
  // within a given bounding box.  A starting point is also given.
  template <typename Func = bool(const Bbox<float, 3>&)> void enter(Univ id, const Point& startp, Func fcontains);

  // Find the objects that could possibly intersect the segment (p1, p2).
  // The objects are not returned in the exact order of intersection!
  // However, once should_stop is set (ftest's return), the procedure
  // will keep calling ftest with all objects that could be closer.
  template <typename Func = bool(Univ)> void search_segment(const Point& p1, const Point& p2, Func ftest) const;

 private:
  Map<int, Array<Univ>> _map;  // encoded cube index -> vector

  void add_cell(const Ind& ci, Pqueue<Univ>& pq, const Point& pcenter, Set<Univ>& set) const override;
  void pq_refine(Pqueue<Univ>& pq, const Point& pcenter) const override;
  Univ pq_id(Univ pqe) const override { return pqe; }
};

namespace details {

class BSpatialSearch : noncopyable {
 public:
  // pmaxdis is only a request, you may get objects that lie farther
  explicit BSpatialSearch(const Spatial* pspatial, const Point& p, float maxdis = 10.f);
  ~BSpatialSearch();
  bool done();
  struct Result {
    Univ id;
    float d2;  // Squared distance.
  };
  Result next();

 private:
  friend Spatial;
  using Ind = Vec3<int>;
  const Spatial& _spatial;
  const Point _pcenter;
  float _maxdis;
  Pqueue<Univ> _pq;    // pq of entries by distance
  Vec2<Ind> _ssi;      // search space indices (extents)
  float _disbv2{0.f};  // distance to search space boundary
  int _axis;           // axis to expand next
  int _dir;            // direction in which to expand next (0, 1)
  Set<Univ> _setevis;  // may be used by add_cell()
  int _ncellsv{0};
  int _nelemsv{0};

  void get_closest_next_cell();
  void expand_search_space();
  void consider(const Ind& ci);
};

}  // namespace details

// Search for nearest element(s) from a given query point.
template <typename T> class SpatialSearch : public details::BSpatialSearch {
 public:
  SpatialSearch(const Spatial* pspatial, const Point& pp, float pmaxdis = 10.f)
      : BSpatialSearch(pspatial, pp, pmaxdis) {}
  struct Result {
    T id;
    float d2;  // Squared distance
  };
  Result next() {
    const auto [id, d2] = BSpatialSearch::next();
    return {Conv<T>::d(id), d2};
  }
};

//----------------------------------------------------------------------------

inline int Spatial::index_from_float(float fd) const {
  float f = fd;
  if (f < 0.f) {
    ASSERTX(f > -.01f);
    f = 0.f;
  }
  if (f >= .99999f) {
    ASSERTX(f < 1.01f);
    f = .99999f;
  }
  return int(f * _gn);
}

inline Bbox<float, 3> Spatial::bbox_of_indices(const Ind& ci) const {
  const Point bb0 = point_from_indices(ci);
  const float eps = 1e-7f;
  return Bbox{bb0 - eps, bb0 + thrice(_gni + eps)};
}

inline Spatial::Ind Spatial::decode(int en) const {
  Ind ci;
  // Note: k_max_gn implied here.
  ci[2] = en & ((1 << 10) - 1);
  en = en >> 10;
  ci[1] = en & ((1 << 10) - 1);
  en = en >> 10;
  ci[0] = en;
  return ci;
}

template <typename Approx2, typename Exact2>
void ObjectSpatial<Approx2, Exact2>::add_cell(const Ind& ci, Pqueue<Univ>& pq, const Point& pcenter,
                                              Set<Univ>& set) const {
  int en = encode(ci);
  bool present;
  auto& cell = _map.retrieve(en, present);
  if (!present) return;
  Approx2 approx2;
  for (Univ e : cell) {
    if (!set.add(e)) continue;
    pq.enter(e, approx2(pcenter, e));
  }
}

template <typename Approx2, typename Exact2>
void ObjectSpatial<Approx2, Exact2>::pq_refine(Pqueue<Univ>& pq, const Point& pcenter) const {
  Univ id = pq.min();
  float oldv = pq.min_priority();
  Exact2 exact2;
  float newv = exact2(pcenter, id);
  if (newv == oldv) return;
  if (newv < oldv - 1e-12f && Warning("newv<oldv")) SHOW(oldv, newv);
  assertx(pq.remove_min() == id);
  pq.enter(id, newv);
}

template <typename Approx2, typename Exact2>
template <typename Func>
void ObjectSpatial<Approx2, Exact2>::enter(Univ id, const Point& startp, Func fcontains) {
  Set<int> set;
  Queue<int> queue;
  int ncubes = 0;
  Ind ci = indices_from_point(startp);
  assertx(indices_inbounds(ci));
  int enf = encode(ci);
  set.enter(enf);
  queue.enqueue(enf);
  while (!queue.empty()) {
    int en = queue.dequeue();
    ci = decode(en);
    Bbox bbox = bbox_of_indices(ci);
    const bool in_cell = fcontains(bbox);
    if (en == enf) assertx(in_cell);
    if (!in_cell) continue;
    _map[en].push(id);
    ncubes++;
    Vec2<Ind> bi;
    for_int(c, 3) {
      bi[0][c] = max(ci[c] - 1, 0);
      bi[1][c] = min(ci[c] + 1, _gn - 1);
    }
    for (const Ind& cit : range(bi[0], bi[1] + 1)) {
      int enc = encode(cit);
      if (set.add(enc)) queue.enqueue(enc);
    }
  }
  HH_SSTAT(Sospobcells, ncubes);
}

template <typename Approx2, typename Exact2>
template <typename Func>
void ObjectSpatial<Approx2, Exact2>::search_segment(const Point& p1, const Point& p2, Func ftest) const {
  Set<Univ> set;
  bool should_stop = false;
  for_int(c, 3) {
    assertx(p1[c] >= 0.f && p1[c] <= 1.f);
    assertx(p2[c] >= 0.f && p2[c] <= 1.f);
  }
  float maxe = max_abs_element(p2 - p1);
  int ni = index_from_float(maxe) + 2;  // Add 2 there just to be safe.
  Vector v = (p2 - p1) * ((1.f + 1e-7f) / float(ni));
  Point p = p1;
  Ind pci = indices_from_point(p);
  int pen = -1;
  for (int i = 0;; i++) {
    Ind cci = indices_from_point(p);
    ASSERTX(indices_inbounds(cci));
    Vec2<Ind> bi;
    for_int(c, 3) {
      bi[0][c] = min(cci[c], pci[c]);
      bi[1][c] = max(cci[c], pci[c]);
    }
    for (const Ind& cit : range(bi[0], bi[1] + 1)) {
      int en = encode(cit);
      if (en == pen) continue;
      bool present;
      auto& cell = _map.retrieve(en, present);
      if (!present) continue;
      for (Univ e : cell)
        if (set.add(e) && ftest(e)) should_stop = true;
    }
    if (i == ni || should_stop) break;
    pci = cci;
    pen = encode(pci);
    p += v;
  }
  if (!should_stop) assertw(!compare(p, p2, 1e-6f));
}

}  // namespace hh

#endif  // MESH_PROCESSING_LIBHH_SPATIAL_H_
