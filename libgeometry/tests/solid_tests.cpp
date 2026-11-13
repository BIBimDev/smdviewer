#include "smd_geometry/solid.hpp"
#include "smd/json.hpp"
#include "smd/parser.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Plane.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace {
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
void require(bool value, const std::string& why) { if (!value) throw std::runtime_error(why); }
template<class F> void rejects(F&& f, std::initializer_list<std::string> texts) {
    try { f(); } catch (const std::runtime_error& e) {
        for (const auto& s : texts) require(std::string(e.what()).find(s) != std::string::npos,
                                            "Missing '" + s + "' in " + e.what());
        std::cout << "  rejected: " << e.what() << '\n';
        return;
    }
    throw std::runtime_error("invalid input returned a solid");
}
std::string sourceSnapshot(const smd::ParsedBrep& b) {
    std::ostringstream s;
    const auto n = [&](double x) { s << std::bit_cast<std::uint64_t>(x) << ' '; };
    const auto p = [&](const smd::Plane& x) { n(x.a); n(x.b); n(x.c); n(x.d); };
    for (const auto* vs : {&b.verticesLocal, &b.verticesWorld}) {
        s << vs->size() << ' '; for (const auto& v : *vs) { n(v.x); n(v.y); n(v.z); }
    }
    s << b.faces.size() << ' ';
    for (const auto& f : b.faces) {
        n(f.flag); s << f.indices.size() << ' '; for (auto i : f.indices) s << i << ' ';
        s << f.plane.has_value() << ' '; if (f.plane) p(*f.plane);
        s << f.name.has_value() << ' '; if (f.name) s << std::quoted(*f.name) << ' ';
    }
    s << b.planes.size() << ' '; for (const auto& x : b.planes) p(x);
    s << b.faceNames.size() << ' '; for (const auto& x : b.faceNames) s << std::quoted(x) << ' ';
    s << b.transform.has_value() << ' '; if (b.transform) for (auto x : b.transform->values) n(x);
    s << b.vertexCountTokenIndex;
    return s.str();
}
void flags(std::ostream& out, const TopoDS_Shape& s) {
    if (s.IsNull()) { out << "null "; return; }
    out << s.TShape().get() << ' ' << s.Orientation() << ' ' << s.Free() << s.Locked()
        << s.Modified() << s.Checked() << s.Orientable() << s.Closed() << s.Infinite() << s.Convex() << ' ';
    for (TopoDS_Iterator it(s); it.More(); it.Next()) flags(out, it.Value());
}
std::string snapshot(const smd_geometry::ShellAssembly& b) {
    std::ostringstream out;
    const auto shape = [&](const TopoDS_Shape& s) { BRepTools::Write(s, out); flags(out, s); };
    shape(b.shell);
    out << b.vertices.size() << ' '; for (const auto& v : b.vertices) shape(v);
    out << b.faces.size() << ' '; for (const auto& f : b.faces) shape(f);
    out << b.edges.size() << ' ';
    for (const auto& e : b.edges) {
        shape(e.edge); out << e.firstVertex << ' ' << e.secondVertex << ' ';
        for (const auto& u : e.uses) out << u.faceOrdinal << ' ' << u.loopPosition << ' ' << u.forward << ' ';
    }
    out << b.loopEdges.size() << ' ';
    for (const auto& loop : b.loopEdges) { out << loop.size() << ' '; for (const auto& e : loop) shape(e); }
    out << b.reversedFaceOrdinals.size() << ' '; for (auto f : b.reversedFaceOrdinals) out << f << ' ';
    return out.str();
}
smd_geometry::ShellAssembly assemble(const smd::ParsedBrep& b) {
    return smd_geometry::assembleShell(b, smd_geometry::convertFaces(b));
}
void reverse(smd_geometry::ShellAssembly& b) {
    b.shell.Reverse(); for (auto& f : b.faces) f.Reverse();
    for (auto& loop : b.loopEdges) for (auto& e : loop) e.Reverse();
}
void verify(const smd::ParsedBrep& source, const smd_geometry::ShellAssembly& input,
            const smd_geometry::SolidAssembly& result, double volume, double volumeTolerance) {
    require(result.solid.ShapeType() == TopAbs_SOLID, "one TopoDS_Solid");
    int solids = 0, shells = 0;
    for (TopExp_Explorer it(result.solid, TopAbs_SOLID); it.More(); it.Next()) ++solids;
    for (TopoDS_Iterator it(result.solid); it.More(); it.Next()) {
        require(it.Value().IsEqual(result.boundary.shell), "single actual mapped boundary"); ++shells;
    }
    require(solids == 1 && shells == 1 && BRep_Tool::IsClosed(result.boundary.shell), "one solid and closed shell");
    // Validation tests run on a copy to avoid altering flags of the object under test.
    auto copy = BRepBuilderAPI_Copy(result.solid, true, false).Shape();
    require(BRepCheck_Analyzer(copy, true, false, true).IsValid(), "contextual OCCT validity");
    BRepClass3d_SolidClassifier classifier(copy); classifier.PerformInfinitePoint(smd_geometry::distanceToleranceMm);
    require(classifier.State() == TopAbs_OUT, "infinity OUT");
    require(std::isfinite(result.volumeMm3) && result.volumeMm3 > 0 &&
            std::abs(result.volumeMm3 - volume) <= volumeTolerance, "independent reference volume");
    const auto& b = result.boundary;
    require(b.reversedFaceOrdinals == input.reversedFaceOrdinals, "relative reversal history retained");
    ShapeMap vs, es, fs, inputs;
    TopExp::MapShapes(b.shell, TopAbs_VERTEX, vs); TopExp::MapShapes(b.shell, TopAbs_EDGE, es);
    TopExp::MapShapes(b.shell, TopAbs_FACE, fs); TopExp::MapShapes(input.shell, inputs);
    require(es.Extent() == static_cast<int>(input.edges.size()) && fs.Extent() == static_cast<int>(input.faces.size()),
            "unchanged shared edge and face counts");
    require(b.vertices.size() == input.vertices.size(), "all vertex ordinals retained");
    for (std::size_t v = 0; v < b.vertices.size(); ++v) {
        const auto p = BRep_Tool::Pnt(b.vertices[v]); const auto& s = source.verticesLocal[v];
        require(p.X() == s.x && p.Y() == s.y && p.Z() == s.z, "exact source-local vertex coordinates");
        require(!inputs.Contains(b.vertices[v]) && !b.vertices[v].IsSame(input.vertices[v]), "private vertices");
        require(BRep_Tool::Tolerance(b.vertices[v]) == BRep_Tool::Tolerance(input.vertices[v]), "vertex tolerance unchanged");
    }
    for (std::size_t f = 0; f < b.faces.size(); ++f) {
        auto expected = input.faces[f]; if (result.globallyReversed) expected.Reverse();
        require(b.faces[f].Orientation() == expected.Orientation(), "actual face orientation mapping");
        require(!inputs.Contains(b.faces[f]) && BRep_Tool::Surface(b.faces[f]) != BRep_Tool::Surface(input.faces[f]),
                "private face topology and surface");
        double a,bn,c,d, ia,ib,ic,id;
        BRepAdaptor_Surface(b.faces[f]).Plane().Coefficients(a,bn,c,d);
        BRepAdaptor_Surface(input.faces[f]).Plane().Coefficients(ia,ib,ic,id);
        require(a == ia && bn == ib && c == ic && d == id, "supporting plane unchanged");
        require(BRep_Tool::Tolerance(b.faces[f]) == BRep_Tool::Tolerance(input.faces[f]), "face tolerance unchanged");
        std::vector<bool> seen(b.loopEdges[f].size(), false);
        for (TopExp_Explorer it(b.faces[f], TopAbs_EDGE); it.More(); it.Next()) {
            std::size_t j = 0;
            while (j < seen.size() && !it.Current().IsEqual(b.loopEdges[f][j])) ++j;
            require(j < seen.size() && !seen[j], "actual edge occurrence correspondence"); seen[j] = true;
        }
        require(std::find(seen.begin(), seen.end(), false) == seen.end(), "all occurrences seen");
        for (std::size_t j = 0; j < seen.size(); ++j) {
            auto e = input.loopEdges[f][j]; if (result.globallyReversed) e.Reverse();
            require(b.loopEdges[f][j].Orientation() == e.Orientation(), "global reversal reflected in every occurrence");
        }
        // Recover original source winding independently of final face orientation.
        auto forward = TopoDS::Face(b.faces[f].Oriented(TopAbs_FORWARD));
        std::size_t j = 0;
        for (BRepTools_WireExplorer it(BRepTools::OuterWire(forward), forward); it.More(); it.Next(), ++j)
            require(j < source.faces[f].indices.size() && it.CurrentVertex().IsSame(b.vertices[source.faces[f].indices[j]]),
                    "source ordered loop preserved");
        require(j == source.faces[f].indices.size(), "complete source loop");
    }
    for (std::size_t i = 0; i < b.edges.size(); ++i) {
        const auto& e = b.edges[i]; const auto& original = input.edges[i];
        require(!inputs.Contains(e.edge) && e.edge.Orientation() == TopAbs_FORWARD, "private canonical edge");
        require(e.firstVertex == original.firstVertex && e.secondVertex == original.secondVertex, "edge source endpoints");
        require(BRep_Tool::Tolerance(e.edge) == BRep_Tool::Tolerance(original.edge), "edge tolerance unchanged");
        for (std::size_t u = 0; u < 2; ++u) {
            const auto& use = e.uses[u]; const auto& old = original.uses[u];
            require(use.faceOrdinal == old.faceOrdinal && use.loopPosition == old.loopPosition && use.forward == old.forward,
                    "source edge-use records unchanged");
            require(b.loopEdges[use.faceOrdinal][use.loopPosition].IsSame(e.edge), "shared canonical edge identity");
        }
        require(b.loopEdges[e.uses[0].faceOrdinal][e.uses[0].loopPosition].Orientation() !=
                b.loopEdges[e.uses[1].faceOrdinal][e.uses[1].loopPosition].Orientation(), "opposite final shared uses");
    }
}
smd_geometry::SolidAssembly buildAndCheck(const smd::ParsedBrep& source, const smd_geometry::ShellAssembly& shell,
                                         double volume, double tolerance) {
    const auto before = snapshot(shell), records = sourceSnapshot(source);
    auto result = smd_geometry::buildSolid(shell);
    require(snapshot(shell) == before && sourceSnapshot(source) == records, "all inputs unchanged on success");
    verify(source, shell, result, volume, tolerance);
    return result;
}
// A triangulated octahedral sphere: every edge has two opposite uses and each
// vertex one face fan. Moving the bottom pole THROUGH the top pyramid creates
// crossings while preserving closure, orientation and individually valid faces.
smd::ParsedBrep octahedron(smd::Vec3 bottom) {
    smd::ParsedBrep b{};
    b.verticesLocal = {{-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0},{0,0,10},bottom};
    for (std::size_t i = 0; i < 4; ++i) {
        for (const auto& loop : {std::vector<std::size_t>{i,(i+1)%4,4}, {i,5,(i+1)%4}}) {
            const auto& a = b.verticesLocal[loop[0]]; const auto& z = b.verticesLocal[loop[1]]; const auto& c = b.verticesLocal[loop[2]];
            gp_Vec u(z.x-a.x,z.y-a.y,z.z-a.z), v(c.x-a.x,c.y-a.y,c.z-a.z);
            const auto n = u.Crossed(v).Normalized();
            b.faces.push_back({0,loop,smd::Plane{n.X(),n.Y(),n.Z(),-(n.X()*a.x+n.Y()*a.y+n.Z()*a.z)},"synthetic"});
        }
    }
    return b;
}
void rejectsInterference(const smd::ParsedBrep& b, const std::string& ordinal) {
    auto faces = smd_geometry::convertFaces(b);
    for (const auto& f : faces.faces)
        require(BRepCheck_Analyzer(BRepBuilderAPI_Copy(f,true,false).Shape(),true).IsValid(), "individual face valid");
    const auto shell = smd_geometry::assembleShell(b, faces); // MUST succeed before rejection test.
    require(BRepCheck_Analyzer(BRepBuilderAPI_Copy(shell.shell,true,false).Shape(),true,false,true).IsValid(), "GEO-02A shell valid");
    const auto before = snapshot(shell), records = sourceSnapshot(b);
    rejects([&]{ (void)smd_geometry::buildSolid(shell); }, {"GEO-02B [self-interference]", "geometric", ordinal});
    require(snapshot(shell) == before && sourceSnapshot(b) == records, "failed checker leaves inputs unchanged");
}
} // namespace

