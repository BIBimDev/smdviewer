#pragma once

#include "smd_geometry/shell.hpp"
#include <TopoDS_Solid.hxx>

namespace smd_geometry {
struct SolidAssembly {
    TopoDS_Solid solid;
    // Private topology AND geometry. Faces and loopEdges have the cumulative
    // orientations actually used by solid; canonical edges remain FORWARD.
    // reversedFaceOrdinals retains the GEO-02A relative-correction history.
    ShellAssembly boundary;
    bool globallyReversed = false; // One whole-shell reversal, never per-face repair.
    double volumeMm3 = 0;
};

// GEO-02B: an unmodified GEO-02A shell/correspondence, optionally globally
// reversed together with its face/occurrence mappings. Source-local mm only.
// Requires explicit geometric non-interference, one closed boundary, contextual
// validity, infinity OUT, and finite positive volume. No fuzzy matching/healing.
// Throws std::runtime_error with failing stage and source ordinals where known;
// no partial solid escapes. Caller-owned OCCT handles are never modified/shared.
SolidAssembly buildSolid(const ShellAssembly& input);
} // namespace smd_geometry
