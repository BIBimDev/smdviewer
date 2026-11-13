#include "smd_geometry/shell.hpp"
#include "../src/shell_topology.hpp"
#include "smd/json.hpp"
#include "smd/parser.hpp"

#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepCheck_Shell.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Geom_Plane.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_IndexedMap.hxx>
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
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
template<class F>
void rejects(F&& action, std::initializer_list<std::string> diagnostics) {
    try { action(); }
    catch (const std::runtime_error& e) {
        for (const auto& text : diagnostics)
            require(std::string(e.what()).find(text) != std::string::npos,
                    "Missing '" + text + "' in: " + e.what());
        return;
    }
    throw std::runtime_error("Invalid input returned a shell/topology");
}
smd::ParsedBrep cube() {
    smd::ParsedBrep b{};
    b.verticesLocal = {{0,0,0},{10,0,0},{10,10,0},{0,10,0},
                       {0,0,10},{10,0,10},{10,10,10},{0,10,10}};
    b.faces = {{1,{0,3,2,1},smd::Plane{0,0,-1,0},"bottom"},
               {2,{4,5,6,7},smd::Plane{0,0,1,-10},"top"},
               {3,{0,1,5,4},smd::Plane{0,-1,0,0},"front"},
               {4,{1,2,6,5},smd::Plane{1,0,0,-10},"right"},
               {5,{2,3,7,6},smd::Plane{0,1,0,-10},"back"},
               {6,{3,0,4,7},smd::Plane{-1,0,0,0},"left"}};
    return b;
}
smd::ParsedBrep disjointCubes() {
    auto b = cube();
    const auto second = cube();
    for (const auto& v : second.verticesLocal) b.verticesLocal.push_back({v.x+30,v.y,v.z});
    for (auto face : second.faces) {
        for (auto& v : face.indices) v += 8;
        face.plane->d -= face.plane->a * 30;
        b.faces.push_back(face);
    }
    return b;
}

// All ParsedBrep fields, including signed zeros/NaNs, optional metadata and
// transform, independently of the diagnostic JSON's persistence limitations.
std::string sourceSnapshot(const smd::ParsedBrep& source) {
    std::ostringstream out;
    const auto number = [&](double n) { out << std::bit_cast<std::uint64_t>(n) << ' '; };
    const auto plane = [&](const smd::Plane& p) { number(p.a); number(p.b); number(p.c); number(p.d); };
    const auto names = [&](const auto& strings) {
        out << strings.size() << ' '; for (const auto& s : strings) out << std::quoted(s) << ' ';
    };
    for (const auto* vertices : {&source.verticesLocal, &source.verticesWorld}) {
        out << vertices->size() << ' ';
        for (const auto& v : *vertices) { number(v.x); number(v.y); number(v.z); }
    }
    out << source.faces.size() << ' ';
    for (const auto& f : source.faces) {
        number(f.flag); out << f.indices.size() << ' ';
        for (const auto index : f.indices) out << index << ' ';
        out << f.plane.has_value() << ' '; if (f.plane) plane(*f.plane);
        out << f.name.has_value() << ' '; if (f.name) out << std::quoted(*f.name) << ' ';
    }
    out << source.planes.size() << ' '; for (const auto& p : source.planes) plane(p);
    names(source.faceNames);
    out << source.transform.has_value() << ' ';
    if (source.transform) for (const auto n : source.transform->values) number(n);
    out << source.vertexCountTokenIndex;
    return out.str();
}
void flags(std::ostream& out, const TopoDS_Shape& shape) {
    if (shape.IsNull()) { out << "null "; return; }
    out << shape.TShape().get() << ' ' << shape.Orientation() << ' '
        << shape.Free() << shape.Locked() << shape.Modified() << shape.Checked()
        << shape.Orientable() << shape.Closed() << shape.Infinite() << shape.Convex() << ' ';
    for (TopoDS_Iterator it(shape); it.More(); it.Next()) flags(out, it.Value());
}
std::string inputSnapshot(const smd_geometry::FaceConversion& input) {
    std::ostringstream out;
    for (const auto& v : input.vertices) { BRepTools::Write(v, out); flags(out, v); }
    for (const auto& f : input.faces) { BRepTools::Write(f, out); flags(out, f); }
    return out.str();
}

