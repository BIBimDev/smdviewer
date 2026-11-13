#include "smd_geometry/faces.hpp"
#include "smd/json.hpp"
#include "smd/parser.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS_Iterator.hxx>
#include <TopoDS_Wire.hxx>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const std::string& message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
smd::ParsedBrep square() {
    smd::ParsedBrep b{};
    b.verticesLocal = {{0,0,2}, {10,0,2}, {10,10,2}, {0,10,2}};
    b.faces = {{1, {0,1,2,3}, smd::Plane{0,0,1,-2}, "square"}};
    return b;
}
void rejects(const smd::ParsedBrep& b, std::initializer_list<std::string> diagnostics) {
    try { (void)smd_geometry::convertFaces(b); }
    catch (const std::runtime_error& e) {
        for (const auto& part : diagnostics)
            require(std::string(e.what()).find(part) != std::string::npos,
                    "Missing diagnostic '" + part + "' in: " + e.what());
        return;
    }
    throw std::runtime_error("Invalid input was accepted");
}
void check(const smd::ParsedBrep& b, const smd_geometry::FaceConversion& result,
           const std::vector<double>& expectedAreas) {
    require(result.vertices.size() == b.verticesLocal.size(), "source vertex count");
    require(result.faces.size() == b.faces.size(), "source face count");
    for (std::size_t v = 0; v < result.vertices.size(); ++v) {
        const auto p = BRep_Tool::Pnt(result.vertices[v]);
        const auto& raw = b.verticesLocal[v];
        require(p.X() == raw.x && p.Y() == raw.y && p.Z() == raw.z, "exact source coordinates");
        for (std::size_t other = 0; other < v; ++other)
            require(!result.vertices[v].IsSame(result.vertices[other]), "distinct source vertex entities");
    }
    for (std::size_t f = 0; f < result.faces.size(); ++f) {
        const auto& face = result.faces[f];
        require(face.ShapeType() == TopAbs_FACE && BRepCheck_Analyzer(face, true).IsValid(), "valid face");
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        near(props.Mass(), expectedAreas[f], 1e-5, "area at source face " + std::to_string(f));
        BRepAdaptor_Surface surface(face);
        require(surface.GetType() == GeomAbs_Plane, "planar support");
        const auto plane = surface.Plane();
        const auto& originalPlane = *b.faces[f].plane;
        const gp_Dir originalNormal(originalPlane.a, originalPlane.b, originalPlane.c);
        near(std::abs(plane.Axis().Direction().Dot(originalNormal)), 1, 1e-12, "original plane normal locus");
        const auto wire = BRepTools::OuterWire(face);
        require(!wire.IsNull() && BRep_Tool::IsClosed(wire), "closed outer wire");
        std::size_t wires = 0;
        for (TopoDS_Iterator it(face); it.More(); it.Next()) {
            require(it.Value().ShapeType() == TopAbs_WIRE, "face only contains wires");
            ++wires;
        }
        require(wires == 1, "single wire only");
        std::size_t j = 0;
        gp_Vec polygonNormal;
        const auto& loop = b.faces[f].indices;
        for (BRepTools_WireExplorer it(wire, face); it.More(); it.Next(), ++j) {
            require(j < loop.size(), "wire length");
            require(it.CurrentVertex().IsSame(result.vertices[loop[j]]), "ordered source vertex identity");
            const auto p = BRep_Tool::Pnt(it.CurrentVertex());
            near(plane.Distance(p), 0, smd_geometry::distanceToleranceMm, "source plane location");
            const auto& next = b.verticesLocal[loop[(j + 1) % loop.size()]];
            polygonNormal += gp_Vec(p.X(), p.Y(), p.Z()).Crossed(gp_Vec(next.x, next.y, next.z));
        }
        require(j == loop.size(), "full ordered boundary");
        require(polygonNormal.Dot(gp_Vec(plane.Axis().Direction())) > 0, "source winding represented by plane normal");
    }
}
} // namespace

