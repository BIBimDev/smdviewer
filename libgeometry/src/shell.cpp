#include "smd_geometry/shell.hpp"
#include "shell_topology.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Shell.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Wire.hxx>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace smd_geometry {
namespace {
[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error("GEO-02A: " + message);
}
std::string faceLabel(std::size_t f) { return "SourceFace[" + std::to_string(f) + "]"; }
std::string vertexLabel(std::size_t v) { return "SourceVertex[" + std::to_string(v) + "]"; }
std::string edgeLabel(std::size_t a, std::size_t b) {
    return "source edge (" + std::to_string(a) + ", " + std::to_string(b) + ")";
}
std::string useLabel(const SourceEdgeUse& use) {
    return faceLabel(use.faceOrdinal) + " loop position " + std::to_string(use.loopPosition);
}
std::string pairLabel(const detail::SourceEdgePair& pair) {
    return edgeLabel(pair.firstVertex, pair.secondVertex) + ": " +
           useLabel(pair.uses[0]) + ", " + useLabel(pair.uses[1]);
}
// Exact comparison against the deterministic GEO-01 support equation. No plane
// fitting, sign adjustment, or coordinate-based incidence is performed here.
bool samePlane(const gp_Pln& a, const gp_Pln& b) {
    double aa, ab, ac, ad, ba, bb, bc, bd;
    a.Coefficients(aa, ab, ac, ad); b.Coefficients(ba, bb, bc, bd);
    return aa == ba && ab == bb && ac == bc && ad == bd;
}

std::vector<gp_Pln> checkInput(const smd::ParsedBrep& source, const FaceConversion& input,
                              const FaceConversion& reference) {
    if (input.vertices.size() != reference.vertices.size() || input.faces.size() != reference.faces.size())
        fail("GEO-01 collection size does not match source vertex/face ordinals");
    for (std::size_t v = 0; v < input.vertices.size(); ++v) {
        if (input.vertices[v].IsNull()) fail(vertexLabel(v) + ": null GEO-01 vertex");
        const auto p = BRep_Tool::Pnt(input.vertices[v]);
        const auto& raw = source.verticesLocal[v];
        if (p.X() != raw.x || p.Y() != raw.y || p.Z() != raw.z ||
            BRep_Tool::Tolerance(input.vertices[v]) > distanceToleranceMm)
            fail(vertexLabel(v) + ": GEO-01 coordinates/tolerance differ from geometry policy");
    }
    std::vector<gp_Pln> planes;
    for (std::size_t f = 0; f < input.faces.size(); ++f) {
        const auto& face = input.faces[f];
        const auto label = faceLabel(f);
        if (face.IsNull() || face.Orientation() != TopAbs_FORWARD || !face.Location().IsIdentity())
            fail(label + ": expected unmodified local GEO-01 face");
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane ||
            !samePlane(surface.Plane(), BRepAdaptor_Surface(reference.faces[f]).Plane()))
            fail(label + ": GEO-01 supporting plane differs from source conversion");
        if (BRep_Tool::Tolerance(face) > distanceToleranceMm)
            fail(label + ": GEO-01 face exceeds tolerance policy 1e-7 mm");
        std::size_t wires = 0;
        for (TopoDS_Iterator it(face); it.More(); it.Next()) {
            if (it.Value().ShapeType() != TopAbs_WIRE) fail(label + ": unexpected face child");
            ++wires;
        }
        if (wires != 1) fail(label + ": expected one GEO-01 wire");
        const auto wire = BRepTools::OuterWire(face);
        const auto& loop = source.faces[f].indices;
        std::size_t count = 0;
        for (TopoDS_Iterator it(wire); it.More(); it.Next()) ++count;
        if (count != loop.size()) fail(label + ": GEO-01 edge count differs from source loop");
        std::size_t j = 0;
        for (BRepTools_WireExplorer it(wire, face); it.More(); it.Next(), ++j) {
            if (j >= loop.size() || !it.CurrentVertex().IsSame(input.vertices[loop[j]]) ||
                !TopExp::LastVertex(it.Current(), true).IsSame(input.vertices[loop[(j + 1) % loop.size()]]))
                fail(label + " loop position " + std::to_string(j) + ": GEO-01 source correspondence differs");
            if (BRepAdaptor_Curve(it.Current()).GetType() != GeomAbs_Line ||
                BRep_Tool::Tolerance(it.Current()) > distanceToleranceMm)
                fail(label + " loop position " + std::to_string(j) + ": unsupported GEO-01 edge geometry/tolerance");
        }
        if (j != loop.size()) fail(label + ": incomplete GEO-01 boundary traversal");
        // Analyzer may maintain kernel flags; run it only on an independent
        // topology AND geometry copy, never on caller-owned mutable handles.
        const auto copy = BRepBuilderAPI_Copy(face, true, false).Shape();
        if (!BRepCheck_Analyzer(copy, true).IsValid()) fail(label + ": invalid GEO-01 face");
        planes.push_back(surface.Plane()); // Value copy, no shared surface handle.
    }
    return planes;
}

