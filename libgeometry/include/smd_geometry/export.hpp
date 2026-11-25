#pragma once

#include <TopoDS_Solid.hxx>
#include <filesystem>
#include <vector>

namespace smd_geometry {
struct ExportBodies {
    TopoDS_Solid S;
    TopoDS_Solid B;
    std::vector<TopoDS_Solid> A;
    std::vector<TopoDS_Solid> R;
};

// Version 1 JSON containing native ASCII BREP bodies in source-local mm.
// Supply solids with geometric surfaces. Export preserves the inputs and
// requires a new destination file. Both functions report errors via std::runtime_error.
void exportBrep(const ExportBodies& bodies, const std::filesystem::path& filename);
ExportBodies importBrep(const std::filesystem::path& filename);
} // namespace smd_geometry
