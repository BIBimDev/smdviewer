#include "smd_geometry/boolean_decomposition.hpp"
#include "smd/json.hpp"
#include "smd/parser.hpp"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <TopoDS_Compound.hxx>
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
std::string shapeSnapshot(const TopoDS_Shape& shape) {
    std::ostringstream out; BRepTools::Write(shape, out); flags(out, shape); return out.str();
}
std::string candidateSnapshot(const smd_geometry::PrincipalPrismCandidate& c) {
    std::ostringstream out; out << shapeSnapshot(c.solid) << c.supportingFaceOrdinal;
    for (auto p : {c.minimumLocalMm, c.maximumLocalMm, c.dimensionsMm})
        for (double x : {p.x,p.y,p.z}) out << ' ' << std::bit_cast<std::uint64_t>(x);
    return out.str();
}
void rejects(const std::function<void()>& action, const std::string& expected) {
    try { action(); } catch (const std::runtime_error& e) {
        require(std::string(e.what()).find("GEO-03B [") != std::string::npos &&
                std::string(e.what()).find(expected) != std::string::npos, std::string("unexpected diagnostic: ")+e.what());
        std::cout << "  rejected: " << e.what() << '\n'; return;
    }
    throw std::runtime_error("expected rejection: " + expected);
}
void independent(const TopoDS_Shape& output, const TopoDS_Shape& input) {
    ShapeMap a,b; TopExp::MapShapes(output,a); TopExp::MapShapes(input,b);
    for(int i=1;i<=a.Extent();++i) {
        require(!b.Contains(a(i)),"independent output topology");
        for(int j=1;j<=b.Extent();++j) {
            if(a(i).ShapeType()==TopAbs_FACE && b(j).ShapeType()==TopAbs_FACE)
                require(BRep_Tool::Surface(TopoDS::Face(a(i))) != BRep_Tool::Surface(TopoDS::Face(b(j))),"independent surfaces");
            if(a(i).ShapeType()==TopAbs_EDGE && b(j).ShapeType()==TopAbs_EDGE) {
                double x,y,z,w;
                require(BRep_Tool::Curve(TopoDS::Edge(a(i)),x,y) != BRep_Tool::Curve(TopoDS::Edge(b(j)),z,w),"independent curves");
            }
        }
    }
}
void lineage(const smd_geometry::BooleanSolid& part, std::size_t sourceCount) {
    ShapeMap actual; TopExp::MapShapes(part.solid,TopAbs_FACE,actual);
    require(actual.Extent()==static_cast<int>(part.faces.size()),"complete output face lineage table");
    ShapeMap seen;
    for(const auto& f:part.faces) {
        require(actual.Contains(f.face) && !seen.Contains(f.face),"unique actual output face"); seen.Add(f.face);
        require(!f.ancestors.empty(),"unknown provenance explicit");
        for(const auto& a:f.ancestors) {
            if(a.origin==smd_geometry::FaceOrigin::Source)
                require(a.sourceFaceOrdinal && *a.sourceFaceOrdinal<sourceCount && a.change!=smd_geometry::FaceChange::Generated,"source identity from history only");
            else require(!a.sourceFaceOrdinal,"candidate/generated/unknown face has no source ordinal");
        }
    }
}
} // namespace

