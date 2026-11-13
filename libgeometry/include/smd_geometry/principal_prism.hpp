#pragma once

#include "smd_geometry/solid.hpp"

namespace smd_geometry {
struct PrincipalPrismCandidate {
    TopoDS_Solid solid; // Independent derived topology and geometry, not source evidence.
    smd::Vec3 minimumLocalMm, maximumLocalMm, dimensionsMm;
    std::size_t supportingFaceOrdinal; // Zero-based source face, not OCCT traversal index.
};

// GEO-03A: requires the unmodified validated GEO-02 SolidAssembly and its
// authoritative ParsedBrep. Checks their boundary binding, but does not repeat
// GEO-02 validation. Source-local +Z and millimetres only; no placements.
// Exactly one minimum-Z face must be an exact four-corner XY-aligned rectangle.
// Z extent uses only mapped solid-boundary vertices. No fitting or healing.
// Throws std::runtime_error with GEO-03A [input-correspondence], [unsupported],
// [kernel-construction] or [kernel-validation]; no partial result escapes.
// Does not modify/share input topology or geometry. Success proves only that
// this rule produced a candidate, not constant section or intended mould/history.
PrincipalPrismCandidate buildPrincipalPrismCandidate(
    const SolidAssembly& input, const smd::ParsedBrep& source);
} // namespace smd_geometry
