#include "smd_geometry/boolean_decomposition.hpp"

#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BOPAlgo_CheckerSI.hxx>
#include <BOPDS_DS.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace smd_geometry {
namespace {
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
struct TrackedShape { TopoDS_Shape shape; std::vector<BooleanFace> faces; };
[[noreturn]] void fail(const std::string& stage, const std::string& reason) {
    throw std::runtime_error("GEO-03B [" + stage + "]: " + reason);
}
ShapeMap facesOf(const TopoDS_Shape& shape) {
    ShapeMap faces; TopExp::MapShapes(shape, TopAbs_FACE, faces); return faces;
}
TrackedShape copy(const TrackedShape& input, const std::string& stage) {
    if (input.shape.IsNull()) fail(stage, "null operand is not an empty difference");
    BRepBuilderAPI_Copy copier(input.shape, true, false);
    if (!copier.IsDone() || copier.Shape().IsNull()) fail(stage, "deep copy failed");
    TrackedShape result{copier.Shape(), {}};
    for (const auto& f : input.faces) {
        const auto mapped = copier.ModifiedShape(f.face);
        if (mapped.IsNull() || mapped.ShapeType() != TopAbs_FACE) fail(stage, "missing working-copy face correspondence");
        result.faces.push_back({TopoDS::Face(mapped), f.ancestors});
    }
    return result;
}
void connectedShell(const TopoDS_Shape& shell, const std::string& stage) {
    const auto faces = facesOf(shell);
    if (faces.IsEmpty()) fail(stage, "solid has no boundary faces");
    std::vector<bool> reached(static_cast<std::size_t>(faces.Extent()), false);
    reached[0] = true;
    for (int pass = 0; pass < faces.Extent(); ++pass) {
        for (int i = 1; i <= faces.Extent(); ++i) if (reached[static_cast<std::size_t>(i-1)]) {
            ShapeMap edges; TopExp::MapShapes(faces(i), TopAbs_EDGE, edges);
            for (int j = 1; j <= faces.Extent(); ++j) if (!reached[static_cast<std::size_t>(j-1)]) {
                ShapeMap other; TopExp::MapShapes(faces(j), TopAbs_EDGE, other);
                for (int k = 1; k <= other.Extent(); ++k) if (edges.Contains(other(k))) {
                    reached[static_cast<std::size_t>(j-1)] = true; break;
                }
            }
        }
    }
    if (std::find(reached.begin(), reached.end(), false) != reached.end()) fail(stage, "disconnected solid boundary");
}
double validSolid(const TopoDS_Solid& solid, const std::string& stage) {
    if (!BRepCheck_Analyzer(solid, true, false, true).IsValid()) fail(stage, "invalid solid");
    // Check geometry on an additional copy, so checker state cannot escape.
    const auto checkCopy = copy({solid, {}}, stage);
    BOPAlgo_CheckerSI checker; checker.SetLevelOfCheck(5);
    checker.SetNonDestructive(true); checker.SetRunParallel(false);
    checker.SetFuzzyValue(booleanToleranceMm); checker.AddArgument(checkCopy.shape); checker.Perform();
    if (checker.HasErrors() || checker.HasWarnings() || !checker.PDS()) {
        std::ostringstream report; checker.DumpErrors(report); checker.DumpWarnings(report);
        fail(stage, "solid interference check failed/inconclusive: " + report.str());
    }
    if (!checker.DS().Interferences().IsEmpty()) fail(stage, "solid boundary self-interference");
    // Exactly one outward connected shell; optional inward cavity shells must
    // lie inside it and outside one another. No disconnected solids hidden in
    // a single TopoDS_Solid, and no arbitrary single-shell-only restriction.
    TopoDS_Solid outer;
    std::vector<TopoDS_Solid> cavities;
    for (TopoDS_Iterator it(solid); it.More(); it.Next()) {
        if (it.Value().ShapeType() != TopAbs_SHELL || !BRep_Tool::IsClosed(it.Value()))
            fail(stage, "solid contains an open or unexpected boundary child");
        connectedShell(it.Value(), stage);
        auto shell = TopoDS::Shell(copy({it.Value(), {}}, stage).shape);
        auto enclosed = BRepBuilderAPI_MakeSolid(shell).Solid();
        BRepClass3d_SolidClassifier shellClassifier(enclosed); shellClassifier.PerformInfinitePoint(booleanToleranceMm);
        if (shellClassifier.State() == TopAbs_OUT) {
            if (!outer.IsNull()) fail(stage, "multiple outward shells/disconnected solid material");
            outer = enclosed;
        } else if (shellClassifier.State() == TopAbs_IN) {
            enclosed.Reverse(); cavities.push_back(enclosed);
        } else fail(stage, "indeterminate shell orientation");
    }
    if (outer.IsNull()) fail(stage, "missing outward shell");
    for (std::size_t i = 0; i < cavities.size(); ++i) {
        ShapeMap vertices; TopExp::MapShapes(cavities[i], TopAbs_VERTEX, vertices);
        if (vertices.IsEmpty()) fail(stage, "cavity has no vertices");
        const auto p = BRep_Tool::Pnt(TopoDS::Vertex(vertices(1)));
        BRepClass3d_SolidClassifier inOuter(outer, p, booleanToleranceMm);
        if (inOuter.State() != TopAbs_IN) fail(stage, "cavity outside outer shell");
        for (std::size_t j = 0; j < cavities.size(); ++j) if (j != i) {
            BRepClass3d_SolidClassifier inOther(cavities[j], p, booleanToleranceMm);
            if (inOther.State() != TopAbs_OUT) fail(stage, "nested/overlapping cavity shells");
        }
    }
    BRepClass3d_SolidClassifier classifier(solid); classifier.PerformInfinitePoint(booleanToleranceMm);
    if (classifier.State() != TopAbs_OUT) fail(stage, "solid infinity is not OUT");
    GProp_GProps properties; BRepGProp::VolumeProperties(solid, properties, true, false, false);
    if (!std::isfinite(properties.Mass()) || properties.Mass() <= 0) fail(stage, "nonpositive or non-finite solid volume");
    return properties.Mass();
}
// Only an actually empty compound is empty. Never turn a null, face, wire,
// compsolid or failed operation into a successful empty solid collection.
void collect(const TopoDS_Shape& shape, std::vector<BooleanSolid>& solids, const std::string& stage) {
    if (shape.IsNull()) fail(stage, "null result");
    if (shape.ShapeType() == TopAbs_SOLID) {
        solids.push_back({TopoDS::Solid(shape), validSolid(TopoDS::Solid(shape), stage), {}});
    } else if (shape.ShapeType() == TopAbs_COMPOUND) {
        for (TopoDS_Iterator it(shape); it.More(); it.Next()) collect(it.Value(), solids, stage);
    } else fail(stage, "unexpected lower-dimensional or non-solid result, type=" + std::to_string(shape.ShapeType()));
}
std::vector<BooleanSolid> solidsOf(const TrackedShape& input, const std::string& stage) {
    try {
    std::vector<BooleanSolid> solids; collect(input.shape, solids, stage);
    ShapeMap unique;
    for (auto& solid : solids) {
        if (unique.Contains(solid.solid)) fail(stage, "duplicate solid occurrence");
        unique.Add(solid.solid);
        const auto faces = facesOf(solid.solid);
        for (const auto& f : input.faces) if (faces.Contains(f.face)) solid.faces.push_back(f);
    }
    return solids;
    } catch (const Standard_Failure& e) { fail(stage, std::string("OCCT exception: ") + e.what()); }
}
TrackedShape sourceShape(const SolidAssembly& input) {
    const std::string stage = "input-correspondence";
    if (input.solid.IsNull() || !input.solid.Location().IsIdentity() || input.boundary.shell.IsNull())
        fail(stage, "expected source-local validated GEO-02 solid");
    std::size_t shells = 0;
    for (TopoDS_Iterator it(input.solid); it.More(); it.Next(), ++shells)
        if (!it.Value().IsEqual(input.boundary.shell)) fail(stage, "boundary shell identity/orientation differs");
    if (shells != 1) fail(stage, "expected one mapped shell");
    const auto faces = facesOf(input.solid);
    if (faces.Extent() != static_cast<int>(input.boundary.faces.size())) fail(stage, "incomplete source face mapping");
    ShapeMap seen; TrackedShape result{input.solid, {}};
    for (std::size_t i = 0; i < input.boundary.faces.size(); ++i) {
        const auto& face = input.boundary.faces[i];
        if (face.IsNull() || !faces.Contains(face) || seen.Contains(face) ||
            !faces(faces.FindIndex(face)).IsEqual(face))
            fail(stage, "SourceFace[" + std::to_string(i) + "]: mismatched identity/orientation");
        seen.Add(face); result.faces.push_back({face, {{FaceOrigin::Source, i, FaceChange::Unchanged}}});
    }
    return copy(result, stage);
}
TrackedShape candidateShape(const PrincipalPrismCandidate& candidate) {
    if (candidate.solid.IsNull() || !candidate.solid.Location().IsIdentity())
        fail("input-candidate", "expected source-local GEO-03A candidate");
    TrackedShape result{candidate.solid, {}};
    const auto faces = facesOf(candidate.solid);
    for (int i = 1; i <= faces.Extent(); ++i)
        result.faces.push_back({TopoDS::Face(faces(i)), {{FaceOrigin::Candidate, {}, FaceChange::Unchanged}}});
    return copy(result, "input-candidate");
}
void appendAncestor(std::vector<FaceAncestor>& list, const FaceAncestor& a) {
    for (const auto& b : list) if (a.origin == b.origin && a.sourceFaceOrdinal == b.sourceFaceOrdinal && a.change == b.change) return;
    list.push_back(a);
}
TrackedShape booleanOp(const TrackedShape& left, const TrackedShape& right, BOPAlgo_Operation operation,
                       const std::string& stage, std::vector<FaceHistory>* history = nullptr) {
    try {
        // Independent copies for EVERY operation, including verification. Even
        // container assembly and validation must never touch caller-owned flags.
        const auto a = copy(left, stage), b = copy(right, stage);
        BRepAlgoAPI_BooleanOperation op;
        NCollection_List<TopoDS_Shape> args, tools; args.Append(a.shape); tools.Append(b.shape);
        op.SetArguments(args); op.SetTools(tools); op.SetOperation(operation);
        op.SetNonDestructive(true); op.SetRunParallel(false); op.SetToFillHistory(true);
        op.SetFuzzyValue(booleanToleranceMm); op.Build();
        if (!op.IsDone() || op.HasErrors() || op.HasWarnings() || op.Shape().IsNull()) {
            std::ostringstream report; op.DumpErrors(report); op.DumpWarnings(report);
            fail(stage, "OCCT Boolean incomplete/error/warning: " + report.str());
        }
        if (op.FuzzyValue() != booleanToleranceMm) fail(stage, "unexpected Boolean tolerance");
        TrackedShape result{op.Shape(), {}};
        const auto faces = facesOf(result.shape);
        for (int i = 1; i <= faces.Extent(); ++i) result.faces.push_back({TopoDS::Face(faces(i)), {}});
        for (const auto* operand : {&a, &b}) for (const auto& f : operand->faces) {
            const auto record = [&](FaceChange change, const std::vector<TopoDS_Face>& descendants) {
                for (const auto& ancestor : f.ancestors) {
                    if (history) history->push_back({stage, ancestor, change, descendants});
                    for (const auto& face : descendants) {
                        auto propagated = ancestor;
                        if (change == FaceChange::Generated) propagated = {FaceOrigin::Unknown, {}, FaceChange::Generated};
                        else if (change == FaceChange::Modified && propagated.change != FaceChange::Generated)
                            propagated.change = FaceChange::Modified;
                        appendAncestor(result.faces[static_cast<std::size_t>(faces.FindIndex(face)-1)].ancestors, propagated);
                    }
                }
            };
            if (faces.Contains(f.face)) record(FaceChange::Unchanged, {TopoDS::Face(faces(faces.FindIndex(f.face)))});
            for (const auto change : {FaceChange::Modified, FaceChange::Generated}) {
                const auto& shapes = change == FaceChange::Modified ? op.Modified(f.face) : op.Generated(f.face);
                std::vector<TopoDS_Face> descendants;
                for (const auto& shape : shapes) if (shape.ShapeType() == TopAbs_FACE && faces.Contains(shape))
                    descendants.push_back(TopoDS::Face(faces(faces.FindIndex(shape))));
                if (!descendants.empty()) record(change, descendants);
            }
            if (op.IsDeleted(f.face)) record(FaceChange::Removed, {});
        }
        for (auto& face : result.faces) if (face.ancestors.empty())
            face.ancestors.push_back({FaceOrigin::Unknown, {}, FaceChange::Unchanged});
        return result;
    } catch (const Standard_Failure& e) { fail(stage, std::string("OCCT exception: ") + e.what()); }
}
bool emptyTopology(const TopoDS_Shape& shape) {
    if (shape.IsNull() || shape.ShapeType() != TopAbs_COMPOUND) return false;
    for (TopoDS_Iterator it(shape); it.More(); it.Next()) if (!emptyTopology(it.Value())) return false;
    return true;
}
std::size_t coveredFaces(const TrackedShape& from, const TrackedShape& by, const std::string& stage) {
    const auto faces = facesOf(from.shape), tools = facesOf(by.shape);
    // Subtract bounded faces sequentially. Coplanar partial overlaps/splits are
    // handled by OCCT trimming, never by support-plane equivalence or sampling.
    for (int i = 1; i <= faces.Extent(); ++i) {
        TrackedShape remaining{faces(i), {}};
        for (int j = 1; j <= tools.Extent() && !emptyTopology(remaining.shape); ++j)
            remaining = booleanOp(remaining, {tools(j), {}}, BOPAlgo_CUT, stage);
        if (!emptyTopology(remaining.shape)) fail(stage, "uncovered bounded face (verification index " + std::to_string(i-1) + ", not source identity)");
    }
    return static_cast<std::size_t>(faces.Extent());
}
ReconstructionVerification verify(const TrackedShape& source, const TrackedShape& reconstructed) {
    const auto s = solidsOf(source, "verify-source"), r = solidsOf(reconstructed, "verify-reconstructed");
    if (s.size() != 1 || r.size() != 1) fail("verify-reconstructed", "expected exactly one source and reconstructed solid");
    ReconstructionVerification result;
    const auto missing = booleanOp(source, reconstructed, BOPAlgo_CUT, "verify-source-minus-reconstructed");
    const auto extra = booleanOp(reconstructed, source, BOPAlgo_CUT, "verify-reconstructed-minus-source");
    result.sourceMinusReconstructedSolids = solidsOf(missing, "verify-source-minus-reconstructed").size();
    result.reconstructedMinusSourceSolids = solidsOf(extra, "verify-reconstructed-minus-source").size();
    if (result.sourceMinusReconstructedSolids || result.reconstructedMinusSourceSolids)
        fail("verify-residuals", "solid material remains: S\\S'=" + std::to_string(result.sourceMinusReconstructedSolids) +
             ", S'\\S=" + std::to_string(result.reconstructedMinusSourceSolids));
    result.sourceFacesCovered = coveredFaces(source, reconstructed, "verify-boundary-source");
    result.reconstructedFacesCovered = coveredFaces(reconstructed, source, "verify-boundary-reconstructed");
    result.volumeDifferenceMm3 = r[0].volumeMm3 - s[0].volumeMm3;
    if (std::abs(result.volumeDifferenceMm3) > reconstructionVolumeToleranceMm3)
        fail("verify-volume", "volume difference exceeds reporting tolerance (mm^3)");
    return result;
}
} // namespace