void check(const smd::ParsedBrep& b, const smd_geometry::FaceConversion& input,
           const smd_geometry::ShellAssembly& result, const std::vector<std::size_t>& reversed) {
    require(!result.shell.IsNull() && result.shell.ShapeType() == TopAbs_SHELL, "shell result only");
    require(!TopExp_Explorer(result.shell, TopAbs_SOLID).More(), "no solid");
    require(BRepCheck_Analyzer(result.shell, true, false, true).IsValid(), "contextually valid shell");
    BRepCheck_Shell validation(result.shell);
    require(validation.Closed() == BRepCheck_NoError && validation.Orientation() == BRepCheck_NoError,
            "actual closure and orientation");
    require(BRep_Tool::IsClosed(result.shell), "topological closure");
    require(result.reversedFaceOrdinals == reversed, "exact relative corrections");
    require(result.faces.size() == b.faces.size() && result.vertices.size() == b.verticesLocal.size(), "ordinal counts");
    std::set<std::size_t> used;
    std::map<std::pair<std::size_t,std::size_t>, std::vector<smd_geometry::SourceEdgeUse>> sourceUses;
    for (std::size_t f = 0; f < b.faces.size(); ++f) {
        const auto& loop = b.faces[f].indices;
        for (std::size_t j = 0; j < loop.size(); ++j) {
            const auto a = loop[j], z = loop[(j+1)%loop.size()];
            sourceUses[{std::min(a,z),std::max(a,z)}].push_back({f,j,a<z});
            used.insert(a);
        }
    }
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> shellVertices, shellEdges, shellFaces, inputShapes;
    TopExp::MapShapes(result.shell, TopAbs_VERTEX, shellVertices);
    TopExp::MapShapes(result.shell, TopAbs_EDGE, shellEdges);
    TopExp::MapShapes(result.shell, TopAbs_FACE, shellFaces);
    for (const auto& face : input.faces) TopExp::MapShapes(face, inputShapes);
    require(shellVertices.Extent() == static_cast<int>(used.size()), "unique used shell vertices");
    require(shellEdges.Extent() == static_cast<int>(sourceUses.size()), "one actual edge per source pair");
    require(shellFaces.Extent() == static_cast<int>(b.faces.size()), "one actual face per source face");
    require(result.edges.size() == sourceUses.size(), "full edge correspondence");
    for (std::size_t v = 0; v < result.vertices.size(); ++v) {
        const auto& vertex = result.vertices[v];
        const auto p = BRep_Tool::Pnt(vertex); const auto& raw = b.verticesLocal[v];
        require(p.X() == raw.x && p.Y() == raw.y && p.Z() == raw.z, "exact local coordinates");
        require(!inputShapes.Contains(vertex), "private vertex entity");
        require(BRep_Tool::Tolerance(vertex) <= smd_geometry::distanceToleranceMm, "no vertex tolerance inflation");
        require(shellVertices.Contains(vertex) == used.contains(v), "unused vertex stays outside shell");
    }
    std::map<int,std::vector<TopAbs_Orientation>> actualUses;
    for (TopExp_Explorer f(result.shell, TopAbs_FACE); f.More(); f.Next())
        for (TopExp_Explorer e(f.Current(), TopAbs_EDGE); e.More(); e.Next())
            actualUses[shellEdges.FindIndex(e.Current())].push_back(e.Current().Orientation());
    for (const auto& [index, orientations] : actualUses) {
        require(index > 0 && orientations.size() == 2 && orientations[0] != orientations[1], "two opposite actual uses");
        require((orientations[0] == TopAbs_FORWARD || orientations[0] == TopAbs_REVERSED) &&
                (orientations[1] == TopAbs_FORWARD || orientations[1] == TopAbs_REVERSED), "boundary uses only");
    }
    for (const auto& edge : result.edges) {
        const auto& expected = sourceUses.at({edge.firstVertex,edge.secondVertex});
        require(shellEdges.Contains(edge.edge) && !inputShapes.Contains(edge.edge), "new shared edge entity");
        require(edge.edge.Orientation() == TopAbs_FORWARD, "canonical edge direction");
        require(BRep_Tool::Tolerance(edge.edge) <= smd_geometry::distanceToleranceMm, "no edge tolerance inflation");
        for (std::size_t i = 0; i < 2; ++i) {
            const auto& use = edge.uses[i]; const auto& exp = expected[i];
            require(use.faceOrdinal == exp.faceOrdinal && use.loopPosition == exp.loopPosition &&
                    use.forward == exp.forward, "source edge use metadata");
            const bool flipped = std::find(reversed.begin(),reversed.end(),use.faceOrdinal) != reversed.end();
            const auto& mapped = result.loopEdges[use.faceOrdinal][use.loopPosition];
            require(mapped.IsSame(edge.edge) && mapped.Orientation() ==
                    ((use.forward != flipped) ? TopAbs_FORWARD : TopAbs_REVERSED), "oriented occurrence mapping");
        }
    }
    for (std::size_t f = 0; f < b.faces.size(); ++f) {
        const auto& face = result.faces[f]; const auto& loop = b.faces[f].indices;
        const bool flipped = std::find(reversed.begin(),reversed.end(),f) != reversed.end();
        require(face.Orientation() == (flipped ? TopAbs_REVERSED : TopAbs_FORWARD), "face orientation mapping");
        require(!inputShapes.Contains(face), "private face entity");
        require(BRep_Tool::Surface(face) != BRep_Tool::Surface(input.faces[f]), "private surface handle");
        require(BRep_Tool::Tolerance(face) <= smd_geometry::distanceToleranceMm, "no face tolerance inflation");
        const auto plane = BRepAdaptor_Surface(face).Plane(); const auto& raw = *b.faces[f].plane;
        const gp_Dir n(raw.a, raw.b, raw.c);
        require(std::abs(std::abs(plane.Axis().Direction().Dot(n))-1) <= 1e-12, "source plane normal locus");
        const double d = raw.d/std::hypot(raw.a,raw.b,raw.c);
        require(plane.Distance(gp_Pnt(-d*n.X(),-d*n.Y(),-d*n.Z())) <= 1e-10, "original plane location");
        // Undo only the derived face reversal to inspect the original loop start
        // and direction, independent of occurrence ordering in loopEdges.
        const auto forward = TopoDS::Face(face.Oriented(TopAbs_FORWARD));
        std::size_t j = 0;
        for (BRepTools_WireExplorer it(BRepTools::OuterWire(forward), forward); it.More(); it.Next(), ++j) {
            require(j < loop.size() && it.CurrentVertex().IsSame(result.vertices[loop[j]]), "full original ordered loop");
            auto expected = result.loopEdges[f][j];
            if (flipped) expected.Reverse();
            require(it.Current().IsEqual(expected), "actual edge occurrence identity");
        }
        require(j == loop.size(), "complete loop");
    }
}
smd_geometry::ShellAssembly assembleAndCheck(const smd::ParsedBrep& b, const std::vector<std::size_t>& reversed = {}) {
    const auto input = smd_geometry::convertFaces(b);
    const auto sourceBefore = sourceSnapshot(b), inputBefore = inputSnapshot(input);
    auto result = smd_geometry::assembleShell(b,input);
    require(sourceSnapshot(b) == sourceBefore, "all source records unchanged");
    require(inputSnapshot(input) == inputBefore, "all GEO-01 topology, geometry, tolerances and flags unchanged");
    check(b,input,result,reversed);
    return result;
}
} // namespace

