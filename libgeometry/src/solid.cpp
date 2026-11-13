#include "smd_geometry/solid.hpp"

#include <BOPAlgo_CheckerSI.hxx>
#include <BOPDS_DS.hxx>
#include <BOPDS_Pair.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Shell.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>

namespace smd_geometry {
namespace {
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
[[noreturn]] void fail(const std::string& stage, const std::string& reason) {
    throw std::runtime_error("GEO-02B [" + stage + "]: " + reason);
}
std::string faceLabel(std::size_t f) { return "SourceFace[" + std::to_string(f) + "]"; }
std::string vertexLabel(std::size_t v) { return "SourceVertex[" + std::to_string(v) + "]"; }
std::string edgeLabel(const SharedShellEdge& e) {
    std::string s = "source edge (" + std::to_string(e.firstVertex) + ", " + std::to_string(e.secondVertex) + ")";
    for (const auto& u : e.uses)
        s += " " + faceLabel(u.faceOrdinal) + " loop position " + std::to_string(u.loopPosition);
    return s;
}
bool oriented(const TopoDS_Shape& s) {
    return s.Orientation() == TopAbs_FORWARD || s.Orientation() == TopAbs_REVERSED;
}
void tolerance(double t, const std::string& label) {
    if (!std::isfinite(t) || t < 0 || t > distanceToleranceMm)
        fail("correspondence", label + ": tolerance exceeds 1e-7 mm policy");
}

// Read-only checks, including cumulative orientation when the shell is reversed.
void correspondence(const ShellAssembly& b) {
    if (b.shell.IsNull() || !oriented(b.shell) || !b.shell.Location().IsIdentity() || b.faces.empty())
        fail("correspondence", "expected one nonempty local GEO-02A shell");
    ShapeMap vs, es, fs;
    TopExp::MapShapes(b.shell, TopAbs_VERTEX, vs);
    TopExp::MapShapes(b.shell, TopAbs_EDGE, es);
    TopExp::MapShapes(b.shell, TopAbs_FACE, fs);
    if (fs.Extent() != static_cast<int>(b.faces.size()) || es.Extent() != static_cast<int>(b.edges.size()) ||
        b.loopEdges.size() != b.faces.size()) fail("correspondence", "face/edge mapping sizes differ");
    ShapeMap mappedVertices, mappedEdges, mappedFaces;
    std::set<std::size_t> used;
    for (std::size_t v = 0; v < b.vertices.size(); ++v) {
        const auto& vertex = b.vertices[v];
        if (vertex.IsNull() || !vertex.Location().IsIdentity() || mappedVertices.Contains(vertex))
            fail("correspondence", vertexLabel(v) + ": null, placed or duplicate identity");
        mappedVertices.Add(vertex);
        const auto p = BRep_Tool::Pnt(vertex);
        if (!std::isfinite(p.X()) || !std::isfinite(p.Y()) || !std::isfinite(p.Z()))
            fail("correspondence", vertexLabel(v) + ": non-finite coordinates");
        tolerance(BRep_Tool::Tolerance(vertex), vertexLabel(v));
    }
    std::vector<std::vector<bool>> uses;
    std::vector<std::vector<std::size_t>> adjacent(b.faces.size());
    for (const auto& loop : b.loopEdges) uses.emplace_back(loop.size(), false);
    std::size_t f = 0;
    for (TopoDS_Iterator it(b.shell); it.More(); it.Next(), ++f) {
        if (f >= b.faces.size() || !it.Value().IsEqual(b.faces[f]))
            fail("correspondence", faceLabel(f) + ": shell child identity/orientation differs");
    }
    if (f != b.faces.size()) fail("correspondence", "missing shell children");
    for (f = 0; f < b.faces.size(); ++f) {
        const auto& face = b.faces[f];
        if (face.IsNull() || !oriented(face) || !face.Location().IsIdentity() || mappedFaces.Contains(face))
            fail("correspondence", faceLabel(f) + ": null, placed, unoriented or duplicate face");
        mappedFaces.Add(face);
        tolerance(BRep_Tool::Tolerance(face), faceLabel(f));
        if (BRepAdaptor_Surface(face).GetType() != GeomAbs_Plane)
            fail("correspondence", faceLabel(f) + ": non-planar support");
        std::vector<bool> seen(b.loopEdges[f].size(), false);
        for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
            std::size_t j = 0;
            while (j < seen.size() && !it.Current().IsEqual(b.loopEdges[f][j])) ++j;
            if (j == seen.size() || seen[j]) fail("correspondence", faceLabel(f) + ": edge occurrence differs");
            seen[j] = true;
        }
        if (seen.size() < 3 || std::find(seen.begin(), seen.end(), false) != seen.end())
            fail("correspondence", faceLabel(f) + ": missing boundary occurrence");
    }
    for (const auto& e : b.edges) {
        const auto label = edgeLabel(e);
        if (e.firstVertex >= b.vertices.size() || e.secondVertex >= b.vertices.size() || e.firstVertex >= e.secondVertex ||
            e.edge.IsNull() || e.edge.Orientation() != TopAbs_FORWARD || !e.edge.Location().IsIdentity() ||
            !es.Contains(e.edge) || mappedEdges.Contains(e.edge))
            fail("correspondence", label + ": invalid canonical edge");
        mappedEdges.Add(e.edge);
        used.insert(e.firstVertex); used.insert(e.secondVertex);
        tolerance(BRep_Tool::Tolerance(e.edge), label);
        if (BRepAdaptor_Curve(e.edge).GetType() != GeomAbs_Line ||
            !TopExp::FirstVertex(e.edge, true).IsSame(b.vertices[e.firstVertex]) ||
            !TopExp::LastVertex(e.edge, true).IsSame(b.vertices[e.secondVertex]))
            fail("correspondence", label + ": geometry/endpoints differ");
        if (e.uses[0].faceOrdinal == e.uses[1].faceOrdinal)
            fail("correspondence", label + ": same face used twice");
        TopAbs_Orientation previous = TopAbs_INTERNAL;
        for (const auto& u : e.uses) {
            if (u.faceOrdinal >= uses.size() || u.loopPosition >= uses[u.faceOrdinal].size())
                fail("correspondence", label + ": invalid use ordinal");
            const auto& occurrence = b.loopEdges[u.faceOrdinal][u.loopPosition];
            if (uses[u.faceOrdinal][u.loopPosition] || !occurrence.IsSame(e.edge) || !oriented(occurrence) ||
                occurrence.Orientation() == previous)
                fail("correspondence", label + ": non-opposite or duplicate uses");
            uses[u.faceOrdinal][u.loopPosition] = true;
            previous = occurrence.Orientation();
        }
        adjacent[e.uses[0].faceOrdinal].push_back(e.uses[1].faceOrdinal);
        adjacent[e.uses[1].faceOrdinal].push_back(e.uses[0].faceOrdinal);
    }
    if (vs.Extent() != static_cast<int>(used.size())) fail("correspondence", "used vertex count differs");
    for (const auto v : used) if (!vs.Contains(b.vertices[v])) fail("correspondence", vertexLabel(v) + ": absent from shell");
    for (f = 0; f < uses.size(); ++f)
        if (std::find(uses[f].begin(), uses[f].end(), false) != uses[f].end())
            fail("correspondence", faceLabel(f) + ": occurrence has no shared edge use");
    std::vector<bool> visited(b.faces.size(), false);
    std::vector<std::size_t> pending{0}; visited[0] = true;
    for (std::size_t i = 0; i < pending.size(); ++i)
        for (const auto neighbor : adjacent[pending[i]])
            if (!visited[neighbor]) { visited[neighbor] = true; pending.push_back(neighbor); }
    for (f = 0; f < visited.size(); ++f)
        if (!visited[f]) fail("correspondence", faceLabel(f) + ": disconnected from SourceFace[0]");
}

ShellAssembly privateCopy(const ShellAssembly& input) {
    // Do not even add caller shapes to a temporary compound: BRep_Builder::Add
    // changes the child's shared Free flag. Copy the shell directly and copy
    // unused vertices separately, preserving their source ordinals.
    BRepBuilderAPI_Copy copy(input.shell, true, false);
    if (!copy.IsDone()) fail("copy", "OCCT deep copy did not complete");
    const auto mapped = [&](const TopoDS_Shape& s) { return copy.ModifiedShape(s).Oriented(s.Orientation()); };
    ShellAssembly b = input;
    b.shell = TopoDS::Shell(mapped(input.shell));
    ShapeMap usedVertices;
    TopExp::MapShapes(input.shell, TopAbs_VERTEX, usedVertices);
    for (auto& v : b.vertices) {
        if (usedVertices.Contains(v)) v = TopoDS::Vertex(mapped(v));
        else {
            BRepBuilderAPI_Copy vertexCopy(v, true, false);
            if (!vertexCopy.IsDone()) fail("copy", "unused source vertex copy incomplete");
            v = TopoDS::Vertex(vertexCopy.Shape());
        }
    }
    for (auto& f : b.faces) f = TopoDS::Face(mapped(f));
    for (auto& e : b.edges) e.edge = TopoDS::Edge(mapped(e.edge));
    for (auto& loop : b.loopEdges) for (auto& e : loop) e = TopoDS::Edge(mapped(e));
    return b;
}

// OCCT 8.0.1 SetFuzzyValue(0) clamps to Precision::Confusion(). Use the
// protected option to disable ADDITIONAL fuzzy tolerance explicitly. Intrinsic
// kernel intersection tolerances still apply; see docs/GEO-02B.md.
class NoFuzzChecker final : public BOPAlgo_CheckerSI {
public:
    NoFuzzChecker() { myFuzzyValue = 0.0; }
};
void interference(const ShellAssembly& b) {
    // Even non-destructive BOP algorithms maintain working data. Use a separate
    // deep copy: neither caller nor returned topology/geometry can be altered.
    BRepBuilderAPI_Copy copy(b.shell, true, false);
    if (!copy.IsDone()) fail("self-interference", "checker copy incomplete");
    NoFuzzChecker checker;
    checker.SetLevelOfCheck(5); // VV, VE, EE, VF, EF, FF: all shell dimensions.
    checker.SetNonDestructive(true);
    checker.SetRunParallel(false);
    checker.AddArgument(copy.Shape());
    checker.Perform();
    if (checker.HasErrors() || checker.HasWarnings() || !checker.PDS() || checker.FuzzyValue() != 0.0) {
        std::ostringstream report;
        checker.DumpErrors(report); checker.DumpWarnings(report);
        fail("self-interference", "checker failed or was inconclusive: " + report.str());
    }
    const auto label = [&](const TopoDS_Shape& s) {
        for (std::size_t f = 0; f < b.faces.size(); ++f)
            if (copy.ModifiedShape(b.faces[f]).IsSame(s)) return faceLabel(f);
        for (const auto& e : b.edges)
            if (copy.ModifiedShape(e.edge).IsSame(s)) return edgeLabel(e);
        ShapeMap usedVertices;
        TopExp::MapShapes(b.shell, TopAbs_VERTEX, usedVertices);
        for (std::size_t v = 0; v < b.vertices.size(); ++v)
            if (usedVertices.Contains(b.vertices[v]) && copy.ModifiedShape(b.vertices[v]).IsSame(s))
                return vertexLabel(v);
        return std::string("unmapped checker subshape");
    };
    std::set<std::string> reports;
    for (NCollection_Map<BOPDS_Pair>::Iterator it(checker.DS().Interferences()); it.More(); it.Next()) {
        int a, z; it.Value().Indices(a, z);
        if (a < 0 || z < 0 || a >= checker.DS().NbSourceShapes() || z >= checker.DS().NbSourceShapes())
            fail("self-interference", "inconclusive interference without original checker shapes");
        reports.insert(label(checker.DS().Shape(a)) + " / " + label(checker.DS().Shape(z)));
    }
    if (!reports.empty()) {
        std::string message = "geometric crossing, overlap or unintended contact:";
        for (const auto& report : reports) message += "\n  " + report;
        fail("self-interference", message);
    }
}
void closedShell(const ShellAssembly& b) {
    BRepCheck_Shell check(b.shell);
    if (check.Closed() != BRepCheck_NoError || check.Orientation() != BRepCheck_NoError || !BRep_Tool::IsClosed(b.shell))
        fail("shell-validation", "boundary is not closed and consistently oriented");
    const BRepCheck_Analyzer analyzer(b.shell, true, false, true);
    if (!analyzer.IsValid()) {
        for (std::size_t f = 0; f < b.faces.size(); ++f)
            if (!analyzer.IsValid(b.faces[f])) fail("shell-validation", faceLabel(f) + ": contextual validity failed");
        fail("shell-validation", "contextual OCCT shell validation failed");
    }
}
void unchangedGeometry(const ShellAssembly& input, const ShellAssembly& output) {
    for (std::size_t v = 0; v < input.vertices.size(); ++v) {
        const auto a = BRep_Tool::Pnt(input.vertices[v]), b = BRep_Tool::Pnt(output.vertices[v]);
        if (a.X() != b.X() || a.Y() != b.Y() || a.Z() != b.Z() ||
            BRep_Tool::Tolerance(input.vertices[v]) != BRep_Tool::Tolerance(output.vertices[v]))
            fail("preservation", vertexLabel(v) + ": coordinates or tolerance changed");
    }
    for (std::size_t f = 0; f < input.faces.size(); ++f) {
        double a,b,c,d, oa,ob,oc,od;
        BRepAdaptor_Surface(input.faces[f]).Plane().Coefficients(a,b,c,d);
        BRepAdaptor_Surface(output.faces[f]).Plane().Coefficients(oa,ob,oc,od);
        if (a != oa || b != ob || c != oc || d != od ||
            BRep_Tool::Tolerance(input.faces[f]) != BRep_Tool::Tolerance(output.faces[f]))
            fail("preservation", faceLabel(f) + ": supporting plane or tolerance changed");
    }
    for (std::size_t e = 0; e < input.edges.size(); ++e)
        if (BRep_Tool::Tolerance(input.edges[e].edge) != BRep_Tool::Tolerance(output.edges[e].edge))
            fail("preservation", edgeLabel(input.edges[e]) + ": tolerance changed");
}
TopAbs_State infinity(const TopoDS_Solid& solid) {
    BRepClass3d_SolidClassifier classifier(solid);
    classifier.PerformInfinitePoint(distanceToleranceMm);
    return classifier.State();
}
TopoDS_Solid makeSolid(const TopoDS_Shell& shell) {
    BRepBuilderAPI_MakeSolid maker(shell);
    if (!maker.IsDone() || maker.Solid().IsNull()) fail("construction", "OCCT solid construction incomplete");
    return maker.Solid();
}
} // namespace

