#pragma once
#include "smd_geometry/boolean_decomposition.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <string>

namespace inspector {
enum class Representation { Faces, Solid, Candidate, Extensions, Reductions };
enum class Kind { Vertex, Edge, Face, Body };
struct Specimen { const char* key; const char* label; const char* filename; };
inline constexpr Specimen specimens[] = {
    {"plain", "Plain column", "plain_column.smd"},
    {"combined", "Combined pocket/corbel column", "SinglePocketingSingleCorbelColumn.smd"}
};
const char* name(Representation);
const char* name(Kind);

// Owned pipeline evidence. The UI retains this through a const shared pointer.
struct Document {
    std::string specimen;
    smd::ParsedSmd parsed;
    smd_geometry::FaceConversion converted;
    smd_geometry::ShellAssembly shell;
    smd_geometry::SolidAssembly source;
    smd_geometry::PrincipalPrismCandidate candidate;
    smd_geometry::BooleanDecomposition decomposition;
};
std::shared_ptr<const Document> buildDocument(std::string specimen, smd::ParsedSmd parsed);
std::shared_ptr<const Document> loadDocument(std::size_t specimen, const std::filesystem::path& directory);

struct Entity {
    std::string id;
    Kind kind;
    TopoDS_Shape original, display;
    std::optional<std::size_t> sourceFace, sourceVertex;
    std::optional<std::pair<std::size_t, std::size_t>> sourceEndpoints;
    std::vector<smd_geometry::SourceEdgeUse> edgeUses;
    std::vector<smd_geometry::FaceAncestor> ancestors;
};
struct Component {
    std::string id, label;
    TopoDS_Shape original, display;
    std::vector<Entity> entities;
};
struct Display {
    Representation representation;
    std::vector<Component> components;
};
// No meshing of original shapes. Copy history establishes identity, never proximity.
Display prepareDisplay(const Document&, Representation);
std::string describe(const Document&, const Entity&);
// Session-only and independent of all geometry/source identities.
class Tags {
public:
    void set(const std::string& id, const std::string& value);
    std::string get(const std::string& id) const;
private:
    std::map<std::string, std::string> values_;
};
} // namespace inspector