int main(int argc,char** argv) {
    using namespace smd_geometry;
    int checks=0;
    const auto test=[&](const std::string& name,const std::function<void()>& action) {
        try { action(); } catch(const std::exception& e) { throw std::runtime_error(name+": "+e.what()); }
        std::cout<<"ok "<<++checks<<" - "<<name<<'\n';
    };
    try {
        require(argc==4,"usage: boolean-tests plain pocket corbel");
        for(int fixture=1;fixture<=3;++fixture) test(std::string("full unchanged pipeline: ")+argv[fixture],[&] {
            std::ifstream file(argv[fixture],std::ios::binary); require(file.good(),"fixture readable");
            const std::string text{std::istreambuf_iterator<char>(file),{}};
            const auto parsed=smd::parseSmd10125(text);
            const auto json=smd::toJsonV1(parsed), records=sourceSnapshot(parsed.geometry.brep);
            const auto s=pipeline(parsed.geometry.brep);
            const auto b=buildPrincipalPrismCandidate(s,parsed.geometry.brep);
            const auto before=solidSnapshot(s), candidateBefore=candidateSnapshot(b);
            auto result=decomposeBoolean(s,b);
            require(result.extensions.size()==(fixture==3?1u:0u),"extension connected-solid count");
            require(result.reductions.size()==(fixture==2?1u:0u),"reduction connected-solid count");
            if(fixture==2) require(std::abs(result.reductions[0].volumeMm3-30000000.0)<=1.0,"independent pocket volume");
            if(fixture==3) require(std::abs(result.extensions[0].volumeMm3-68625000.0)<=1.0,"independent corbel volume");
            const auto& v=result.verification;
            require(v.sourceMinusReconstructedSolids==0 && v.reconstructedMinusSourceSolids==0,"both directed residuals empty");
            require(v.sourceFacesCovered==s.boundary.faces.size() && v.reconstructedFacesCovered==result.reconstructed.faces.size(),"both full boundaries covered");
            const auto checkPart=[&](const BooleanSolid& part) {
                require(part.volumeMm3>0 && BRepCheck_Analyzer(BRepBuilderAPI_Copy(part.solid,true,false).Shape(),true,false,true).IsValid(),"valid positive-volume solid");
                independent(part.solid,s.solid); independent(part.solid,b.solid); lineage(part,s.boundary.faces.size());
            };
            checkPart(result.reconstructed);
            for(const auto& a:result.extensions) checkPart(a);
            for(const auto& r:result.reductions) checkPart(r);
            bool removed=false, modified=false, unchanged=false, candidate=false, source=false;
            for(const auto& h:result.history) {
                removed |= h.change==FaceChange::Removed; modified |= h.change==FaceChange::Modified;
                unchanged |= h.change==FaceChange::Unchanged;
                candidate |= h.ancestor.origin==FaceOrigin::Candidate;
                source |= h.ancestor.origin==FaceOrigin::Source;
                if(h.change==FaceChange::Removed) require(h.descendants.empty(),"removed history has no descendants");
                if(h.ancestor.origin!=FaceOrigin::Source) require(!h.ancestor.sourceFaceOrdinal,"candidate history has no source identity");
            }
            require(removed && candidate && source,"removed source and candidate history retained");
            if(fixture!=1) require(modified && unchanged,"modified/split and unchanged lineage exercised");
            if(fixture==1) for(const auto& f:result.reconstructed.faces) for(const auto& a:f.ancestors)
                require(a.origin==FaceOrigin::Candidate && !a.sourceFaceOrdinal,"coincident candidate faces never relabelled as source");
            require(solidSnapshot(s)==before && candidateSnapshot(b)==candidateBefore,"all input topology, geometry, flags and mappings unchanged");
            require(sourceSnapshot(parsed.geometry.brep)==records && smd::toJsonV1(parsed)==json,"all parser/source evidence preserved");
            std::cout<<std::setprecision(17)<<"  A="<<(result.extensions.empty()?0:result.extensions[0].volumeMm3)
                     <<" R="<<(result.reductions.empty()?0:result.reductions[0].volumeMm3)
                     <<" boundary="<<v.sourceFacesCovered<<'/'<<v.reconstructedFacesCovered<<" dV="<<v.volumeDifferenceMm3<<'\n';
        });
        const auto source=box(); const auto s=pipeline(source); const auto b=buildPrincipalPrismCandidate(s,source);
        test("null candidate fails, identical valid candidate is legitimately empty",[&] {
            auto broken=b; broken.solid.Nullify(); const auto before=solidSnapshot(s);
            rejects([&]{(void)decomposeBoolean(s,broken);},"input-candidate");
            auto empty=decomposeBoolean(s,b); require(empty.extensions.empty() && empty.reductions.empty(),"successful empty differences");
            require(solidSnapshot(s)==before,"failure and success preserve input");
        });
        test("incomplete and duplicate face correspondence rejected",[&] {
            auto broken=s; broken.boundary.faces.pop_back();
            rejects([&]{(void)decomposeBoolean(broken,b);},"input-correspondence");
            broken=s; broken.boundary.faces[1]=broken.boundary.faces[0]; const auto before=solidSnapshot(broken);
            rejects([&]{(void)decomposeBoolean(broken,b);},"SourceFace[1]");
            require(solidSnapshot(broken)==before,"failed correspondence preserved");
        });
        test("invalid solid is failure, never empty",[&] {
            auto broken=b; BRep_Builder builder; builder.MakeSolid(broken.solid);
            rejects([&]{(void)decomposeBoolean(s,broken);},"input-candidate");
        });
        test("displaced equal-volume reconstruction fails both residuals",[&] {
            gp_Trsf shift; shift.SetTranslation(gp_Vec(1,0,0));
            const auto moved=BRepBuilderAPI_Transform(b.solid,shift,true).Shape();
            const auto before=solidSnapshot(s), movedBefore=shapeSnapshot(moved);
            GProp_GProps props; BRepGProp::VolumeProperties(moved,props,true,false,false);
            require(std::abs(props.Mass()-s.volumeMm3)<1e-9,"equal volume control");
            rejects([&]{(void)verifyReconstruction(s,moved);},"S\\S'=1, S'\\S=1");
            require(solidSnapshot(s)==before && shapeSnapshot(moved)==movedBefore,"failed verification preserves inputs");
        });
        test("sub-1 mm3 residual is not discarded",[&] {
            gp_Trsf shift; shift.SetTranslation(gp_Vec(0.001,0,0));
            const auto moved=BRepBuilderAPI_Transform(b.solid,shift,true).Shape();
            // 10 * 5 * 0.001 = 0.05 mm3 in each residual, above linear tolerance.
            rejects([&]{(void)verifyReconstruction(s,moved);},"verify-residuals");
        });
        test("lower-dimensional and empty reconstructions are failures",[&] {
            rejects([&]{(void)verifyReconstruction(s,s.boundary.faces[0]);},"lower-dimensional");
            TopoDS_Compound empty; BRep_Builder builder; builder.MakeCompound(empty);
            rejects([&]{(void)verifyReconstruction(s,empty);},"exactly one");
            rejects([&]{(void)verifyReconstruction(s,TopoDS_Shape{});},"null operand");
        });
        test("equivalent boundary with split coplanar faces is accepted",[&] {
            auto split=box(); split.faces.erase(split.faces.begin()); addFace(split,{0,3,2}); addFace(split,{0,2,1});
            const auto alternative=pipeline(split);
            const auto v=verifyReconstruction(s,alternative.solid);
            require(v.sourceFacesCovered==6 && v.reconstructedFacesCovered==7,"six versus seven bounded faces covered");
        });
        test("two separate extensions are returned as two connected solids",[&] {
            // Synthetic prism with a narrower candidate spanning the same Z range.
            auto narrower=b; narrower.solid=BRepPrimAPI_MakeBox(gp_Pnt(20,0,0),40,10,5).Solid();
            auto result=decomposeBoolean(s,narrower);
            require(result.extensions.size()==2 && result.reductions.empty(),"two extension components");
            for(const auto& part:result.extensions) require(std::abs(part.volumeMm3-1000)<1e-9,"component volume");
        });
        test("hollow extension is one connected solid with a cavity",[&] {
            auto interior=b; interior.solid=BRepPrimAPI_MakeBox(gp_Pnt(20,2,1),40,6,3).Solid();
            const auto result=decomposeBoolean(s,interior);
            require(result.extensions.size()==1 && result.reductions.empty(),"one hollow extension");
            require(std::abs(result.extensions[0].volumeMm3-3280)<1e-9,"hollow extension volume");
        });
        test("empty B minus R reconstructs explicitly from A",[&] {
            auto disjoint=b; disjoint.solid=BRepPrimAPI_MakeBox(gp_Pnt(100,0,0),80,10,5).Solid();
            const auto result=decomposeBoolean(s,disjoint);
            require(result.extensions.size()==1 && result.reductions.size()==1,"disjoint A and R");
            require(std::abs(result.reconstructed.volumeMm3-4000)<1e-9,"reconstruction from actual A");
        });
        test("source ordinal follows reordered input mapping, not kernel face order",[&] {
            auto reordered=box(); std::rotate(reordered.faces.begin(),reordered.faces.begin()+3,reordered.faces.end());
            const auto input=pipeline(reordered); auto candidate=buildPrincipalPrismCandidate(input,reordered);
            candidate.solid=BRepPrimAPI_MakeBox(gp_Pnt(20,0,0),40,10,5).Solid();
            const auto result=decomposeBoolean(input,candidate);
            std::vector<bool> seen(reordered.faces.size(),false);
            for(const auto& part:result.extensions) for(const auto& f:part.faces) for(const auto& a:f.ancestors)
                if(a.origin==FaceOrigin::Source) {
                    require(a.sourceFaceOrdinal.has_value(),"mapped source ordinal");
                    const auto index=*a.sourceFaceOrdinal;
                    seen.at(index)=true;
                    // Independent test oracle checks the claimed ancestral support.
                    // Production lineage uses identities/copy/history, never this test.
                    const auto& plane=*reordered.faces[index].plane;
                    ShapeMap vertices; TopExp::MapShapes(f.face,TopAbs_VERTEX,vertices);
                    for(int i=1;i<=vertices.Extent();++i) {
                        const auto p=BRep_Tool::Pnt(TopoDS::Vertex(vertices(i)));
                        require(std::abs(plane.a*p.X()+plane.b*p.Y()+plane.c*p.Z()+plane.d)<1e-7,"claimed source face geometry");
                    }
                }
            require(std::all_of(seen.begin(),seen.end(),[](bool v){return v;}),"all six reordered source ordinals exercised");
        });
        test("result survives input lifetime and mutation cannot reach inputs",[&] {
            const auto before=solidSnapshot(s), candidateBefore=candidateSnapshot(b);
            auto result=[&]{const auto local=pipeline(box()); const auto candidate=buildPrincipalPrismCandidate(local,box()); return decomposeBoolean(local,candidate);}();
            require(BRepCheck_Analyzer(result.reconstructed.solid,true,false,true).IsValid(),"result owns its lifetime");
            auto result2=decomposeBoolean(s,b); ShapeMap vertices; TopExp::MapShapes(result2.reconstructed.solid,TopAbs_VERTEX,vertices);
            BRep_Builder builder; builder.UpdateVertex(TopoDS::Vertex(vertices(1)),gp_Pnt(999,888,777),1e-7);
            BRep_Tool::Surface(result2.reconstructed.faces[0].face)->Translate(gp_Vec(1,2,3));
            require(solidSnapshot(s)==before && candidateSnapshot(b)==candidateBefore,"mutation isolated from both inputs");
        });
        std::cout<<checks<<" GEO-03B checks passed\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
