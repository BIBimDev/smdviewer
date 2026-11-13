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

struct Point2 { double x, y; };
struct Edge2 {
    Point2 start, end;
    double ux, uy, length; // Unit direction and length in the temporary plane.
};

double finitePredicate(double value, std::size_t face) {
    if (!std::isfinite(value)) fail(face, "2D boundary predicate outside numerical range");
    return value;
}
double lineDistance(const Edge2& edge, const Point2& p, std::size_t face) {
    return finitePredicate(edge.ux * (p.y - edge.start.y) - edge.uy * (p.x - edge.start.x), face);
}
double segmentDistance(const Edge2& edge, const Point2& p, std::size_t face) {
    const double dx = p.x - edge.start.x, dy = p.y - edge.start.y;
    const double along = std::clamp(finitePredicate(dx * edge.ux + dy * edge.uy, face), 0.0, edge.length);
    return finitePredicate(std::hypot(dx - along * edge.ux, dy - along * edge.uy), face);
}
bool oppositeSigns(double a, double b) {
    return (a < 0 && b > 0) || (a > 0 && b < 0);
}

void validateBoundary(const smd::ParsedBrep& source, std::size_t f, const gp_Pln& plane) {
    const auto& loop = source.faces[f].indices;
    const auto origin = point(source.verticesLocal[loop[0]]);
    const gp_Vec xAxis(plane.Position().XDirection()), yAxis(plane.Position().YDirection());
    std::vector<Point2> projected;
    projected.reserve(loop.size());
    for (const auto index : loop) {
        const gp_Vec relative(origin, point(source.verticesLocal[index]));
        // Orthogonal projection for validation only; no source/OCCT vertex changes.
        projected.push_back({finitePredicate(relative.Dot(xAxis), f), finitePredicate(relative.Dot(yAxis), f)});
    }
    std::vector<Edge2> edges;
    edges.reserve(loop.size());
    for (std::size_t j = 0; j < loop.size(); ++j) {
        const auto a = projected[j], b = projected[(j + 1) % loop.size()];
        const double dx = b.x - a.x, dy = b.y - a.y;
        const double length = finitePredicate(std::hypot(dx, dy), f);
        if (length <= distanceToleranceMm)
            fail(f, "degenerate projected boundary edge " + std::to_string(j));
        edges.push_back({a, b, dx / length, dy / length, length});
    }
    for (std::size_t j = 0; j < edges.size(); ++j) {
        const auto& incoming = edges[j];
        const auto next = (j + 1) % edges.size();
        const auto& outgoing = edges[next];
        // Both free endpoints must be farther than tolerance from the other
        // edge's supporting line. Either turn sign is valid (convex or concave).
        if (std::abs(lineDistance(incoming, outgoing.end, f)) <= distanceToleranceMm ||
            std::abs(lineDistance(outgoing, incoming.start, f)) <= distanceToleranceMm)
            fail(f, "adjacent backtracking or collinear corner at SourceVertex[" +
                 std::to_string(loop[next]) + "]");
    }
    for (std::size_t j = 0; j < edges.size(); ++j) {
        for (std::size_t k = j + 1; k < edges.size(); ++k) {
            // Ordinary adjacent shared endpoints, including last/first, are valid.
            if (k == j + 1 || (j == 0 && k + 1 == edges.size())) continue;
            const auto& a = edges[j];
            const auto& b = edges[k];
            const bool crosses = oppositeSigns(lineDistance(a, b.start, f), lineDistance(a, b.end, f)) &&
                                 oppositeSigns(lineDistance(b, a.start, f), lineDistance(b, a.end, f));
            // For disjoint finite segments the minimum distance is attained at
            // an endpoint. This catches touches, overlaps and near contacts.
            const double separation = std::min({segmentDistance(a, b.start, f), segmentDistance(a, b.end, f),
                                                segmentDistance(b, a.start, f), segmentDistance(b, a.end, f)});
            if (crosses || separation <= distanceToleranceMm)
                fail(f, "nonadjacent boundary edges " + std::to_string(j) + " and " + std::to_string(k) +
                     " cross, touch or overlap (contact tolerance 1e-7 mm)");
        }
    }
}

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
    // A point derived from the ORIGINAL equation, not a fitted polygon plane.
    const gp_Pnt planeOrigin(-offset * normal.X(), -offset * normal.Y(), -offset * normal.Z());
    validateBoundary(source, f, gp_Pln(planeOrigin, gp_Dir(normal)));
    const auto origin = point(source.verticesLocal[loop[0]]);
    gp_Vec area;
    for (std::size_t j = 1; j + 1 < loop.size(); ++j)
        area += gp_Vec(origin, point(source.verticesLocal[loop[j]])).Crossed(
            gp_Vec(origin, point(source.verticesLocal[loop[j + 1]])));
    const double signedArea2 = area.Dot(normal);
    if (!std::isfinite(signedArea2) || std::abs(signedArea2) <= distanceToleranceMm * distanceToleranceMm)
        fail(f, "degenerate polygon area");
    // Orient the supporting plane to source winding, never reverse the loop.
    if (signedArea2 < 0) normal.Reverse();
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
