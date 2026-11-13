#pragma once

#include "smd/types.hpp"
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <vector>

namespace smd_geometry {
// Millimetres. Also the OCCT default vertex/edge tolerance. No source healing.
inline constexpr double distanceToleranceMm = 1e-7;

struct FaceConversion {
    // SourceVertex[i] <-> vertices[i]; shared entities across faces, no merging.
    std::vector<TopoDS_Vertex> vertices;
    // SourceFace[i] <-> faces[i]; source boundary start and direction retained.
    std::vector<TopoDS_Face> faces;
};

// GEO-01B: simple convex/concave single-loop planar polygons, no collinear
// corners, in verticesLocal (mm). Boundary contact tolerance: 1e-7 mm.
// Never reads verticesWorld or applies transforms; input remains authoritative.
// Owns all returned OCCT handles, with no references to the input's lifetime.
// Throws std::runtime_error with zero-based source indices on invalid/unsupported
// input or OCCT failure. No partial result escapes. See libgeometry/README.md.
FaceConversion convertFaces(const smd::ParsedBrep& source);
} // namespace smd_geometry
