#include "smd_geometry/principal_prism.hpp"
#include "smd/json.hpp"
#include "smd/parser.hpp"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Surface.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
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
// Preservation helpers are local to this stage so prior regression tests stay unchanged.
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

std::string solidSnapshot(const smd_geometry::SolidAssembly& s) {
    std::ostringstream out;
    BRepTools::Write(s.solid, out); flags(out, s.solid);
    out << snapshot(s.boundary) << ' ' << s.globallyReversed << ' ' << std::bit_cast<std::uint64_t>(s.volumeMm3);
    return out.str();
}
smd_geometry::SolidAssembly pipeline(const smd::ParsedBrep& source) {
    return smd_geometry::buildSolid(smd_geometry::assembleShell(source, smd_geometry::convertFaces(source)));
}
void addFace(smd::ParsedBrep& b, std::vector<std::size_t> loop) {
    const auto& a = b.verticesLocal[loop[0]];
    const auto& z = b.verticesLocal[loop[1]];
    const auto& c = b.verticesLocal[loop[2]];
    const auto n = gp_Vec(z.x-a.x,z.y-a.y,z.z-a.z).Crossed(gp_Vec(c.x-a.x,c.y-a.y,c.z-a.z)).Normalized();
    b.faces.push_back({0, std::move(loop), smd::Plane{n.X(),n.Y(),n.Z(),-(n.X()*a.x+n.Y()*a.y+n.Z()*a.z)}, "synthetic"});
}
smd::ParsedBrep prism(const std::vector<smd::Vec2>& polygon, double height = 5) {
    smd::ParsedBrep b{};
    const auto count = polygon.size();
    for (double z : {0.0, height}) for (const auto& p : polygon) b.verticesLocal.push_back({p.x,p.y,z});
    std::vector<std::size_t> bottom, top;
    for (std::size_t i = 0; i < count; ++i) { bottom.push_back(count-1-i); top.push_back(count+i); }
    addFace(b, bottom); addFace(b, top);
    for (std::size_t i = 0; i < count; ++i) addFace(b, {i,(i+1)%count,(i+1)%count+count,i+count});
    return b;
}
smd::ParsedBrep box() { return prism({{0,0},{80,0},{80,10},{0,10}}); }
void refreshPlanes(smd::ParsedBrep& b) {
    auto faces = std::move(b.faces); b.faces.clear();
    for (const auto& f : faces) addFace(b, f.indices);
}
void samePoint(const smd::Vec3& a, const smd::Vec3& b, const std::string& label) {
    require(a.x == b.x && a.y == b.y && a.z == b.z, label);
}
smd_geometry::PrincipalPrismCandidate check(const smd::ParsedBrep& source,
        const smd_geometry::SolidAssembly& input, smd::Vec3 minimum, smd::Vec3 maximum,
        std::size_t cap, double volume, double volumeTolerance) {
    const auto records = sourceSnapshot(source), before = solidSnapshot(input);
    auto result = smd_geometry::buildPrincipalPrismCandidate(input, source);
    require(sourceSnapshot(source) == records && solidSnapshot(input) == before, "source, solid and mappings unchanged");
    samePoint(result.minimumLocalMm, minimum, "exact minimum local bounds");
    samePoint(result.maximumLocalMm, maximum, "exact maximum local bounds");
    samePoint(result.dimensionsMm, {maximum.x-minimum.x,maximum.y-minimum.y,maximum.z-minimum.z}, "exact dimensions");
    require(result.supportingFaceOrdinal == cap, "supporting source face ordinal");
    require(!result.solid.IsNull() && result.solid.ShapeType() == TopAbs_SOLID, "one candidate solid");
    const auto copy = BRepBuilderAPI_Copy(result.solid,true,false).Shape();
    require(BRepCheck_Analyzer(copy,true,false,true).IsValid(), "valid candidate OCCT solid");
    BRepClass3d_SolidClassifier classifier(copy); classifier.PerformInfinitePoint(smd_geometry::distanceToleranceMm);
    require(classifier.State() == TopAbs_OUT, "candidate infinity OUT");
    GProp_GProps props; BRepGProp::VolumeProperties(copy,props,true,false,false);
    require(std::isfinite(props.Mass()) && std::abs(props.Mass()-volume) <= volumeTolerance, "independent volume reference");
    ShapeMap original, candidate, vertices;
    TopExp::MapShapes(input.solid,original); TopExp::MapShapes(result.solid,candidate);
    TopExp::MapShapes(result.solid,TopAbs_VERTEX,vertices);
    require(vertices.Extent()==8, "eight candidate corners");
    for (int i=1; i<=vertices.Extent(); ++i) {
        const auto p=BRep_Tool::Pnt(TopoDS::Vertex(vertices(i)));
        require((p.X()==minimum.x || p.X()==maximum.x) && (p.Y()==minimum.y || p.Y()==maximum.y) &&
                (p.Z()==minimum.z || p.Z()==maximum.z), "actual candidate corners match local bounds");
    }
    for (int i=1; i<=candidate.Extent(); ++i) {
        require(!original.Contains(candidate(i)), "independent candidate topology");
        for (int j=1; j<=original.Extent(); ++j) {
            if (candidate(i).ShapeType()==TopAbs_FACE && original(j).ShapeType()==TopAbs_FACE)
                require(BRep_Tool::Surface(TopoDS::Face(candidate(i))) != BRep_Tool::Surface(TopoDS::Face(original(j))), "independent surfaces");
            if (candidate(i).ShapeType()==TopAbs_EDGE && original(j).ShapeType()==TopAbs_EDGE) {
                double a,b,c,d;
                require(BRep_Tool::Curve(TopoDS::Edge(candidate(i)),a,b) != BRep_Tool::Curve(TopoDS::Edge(original(j)),c,d), "independent curves");
            }
        }
    }
    require(sourceSnapshot(source)==records && solidSnapshot(input)==before, "verification leaves inputs unchanged");
    return result;
}
void rejects(const smd::ParsedBrep& source, const smd_geometry::SolidAssembly& input,
             std::initializer_list<std::string> expected) {
    const auto before=solidSnapshot(input), records=sourceSnapshot(source);
    bool rejected=false;
    try { (void)smd_geometry::buildPrincipalPrismCandidate(input,source); }
    catch (const std::runtime_error& e) {
        for (const auto& text : expected) require(std::string(e.what()).find(text)!=std::string::npos,
            "missing '"+text+"' in: "+e.what());
        std::cout << "  rejected: " << e.what() << '\n'; rejected=true;
    }
    require(rejected,"unsupported input returned a candidate");
    require(solidSnapshot(input)==before && sourceSnapshot(source)==records,"rejection preserves all inputs");
}
void rejectValidSolid(const smd::ParsedBrep& source, const std::string& reason) {
    // All rule rejection specimens must pass the unchanged GEO-01/GEO-02 pipeline first.
    const auto input=pipeline(source);
    rejects(source,input,{"GEO-03A [unsupported]",reason});
}
} // namespace

