#include "smd_geometry/export.hpp"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <Poly_Triangulation.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <gp_Trsf.hxx>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using smd_geometry::ExportBodies;
using smd_geometry::exportBrep;
using smd_geometry::importBrep;
using Json = nlohmann::ordered_json;
namespace fs = std::filesystem;

void require(bool condition, std::string_view reason) {
    if (!condition) {
        throw std::runtime_error(std::string(reason));
    }
}

template<class Operation>
void rejects(Operation operation, const std::string& field) {
    try {
        operation();
    } catch (const std::runtime_error& error) {
        require(std::string_view(error.what()).starts_with("GEO-EXP-01 [" + field + "]"),
                "expected error for " + field + ", got " + error.what());
        return;
    }
    throw std::runtime_error("expected rejection for " + field);
}

struct TestDirectory {
    fs::path path = fs::temp_directory_path() /
        ("smd-geo-export-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    TestDirectory() { require(fs::create_directory(path), "create test directory"); }
    ~TestDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

std::string brep(const TopoDS_Shape& shape, bool mesh = false,
                 TopTools_FormatVersion version = TopTools_FormatVersion_VERSION_3) {
    std::ostringstream stream;
    BRepTools::Write(shape, stream, mesh, mesh, version);
    return stream.str();
}

void put(const fs::path& path, std::string_view data) {
    std::ofstream stream(path, std::ios::binary);
    stream << data;
    stream.close();
    require(static_cast<bool>(stream), "write test input");
}

Json load(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return Json::parse(stream);
}

std::array<double, 6> bounds(const TopoDS_Solid& solid) {
    Bnd_Box box;
    BRepBndLib::AddOptimal(solid, box, false, false);
    std::array<double, 6> values;
    box.Get(values[0], values[1], values[2], values[3], values[4], values[5]);
    return values;
}

void verifyBox(const TopoDS_Solid& solid, double volume, const std::array<double, 6>& expectedBounds) {
    require(BRepCheck_Analyzer(solid).IsValid(), "imported box passes OCCT validation");
    GProp_GProps props;
    BRepGProp::VolumeProperties(solid, props);
    require(std::abs(props.Mass() - volume) < 1e-8, "box volume in mm³");
    const auto actualBounds = bounds(solid);
    for (std::size_t i = 0; i < actualBounds.size(); ++i) {
        require(std::abs(actualBounds[i] - expectedBounds[i]) < 1e-8, "source-local bounds in mm");
    }
    const std::array shapeCounts = {std::pair{TopAbs_SHELL, 1}, std::pair{TopAbs_FACE, 6},
        std::pair{TopAbs_WIRE, 6}, std::pair{TopAbs_EDGE, 12}, std::pair{TopAbs_VERTEX, 8}};
    for (const auto& [type, count] : shapeCounts) {
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> shapes;
        TopExp::MapShapes(solid, type, shapes);
        require(shapes.Extent() == count, "shared box topology");
    }
    for (TopExp_Explorer it(solid, TopAbs_FACE); it.More(); it.Next()) {
        TopLoc_Location location;
        require(BRep_Tool::Triangulation(TopoDS::Face(it.Current()), location).IsNull(),
                "triangulation excluded from imported face");
    }
}

void testExport(const fs::path& directory, ExportBodies bodies) {
    const auto path = directory / "bodies.json";
    const auto mesh = new Poly_Triangulation(3, 1, false, true);
    mesh->SetNode(1, gp_Pnt(0, 0, 0));
    mesh->SetNode(2, gp_Pnt(0, 0, 1));
    mesh->SetNode(3, gp_Pnt(0, 1, 1));
    mesh->SetTriangle(1, Poly_Triangle(1, 2, 3));
    for (int node = 1; node <= 3; ++node) {
        mesh->SetNormal(node, gp_Dir(-1, 0, 0));
    }
    const auto face = TopoDS::Face(TopExp_Explorer(bodies.S, TopAbs_FACE).Current());
    BRep_Builder{}.UpdateFace(face, mesh);
    const auto beforeS = brep(bodies.S, true);
    const auto beforeB = brep(bodies.B, true);
    require(beforeS != brep(bodies.S), "source solid has serializable mesh data");

    exportBrep(bodies, path);
    require(std::distance(fs::directory_iterator(directory), fs::directory_iterator{}) == 1,
            "only one output file");
    const auto document = load(path);
    require(document.size() == 5 && document.at("head").size() == 5, "only v1 container fields");
    require(document.at("head") == Json{{"format", "smdviewer.geo-export"}, {"version", 1},
        {"coordinates", "source-local"}, {"units", "mm"}, {"brepVersion", 3}}, "canonical header");
    require(document.at("S") == brep(bodies.S) && document.at("B") == brep(bodies.B),
            "JSON preserves native BREP bytes with mesh excluded");
    require(document.at("A") == Json::array() && document.at("R") == Json::array(), "empty arrays");
    const auto imported = importBrep(path);
    require(imported.A.empty() && imported.R.empty(), "empty collections return empty");
    verifyBox(imported.S, 105, {-2, 3, 5, 1, 8, 12});
    verifyBox(imported.B, 70, {-2, 3, 5, 0, 8, 12});
    require(brep(bodies.S, true) == beforeS && brep(bodies.B, true) == beforeB,
            "source solids, flags and mesh were preserved");
    TopLoc_Location location;
    require(BRep_Tool::Triangulation(face, location).get() == mesh && mesh->HasNormals(),
            "original mesh identity and normals were preserved");

    const auto extension = BRepPrimAPI_MakeBox(gp_Pnt(0, 3, 5), 1, 5, 7).Solid();
    bodies.A = {extension};
    rejects([&] { exportBrep(bodies, path); }, "output");
    require(load(path) == document, "existing export preserved");
    const auto extended = directory / "extension.json";
    exportBrep(bodies, extended);
    const auto withExtension = importBrep(extended);
    require(withExtension.A.size() == 1 && withExtension.R.empty(), "one addition preserved");
    verifyBox(withExtension.A.front(), 35, {0, 3, 5, 1, 8, 12});

    const auto reduced = directory / "reduction.json";
    exportBrep({bodies.B, bodies.S, {}, {extension}}, reduced);
    const auto withReduction = importBrep(reduced);
    require(withReduction.A.empty() && withReduction.R.size() == 1, "one reduction preserved");
    verifyBox(withReduction.R.front(), 35, {0, 3, 5, 1, 8, 12});
}

void testRejections(const fs::path& directory, const ExportBodies& bodies) {
    const auto path = directory / "invalid.json";
    const auto valid = load(directory / "bodies.json");
    const auto rejectDocument = [&](const Json& invalid, const std::string& field) {
        put(path, invalid.dump());
        rejects([&] { importBrep(path); }, field);
    };
    const auto rejectField = [&](const Json::json_pointer& pointer, const Json& value, const std::string& field) {
        auto invalid = valid;
        invalid.at(pointer) = value;
        rejectDocument(invalid, field);
    };

    put(path, "{ malformed");
    rejects([&] { importBrep(path); }, "JSON");
    put(path, valid.dump() + " []");
    rejects([&] { importBrep(path); }, "JSON");
    put(path, valid.dump() + '\0');
    rejects([&] { importBrep(path); }, "JSON");
    rejectDocument(Json::array(), "JSON");
    rejectField("/head"_json_pointer, Json::array(), "head");
    auto missing = valid;
    missing["head"].erase("format");
    rejectDocument(missing, "head.format");
    missing = valid;
    missing.erase("R");
    rejectDocument(missing, "R");
    rejectField("/head/format"_json_pointer, "other", "head.format");
    rejectField("/head/version"_json_pointer, 2, "head.version");
    rejectField("/head/version"_json_pointer, 1.0, "head.version");
    rejectField("/head/coordinates"_json_pointer, "project-global", "head.coordinates");
    rejectField("/head/units"_json_pointer, "in", "head.units");
    rejectField("/head/brepVersion"_json_pointer, 2, "head.brepVersion");
    rejectField("/head/brepVersion"_json_pointer, "3", "head.brepVersion");
    rejectField("/S"_json_pointer, 1, "S");
    rejectField("/B"_json_pointer, "", "B");
    rejectField("/A"_json_pointer, Json::object(), "A");
    rejectField("/R"_json_pointer, Json::array({false}), "R[0]");

    const auto ascii = valid.at("S").get<std::string>();
    rejectField("/S"_json_pointer, "invalid", "S");
    rejectField("/S"_json_pointer, ascii.substr(0, ascii.size() / 2), "S");
    rejectField("/S"_json_pointer, ascii.substr(0, ascii.find_last_of('\n') + 1), "S");
    rejectField("/S"_json_pointer, ascii + ascii, "S");
    rejectField("/S"_json_pointer, brep(bodies.S, false, TopTools_FormatVersion_VERSION_2), "S");
    rejectField("/S"_json_pointer, brep(TopExp_Explorer(bodies.S, TopAbs_FACE).Current()), "S");
    rejectField("/A"_json_pointer, Json::array({brep(TopoDS_Shape{})}), "A[0]");
    TopoDS_Solid empty;
    BRep_Builder{}.MakeSolid(empty);
    rejectField("/S"_json_pointer, brep(empty), "S");

    const auto missingFile = directory / "missing.json";
    rejects([&] { importBrep(missingFile); }, "input");
    rejects([&] { importBrep(directory); }, "input");
    rejects([&] { exportBrep(bodies, directory); }, "output");
    rejects([&] { exportBrep(bodies, directory / "missing" / "output.json"); }, "output");
    rejects([&] { exportBrep({{}, bodies.B, {}, {}}, missingFile); }, "S");
    rejects([&] { exportBrep({bodies.S, bodies.B, {}, {empty}}, missingFile); }, "R[0]");
    BRep_Builder builder;
    TopoDS_Face surfaceLessFace;
    builder.MakeFace(surfaceLessFace);
    TopoDS_Shell shell;
    builder.MakeShell(shell);
    builder.Add(shell, surfaceLessFace);
    builder.Add(empty, shell);
    rejects([&] { exportBrep({empty, bodies.B, {}, {}}, missingFile); }, "S");
    require(!fs::exists(missingFile), "invalid shapes do not create files");
}
} // namespace

int main() {
    try {
        const TestDirectory directory;
        const auto S = BRepPrimAPI_MakeBox(gp_Pnt(-2, 3, 5), 3, 5, 7).Solid();
        auto B = BRepPrimAPI_MakeBox(2, 5, 7).Solid();
        gp_Trsf translation;
        translation.SetTranslation(gp_Vec(-2, 3, 5));
        B.Location(TopLoc_Location(translation));
        const ExportBodies bodies{S, B, {}, {}};
        testExport(directory.path, bodies);
        testRejections(directory.path, bodies);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