void checkResult(const smd::ParsedBrep& source, const ShellAssembly& result,
                 const std::vector<gp_Pln>& planes) {
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> vertices, edges, faces;
    TopExp::MapShapes(result.shell, TopAbs_VERTEX, vertices);
    TopExp::MapShapes(result.shell, TopAbs_EDGE, edges);
    TopExp::MapShapes(result.shell, TopAbs_FACE, faces);
    std::set<std::size_t> used;
    for (const auto& face : source.faces) used.insert(face.indices.begin(), face.indices.end());
    if (vertices.Extent() != static_cast<int>(used.size()) ||
        edges.Extent() != static_cast<int>(result.edges.size()) ||
        faces.Extent() != static_cast<int>(source.faces.size()))
        fail("assembled topology counts differ from source correspondence (starting at SourceFace[0])");
    for (const auto v : used) {
        const auto& vertex = result.vertices[v];
        const auto p = BRep_Tool::Pnt(vertex);
        const auto& raw = source.verticesLocal[v];
        if (!vertices.Contains(vertex) || p.X() != raw.x || p.Y() != raw.y || p.Z() != raw.z ||
            BRep_Tool::Tolerance(vertex) > distanceToleranceMm)
            fail(vertexLabel(v) + ": assembled identity/coordinates/tolerance differ");
    }
    for (std::size_t f = 0; f < result.faces.size(); ++f) {
        const auto& face = result.faces[f];
        if (!faces.Contains(face) || !samePlane(BRepAdaptor_Surface(face).Plane(), planes[f]) ||
            BRep_Tool::Tolerance(face) > distanceToleranceMm)
            fail(faceLabel(f) + ": assembled face/support/tolerance differs");
        std::vector<bool> seen(result.loopEdges[f].size());
        for (TopExp_Explorer it(face, TopAbs_EDGE); it.More(); it.Next()) {
            const auto& actual = it.Current(); // Includes face reversal.
            std::size_t j = 0;
            while (j < seen.size() && !actual.IsEqual(result.loopEdges[f][j])) ++j;
            if (j == seen.size() || seen[j]) fail(faceLabel(f) + ": assembled edge identity/orientation differs");
            seen[j] = true;
        }
        if (std::find(seen.begin(), seen.end(), false) != seen.end())
            fail(faceLabel(f) + ": assembled boundary occurrence missing");
    }
    for (const auto& shared : result.edges) {
        const auto label = edgeLabel(shared.firstVertex, shared.secondVertex);
        const auto& a = result.loopEdges[shared.uses[0].faceOrdinal][shared.uses[0].loopPosition];
        const auto& b = result.loopEdges[shared.uses[1].faceOrdinal][shared.uses[1].loopPosition];
        if (!edges.Contains(shared.edge) || !a.IsSame(shared.edge) || !b.IsSame(shared.edge) ||
            a.Orientation() == b.Orientation() ||
            !TopExp::FirstVertex(shared.edge, true).IsSame(result.vertices[shared.firstVertex]) ||
            !TopExp::LastVertex(shared.edge, true).IsSame(result.vertices[shared.secondVertex]) ||
            BRep_Tool::Tolerance(shared.edge) > distanceToleranceMm)
            fail(label + ": shared identity/orientation/endpoints/tolerance differ; " +
                 useLabel(shared.uses[0]) + ", " + useLabel(shared.uses[1]));
    }
    // Actual shell children must be exactly the mapped oriented faces.
    std::size_t f = 0;
    for (TopoDS_Iterator it(result.shell); it.More(); it.Next(), ++f)
        if (f >= result.faces.size() || !it.Value().IsEqual(result.faces[f]))
            fail(faceLabel(f) + ": shell child correspondence differs");
    if (f != result.faces.size()) fail("shell is missing mapped source faces");

    BRepCheck_Shell check(result.shell);
    const auto closed = check.Closed(), orientation = check.Orientation();
    if (closed != BRepCheck_NoError || orientation != BRepCheck_NoError || !BRep_Tool::IsClosed(result.shell))
        fail("SourceFace[0] component: OCCT shell closure/orientation failure (status " +
             std::to_string(static_cast<int>(closed)) + ", " + std::to_string(static_cast<int>(orientation)) + ")");
    const BRepCheck_Analyzer analyzer(result.shell, true, false, true);
    if (!analyzer.IsValid()) {
        for (std::size_t i = 0; i < result.faces.size(); ++i)
            if (!analyzer.IsValid(result.faces[i])) fail(faceLabel(i) + ": contextual OCCT validation failed");
        fail("SourceFace[0] component: contextual OCCT shell validation failed");
    }
}
} // namespace