int main(int argc, char** argv) {
    int checks = 0;
    const auto test = [&](const char* name, const std::function<void()>& body) {
        try { body(); } catch (const std::exception& e) { throw std::runtime_error(std::string(name)+": "+e.what()); }
        std::cout << "ok " << ++checks << " - " << name << '\n';
    };
    try {
        require(argc == 2, "Usage: smd-shell-tests plain_column.smd");
        test("plain column: 8 vertices, 12 shared edges, 6 faces; unchanged source and GEO-01 input", [&] {
            std::ifstream file(argv[1], std::ios::binary); require(file.good(),"fixture readable");
            const std::string text{std::istreambuf_iterator<char>(file),{}};
            const auto parsed = smd::parseSmd10125(text);
            const auto before = smd::toJsonV1(parsed);
            const auto& source = parsed.geometry.brep;
            require(source.verticesLocal.size() == 8 && source.faces.size() == 6, "acceptance specimen counts");
            require(source.verticesLocal[0].x == -750 && source.verticesWorld[0].x == -375, "distinct source frames");
            const auto result = assembleAndCheck(source);
            require(result.vertices.size() == 8 && result.edges.size() == 12 && result.faces.size() == 6, "8/12/6");
            require(smd::toJsonV1(parsed) == before, "entire parser diagnostic output unchanged");
        });
        test("closed synthetic cube and each non-reference face reversed", [&] {
            assembleAndCheck(cube());
            for (std::size_t f = 1; f < 6; ++f) {
                auto b = cube(); std::reverse(b.faces[f].indices.begin(),b.faces[f].indices.end());
                assembleAndCheck(b,{f});
            }
        });
        test("reference face is never corrected; global inward/outward choice is not made", [&] {
            auto b = cube(); std::reverse(b.faces[0].indices.begin(),b.faces[0].indices.end());
            assembleAndCheck(b,{1,2,3,4,5});
            for (auto& face : b.faces) std::reverse(face.indices.begin(),face.indices.end());
            assembleAndCheck(b,{1,2,3,4,5});
        });
        test("local coordinates only and unused near-coincident vertex stays distinct", [&] {
            auto b = cube(); b.verticesLocal.push_back({1e-10,0,0});
            b.verticesWorld = {{std::numeric_limits<double>::quiet_NaN(),0,0}};
            b.transform = smd::Transform3x4{{2,0,0,999,0,3,0,888,0,0,4,777}};
            assembleAndCheck(b);
        });
        test("within-policy off-plane vertex retained exactly without refitting or tolerance inflation", [&] {
            auto b = cube(); b.verticesLocal[0].z = 5e-8;
            assembleAndCheck(b);
        });
        test("returned vertices, surfaces and flags are independent of GEO-01 input handles", [&] {
            const auto b = cube(); const auto input = smd_geometry::convertFaces(b);
            const auto before = inputSnapshot(input);
            auto result = smd_geometry::assembleShell(b,input);
            BRep_Builder builder;
            builder.UpdateVertex(result.vertices[0],gp_Pnt(101,102,103),smd_geometry::distanceToleranceMm);
            const auto surface = occ::down_cast<Geom_Plane>(BRep_Tool::Surface(result.faces[0]));
            require(!surface.IsNull(),"derived plane handle");
            surface->Translate(gp_Vec(1,2,3));
            result.faces[0].Checked(!result.faces[0].Checked());
            result.edges[0].edge.Checked(!result.edges[0].edge.Checked());
            require(inputSnapshot(input) == before,"all result mutations isolated from GEO-01");
            const auto detached = [] {
                const auto source=cube();
                return smd_geometry::assembleShell(source,smd_geometry::convertFaces(source));
            }();
            require(BRepCheck_Analyzer(detached.shell,true).IsValid(),"shell owns geometry after inputs destroyed");
        });
        test("unmatched edge identifies source endpoints, face and closing loop position", [&] {
            auto b = cube(); b.faces.erase(b.faces.begin()+1);
            const auto input = smd_geometry::convertFaces(b);
            const auto before = inputSnapshot(input);
            rejects([&]{ (void)smd_geometry::assembleShell(b,input); },
                    {"source edge (4, 5)","SourceFace[1] loop position 2","missing mate"});
            require(inputSnapshot(input) == before, "failure leaves GEO-01 unchanged");
            // An open triangle's first key is its implicit closing pair.
            b.verticesLocal.resize(3); b.faces.resize(1); b.faces[0].indices={2,1,0};
            rejects([&]{ (void)smd_geometry::detail::analyzeShellTopology(b); },
                    {"source edge (0, 1)","loop position 1","missing mate"});
            b.faces[0].indices={0,2,1};
            rejects([&]{ (void)smd_geometry::detail::analyzeShellTopology(b); },
                    {"source edge (0, 1)","loop position 2","missing mate"});
        });
        test("non-manifold edge incidence reports all affected occurrences", [&] {
            auto b = cube(); b.faces.push_back(b.faces[0]);
            rejects([&]{ (void)smd_geometry::assembleShell(b,smd_geometry::convertFaces(b)); },
                    {"source edge (0, 1)","SourceFace[0] loop position 3",
                     "SourceFace[2] loop position 0","SourceFace[6] loop position 3","non-manifold edge"});
        });
        test("two edge uses on one face rejected before geometry construction", [&] {
            auto b = cube(); b.faces.resize(1); b.faces[0].indices={0,1,2,1};
            rejects([&]{ (void)smd_geometry::detail::analyzeShellTopology(b); },
                    {"source edge (0, 1)","SourceFace[0] loop position 0","loop position 3","same face"});
        });
        test("two closed disconnected cubes rejected", [&] {
            const auto b = disjointCubes();
            rejects([&]{ (void)smd_geometry::assembleShell(b,smd_geometry::convertFaces(b)); },
                    {"SourceFace[6]","disconnected face set","SourceFace[0]"});
        });
        test("pinched vertex has two fans despite exact edge pairing", [&] {
            auto b = cube(); const auto second = cube();
            std::vector<std::size_t> indices;
            // Two geometrically valid cubes touch at the single shared corner
            // (0,0,0). Opposite second-cube corner 6 uses source index 0.
            for (std::size_t v=0; v<8; ++v) {
                indices.push_back(v==6 ? 0 : b.verticesLocal.size());
                if (v!=6) {
                    const auto p=second.verticesLocal[v];
                    b.verticesLocal.push_back({p.x-10,p.y-10,p.z-10});
                }
            }
            for (auto face : second.faces) {
                for (auto& v : face.indices) v=indices[v];
                face.plane->d += 10*(face.plane->a+face.plane->b+face.plane->c);
                b.faces.push_back(face);
            }
            rejects([&]{ (void)smd_geometry::assembleShell(b,smd_geometry::convertFaces(b)); },
                    {"SourceVertex[0]","non-manifold vertex fan","SourceFace[0]","SourceFace[7]"});
        });
        test("edge-connected pinched surface rejects a disconnected vertex link", [&] {
            // Triangulated sphere: two disjoint five-vertex rings and two poles.
            // Identifying its nonadjacent poles keeps every edge paired and the
            // face graph connected, but leaves two fans at SourceVertex[0].
            smd::ParsedBrep b{}; b.verticesLocal.resize(11);
            for (std::size_t i=0; i<5; ++i) {
                const auto a=1+i, an=1+(i+1)%5, z=6+i, zn=6+(i+1)%5;
                for (auto loop : {std::vector<std::size_t>{0,a,an}, {a,z,an}, {an,z,zn}, {0,zn,z}})
                    b.faces.push_back({0,loop,std::nullopt,std::nullopt});
            }
            rejects([&]{ (void)smd_geometry::detail::analyzeShellTopology(b); },
                    {"SourceVertex[0]","non-manifold vertex fan","SourceFace[0]","SourceFace[3]"});
        });
        test("projective-plane incidence rejects contradictory orientation constraints", [&] {
            smd::ParsedBrep b{}; b.verticesLocal.resize(6);
            for (auto loop : {std::vector<std::size_t>{0,1,2},{0,1,3},{0,2,4},{0,3,5},{0,4,5},
                              {1,2,5},{1,3,4},{1,4,5},{2,3,4},{2,3,5}})
                b.faces.push_back({0,loop,std::nullopt,std::nullopt});
            rejects([&]{ (void)smd_geometry::detail::analyzeShellTopology(b); },
                    {"source edge (","SourceFace[","loop position","contradictory orientation"});
        });
        test("mismatched GEO-01 coordinates, support, orientation and occurrence correspondence rejected", [&] {
            const auto b = cube();
            auto input = smd_geometry::convertFaces(b); input.vertices.pop_back();
            rejects([&]{ (void)smd_geometry::assembleShell(b,input); },{"collection size"});
            input = smd_geometry::convertFaces(b); input.faces[1].Reverse();
            rejects([&]{ (void)smd_geometry::assembleShell(b,input); },{"SourceFace[1]","unmodified"});
            input = smd_geometry::convertFaces(b); std::swap(input.faces[0],input.faces[1]);
            rejects([&]{ (void)smd_geometry::assembleShell(b,input); },{"SourceFace[0]","supporting plane"});
            input = smd_geometry::convertFaces(b); std::swap(input.vertices[0],input.vertices[1]);
            rejects([&]{ (void)smd_geometry::assembleShell(b,input); },{"SourceVertex[0]","coordinates"});
            auto rotated = b; std::rotate(rotated.faces[0].indices.begin(),rotated.faces[0].indices.begin()+1,rotated.faces[0].indices.end());
            input = smd_geometry::convertFaces(rotated);
            rejects([&]{ (void)smd_geometry::assembleShell(b,input); },{"SourceFace[0]","loop position 0","correspondence"});
        });
        test("inflated input tolerance rejected without normalizing the caller's shape", [&] {
            const auto b = cube(); auto input = smd_geometry::convertFaces(b);
            BRep_Builder builder;
            builder.UpdateVertex(input.vertices[0],1e-5);
            const auto before = inputSnapshot(input);
            rejects([&]{ (void)smd_geometry::assembleShell(b,input); },{"SourceVertex[0]","tolerance"});
            require(inputSnapshot(input) == before,"rejected input tolerance left unchanged");
        });
        test("invalid source indices and empty collection fail without a partial shell", [&] {
            auto b=cube(); const auto input=smd_geometry::convertFaces(b);
            b.faces[5].indices[2]=99;
            rejects([&]{ (void)smd_geometry::assembleShell(b,input); },{"SourceFace[5]","SourceVertex[99]"});
            b.faces.clear();
            rejects([&]{ (void)smd_geometry::assembleShell(b,input); },{"no faces"});
        });
        std::cout << checks << " GEO-02A checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "After " << checks << " successful checks: " << e.what() << '\n'; return 1;
    }
}
