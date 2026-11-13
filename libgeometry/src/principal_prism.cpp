#include "smd_geometry/principal_prism.hpp"

#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
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
#include <limits>
#include <set>
#include <stdexcept>

namespace smd_geometry {
namespace {
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
[[noreturn]] void fail(const std::string& stage, const std::string& reason) {
    throw std::runtime_error("GEO-03A [" + stage + "]: " + reason);
}
std::string faceLabel(std::size_t f) { return "SourceFace[" + std::to_string(f) + "]"; }
bool finite(const smd::Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Bind the source records to the already validated boundary without running
// mutating kernel validators on caller-owned shapes. Unused vertices are ignored.
std::set<std::size_t> boundaryVertices(const SolidAssembly& input, const smd::ParsedBrep& source) {
    const auto& b = input.boundary;
    const auto invalid = [](const std::string& why) { fail("input-correspondence", why); };
    if (input.solid.IsNull() || !input.solid.Location().IsIdentity() || b.shell.IsNull() ||
        source.faces.empty() || b.faces.size() != source.faces.size() ||
        b.vertices.size() != source.verticesLocal.size() || b.loopEdges.size() != b.faces.size())
        invalid("expected a validated source-local GEO-02 solid and matching source ordinals");
    std::size_t shells = 0;
    for (TopoDS_Iterator it(input.solid); it.More(); it.Next(), ++shells)
        if (!it.Value().IsEqual(b.shell)) invalid("solid boundary shell identity/orientation differs");
    if (shells != 1) invalid("expected exactly one mapped boundary shell");
    ShapeMap vertices, faces, mappedVertices, mappedFaces;
    TopExp::MapShapes(input.solid, TopAbs_VERTEX, vertices);
    TopExp::MapShapes(input.solid, TopAbs_FACE, faces);
    if (faces.Extent() != static_cast<int>(b.faces.size())) invalid("boundary face count differs");
    std::set<std::size_t> used;
    for (std::size_t f = 0; f < b.faces.size(); ++f) {
        const auto label = faceLabel(f);
        const auto& face = b.faces[f];
        const auto& loop = source.faces[f].indices;
        if (face.IsNull() || !faces.Contains(face) || mappedFaces.Contains(face) ||
            !faces.FindKey(faces.FindIndex(face)).IsEqual(face) ||
            loop.size() < 3 || b.loopEdges[f].size() != loop.size())
            invalid(label + ": invalid face/loop mapping");
        mappedFaces.Add(face);
        for (const auto v : loop) {
            if (v >= b.vertices.size() || b.vertices[v].IsNull() || !vertices.Contains(b.vertices[v]))
                invalid(label + ": source vertex absent from solid boundary");
            used.insert(v);
        }
        // Recover source order independently of GEO-02 orientation corrections.
        const auto forward = TopoDS::Face(face.Oriented(TopAbs_FORWARD));
        std::size_t j = 0;
        for (BRepTools_WireExplorer it(BRepTools::OuterWire(forward), forward); it.More(); it.Next(), ++j) {
            if (j >= loop.size() || !it.CurrentVertex().IsSame(b.vertices[loop[j]]) ||
                !it.Current().IsSame(b.loopEdges[f][j]))
                invalid(label + ": ordered source loop differs from boundary mapping");
        }
        if (j != loop.size()) invalid(label + ": incomplete ordered source loop");
    }
    for (const auto v : used) {
        const auto& vertex = b.vertices[v];
        const auto& p = source.verticesLocal[v];
        const auto q = BRep_Tool::Pnt(vertex);
        if (mappedVertices.Contains(vertex) || !finite(p) || p.x != q.X() || p.y != q.Y() || p.z != q.Z())
            invalid("SourceVertex[" + std::to_string(v) + "]: duplicate mapping or source-local coordinates differ");
        mappedVertices.Add(vertex);
    }
    if (used.empty() || mappedVertices.Extent() != vertices.Extent()) invalid("incomplete boundary vertex mapping");
    return used;
}
} // namespace

PrincipalPrismCandidate buildPrincipalPrismCandidate(const SolidAssembly& input, const smd::ParsedBrep& source) {
    std::string stage = "input-correspondence";
    try {
        const auto used = boundaryVertices(input, source);
        stage = "unsupported";
        double zMin = std::numeric_limits<double>::infinity(), zMax = -zMin;
        for (const auto v : used) {
            zMin = std::min(zMin, source.verticesLocal[v].z);
            zMax = std::max(zMax, source.verticesLocal[v].z);
        }
        const double height = zMax - zMin;
        if (!std::isfinite(height) || height <= 0) fail(stage, "nonpositive or non-finite boundary Z height");
        std::vector<std::size_t> caps;
        for (std::size_t f = 0; f < source.faces.size(); ++f) {
            const auto& loop = source.faces[f].indices;
            if (std::all_of(loop.begin(), loop.end(), [&](auto v) { return source.verticesLocal[v].z == zMin; }))
                caps.push_back(f);
        }
        if (caps.empty()) fail(stage, "missing minimum-Z cap");
        if (caps.size() != 1) {
            std::string labels;
            for (auto f : caps) labels += " " + faceLabel(f);
            fail(stage, "multiple minimum-Z cap faces:" + labels);
        }
        const auto f = caps.front();
        const auto& loop = source.faces[f].indices;
        const auto rectangleFailure = [&] { fail(stage, faceLabel(f) + ": nonrectangular minimum-Z cap; expected four distinct exact XY-aligned corners"); };
        if (loop.size() != 4) rectangleFailure();
        const auto& first = source.verticesLocal[loop.front()];
        double xMin = first.x, xMax = first.x, yMin = first.y, yMax = first.y;
        for (const auto v : loop) {
            const auto& p = source.verticesLocal[v];
            xMin = std::min(xMin, p.x); xMax = std::max(xMax, p.x);
            yMin = std::min(yMin, p.y); yMax = std::max(yMax, p.y);
        }
        if (!(xMax > xMin && yMax > yMin)) rectangleFailure();
        std::set<std::pair<double, double>> corners;
        for (std::size_t j = 0; j < loop.size(); ++j) {
            const auto& a = source.verticesLocal[loop[j]];
            const auto& b = source.verticesLocal[loop[(j + 1) % loop.size()]];
            if ((a.x != xMin && a.x != xMax) || (a.y != yMin && a.y != yMax) ||
                (a.x == b.x) == (a.y == b.y)) rectangleFailure();
            corners.emplace(a.x, a.y);
        }
        if (corners.size() != 4) rectangleFailure();
        const smd::Vec3 dimensions{xMax - xMin, yMax - yMin, height};
        if (!finite(dimensions)) fail(stage, "non-finite candidate dimensions");

        stage = "kernel-construction";
        BRepPrimAPI_MakeBox maker(gp_Pnt(xMin, yMin, zMin), dimensions.x, dimensions.y, dimensions.z);
        maker.Build();
        if (!maker.IsDone() || maker.Shape().IsNull() || maker.Shape().ShapeType() != TopAbs_SOLID)
            fail(stage, "OCCT box construction incomplete");
        PrincipalPrismCandidate result{maker.Solid(), {xMin, yMin, zMin}, {xMax, yMax, zMax}, dimensions, f};
        stage = "kernel-validation";
        if (!BRepCheck_Analyzer(result.solid, true, false, true).IsValid())
            fail(stage, "candidate fails contextual OCCT solid validation");
        BRepClass3d_SolidClassifier classifier(result.solid);
        classifier.PerformInfinitePoint(distanceToleranceMm);
        if (classifier.State() != TopAbs_OUT) fail(stage, "candidate infinity classification is not OUT");
        GProp_GProps properties;
        BRepGProp::VolumeProperties(result.solid, properties, true, false, false);
        if (!std::isfinite(properties.Mass()) || properties.Mass() <= 0)
            fail(stage, "candidate volume is not finite and strictly positive (mm^3)");
        return result;
    } catch (const Standard_Failure& error) {
        fail(stage, std::string("OCCT exception: ") + error.what());
    }
}
} // namespace smd_geometry