detail::ShellTopology detail::analyzeShellTopology(const smd::ParsedBrep& source) {
    if (source.faces.empty()) fail("source B-rep has no faces");
    using Key = std::pair<std::size_t, std::size_t>;
    std::map<Key, std::vector<SourceEdgeUse>> incidence;
    std::map<std::size_t, std::set<std::size_t>> vertexFaces;
    for (std::size_t f = 0; f < source.faces.size(); ++f) {
        const auto& loop = source.faces[f].indices;
        if (loop.size() < 3) fail(faceLabel(f) + ": insufficient loop vertices");
        for (std::size_t j = 0; j < loop.size(); ++j) {
            const auto a = loop[j], b = loop[(j + 1) % loop.size()];
            if (a >= source.verticesLocal.size()) fail(faceLabel(f) + ": invalid " + vertexLabel(a));
            if (a == b) fail(faceLabel(f) + " loop position " + std::to_string(j) + ": repeated endpoint " + vertexLabel(a));
            incidence[{std::min(a,b), std::max(a,b)}].push_back({f,j,a < b});
            vertexFaces[a].insert(f);
        }
    }
    ShellTopology result;
    std::vector<std::vector<std::size_t>> adjacent(source.faces.size());
    // Vertex links: each face corner has two neighbors through its incident
    // edges. A single connected degree-two multigraph is one continuous fan.
    std::map<std::size_t, std::map<std::size_t, std::vector<std::size_t>>> fans;
    for (const auto& [key, uses] : incidence) {
        std::string context = edgeLabel(key.first, key.second) + ":";
        for (const auto& use : uses) context += " " + useLabel(use) + ";";
        if (uses.size() != 2) fail(context + (uses.size() < 2 ? " unmatched edge (missing mate)" : " non-manifold edge incidence"));
        if (uses[0].faceOrdinal == uses[1].faceOrdinal) fail(context + " two uses on the same face");
        const auto index = result.edges.size();
        result.edges.push_back({key.first, key.second, {uses[0], uses[1]}});
        const auto f = uses[0].faceOrdinal, g = uses[1].faceOrdinal;
        adjacent[f].push_back(index); adjacent[g].push_back(index);
        for (const auto vertex : {key.first, key.second}) {
            fans[vertex][f].push_back(g); fans[vertex][g].push_back(f);
        }
    }
    for (const auto& [v, incident] : vertexFaces) {
        const auto& fan = fans.at(v);
        std::set<std::size_t> visited;
        std::vector<std::size_t> pending{*incident.begin()};
        while (!pending.empty()) {
            const auto f = pending.back(); pending.pop_back();
            if (!visited.insert(f).second) continue;
            if (fan.at(f).size() != 2) fail(vertexLabel(v) + ": non-manifold vertex fan at " + faceLabel(f));
            for (const auto g : fan.at(f)) pending.push_back(g);
        }
        if (visited.size() != incident.size()) {
            std::string context = vertexLabel(v) + ": non-manifold vertex fan; incident faces:";
            for (const auto f : incident) context += " " + faceLabel(f);
            fail(context);
        }
    }
    std::vector<int> reversed(source.faces.size(), -1);
    reversed[0] = 0;
    std::vector<std::size_t> pending{0};
    for (std::size_t i = 0; i < pending.size(); ++i) {
        const auto f = pending[i];
        for (const auto index : adjacent[f]) {
            const auto& pair = result.edges[index];
            const auto& a = pair.uses[0]; const auto& b = pair.uses[1];
            const auto g = a.faceOrdinal == f ? b.faceOrdinal : a.faceOrdinal;
            const int required = reversed[f] ^ static_cast<int>(a.forward == b.forward);
            if (reversed[g] == -1) { reversed[g] = required; pending.push_back(g); }
            else if (reversed[g] != required) fail(pairLabel(pair) + ": contradictory orientation constraints");
        }
    }
    for (std::size_t f = 0; f < reversed.size(); ++f) {
        if (reversed[f] == -1) fail(faceLabel(f) + ": disconnected face set from SourceFace[0] (edge connectivity required)");
        result.reversed.push_back(reversed[f] != 0);
    }
    return result;
}

