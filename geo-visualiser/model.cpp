#include "model.hpp"
#include "smd/parser.hpp"
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Wire.hxx>
#include <Standard_Failure.hxx>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace inspector {
const char* name(Representation r) {
    switch(r) {
    case Representation::Faces: return "Converted faces (not a solid)";
    case Representation::Solid: return "Validated solid S";
    case Representation::Candidate: return "Principal-prism candidate B";
    case Representation::Extensions: return "Extensions A = S minus B";
    case Representation::Reductions: return "Reductions R = B minus S (missing material)";
    }
    return "Unknown";
}
const char* name(Kind k) {
    switch(k) { case Kind::Vertex: return "Vertex"; case Kind::Edge: return "Edge";
    case Kind::Face: return "Face"; case Kind::Body: return "Body"; }
    return "Unknown";
}
std::shared_ptr<const Document> buildDocument(std::string specimen, smd::ParsedSmd parsed) {
    auto d = std::make_shared<Document>();
    d->specimen = std::move(specimen);
    d->parsed = std::move(parsed);
    std::string stage = "convertFaces";
    try {
        const auto& b = d->parsed.geometry.brep;
        d->converted = smd_geometry::convertFaces(b);
        stage = "assembleShell";
        d->shell = smd_geometry::assembleShell(b, d->converted);
        stage = "buildSolid";
        d->source = smd_geometry::buildSolid(d->shell);
        stage = "buildPrincipalPrismCandidate";
        d->candidate = smd_geometry::buildPrincipalPrismCandidate(d->source, b);
        stage = "decomposeBoolean";
        d->decomposition = smd_geometry::decomposeBoolean(d->source, d->candidate);
    } catch(const Standard_Failure& e) {
        throw std::runtime_error(stage + ": " + e.what());
    } catch(const std::exception& e) {
        throw std::runtime_error(stage + ": " + e.what());
    }
    return d; // Publish only a complete, validated pipeline.
}
std::shared_ptr<const Document> loadDocument(std::size_t specimen, const std::filesystem::path& directory) {
    if(specimen >= std::size(specimens)) throw std::runtime_error("Unknown specimen index");
    const auto path = directory / specimens[specimen].filename;
    std::ifstream input(path, std::ios::binary);
    if(!input) throw std::runtime_error("Cannot read original fixture: " + path.string());
    const std::string text{std::istreambuf_iterator<char>(input), {}};
    if(input.bad()) throw std::runtime_error("Failed reading fixture: " + path.string());
    try { return buildDocument(specimens[specimen].key, smd::parseSmd10125(text)); }
    catch(const std::exception& e) { throw std::runtime_error(path.string() + ": " + e.what()); }
}
namespace {
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
const char* key(Representation r) {
    switch(r) { case Representation::Faces: return "converted"; case Representation::Solid: return "S";
    case Representation::Candidate: return "B"; case Representation::Extensions: return "A";
    case Representation::Reductions: return "R"; }
    return "unknown";
}
Component copyComponent(const Document& d, Representation r, std::size_t index,
                        const TopoDS_Shape& original, const smd_geometry::BooleanSolid* booleanBody) {
    Component c;
    c.id = d.specimen + "/" + key(r) + "/" + std::to_string(index);
    c.label = r == Representation::Faces ? "Source face " + std::to_string(index) : "Body " + std::to_string(index);
    c.original = original;
    BRepBuilderAPI_Copy copier(original, true, false); // Private surfaces/curves; no shared meshes.
    c.display = copier.Shape();
    if(c.display.IsNull()) throw std::runtime_error("Display copy failed: " + c.id);
    for(const auto kind : {Kind::Body, Kind::Vertex, Kind::Edge, Kind::Face}) {
        if(kind == Kind::Body && r == Representation::Faces) continue;
        const auto type = kind == Kind::Body ? TopAbs_SOLID : kind == Kind::Vertex ? TopAbs_VERTEX :
                          kind == Kind::Edge ? TopAbs_EDGE : TopAbs_FACE;
        ShapeMap shapes;
        TopExp::MapShapes(original, type, shapes);
        for(int i=1; i<=shapes.Extent(); ++i) {
            Entity e{};
            e.kind = kind;
            e.id = c.id + "/" + name(kind) + "/" + std::to_string(i-1);
            e.original = shapes(i);
            e.display = copier.ModifiedShape(e.original);
            if(e.display.IsNull() || e.display.IsSame(e.original)) throw std::runtime_error("Missing private copy correspondence: " + e.id);
            if(r == Representation::Faces) {
                if(kind == Kind::Face) e.sourceFace = index;
                if(kind == Kind::Vertex) for(std::size_t v=0; v<d.converted.vertices.size(); ++v)
                    if(e.original.IsSame(d.converted.vertices[v])) e.sourceVertex = v;
                if(kind == Kind::Edge) {
                    std::size_t position = 0;
                    const auto& face = d.converted.faces.at(index);
                    const auto& loop = d.parsed.geometry.brep.faces.at(index).indices;
                    for(BRepTools_WireExplorer it(BRepTools::OuterWire(face), face); it.More(); it.Next(), ++position)
                        if(e.original.IsSame(it.Current())) {
                            const auto a = loop.at(position), b = loop.at((position+1)%loop.size());
                            e.sourceEndpoints = std::minmax(a,b);
                            e.edgeUses.push_back({index, position, a < b});
                        }
                }
            } else if(r == Representation::Solid) {
                const auto& b = d.source.boundary;
                if(kind == Kind::Face) for(std::size_t f=0; f<b.faces.size(); ++f)
                    if(e.original.IsSame(b.faces[f])) e.sourceFace = f;
                if(kind == Kind::Vertex) for(std::size_t v=0; v<b.vertices.size(); ++v)
                    if(!b.vertices[v].IsNull() && e.original.IsSame(b.vertices[v])) e.sourceVertex = v;
                if(kind == Kind::Edge) for(const auto& edge : b.edges) if(e.original.IsSame(edge.edge)) {
                    e.sourceEndpoints = {edge.firstVertex, edge.secondVertex};
                    e.edgeUses.assign(edge.uses.begin(), edge.uses.end());
                }
            } else if(booleanBody && kind == Kind::Face) {
                for(const auto& face : booleanBody->faces)
                    if(e.original.IsSame(face.face)) e.ancestors = face.ancestors;
            }
            c.entities.push_back(std::move(e));
        }
    }
    return c;
}
const char* origin(smd_geometry::FaceOrigin o) {
    switch(o) { case smd_geometry::FaceOrigin::Source: return "source";
    case smd_geometry::FaceOrigin::Candidate: return "candidate"; default: return "unknown"; }
}
const char* change(smd_geometry::FaceChange c) {
    using C = smd_geometry::FaceChange;
    switch(c) { case C::Unchanged: return "unchanged"; case C::Modified: return "modified";
    case C::Generated: return "generated"; case C::Removed: return "removed"; default: return "unknown"; }
}
void sourceFace(std::ostream& out, const smd::ParsedBrep& b, std::size_t ordinal) {
    const auto& f = b.faces.at(ordinal);
    out << "Source face ordinal: " << ordinal << "\nName: " << (f.name ? *f.name : "unknown") << "\nFlag: " << f.flag << "\nOrdered source loop:";
    for(auto v : f.indices) out << ' ' << v;
    out << '\n';
    if(f.plane) out << "Source plane (a b c d): " << f.plane->a << ' ' << f.plane->b << ' ' << f.plane->c << ' ' << f.plane->d << '\n';
    else out << "Source plane: unknown\n";
}
}
Display prepareDisplay(const Document& d, Representation r) {
    Display result{r, {}};
    if(r == Representation::Faces) for(std::size_t i=0; i<d.converted.faces.size(); ++i)
        result.components.push_back(copyComponent(d,r,i,d.converted.faces[i],nullptr));
    else if(r == Representation::Solid) result.components.push_back(copyComponent(d,r,0,d.source.solid,nullptr));
    else if(r == Representation::Candidate) result.components.push_back(copyComponent(d,r,0,d.candidate.solid,nullptr));
    else {
        const auto& bodies = r == Representation::Extensions ? d.decomposition.extensions : d.decomposition.reductions;
        for(std::size_t i=0; i<bodies.size(); ++i) result.components.push_back(copyComponent(d,r,i,bodies[i].solid,&bodies[i]));
    }
    return result;
}
std::string describe(const Document& d, const Entity& e) {
    std::ostringstream out;
    out << std::setprecision(17) << "Viewer ID (session scope): " << e.id << "\nEntity: " << name(e.kind)
        << "\nCoordinates: raw source-local millimetres\nSource ordinals: zero-based\n";
    if(e.sourceFace) sourceFace(out,d.parsed.geometry.brep,*e.sourceFace);
    if(e.sourceVertex) out << "Source vertex ordinal: " << *e.sourceVertex << '\n';
    if(e.kind == Kind::Vertex) {
        const auto p = BRep_Tool::Pnt(TopoDS::Vertex(e.original));
        out << "XYZ (mm): " << p.X() << ' ' << p.Y() << ' ' << p.Z() << '\n';
    }
    if(e.sourceEndpoints) out << "Source edge endpoints: " << e.sourceEndpoints->first << ' ' << e.sourceEndpoints->second << '\n';
    for(const auto& u : e.edgeUses) out << "Edge use: source face " << u.faceOrdinal << ", loop position " << u.loopPosition
        << ", " << (u.forward ? "increasing" : "decreasing") << " source index direction\n";
    if(!e.sourceFace && !e.sourceVertex && !e.sourceEndpoints) out << "Direct source identity: not established\n";
    if(e.id.find("/B/") != std::string::npos) out << "Derived candidate. Supporting source face " << d.candidate.supportingFaceOrdinal
        << " is candidate-level evidence only; it is NOT this entity's source identity.\n";
    for(const auto& a : e.ancestors) {
        out << "Boolean ancestor: " << origin(a.origin) << ", " << change(a.change);
        if(a.sourceFaceOrdinal) out << ", source face " << *a.sourceFaceOrdinal;
        out << '\n';
        if(a.sourceFaceOrdinal) sourceFace(out,d.parsed.geometry.brep,*a.sourceFaceOrdinal);
    }
    if(e.id.find("/R/") != std::string::npos) out << "Reduction: positive display body representing missing material.\n";
    out << "Source B-rep vertex-count token index: " << d.parsed.geometry.brep.vertexCountTokenIndex << '\n';
    return out.str();
}
void Tags::set(const std::string& id, const std::string& value) {
    if(value.empty()) values_.erase(id); else values_[id] = value;
}
std::string Tags::get(const std::string& id) const {
    const auto it = values_.find(id); return it == values_.end() ? "" : it->second;
}
} // namespace inspector