ReconstructionVerification verifyReconstruction(const SolidAssembly& source, const TopoDS_Shape& reconstructed) {
    try { return verify(sourceShape(source), copy({reconstructed, {}}, "verify-reconstructed")); }
    catch (const Standard_Failure& e) { fail("verification", std::string("OCCT exception: ") + e.what()); }
}
BooleanDecomposition decomposeBoolean(const SolidAssembly& source, const PrincipalPrismCandidate& candidate) {
    std::string stage = "input-correspondence";
    try {
        auto s = sourceShape(source);
        stage = "input-candidate"; auto b = candidateShape(candidate);
        (void)solidsOf(s, "input-source"); (void)solidsOf(b, stage);
        BooleanDecomposition result;
        const auto a = booleanOp(s, b, BOPAlgo_CUT, "extensions", &result.history);
        result.extensions = solidsOf(a, "extensions");
        const auto r = booleanOp(b, s, BOPAlgo_CUT, "reductions", &result.history);
        result.reductions = solidsOf(r, "reductions");
        // Use the actual A/R operation results, including explicit empty cases.
        auto reconstructed = result.reductions.empty() ? copy(b, "reconstruct-base") :
            booleanOp(b, r, BOPAlgo_CUT, "reconstruct-base", &result.history);
        const auto base = solidsOf(reconstructed, "reconstruct-base");
        if (!result.extensions.empty()) reconstructed = base.empty() ? copy(a, "reconstruct-union") :
            booleanOp(reconstructed, a, BOPAlgo_FUSE, "reconstruct-union", &result.history);
        auto solids = solidsOf(reconstructed, "reconstruct-solid");
        if (solids.size() != 1) fail("reconstruct-solid", "expected exactly one reconstructed solid");
        stage = "verification"; result.verification = verify(s, reconstructed);
        result.reconstructed = std::move(solids.front());
        return result;
    } catch (const Standard_Failure& e) { fail(stage, std::string("OCCT exception: ") + e.what()); }
}
} // namespace smd_geometry
