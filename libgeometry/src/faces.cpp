#include "smd_geometry/faces.hpp"

#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pln.hxx>
#include <gp_Vec.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace smd_geometry {
namespace {
[[noreturn]] void fail(std::size_t face, const std::string& message) {
    throw std::runtime_error("SourceFace[" + std::to_string(face) + "]: " + message);
}
gp_Pnt point(const smd::Vec3& v) { return {v.x, v.y, v.z}; }

void validateVertices(const std::vector<smd::Vec3>& vertices) {
    // Ordered map: O(V log V), exact double comparisons, +0 == -0.
    // Reject NaN before it can violate map ordering. Includes unused vertices.
    std::map<std::array<double, 3>, std::size_t> seen;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto& v = vertices[i];
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            throw std::runtime_error("SourceVertex[" + std::to_string(i) + "]: non-finite coordinates");
        auto [it, inserted] = seen.emplace(std::array{v.x, v.y, v.z}, i);
        if (!inserted) {
            std::ostringstream message;
            message << std::setprecision(std::numeric_limits<double>::max_digits10)
                    << "SourceVertex[" << it->second << "] and SourceVertex[" << i
                    << "]: duplicate coordinates (" << v.x << ", " << v.y << ", " << v.z << ')';
            throw std::runtime_error(message.str());
        }
    }
}

gp_Pln validateFace(const smd::ParsedBrep& source, std::size_t f) {
    const auto& face = source.faces[f];
    const auto& loop = face.indices;
    std::unordered_set<std::size_t> distinct;
    for (const auto index : loop) {
        if (index >= source.verticesLocal.size())
            fail(f, "invalid SourceVertex[" + std::to_string(index) + "] reference");
        distinct.insert(index);
    }
    if (distinct.size() < 3) fail(f, "insufficient distinct vertices (at least three required)");
    for (std::size_t j = 0; j < loop.size(); ++j) {
        const auto a = loop[j], b = loop[(j + 1) % loop.size()];
        const auto length = point(source.verticesLocal[a]).Distance(point(source.verticesLocal[b]));
        if (!std::isfinite(length) || length <= distanceToleranceMm)
            fail(f, "zero-length or sub-tolerance boundary edge SourceVertex[" +
                 std::to_string(a) + "] -> SourceVertex[" + std::to_string(b) + "]");
    }
    if (distinct.size() != loop.size()) fail(f, "repeated vertex within one boundary is unsupported");
    if (!face.plane) fail(f, "missing source plane equation");
    const auto& p = *face.plane;
    if (!std::isfinite(p.a) || !std::isfinite(p.b) || !std::isfinite(p.c) || !std::isfinite(p.d))
        fail(f, "non-finite source plane equation");
    const double scale = std::max({std::abs(p.a), std::abs(p.b), std::abs(p.c)});
    if (scale == 0) fail(f, "degenerate source plane normal");
    const double norm = std::hypot(p.a / scale, p.b / scale, p.c / scale);
    gp_Vec normal(p.a / scale / norm, p.b / scale / norm, p.c / scale / norm);
    const double offset = (p.d / scale) / norm;
    if (!std::isfinite(offset)) fail(f, "source plane offset outside numerical range");
    for (const auto index : loop) {
        const auto& v = source.verticesLocal[index];
        // Perpendicular distance, NOT the parser's unnormalised residual.
        const double residual = normal.X() * v.x + normal.Y() * v.y + normal.Z() * v.z + offset;
        if (!std::isfinite(residual) || std::abs(residual) > distanceToleranceMm)
            fail(f, "SourceVertex[" + std::to_string(index) + "] exceeds plane distance tolerance 1e-7 mm");
    }
    const auto origin = point(source.verticesLocal[loop[0]]);
    gp_Vec area;
    for (std::size_t j = 1; j + 1 < loop.size(); ++j)
        area += gp_Vec(origin, point(source.verticesLocal[loop[j]])).Crossed(
            gp_Vec(origin, point(source.verticesLocal[loop[j + 1]])));
    const double signedArea2 = area.Dot(normal);
    if (!std::isfinite(signedArea2) || std::abs(signedArea2) <= distanceToleranceMm * distanceToleranceMm)
        fail(f, "degenerate polygon area");
    // A point derived from the ORIGINAL equation, not a fitted polygon plane.
    const gp_Pnt planeOrigin(-offset * normal.X(), -offset * normal.Y(), -offset * normal.Z());
    // Orient the supporting plane to source winding, never reverse the loop.
    if (signedArea2 < 0) normal.Reverse();
    // Every non-endpoint must lie strictly to the left of every directed edge.
    // This rejects concavity, collinear corners and self-intersecting star loops.
    for (std::size_t j = 0; j < loop.size(); ++j) {
        const auto a = point(source.verticesLocal[loop[j]]);
        const auto next = (j + 1) % loop.size();
        const gp_Vec edge(a, point(source.verticesLocal[loop[next]]));
        for (std::size_t k = 0; k < loop.size(); ++k) {
            if (k == j || k == next) continue;
            const double side = edge.Crossed(gp_Vec(a, point(source.verticesLocal[loop[k]]))).Dot(normal);
            if (!std::isfinite(side) || side <= distanceToleranceMm * edge.Magnitude())
                fail(f, "degenerate or unsupported non-convex boundary at SourceVertex[" +
                     std::to_string(loop[k]) + "]");
        }
    }
    return {planeOrigin, gp_Dir(normal)};
}
} // namespace

