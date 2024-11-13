// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "libHh/Polygon.h"

#include "libHh/Array.h"
#include "libHh/Bbox.h"
#include "libHh/PArray.h"
#include "libHh/RangeOp.h"

namespace hh {

HH_ALLOCATE_POOL(Polygon);

Vector Polygon::get_normal_dir() const {
  const auto& self = *this;
  if (num() == 3) return cross(self[0], self[1], self[2]);  // short-cut
  assertx(num() >= 3);
  Vector nor{};
  for_intL(i, 1, num() - 1) nor += cross(self[0], self[i], self[i + 1]);
  return nor;
}

Vector Polygon::get_normal() const { return ok_normalized(get_normal_dir()); }

float Polygon::get_planec(const Vector& pnor) const {
  assertx(num() >= 3);
  float sumd = 0.f;
  for_int(i, num()) sumd += dot((*this)[i], pnor);
  return sumd / num();
}

float Polygon::get_tolerance(const Vector& pnor, float d) const {
  assertx(num() >= 3);
  float tol = 0.f;
  for_int(i, num()) {
    float od = abs(dot((*this)[i], pnor) - d);
    if (od > tol) tol = od;
  }
  return tol;
}

float Polygon::get_area() const {
  assertx(num() >= 3);
  float sum = 0.f;
  for_intL(i, 1, num() - 1) sum += sqrt(area2((*this)[0], (*this)[i], (*this)[i + 1]));
  return sum;
}

bool Polygon::intersect_hyperplane(const Point& hp, const Vector& hn) {
  assertx(num() >= 3);
  auto& self = *this;
  PArray<float, 10> sa(num());
  int num_intersections = 0;
  for_int(i, num()) {
    sa[i] = dot(self[i] - hp, hn) + 1e-7f;
    if (sa[i] >= 0.f) num_intersections++;
  }
  // SHOW(sa);
  if (num_intersections == num()) return false;
  if (num_intersections == 0) {
    init(0);
    return true;
  }
  Polygon new_poly;
  for_int(vc, num()) {
    int vp = vc ? vc - 1 : num() - 1;
    bool inc = sa[vc] >= 0.f;
    bool inp = sa[vp] >= 0.f;
    if (inp ^ inc) new_poly.push(interp(self[vp], self[vc], sa[vc] / (sa[vc] - sa[vp])));
    if (inc) new_poly.push(self[vc]);
  }
  *this = std::move(new_poly);
  return true;
}

bool Polygon::intersect_bbox(const Bbox<float, 3>& bbox) {
  assertx(num() >= 3);
  bool modified = false;
  modified |= intersect_hyperplane(bbox[0], Vector(+1.f, +0.f, +0.f));
  if (!num()) return true;
  modified |= intersect_hyperplane(bbox[1], Vector(-1.f, +0.f, +0.f));
  if (!num()) return true;
  modified |= intersect_hyperplane(bbox[0], Vector(+0.f, +1.f, +0.f));
  if (!num()) return true;
  modified |= intersect_hyperplane(bbox[1], Vector(+0.f, -1.f, +0.f));
  if (!num()) return true;
  modified |= intersect_hyperplane(bbox[0], Vector(+0.f, +0.f, +1.f));
  if (!num()) return true;
  modified |= intersect_hyperplane(bbox[1], Vector(+0.f, +0.f, -1.f));
  if (!num()) return true;
  return modified;
}

std::optional<Point> Polygon::intersect_segment(const Point& p1, const Point& p2) const {
  assertx(num() >= 3);
  Vector nor = get_normal();
  assertx(!is_zero(nor));
  const auto pint = intersect_plane_segment(nor, get_planec(nor), p1, p2);
  if (!pint) return {};
  if (!point_inside(nor, *pint)) return {};
  return *pint;
}

std::optional<Point> Polygon::intersect_line(const Point& p, const Vector& v) const {
  assertx(num() >= 3);
  const Vector nor = get_normal();
  if (!assertw(!is_zero(nor))) return {};
  const float d = get_planec(nor);
  const float numerator = d - dot(p, nor);
  const float denominator = dot(nor, v);
  if (!denominator) return {};
  const float alpha = numerator / denominator;
  const Point pint = p + v * alpha;
  if (!point_inside(nor, pint)) return {};
  return pint;
}

namespace {

int cmp_inter(const Point& p1, const Point& p2, const Vector& vint) {
  float a1 = dot(p1, vint);
  float a2 = dot(p2, vint);
  return a1 < a2 ? -1 : a1 > a2 ? 1 : 0;
}

Vector get_vint(const Vector& polynor, const Vector& planenor) {
  Vector vint = cross(polynor, planenor);
  // was 'if (!...) return', then was assertx
  if (!vint.normalize()) vint[0] = 1.f;
  vector_standard_direction(vint);
  return vint;
}

}  // namespace

void Polygon::intersect_plane(const Vector& poly_normal, const Vector& plane_normal, float plane_d, float plane_tol,
                              Array<Point>& pa) const {
  // See example use in Filtera3d.cpp:compute_intersect()
  assertx(num() >= 3);
  const auto& self = *this;
  PArray<float, 8> sa(num());
  for_int(i, num()) {
    float sc = dot(self[i], plane_normal) - plane_d;
    if (abs(sc) <= plane_tol) sc = 0.f;
    sa[i] = sc;
  }
  float sp = 0.f;
  // make points lying in plane fall off to the side using propagation
  {
    int i0 = 0;
    for_int(i, 2 * num()) {
      float sc = sa[i0];
      if (!sc && sp) sc = sa[i0] = 1e-15f * sign(sp);
      sp = sc;
      i0++;
      if (i0 == num()) i0 = 0;
    }
  }
  pa.init(0);
  if (!sp) return;  // polygon lies in plane
  for_int(i, num()) {
    assertx(sa[i]);
    int i0 = i;
    int i1 = i + 1 < num() ? i + 1 : 0;
    if (sa[i0] * sa[i1] > 0.f) continue;
    pa.push(interp(self[i0], self[i1], sa[i1] / (sa[i1] - sa[i0])));
  }
  assertx((pa.num() & 0x1) == 0);
  if (!pa.num()) return;
  Vector vint = get_vint(poly_normal, plane_normal);
  const auto by_increasing_intersection_t = [&](const Point& p1, const Point& p2) {
    return cmp_inter(p1, p2, vint) == -1;
  };
  sort(pa, by_increasing_intersection_t);
}

static inline float adjust_tolerance(float tol) {
  if (tol < 1e-6f) tol = 1e-6f;
  tol *= 1.02f;
  return tol;
}

Array<Point> intersect_poly_poly(const Polygon& p1, const Polygon& p2) {
  assertx(p1.num() >= 3 && p2.num() >= 3);
  Vector n1 = p1.get_normal();
  Vector n2 = p2.get_normal();
  float d1 = p1.get_planec(n1);
  float d2 = p2.get_planec(n2);
  float t1 = adjust_tolerance(p1.get_tolerance(n1, d1));
  float t2 = adjust_tolerance(p2.get_tolerance(n2, d2));
  Array<Point> pa1, pa2;
  p1.intersect_plane(n1, n2, d2, t2, pa1);
  p2.intersect_plane(n2, n1, d1, t1, pa2);
  bool in1 = false, in2 = false, wasin = false;
  int i1 = 0, i2 = 0;
  Array<Point> pa;
  Point* cp;
  Vector vint = get_vint(n2, n1);
  for (;;) {
    if (i1 == pa1.num() && i2 == pa2.num()) break;
    if (i2 == pa2.num() || (i1 < pa1.num() && cmp_inter(pa1[i1], pa2[i2], vint) <= 0)) {
      cp = &pa1[i1];
      in1 = !in1;
      i1++;
    } else {
      cp = &pa2[i2];
      in2 = !in2;
      i2++;
    }
    bool in = in1 && in2;
    if (in != wasin) {
      pa.push(*cp);
      unsigned pn = pa.num();                                         // unsigned to avoid -Werror=strict-overflow
      if (!in && !compare(pa[pn - 2], pa[pn - 1], 1e-6f)) pa.sub(2);  // remove zero-length segment
    }
    wasin = in;
  }
  assertx((pa.num() & 0x1) == 0 && !in1 && !in2);
  return pa;
}

bool Polygon::point_inside(const Vector& pnor, const Point& point) const {
  assertx(num() >= 3);
  const auto& self = *this;
  int axis = -1;
  float maxd = 0.f;
  for_int(c, 3) {
    float d = abs(pnor[c]);
    if (d > maxd) {
      maxd = d;
      axis = c;
    }
  }
  assertx(maxd);
  int ax0 = mod3(axis + 1);
  int ax1 = mod3(axis + 2);
  float py = point[ax0];
  float pz = point[ax1];
  float y0 = last()[ax0] - py;
  float z0 = last()[ax1] - pz;
  float y1, z1;
  dummy_init(y1, z1);
  int num_intersectionst = 0;
  for (int i = 0; i < num(); i++, y0 = y1, z0 = z1) {
    y1 = self[i][ax0] - py;
    z1 = self[i][ax1] - pz;
    if (z0 >= 0.f && z1 >= 0.f) continue;
    if (z0 < 0.f && z1 < 0.f) continue;
    if (y0 < 0.f && y1 < 0.f) continue;
    if (y0 >= 0.f && y1 >= 0.f) {
      num_intersectionst++;
      continue;
    }
    if (y0 - (y1 - y0) / (z1 - z0) * z0 >= 0.f) num_intersectionst++;
  }
  return (num_intersectionst & 0x1) != 0;
}

bool Polygon::is_convex() const {
  assertx(num() >= 3);
  const auto& self = *this;
  if (num() == 3) return true;
  unsigned n = num();  // unsigned to avoid -Werror=strict-overflow
  Vector dir = get_normal_dir();
  for_int(i, int(n - 2)) {
    if (dot(cross(self[i], self[i + 1], self[i + 2]), dir) < 0.f) return false;
  }
  if (dot(cross(self[n - 2], self[n - 1], self[0]), dir) < 0.f) return false;
  if (dot(cross(self[n - 1], self[0], self[1]), dir) < 0.f) return false;
  return true;
}

std::ostream& operator<<(std::ostream& os, const Polygon& poly) {
  os << "Polygon(" << poly.num() << ") = {";
  for_int(i, poly.num()) os << " " << poly[i];
  return os << " }\n";
}

std::optional<Point> intersect_plane_segment(const Vector& normal, float d, const Point& p1, const Point& p2) {
  const float s1 = dot(p1, normal) - d;
  const float s2 = dot(p2, normal) - d;
  if ((s1 < 0.f && s2 < 0.f) || (s1 > 0.f && s2 > 0.f)) return {};
  const float denominator = s2 - s1;
  // When the segment lies in the polygon plane, we report no intersection.  Is this reasonable?
  if (!denominator) return {};
  return interp(p1, p2, s2 / denominator);
}

Vector orthogonal_vector(const Vector& v) {
  int minc = 0;
  float mina = abs(v[0]);
  for_intL(c, 1, 3) {
    float a = abs(v[c]);
    if (a < mina) {
      mina = a;
      minc = c;
    }
  }
  Vector vaxis(0.f, 0.f, 0.f);
  vaxis[minc] = 1.f;
  Vector vo = cross(v, vaxis);
  return vo;
}

void vector_standard_direction(Vector& v) {
  int maxc = 0;
  float maxa = abs(v[0]);
  for_intL(c, 1, 3) {
    float a = abs(v[c]);
    if (a > maxa) {
      maxa = a;
      maxc = c;
    }
  }
  assertx(maxa);
  if (v[maxc] < 0.f) v = -v;
}

}  // namespace hh
