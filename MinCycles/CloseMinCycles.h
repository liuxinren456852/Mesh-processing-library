// -*- C++ -*-  Copyright (c) Microsoft Corporation; see license.txt
#ifndef MESH_PROCESSING_MINCYCLES_CLOSEMINCYCLES_H_
#define MESH_PROCESSING_MINCYCLES_CLOSEMINCYCLES_H_

#include <optional>

#include "libHh/Array.h"
#include "libHh/GMesh.h"
#include "libHh/Map.h"

namespace hh {

// Simplify the topology of a mesh by iteratively locating and closing minimal non-separating cycles.
// Options specify when to stop the topological simplification, and how precise "minimal" must be.
class CloseMinCycles {
 public:
  CloseMinCycles(GMesh& mesh) : _mesh(mesh) {}
  float _max_cycle_length{BIGFLOAT};  // by default, allow cycles with infinite length when iteratively simplifying
  int _max_cycle_nedges{std::numeric_limits<int>::max()};  // by default, allow cycles with infinite number of edges
  int _ncycles{std::numeric_limits<int>::max()};           // by default, perform as many cycle closures as possible
  int _desired_genus{0};                                   // by default, simplify mesh topology to genus zero
  float _frac_cycle_length{1.f};  // by default, find exact minimal cycles (> 1.f means approximate)
  bool _mark_edges_sharp{true};
  bool _mark_faces_filled{true};
  void compute();

 private:
  GMesh& _mesh;
  int _cgenus{std::numeric_limits<int>::max()};  // current mesh genus
  int _tot_handles{0};
  int _tot_tunnels{0};
  Flag e_joined(Edge e);
  void flood_reinitialize(Vertex vseed);
  Array<Vertex> close_cycle(const CArrayView<Vertex> vao);
  bool would_be_nonseparating_cycle(Edge e12, bool exact);
  std::optional<int> look_for_cycle(Vertex v1, Vertex v2, bool process, float verify_dist);  // Ret: num_edges.
  struct MinCycleResult {
    float search_radius;
    Vertex farthest_vertex;
    int num_edges;
  };
  std::optional<MinCycleResult> min_cycle_from_vertex(Vertex vseed, bool process);
  void find_cycles();
};

}  // namespace hh

#endif  // MESH_PROCESSING_MINCYCLES_CLOSEMINCYCLES_H_
