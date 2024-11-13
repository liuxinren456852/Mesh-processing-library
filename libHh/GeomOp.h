// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#ifndef MESH_PROCESSING_LIBHH_GEOMOP_H_
#define MESH_PROCESSING_LIBHH_GEOMOP_H_

#include <optional>

#include "libHh/Geometry.h"
#include "libHh/Polygon.h"

#if 0
{
  Vec3<float> ang = euler_angles_from_frame(f);
  Frame f = frame_from_euler_angles(ang, f);
}
#endif

namespace hh {

// *** Radii

// Compute the circumscribed radius of the 3 points p0, p1, p2.
float circum_radius(const Point& p0, const Point& p1, const Point& p2);

// Compute the inscribed radius of the 3 points p0, p1, p2.
float inscribed_radius(const Point& p0, const Point& p1, const Point& p2);

float aspect_ratio(const Point& p0, const Point& p1, const Point& p2);

// *** Angles

// ret: cos of signed angle away from "flatness" (== exterior angle)
// range -1.f .. 1.f  (1.f if flat, -1.f if foldover inwards/outwards)  (or -2.f if a triangle is degenerate)
float dihedral_angle_cos(const Point& p1, const Point& p2, const Point& po1, const Point& po2);

// Return angle from -TAU / 2 to TAU / 2 (negative is concave), or -10.f if degeneracy.
// (== exterior angle)  (cos(signed_dihedral_angle()) == dihedral_angle_cos()).
float signed_dihedral_angle(const Point& p1, const Point& p2, const Point& po1, const Point& po2);

// Compute solid angle at p surrounded by ordered loop pa[]
// range 0 .. TAU * 2
float solid_angle(const Point& p, CArrayView<Point> pa);

// ret: cos of signed angle away from "flatness" (== exterior angle)
// range -1..1  (or -2 if an edge is degenerate)
float angle_cos(const Point& p1, const Point& p2, const Point& p3);

// *** Frames and Euler angles

// Make the three vectors of `frame` be unit length and orthogonal.
void orthonormalize(Frame& frame);

// Compute Euler angles of f.
Vec3<float> euler_angles_from_frame(const Frame& f);

// Modify f by setting v[0..2] according to Euler angles, keeping origin and axes scaling of prev_frame.
[[nodiscard]] Frame frame_from_euler_angles(const Vec3<float>& ang, const Frame& prev_frame);

// Modify f so that its x axis points towards p and its y axis is vertical;
// f.p() is ignored and unchanged.
void frame_aim_at(Frame& f, const Vector& v);

// Modify frame so that its y axis lies in the xy plane.
Frame make_level(const Frame& f);

// Modify f so that its x and y axes lie in the xy plane.
Frame make_horiz(const Frame& f);

// Affinely broaden the triangle (in all directions) by the factor 1.f + eps * .5f .
[[nodiscard]] Vec3<Point> widen_triangle(const Vec3<Point>& triangle, float eps);

// *** Intersections

// Return the intersection point of a line with a plane, or none if they are parallel.
std::optional<Point> intersect_line_with_plane(const Line& line, const Plane& plane);

// Return the intersection point of a segment (p1, p2) with a plane, if it exists.
std::optional<Point> intersect_segment_with_plane(const Point& p1, const Point& p2, const Plane& plane);

// Return the intersection point of a line with a triangle.
std::optional<Point> intersect_line_with_triangle(const Line& line, const Vec3<Point>& triangle);

// Return the intersection point of a segment (p1, p2) with a triangle.
std::optional<Point> intersect_segment_with_triangle(const Point& p1, const Point& p2, const Vec3<Point>& triangle);

// *** Other

// Return the signed volume of a tetrahedron (positive if p4 is in the direction cross(p1, p2, p3)).
float signed_volume(const Point& p1, const Point& p2, const Point& p3, const Point& p4);

// Return (longitude, latitude) of point on the unit sphere.
Uv lonlat_from_sph(const Point& sph);

// Return point on the unit sphere given (longitude, latitude).
Point sph_from_lonlat(const Uv& lonlat);

}  // namespace hh

#endif  // MESH_PROCESSING_LIBHH_GEOMOP_H_
