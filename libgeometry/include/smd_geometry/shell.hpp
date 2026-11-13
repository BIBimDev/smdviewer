#pragma once

#include "smd_geometry/faces.hpp"
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shell.hxx>
#include <array>

namespace smd_geometry {
struct SourceEdgeUse {
    std::size_t faceOrdinal, loopPosition;
    bool forward; // Source traversal from the smaller to the larger index.
};

struct SharedShellEdge {
    std::size_t firstVertex, secondVertex; // Increasing source indices.
    TopoDS_Edge edge; // FORWARD, from firstVertex to secondVertex.
    std::array<SourceEdgeUse, 2> uses;
};

struct ShellAssembly {
    TopoDS_Shell shell;
    // Source ordinal correspondence; unused source vertices stay outside shell.
    std::vector<TopoDS_Vertex> vertices;
    std::vector<TopoDS_Face> faces; // Orientations as used in shell.
    std::vector<SharedShellEdge> edges; // Sorted by source endpoint pair.
    // [source face][source loop position], oriented as used in shell. A reversed
    // face reverses traversal, but this vector retains SOURCE occurrence order.
    std::vector<std::vector<TopoDS_Edge>> loopEdges;
    std::vector<std::size_t> reversedFaceOrdinals; // Sorted; never includes 0.
};

// GEO-02A: exact source-index assembly of an unmodified convertFaces(source)
// collection. Checks correspondence and reuses the GEO-01 geometry policy.
// Private topology/geometry: neither source nor any GEO-01 handle is modified or
// shared with the result. Source-local mm only; no transforms or healing.
// Requires one edge-connected, orientable, vertex-manifold closed shell, checked
// contextually by OCCT. Throws std::runtime_error; no partial result escapes.
// Face 0 fixes relative orientation. No outward/solid/interference claim.
ShellAssembly assembleShell(const smd::ParsedBrep& source, const FaceConversion& input);
} // namespace smd_geometry