SolidAssembly buildSolid(const ShellAssembly& input) {
    std::string stage = "correspondence";
    try {
        correspondence(input);
        stage = "copy";
        SolidAssembly result;
        result.boundary = privateCopy(input);
        correspondence(result.boundary);
        stage = "shell-validation"; closedShell(result.boundary);
        stage = "self-interference"; interference(result.boundary);
        stage = "construction"; result.solid = makeSolid(result.boundary.shell);
        stage = "orientation";
        const auto state = infinity(result.solid);
        if (state == TopAbs_IN) {
            result.boundary.shell.Reverse(); // Only whole-shell orientation change.
            for (auto& f : result.boundary.faces) f.Reverse();
            for (auto& loop : result.boundary.loopEdges) for (auto& e : loop) e.Reverse();
            result.globallyReversed = true;
            result.solid = makeSolid(result.boundary.shell);
        } else if (state != TopAbs_OUT) fail(stage, "infinity classification is inconclusive");
        if (infinity(result.solid) != TopAbs_OUT) fail(stage, "final infinity classification is not OUT");
        stage = "solid-validation";
        std::size_t shells = 0;
        for (TopoDS_Iterator it(result.solid); it.More(); it.Next(), ++shells)
            if (!it.Value().IsEqual(result.boundary.shell)) fail(stage, "unexpected boundary child");
        if (shells != 1) fail(stage, "expected exactly one boundary shell");
        correspondence(result.boundary); closedShell(result.boundary);
        const BRepCheck_Analyzer analyzer(result.solid, true, false, true);
        if (!analyzer.IsValid()) {
            for (std::size_t f = 0; f < result.boundary.faces.size(); ++f)
                if (!analyzer.IsValid(result.boundary.faces[f])) fail(stage, faceLabel(f) + ": contextual validity failed");
            fail(stage, "contextual OCCT solid validation failed");
        }
        stage = "volume";
        GProp_GProps properties;
        BRepGProp::VolumeProperties(result.solid, properties, true, false, false);
        result.volumeMm3 = properties.Mass();
        if (!std::isfinite(result.volumeMm3) || result.volumeMm3 <= 0)
            fail(stage, "volume must be finite and strictly positive (mm^3)");
        stage = "preservation"; unchangedGeometry(input, result.boundary);
        return result;
    } catch (const Standard_Failure& error) {
        fail(stage, std::string("OCCT exception: ") + error.what());
    }
}
} // namespace smd_geometry
