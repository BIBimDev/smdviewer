#pragma once

#include "smd_geometry/principal_prism.hpp"
#include <optional>
#include <string>

namespace smd_geometry {
// Fixed OCCT 8.0.1 minimum Boolean fuzz; never enlarged/retried.
inline constexpr double booleanToleranceMm = 1e-7;
// Reporting check only. Any residual solid, however small, fails verification.
inline constexpr double reconstructionVolumeToleranceMm3 = 1.0;

enum class FaceOrigin { Source, Candidate, Unknown };
enum class FaceChange { Unchanged, Modified, Generated, Removed };
struct FaceAncestor {
    FaceOrigin origin = FaceOrigin::Unknown;
    std::optional<std::size_t> sourceFaceOrdinal;
    FaceChange change = FaceChange::Unchanged;
};
struct BooleanFace {
    TopoDS_Face face;
    std::vector<FaceAncestor> ancestors; // Multiple contributors are retained.
};
struct FaceHistory {
    std::string stage;
    FaceAncestor ancestor;
    FaceChange change;
    // Stage-local working faces, NOT necessarily faces of the final solid.
    // Empty for removal. Generated descendants do not acquire source identity.
    std::vector<TopoDS_Face> descendants;
};
struct BooleanSolid {
    TopoDS_Solid solid;
    double volumeMm3 = 0;
    std::vector<BooleanFace> faces;
};
struct ReconstructionVerification {
    std::size_t sourceMinusReconstructedSolids = 0;
    std::size_t reconstructedMinusSourceSolids = 0;
    std::size_t sourceFacesCovered = 0, reconstructedFacesCovered = 0;
    double volumeDifferenceMm3 = 0;
};
struct BooleanDecomposition {
    std::vector<BooleanSolid> extensions, reductions;
    BooleanSolid reconstructed;
    ReconstructionVerification verification;
    std::vector<FaceHistory> history;
};

// GEO-03B. Requires unmodified validated GEO-02 and GEO-03A results from the
// same pipeline. Does not rerun candidate selection. Source-local millimetres.
// Deep copies plus non-destructive OCCT Booleans; no healing or sliver removal.
// Throws runtime_error("GEO-03B [stage]: ..."); no partial result on failure.
BooleanDecomposition decomposeBoolean(const SolidAssembly&, const PrincipalPrismCandidate&);

// Independently verifies exactly one reconstructed solid, both directed solid
// residuals and bidirectional bounded-face coverage (allows equivalent splits).
// Operates on deep copies. Exposed to check alternative/perturbed reconstructions.
ReconstructionVerification verifyReconstruction(const SolidAssembly&, const TopoDS_Shape&);
} // namespace smd_geometry