FaceConversion convertFaces(const smd::ParsedBrep& source) {
    validateVertices(source.verticesLocal); // Complete array, before any OCCT construction.
    if (source.faces.empty()) throw std::runtime_error("Source B-rep has no faces");
    std::size_t currentFace = 0;
    try {
        std::vector<gp_Pln> planes;
        planes.reserve(source.faces.size());
        for (; currentFace < source.faces.size(); ++currentFace)
            planes.push_back(validateFace(source, currentFace));
        FaceConversion result;
        result.vertices.reserve(source.verticesLocal.size());
        for (std::size_t i = 0; i < source.verticesLocal.size(); ++i) {
            try {
                result.vertices.push_back(BRepBuilderAPI_MakeVertex(point(source.verticesLocal[i])).Vertex());
            } catch (const Standard_Failure& error) {
                throw std::runtime_error("SourceVertex[" + std::to_string(i) + "]: OCCT failure: " + error.what());
            }
        }
        result.faces.reserve(source.faces.size());
        for (currentFace = 0; currentFace < source.faces.size(); ++currentFace) {
            const auto& loop = source.faces[currentFace].indices;
            BRep_Builder builder;
            TopoDS_Wire wire;
            builder.MakeWire(wire);
            for (std::size_t j = 0; j < loop.size(); ++j) {
                BRepBuilderAPI_MakeEdge edge(result.vertices[loop[j]], result.vertices[loop[(j + 1) % loop.size()]]);
                if (!edge.IsDone()) fail(currentFace, "failed to construct boundary edge " + std::to_string(j));
                // BRep_Builder avoids MakeWire's proximity-based vertex replacement.
                builder.Add(wire, edge.Edge());
            }
            if (!BRep_Tool::IsClosed(wire)) fail(currentFace, "failed to construct closed wire");
            wire.Closed(true);
            BRepBuilderAPI_MakeFace makeFace(planes[currentFace], wire, false);
            if (!makeFace.IsDone()) fail(currentFace, "failed to construct OCCT face");
            auto face = makeFace.Face();
            if (!BRepCheck_Analyzer(face, true).IsValid()) fail(currentFace, "invalid OCCT face (BRepCheck_Analyzer)");
            std::size_t j = 0;
            for (BRepTools_WireExplorer it(BRepTools::OuterWire(face), face); it.More(); it.Next(), ++j) {
                if (j >= loop.size() || !it.CurrentVertex().IsSame(result.vertices[loop[j]]))
                    fail(currentFace, "OCCT wire lost source vertex order/identity");
            }
            if (j != loop.size()) fail(currentFace, "OCCT wire traversal is incomplete");
            result.faces.push_back(face);
        }
        return result;
    } catch (const Standard_Failure& error) {
        fail(currentFace, std::string("OCCT failure: ") + error.what());
    }
}
} // namespace smd_geometry
