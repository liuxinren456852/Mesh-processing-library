// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#include "libHh/Contour.h"

#include "libHh/A3dStream.h"
#include "libHh/FileIO.h"
#include "libHh/MathOp.h"
using namespace hh;

namespace {

// *** Contour2D

void test2D() {
  struct feval2D {
    float operator()(const Vec2<float>& p) const {
      float f = float(dist(p, V(.4f, .4f)) - .25);
      if (dist2(p, V(.3f, .6f)) < square(.3)) f = k_Contour_undefined;
      return f;
    }
  };
  const auto func_polylinetoa3d = [](CArrayView<Vec2<float>> poly, A3dElem& el) {
    el.init(A3dElem::EType::polyline);
    for_int(i, poly.num()) {
      el.push(A3dVertex(Point(0.f, poly[i][0], poly[i][1]), Vector(0.f, 0.f, 0.f), A3dVertexColor(Pixel::red())));
    }
  };
  int gn = 20;
  WFile fcontour("Contour_test.2D");
  WSA3dStream wcontour(fcontour());
  WFile fborder("Contour_test.2Dborder");
  WSA3dStream wborder(fborder());
  A3dElem el;
  const auto func_contour = [&](CArrayView<Vec2<float>> poly) {
    func_polylinetoa3d(poly, el);
    wcontour.write(el);
  };
  const auto func_border = [&](CArrayView<Vec2<float>> poly) {
    func_polylinetoa3d(poly, el);
    wborder.write(el);
  };
  Contour2D contour(gn, feval2D(), func_contour, func_border);
  contour.march_near(V(.64f, .39f));
  // contour.march_from(V(.64f, .39f));
}

// *** Contour3D

struct feval3D {
  float operator()(const Vec3<float>& p) const {
    // Compute at double-precision to avoid numerical differences between different CONFIG.
    Vec3<double> pd = convert<double>(p);
    float f = float((dist(pd, V(.2, .3, .3)) - .15) * (dist(pd, V(.6, .65, .7)) - .35));
    if (dist2(pd, V(.53, .53, .53)) < square(.15)) f = k_Contour_undefined;
    return f;
  }
};

void test3D() {
  int gn = 10;
  WFile fcontour("Contour_test.3D");
  WSA3dStream wcontour(fcontour());
  WFile fborder("Contour_test.3Dborder");
  WSA3dStream wborder(fborder());
  const auto func_polygontoa3d = [](CArrayView<Vec3<float>> poly, A3dElem& el) {
    el.init(A3dElem::EType::polygon);
    for_int(i, poly.num()) el.push(A3dVertex(poly[i], Vector(0.f, 0.f, 0.f), A3dVertexColor(Pixel::red())));
  };
  A3dElem el;
  const auto func_contour = [&](CArrayView<Vec3<float>> poly) {
    func_polygontoa3d(poly, el);
    wcontour.write(el);
  };
  const auto func_border = [&](CArrayView<Vec3<float>> poly) {
    func_polygontoa3d(poly, el);
    wborder.write(el);
  };
  Contour3D contour(gn, func_contour, feval3D(), func_border);
  int nc1 = contour.march_from(Point(.35f, .3f, .3f));
  int nc2 = contour.march_from(Point(.25f, .65f, .7f));
  int nc3 = contour.march_from(Point(.95f, .65f, .7f));
  int nc4 = contour.march_from(Point(.8f, .2f, .1f));
  int nc5 = contour.march_from(Point(.8f, .2f, .1f));
  SHOW(nc1, nc2, nc3, nc4, nc5);
}

void testmesh() {
  GMesh mesh;
  {
    Contour3DMesh<feval3D> contour(10, &mesh);
    if (0) contour.big_mesh_faces();
    contour.set_vertex_tolerance(1e-4f);
    int nc1 = contour.march_from(Point(.35f, .3f, .3f));
    int nc2 = contour.march_from(Point(.25f, .65f, .7f));
    SHOW(nc1, nc2);
  }
  for (Vertex v : mesh.vertices()) {
    Point p = mesh.point(v);
    round_elements(p, 1e4f);
    mesh.set_point(v, p);
  }
  WFile fmesh("Contour_test.m");
  mesh.write(fmesh());
}

struct fmonkey {
  float operator()(const Point& p) const {
    // Monkey saddle, z = x^3 - 3 y^2 x
    const float s = 4.f;
    const Point pp = (p * 2.f - 1.f) * s;
    const float x = pp[0], y = pp[1], z = pp[2];
    float f = z - pow(x, 3.f) + 3.f * y * y * x;
    return f;
  }
};

void do_monkey() {
  GMesh mesh;
  {
    Contour3DMesh<fmonkey> contour(50, &mesh);
    contour.set_vertex_tolerance(1e-5f);
    contour.march_near(Point(.5f, .5f, .5f));
  }
  mesh.write(std::cout);
}

void do_densemonkey() {
  GMesh mesh;
  {
    Contour3DMesh<fmonkey> contour(500, &mesh);
    contour.set_vertex_tolerance(1e-5f);
    contour.march_near(Point(.5f, .5f, .5f));
  }
  mesh.write(std::cout);
}

void do_sphere() {
  static constexpr float k_radius = .4f;
  const auto func_sphere = [](const Vec3<float>& p) {
    float r = dist(p, V(.5f, .5f, .5f));
    if (0) {
      return square(r) - square(k_radius);
    } else if (0) {
      return r - k_radius;
    } else {
      return pow(r, 6.f) - pow(k_radius, 6.f);
    }
  };
  GMesh mesh;
  {
    Contour3DMesh contour(128, &mesh, func_sphere);
    contour.set_vertex_tolerance(1 ? 1e-5f : 0);
    contour.march_near(Point(.5f + k_radius, .5f, .5f));
  }
  mesh.write(std::cout);
}

}  // namespace

int main() {
  if (0) {
  } else if (getenv_bool("PARTIAL_SPHERE")) {
    const auto func_eval = [](const Point& p) {
      return p[0] < .3f ? k_Contour_undefined : dist(p, Point(.5f, .5f, .5f)) - .4f;
    };
    GMesh mesh;
    {
      Contour3DMesh contour(50, &mesh, func_eval);  // or 6
      contour.march_near(Point(.9f, .5f, .5f));
    }
    mesh.write(std::cout);
  } else if (getenv_bool("SPHERE")) {
    do_sphere();
  } else if (getenv_bool("MONKEY")) {
    do_monkey();
  } else if (getenv_bool("DENSE_MONKEY")) {
    do_densemonkey();
  } else {
    testmesh();
    test2D();
    test3D();
  }
}