ShellAssembly assembleShell(const smd::ParsedBrep& source, const FaceConversion& input) {
    std::string context = "source topology";
    try {
        const auto topology = detail::analyzeShellTopology(source);
        // Reuse accepted source validation without changing GEO-01 semantics.
        // These fresh vertices are private, even though the input is handle-based.
        const auto reference = convertFaces(source);
        context = "GEO-01 input correspondence";
        const auto planes = checkInput(source, input, reference);
        ShellAssembly result;
        result.vertices = reference.vertices;
        result.loopEdges.resize(source.faces.size());
        for (std::size_t f = 0; f < source.faces.size(); ++f) result.loopEdges[f].resize(source.faces[f].indices.size());
        for (const auto& pair : topology.edges) {
            context = pairLabel(pair);
            BRepBuilderAPI_MakeEdge makeEdge(result.vertices[pair.firstVertex], result.vertices[pair.secondVertex]);
            if (!makeEdge.IsDone()) fail(context + ": OCCT edge construction failed");
            const auto edge = makeEdge.Edge();
            result.edges.push_back({pair.firstVertex, pair.secondVertex, edge, pair.uses});
            for (const auto& use : pair.uses)
                result.loopEdges[use.faceOrdinal][use.loopPosition] =
                    TopoDS::Edge(edge.Oriented(use.forward ? TopAbs_FORWARD : TopAbs_REVERSED));
        }
        BRep_Builder builder;
        builder.MakeShell(result.shell);
        for (std::size_t f = 0; f < source.faces.size(); ++f) {
            context = faceLabel(f);
            TopoDS_Wire wire;
            builder.MakeWire(wire);
            for (const auto& edge : result.loopEdges[f]) builder.Add(wire, edge);
            if (!BRep_Tool::IsClosed(wire)) fail(context + ": assembled wire is not closed");
            wire.Closed(true);
            BRepBuilderAPI_MakeFace makeFace(planes[f], wire, false);
            if (!makeFace.IsDone()) fail(context + ": OCCT face construction failed");
            auto face = makeFace.Face();
            if (topology.reversed[f]) {
                face.Reverse(); result.reversedFaceOrdinals.push_back(f);
                for (auto& edge : result.loopEdges[f]) edge.Reverse();
            }
            result.faces.push_back(face);
            builder.Add(result.shell, face);
        }
        context = "SourceFace[0] component validation";
        checkResult(source, result, planes);
        result.shell.Closed(true); // Set only after actual topology/OCCT checks.
        return result;
    } catch (const Standard_Failure& error) {
        fail(context + ": OCCT failure: " + error.what());
    }
}
} // namespace smd_geometry