int main(int argc, char** argv) {
    int checks = 0;
    auto test = [&](const char* name, const std::function<void()>& body) {
        body(); std::cout << "ok " << ++checks << " - " << name << '\n';
    };
    try {
        require(argc == 2, "Usage: smd-geometry-tests plain_column.smd");
        std::ifstream input(argv[1], std::ios::binary);
        require(input.good(), "fixture is readable");
        const std::string text{std::istreambuf_iterator<char>(input), {}};
        auto parsed = smd::parseSmd10125(text);
        auto& b = parsed.geometry.brep;
        test("plain column: six ordered faces, areas, planes, exact shared local vertices", [&] {
            require(b.verticesLocal.size() == 8 && b.faces.size() == 6 && b.transform.has_value(), "accepted specimen");
            const auto before = smd::toJsonV1(parsed);
            check(b, smd_geometry::convertFaces(b), {487500,487500,4500000,3900000,4500000,3900000});
            require(smd::toJsonV1(parsed) == before, "all source evidence preserved");
            require(b.verticesLocal[0].x == -750 && b.verticesWorld[0].x == -375, "distinct fixture frames");
        });
        test("world vertices and recorded transforms never used", [&] {
            auto local = b;
            local.verticesWorld = {{std::numeric_limits<double>::quiet_NaN(),0,0}};
            local.transform = smd::Transform3x4{{2,0,0,999,0,3,0,888,0,0,4,777}};
            check(local, smd_geometry::convertFaces(local), {487500,487500,4500000,3900000,4500000,3900000});
        });
        test("result owns topology after input destruction", [&] {
            auto result = smd_geometry::convertFaces(square());
            check(square(), result, {100});
        });
        test("oblique triangular source plane retains coordinates and winding", [&] {
            auto s = square();
            s.verticesLocal = {{1,0,0}, {0,1,0}, {0,0,1}};
            s.faces[0].indices = {0,1,2}; s.faces[0].plane = smd::Plane{2,2,2,-2};
            check(s, smd_geometry::convertFaces(s), {std::sqrt(3.0)/2});
        });
        test("reverse winding retained, plane coefficients may oppose winding", [&] {
            auto s = square();
            std::reverse(s.faces[0].indices.begin(), s.faces[0].indices.end());
            check(s, smd_geometry::convertFaces(s), {100});
        });
        test("unnormalized and negated plane coefficients", [&] {
            for (const auto scale : {1e-200, -7.0, 1e200}) {
                auto s = square(); s.faces[0].plane = smd::Plane{0,0,scale,-2*scale};
                // check() uses gp_Dir, whose tiny-vector cutoff is unrelated to converter normalization.
                const auto result = smd_geometry::convertFaces(s);
                near(BRepAdaptor_Surface(result.faces[0]).Plane().Distance(gp_Pnt(0,0,2)), 0, 1e-12, "scaled plane");
            }
        });
        test("geometric distance policy independent of coefficient scale", [&] {
            auto s = square(); s.verticesLocal[0].z += 5e-8;
            for (const auto scale : {1e-10, 1e10}) {
                s.faces[0].plane = smd::Plane{0,0,scale,-2*scale};
                require(smd_geometry::convertFaces(s).faces.size() == 1, "within geometric tolerance");
            }
            s.verticesLocal[0].z += 1e-5;
            rejects(s, {"SourceFace[0]", "SourceVertex[0]", "plane distance"});
        });
        test("duplicate complete-array vertices precede face errors", [&] {
            auto s = square(); s.verticesLocal.push_back(s.verticesLocal[0]); s.faces[0].indices[0] = 99;
            rejects(s, {"SourceVertex[0]", "SourceVertex[4]", "duplicate coordinates (0, 0, 2)"});
        });
        test("positive and negative zero duplicates", [&] {
            auto s = square(); s.verticesLocal.push_back({-0.0,-0.0,2});
            rejects(s, {"SourceVertex[0]", "SourceVertex[4]", "duplicate"});
        });
        test("near distinct vertices are never deduplicated", [&] {
            auto s = square(); s.verticesLocal.push_back({1e-10,0,2});
            const auto r = smd_geometry::convertFaces(s);
            require(r.vertices.size() == 5 && !r.vertices[0].IsSame(r.vertices[4]), "unmerged close vertices");
        });
        test("NaN and both infinities rejected in every coordinate including unused vertices", [&] {
            for (const auto value : {std::numeric_limits<double>::quiet_NaN(),
                                    std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
                for (int axis = 0; axis < 3; ++axis) {
                    auto s = square();
                    s.verticesLocal.push_back({axis == 0 ? value : 0, axis == 1 ? value : 0, axis == 2 ? value : 2});
                    rejects(s, {"SourceVertex[4]", "non-finite"});
                }
        });
        test("invalid source references", [&] {
            auto s = square(); s.faces[0].indices[1] = 99;
            rejects(s, {"SourceFace[0]", "SourceVertex[99]", "invalid"});
        });
        test("missing, zero and non-finite planes", [&] {
            auto s = square(); s.faces[0].plane.reset(); rejects(s, {"SourceFace[0]", "missing"});
            s.faces[0].plane = smd::Plane{0,0,0,1}; rejects(s, {"SourceFace[0]", "degenerate"});
            for (int coefficient = 0; coefficient < 4; ++coefficient) {
                auto p = smd::Plane{0,0,1,-2};
                const auto nan = std::numeric_limits<double>::quiet_NaN();
                if (coefficient == 0) p.a = nan;
                if (coefficient == 1) p.b = nan;
                if (coefficient == 2) p.c = nan;
                if (coefficient == 3) p.d = nan;
                s.faces[0].plane = p; rejects(s, {"SourceFace[0]", "non-finite"});
            }
        });
        test("insufficient distinct vertices", [&] {
            auto s = square(); s.faces[0].indices = {0,1,0};
            rejects(s, {"SourceFace[0]", "insufficient distinct"});
        });
        test("zero-length and sub-tolerance edges", [&] {
            auto s = square(); s.faces[0].indices = {0,1,1,2,3};
            rejects(s, {"SourceFace[0]", "zero-length", "SourceVertex[1]"});
            s = square(); s.verticesLocal[1] = {1e-10,0,2};
            rejects(s, {"SourceFace[0]", "sub-tolerance"});
        });
        test("collinear, concave and self-intersecting boundaries rejected", [&] {
            auto s = square(); s.verticesLocal[2] = {20,0,2}; s.faces[0].indices = {0,1,2};
            rejects(s, {"SourceFace[0]", "degenerate"});
            s = square(); s.verticesLocal[2] = {2,2,2};
            rejects(s, {"SourceFace[0]", "non-convex"});
            s = square(); s.faces[0].indices = {0,2,1,3};
            rejects(s, {"SourceFace[0]", "degenerate"});
            // Star with consistent local turns still must fail global convexity.
            s = square(); s.verticesLocal = {{0,3,2},{3,1,2},{2,-3,2},{-2,-3,2},{-3,1,2}};
            s.faces[0].indices = {0,2,4,1,3};
            rejects(s, {"SourceFace[0]", "non-convex"});
        });
        test("nonadjacent repeated source index rejected", [&] {
            auto s = square(); s.faces[0].indices = {0,1,2,0,3};
            rejects(s, {"SourceFace[0]", "repeated vertex"});
        });
        test("late invalid face throws whole conversion", [&] {
            auto s = square(); s.faces.push_back(s.faces[0]); s.faces[1].indices[2] = 88;
            rejects(s, {"SourceFace[1]", "SourceVertex[88]"});
        });
        test("empty face collection rejected", [&] {
            auto s = square(); s.faces.clear(); rejects(s, {"no faces"});
        });
        std::cout << checks << " GEO-01A checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "After " << checks << " successful checks: " << e.what() << '\n'; return 1;
    }
}