int main(int argc, char** argv) {
    int checks=0;
    const auto test=[&](const char* name, const std::function<void()>& body) {
        try { body(); } catch (const std::exception& e) { throw std::runtime_error(std::string(name)+": "+e.what()); }
        std::cout << "ok " << ++checks << " - " << name << '\n';
    };
    try {
        require(argc==2,"Usage: smd-principal-prism-tests plain_column.smd");
        test("plain native parser -> faces -> shell -> solid -> candidate",[&] {
            std::ifstream file(argv[1],std::ios::binary); require(file.good(),"plain fixture readable");
            const std::string text{std::istreambuf_iterator<char>(file),{}}; require(!file.bad(),"fixture read complete");
            const auto parsed=smd::parseSmd10125(text);
            const auto& b=parsed.geometry.brep;
            const auto records=sourceSnapshot(b), json=smd::toJsonV1(parsed);
            const auto input=pipeline(b);
            check(b,input,{-750,-650,0},{0,0,6000},1,2925000000.0,1.0);
            require(sourceSnapshot(b)==records && smd::toJsonV1(parsed)==json,"complete parser/source preservation through pipeline");
        });
        test("declared +Z axis even though X is longest",[&] {
            const auto b=box(); check(b,pipeline(b),{0,0,0},{80,10,5},0,4000,1e-9);
        });
        test("unused vertices above and below body do not extend candidate",[&] {
            auto b=box(); b.verticesLocal.push_back({-900,-800,-700}); b.verticesLocal.push_back({900,800,700});
            check(b,pipeline(b),{0,0,0},{80,10,5},0,4000,1e-9);
        });
        test("recorded rotation/translation and transformed coordinates ignored",[&] {
            auto b=box(); b.transform=smd::Transform3x4{{0,0,1,100, 0,1,0,200, -1,0,0,300}};
            for (const auto& v:b.verticesLocal) b.verticesWorld.push_back({v.z+100,v.y+200,-v.x+300});
            b.faceNames={"misleading mould name"}; b.faces[0].name="not a cap";
            check(b,pipeline(b),{0,0,0},{80,10,5},0,4000,1e-9);
        });
        test("translated raw coordinates retain local bounds",[&] {
            auto b=box(); for(auto& v:b.verticesLocal) { v.x-=12; v.y+=3; v.z-=7; } refreshPlanes(b);
            check(b,pipeline(b),{-12,3,-7},{68,13,-2},0,4000,1e-9);
        });
        test("cap winding and start are independent of corrected solid orientation",[&] {
            auto b=box(); std::reverse(b.faces[0].indices.begin(),b.faces[0].indices.end());
            std::rotate(b.faces[0].indices.begin(),b.faces[0].indices.begin()+1,b.faces[0].indices.end());
            check(b,pipeline(b),{0,0,0},{80,10,5},0,4000,1e-9);
        });
        test("cap bounds, not whole-solid XY bounds, define candidate",[&] {
            auto b=prism({{0,0},{10,0},{10,10},{0,10}},20);
            for(std::size_t i=4;i<8;++i) { b.verticesLocal[i].x=2*b.verticesLocal[i].x-5; b.verticesLocal[i].y=2*b.verticesLocal[i].y-5; }
            refreshPlanes(b); check(b,pipeline(b),{0,0,0},{10,10,20},0,2000,1e-9);
        });
        test("missing minimum-Z face on valid pointed solid",[&] {
            smd::ParsedBrep b{}; b.verticesLocal={{0,0,0},{-10,-10,10},{10,-10,10},{10,10,10},{-10,10,10}};
            addFace(b,{1,2,3,4}); for(std::size_t i=1;i<=4;++i) addFace(b,{0,i%4+1,i});
            rejectValidSolid(b,"missing minimum-Z cap");
        });
        test("two triangular minimum-Z caps rejected before rectangle selection",[&] {
            auto b=box(); b.faces.erase(b.faces.begin()); addFace(b,{0,3,2}); addFace(b,{0,2,1});
            rejectValidSolid(b,"multiple minimum-Z cap faces: SourceFace[5] SourceFace[6]");
        });
        test("triangular cap rejected",[&] {
            rejectValidSolid(prism({{0,0},{10,0},{0,10}}),"nonrectangular minimum-Z cap");
        });
        test("trapezoid cap rejected",[&] {
            rejectValidSolid(prism({{0,0},{10,0},{8,10},{0,10}}),"nonrectangular minimum-Z cap");
        });
        test("rotated square is not fitted or rotated",[&] {
            rejectValidSolid(prism({{0,-10},{10,0},{0,10},{-10,0}}),"nonrectangular minimum-Z cap");
        });
        test("sub-tolerance XY skew is not snapped to rectangle",[&] {
            rejectValidSolid(prism({{0,0},{10,0},{10+1e-10,10},{0,10}}),"nonrectangular minimum-Z cap");
        });
        test("sub-tolerance Z slope is not snapped to minimum cap",[&] {
            auto b=box(); for(auto& v:b.verticesLocal) v.z+=v.x*1e-10; refreshPlanes(b);
            rejectValidSolid(b,"missing minimum-Z cap");
        });
        test("candidate mutations cannot alter source solid or records",[&] {
            const auto b=box(); const auto s=pipeline(b); const auto before=solidSnapshot(s), records=sourceSnapshot(b);
            auto candidate=check(b,s,{0,0,0},{80,10,5},0,4000,1e-9);
            ShapeMap vertices,faces,edges; TopExp::MapShapes(candidate.solid,TopAbs_VERTEX,vertices);
            TopExp::MapShapes(candidate.solid,TopAbs_FACE,faces); TopExp::MapShapes(candidate.solid,TopAbs_EDGE,edges);
            BRep_Builder builder; builder.UpdateVertex(TopoDS::Vertex(vertices(1)),gp_Pnt(1,2,3),1e-7);
            BRep_Tool::Surface(TopoDS::Face(faces(1)))->Translate(gp_Vec(1,2,3));
            double a,z; BRep_Tool::Curve(TopoDS::Edge(edges(1)),a,z)->Translate(gp_Vec(3,2,1));
            candidate.solid.Checked(!candidate.solid.Checked());
            require(solidSnapshot(s)==before && sourceSnapshot(b)==records,"candidate mutations are isolated");
            const auto detached=[] { const auto local=box(); return smd_geometry::buildPrincipalPrismCandidate(pipeline(local),local); }();
            require(BRepCheck_Analyzer(detached.solid,true,false,true).IsValid(),"candidate survives input destruction");
        });
        test("mismatched source and invalid mapping rejected separately",[&] {
            auto b=box(); auto s=pipeline(b); b.verticesLocal[0].z=-1;
            rejects(b,s,{"[input-correspondence]","SourceVertex[0]"});
            b=box(); std::swap(b.faces[0].indices[0],b.faces[0].indices[1]);
            rejects(b,s,{"[input-correspondence]","SourceFace[0]"});
            b=box(); s.boundary.loopEdges[0].pop_back();
            rejects(b,s,{"[input-correspondence]","SourceFace[0]"});
        });
        std::cout << checks << " GEO-03A checks passed\n"; return 0;
    } catch(const std::exception& e) {
        std::cerr << "After " << checks << " successful checks: " << e.what() << '\n'; return 1;
    }
}