int main(int argc, char** argv) {
    int checks = 0;
    const auto test = [&](const char* name, const std::function<void()>& body) {
        try { body(); } catch (const std::exception& e) { throw std::runtime_error(std::string(name) + ": " + e.what()); }
        std::cout << "ok " << ++checks << " - " << name << '\n';
    };
    try {
        require(argc == 2, "Usage: smd-solid-tests plain_column.smd");
        std::ifstream file(argv[1], std::ios::binary); require(file.good(), "plain fixture readable");
        const std::string text{std::istreambuf_iterator<char>(file),{}};
        const auto parsed = smd::parseSmd10125(text);
        const auto& b = parsed.geometry.brep;
        test("plain column: one solid, one shell, 8/12/6, infinity OUT, 2925000000 mm^3", [&] {
            const auto records = sourceSnapshot(b), json = smd::toJsonV1(parsed);
            const auto shell = assemble(b);
            const auto result = buildAndCheck(b, shell, 2925000000.0, 1.0);
            require(!result.globallyReversed, "plain column already outward");
            ShapeMap vs, es, fs;
            TopExp::MapShapes(result.solid,TopAbs_VERTEX,vs); TopExp::MapShapes(result.solid,TopAbs_EDGE,es); TopExp::MapShapes(result.solid,TopAbs_FACE,fs);
            require(vs.Extent()==8 && es.Extent()==12 && fs.Extent()==6, "actual 8/12/6 counts");
            require(sourceSnapshot(b)==records && smd::toJsonV1(parsed)==json, "all source records and parser output unchanged");
            std::cout << std::setprecision(17) << "  volume mm^3 = " << result.volumeMm3 << '\n';
        });
        test("same plain boundary inward: one global reversal, exact final occurrence orientations", [&] {
            auto shell = assemble(b); reverse(shell);
            const auto result = buildAndCheck(b,shell,2925000000.0,1.0);
            require(result.globallyReversed, "one reported global reversal");
        });
        test("source winding reversed and relative GEO-02A correction history retained", [&] {
            auto source=b; std::reverse(source.faces[0].indices.begin(),source.faces[0].indices.end());
            auto shell=assemble(source);
            require(!shell.reversedFaceOrdinals.empty(), "GEO-02A relative corrections exercised");
            require(buildAndCheck(source,shell,2925000000.0,1.0).globallyReversed, "inward source corrected globally");
        });
        test("independent result topology, surfaces, curves, flags and lifetime", [&] {
            const auto shell = assemble(b); const auto before=snapshot(shell);
            auto result=buildAndCheck(b,shell,2925000000.0,1.0);
            BRep_Builder builder; builder.UpdateVertex(result.boundary.vertices[0],gp_Pnt(1,2,3),1e-7);
            BRep_Tool::Surface(result.boundary.faces[0])->Translate(gp_Vec(1,2,3));
            double first,last;
            BRep_Tool::Curve(result.boundary.edges[0].edge,first,last)->Translate(gp_Vec(3,2,1));
            result.boundary.shell.Checked(!result.boundary.shell.Checked());
            require(snapshot(shell)==before, "result mutations do not leak through handles");
            const auto detached=[&]{ return smd_geometry::buildSolid(assemble(b)); }();
            require(BRepCheck_Analyzer(detached.solid,true,false,true).IsValid(), "result survives input destruction");
        });
        test("unused source vertex remains private and outside boundary", [&] {
            auto source=b; source.verticesLocal.push_back({123,456,789});
            const auto shell=assemble(source); const auto result=buildAndCheck(source,shell,2925000000.0,1.0);
            ShapeMap vs; TopExp::MapShapes(result.solid,TopAbs_VERTEX,vs);
            require(vs.Extent()==8 && result.boundary.vertices.size()==9 && !vs.Contains(result.boundary.vertices.back()), "unused vertex excluded");
        });
        test("valid synthetic octahedron accepts established shared contacts", [&] {
            const auto source=octahedron({0,0,-10});
            buildAndCheck(source,assemble(source),8000.0/3.0,1e-9);
        });
        test("crossing individually valid triangular faces pass GEO-02A then fail interference", [&] {
            rejectsInterference(octahedron({20,0,5}),"SourceFace[");
        });
        test("pole touching a nonincident edge and overlapping faces fail interference after GEO-02A", [&] {
            rejectsInterference(octahedron({5,-5,5}),"SourceVertex[");
        });
        test("pole touching nonincident face with coplanar overlap fails interference", [&] {
            rejectsInterference(octahedron({0,-5,5}),"SourceFace[");
        });
        test("positive volume below 1 mm^3 is not subject to fixture absolute tolerance", [&] {
            auto source=octahedron({0,0,-10});
            for (auto& v : source.verticesLocal) { v.x*=0.001; v.y*=0.001; v.z*=0.001; }
            for (auto& f : source.faces) f.plane->d*=0.001;
            buildAndCheck(source,assemble(source),8000.0/3.0*1e-9,1e-16);
        });
        test("invalid shell correspondence, orientation and tolerance rejected without mutations", [&] {
            rejects([&]{ (void)smd_geometry::buildSolid({}); },{"[correspondence]"});
            auto shell=assemble(b); shell.loopEdges[0][0].Reverse(); auto before=snapshot(shell);
            rejects([&]{ (void)smd_geometry::buildSolid(shell); },{"[correspondence]","SourceFace[0]"});
            require(snapshot(shell)==before,"malformed mapping unchanged");
            shell=assemble(b); shell.shell.Reverse(); before=snapshot(shell);
            rejects([&]{ (void)smd_geometry::buildSolid(shell); },{"[correspondence]","orientation"});
            require(snapshot(shell)==before,"inconsistent shell orientation unchanged");
            shell=assemble(b); BRep_Builder builder; builder.UpdateVertex(shell.vertices[0],1e-5); before=snapshot(shell);
            rejects([&]{ (void)smd_geometry::buildSolid(shell); },{"[correspondence]","SourceVertex[0]","tolerance"});
            require(snapshot(shell)==before,"inflated tolerance unchanged");
        });
        std::cout << checks << " GEO-02B checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "After " << checks << " successful checks: " << e.what() << '\n'; return 1;
    }
}
