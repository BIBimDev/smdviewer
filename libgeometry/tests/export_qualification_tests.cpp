#include "smd_geometry/boolean_decomposition.hpp"
#include "smd_geometry/export.hpp"
#include "smd/parser.hpp"

#include <BOPAlgo_CheckerSI.hxx>
#include <BOPDS_DS.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Iterator.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace smd_geometry;
namespace fs = std::filesystem;
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

void require(bool condition, std::string_view reason) {
    if (!condition) throw std::runtime_error(std::string(reason));
}

class TestDirectory {
    static fs::path create() {
        const auto prefix = "smd-geo-exp-02-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto root = fs::temp_directory_path();
        for (int attempt = 0; attempt < 10; ++attempt) {
            auto candidate = root / (prefix + "-" + std::to_string(attempt));
            if (fs::create_directory(candidate)) return candidate;
        }
        throw std::runtime_error("cannot reserve qualification directory");
    }

public:
    const fs::path path = create();

    TestDirectory() = default;
    TestDirectory(const TestDirectory&) = delete;
    TestDirectory& operator=(const TestDirectory&) = delete;
    ~TestDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    void verifyCleanup() const {
        require(fs::remove_all(path) > 0, "remove qualification directory and export");
    }
};

ShapeMap shapes(const TopoDS_Shape& shape, TopAbs_ShapeEnum type) {
    ShapeMap result;
    TopExp::MapShapes(shape, type, result);
    return result;
}

TopoDS_Shape copy(const TopoDS_Shape& shape) {
    BRepBuilderAPI_Copy copier(shape, true, false);
    require(copier.IsDone() && !copier.Shape().IsNull(), "deep copy failed");
    return copier.Shape();
}

double volume(const TopoDS_Solid& solid) {
    GProp_GProps properties;
    BRepGProp::VolumeProperties(solid, properties, true, false, false);
    require(std::isfinite(properties.Mass()) && properties.Mass() > 0, "expected finite positive volume");
    return properties.Mass();
}

void checkConnectedShell(const TopoDS_Shape& shell) {
    const auto faces = shapes(shell, TopAbs_FACE);
    require(!faces.IsEmpty(), "shell has no boundary faces");
    NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher> edgeFaces;
    TopExp::MapShapesAndAncestors(shell, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    for (int i = 1; i <= edgeFaces.Extent(); ++i) {
        const auto& uses = edgeFaces.FindFromIndex(i);
        require(uses.Extent() == 2 && !uses.First().IsSame(uses.Last()),
                "expected each shell edge to be shared by two distinct faces");
    }

    ShapeMap reached;
    reached.Add(faces(1));
    for (int i = 1; i <= reached.Extent(); ++i) {
        for (TopExp_Explorer edges(reached(i), TopAbs_EDGE); edges.More(); edges.Next()) {
            for (const auto& face : edgeFaces.FindFromKey(edges.Current())) reached.Add(face);
        }
    }
    require(reached.Extent() == faces.Extent(), "shell is disconnected");
}

double checkSolid(const TopoDS_Solid& solid) {
    require(!solid.IsNull() && solid.ShapeType() == TopAbs_SOLID, "expected one non-null OCCT solid");
    require(BRepCheck_Analyzer(copy(solid), true, false, true).IsValid(), "invalid OCCT solid");

    BOPAlgo_CheckerSI checker;
    checker.SetLevelOfCheck(5);
    checker.SetNonDestructive(true);
    checker.SetRunParallel(false);
    checker.SetFuzzyValue(booleanToleranceMm);
    checker.AddArgument(copy(solid));
    checker.Perform();
    if (checker.HasErrors() || checker.HasWarnings() || !checker.PDS()) {
        std::ostringstream report;
        checker.DumpErrors(report);
        checker.DumpWarnings(report);
        throw std::runtime_error("solid interference check failed: " + report.str());
    }
    require(checker.DS().Interferences().IsEmpty(), "solid boundary self-interference");

    // GEO-03 permits one outward shell and internal cavity shells.
    TopoDS_Solid outer;
    std::vector<TopoDS_Solid> cavities;
    for (TopoDS_Iterator shell(solid); shell.More(); shell.Next()) {
        require(shell.Value().ShapeType() == TopAbs_SHELL && BRep_Tool::IsClosed(shell.Value()),
                "expected closed shell");
        checkConnectedShell(shell.Value());
        auto enclosed = BRepBuilderAPI_MakeSolid(TopoDS::Shell(copy(shell.Value()))).Solid();
        BRepClass3d_SolidClassifier classifier(enclosed);
        classifier.PerformInfinitePoint(booleanToleranceMm);
        if (classifier.State() == TopAbs_OUT) {
            require(outer.IsNull(), "multiple outward shells");
            outer = enclosed;
        } else {
            require(classifier.State() == TopAbs_IN, "indeterminate shell orientation");
            enclosed.Reverse();
            cavities.push_back(enclosed);
        }
    }
    require(!outer.IsNull(), "missing outward shell");
    for (const auto& cavity : cavities) {
        const auto vertices = shapes(cavity, TopAbs_VERTEX);
        require(!vertices.IsEmpty(), "cavity has no vertices");
        const auto point = BRep_Tool::Pnt(TopoDS::Vertex(vertices(1)));
        require(BRepClass3d_SolidClassifier(outer, point, booleanToleranceMm).State() == TopAbs_IN,
                "cavity lies outside outer shell");
        for (const auto& other : cavities) {
            if (!cavity.IsSame(other)) {
                require(BRepClass3d_SolidClassifier(other, point, booleanToleranceMm).State() == TopAbs_OUT,
                        "nested or overlapping cavities");
            }
        }
    }
    BRepClass3d_SolidClassifier classifier(solid);
    classifier.PerformInfinitePoint(booleanToleranceMm);
    require(classifier.State() == TopAbs_OUT, "solid infinity is not OUT");
    return volume(solid);
}

using Loop = std::vector<gp_Pnt>;
using Face = std::vector<Loop>;
struct ShellReference {
    std::array<int, 4> topology;
    std::vector<Face> faces;
};
struct BodyReference {
    double volume;
    std::array<double, 6> bounds;
    std::vector<ShellReference> shells;
};

BodyReference measure(const TopoDS_Solid& solid, double measuredVolume) {
    BodyReference result{measuredVolume, {}, {}};
    Bnd_Box box;
    BRepBndLib::AddOptimal(solid, box, false, false);
    auto& b = result.bounds;
    box.Get(b[0], b[1], b[2], b[3], b[4], b[5]);
    for (TopoDS_Iterator shell(solid); shell.More(); shell.Next()) {
        const auto faceMap = shapes(shell.Value(), TopAbs_FACE);
        ShellReference reference{{faceMap.Extent(), shapes(shell.Value(), TopAbs_WIRE).Extent(),
            shapes(shell.Value(), TopAbs_EDGE).Extent(), shapes(shell.Value(), TopAbs_VERTEX).Extent()}, {}};
        for (const auto& shape : faceMap) {
            const auto& face = TopoDS::Face(shape);
            require(BRepAdaptor_Surface(face).GetType() == GeomAbs_Plane, "expected a planar face");
            Face loops;
            for (TopExp_Explorer wires(face, TopAbs_WIRE); wires.More(); wires.Next()) {
                Loop loop;
                for (BRepTools_WireExplorer edges(TopoDS::Wire(wires.Current()), face); edges.More(); edges.Next()) {
                    require(BRepAdaptor_Curve(edges.Current()).GetType() == GeomAbs_Line, "expected a straight edge");
                    loop.push_back(BRep_Tool::Pnt(edges.CurrentVertex()));
                }
                require(loop.size() >= 3 && loop.size() ==
                        static_cast<std::size_t>(shapes(wires.Current(), TopAbs_EDGE).Extent()),
                        "incomplete polygon loop");
                loops.push_back(std::move(loop));
            }
            require(!loops.empty(), "face has no polygon loops");
            reference.faces.push_back(std::move(loops));
        }
        result.shells.push_back(std::move(reference));
    }
    return result;
}

template<class T, class Match>
bool sameMembers(const std::vector<T>& a, const std::vector<T>& b, Match match) {
    if (a.size() != b.size()) return false;
    std::vector<bool> used(b.size());
    for (const auto& item : a) {
        std::size_t i = 0;
        while (i < b.size() && (used[i] || !match(item, b[i]))) ++i;
        if (i == b.size()) return false;
        used[i] = true;
    }
    return true;
}

bool sameLoop(const Loop& a, const Loop& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t start = 0; start < b.size(); ++start) {
        // A wire can start at a different edge after serialization.
        std::size_t i = 0;
        while (i < a.size() && a[i].Distance(b[(start + i) % b.size()]) <= booleanToleranceMm) ++i;
        if (i == a.size()) return true;
    }
    return false;
}

bool sameShell(const ShellReference& a, const ShellReference& b) {
    return a.topology == b.topology && sameMembers(a.faces, b.faces, [](const Face& x, const Face& y) {
        return sameMembers(x, y, sameLoop);
    });
}

void compareBody(const TopoDS_Solid& solid, const BodyReference& reference, const std::string& role) {
    try {
        const auto actual = measure(solid, checkSolid(solid));
        require(std::abs(actual.volume - reference.volume) <= reconstructionVolumeToleranceMm3,
                "volume changed across export/import");
        for (std::size_t i = 0; i < actual.bounds.size(); ++i) {
            require(std::abs(actual.bounds[i] - reference.bounds[i]) <= booleanToleranceMm,
                    "source-local bounds changed across export/import");
        }
        require(sameMembers(actual.shells, reference.shells, sameShell),
                "shell topology or bounded polygon geometry changed across export/import");
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(role + ": " + error.what());
    }
}

TopoDS_Shape booleanOp(const TopoDS_Shape& left, const TopoDS_Shape& right, BOPAlgo_Operation operation,
                       std::string_view stage) {
    BRepAlgoAPI_BooleanOperation op;
    NCollection_List<TopoDS_Shape> arguments, tools;
    arguments.Append(copy(left));
    tools.Append(copy(right));
    op.SetArguments(arguments);
    op.SetTools(tools);
    op.SetOperation(operation);
    op.SetNonDestructive(true);
    op.SetRunParallel(false);
    op.SetToFillHistory(false);
    op.SetFuzzyValue(booleanToleranceMm);
    op.Build();
    if (!op.IsDone() || op.HasErrors() || op.HasWarnings() || op.Shape().IsNull()) {
        std::ostringstream report;
        op.DumpErrors(report);
        op.DumpWarnings(report);
        throw std::runtime_error(std::string(stage) + ": OCCT Boolean incomplete/error/warning: " + report.str());
    }
    require(op.FuzzyValue() == booleanToleranceMm, "unexpected Boolean tolerance");
    return op.Shape();
}

void collectSolids(const TopoDS_Shape& shape, std::vector<TopoDS_Solid>& solids) {
    require(!shape.IsNull(), "null Boolean result");
    if (shape.ShapeType() == TopAbs_SOLID) {
        const auto& solid = TopoDS::Solid(shape);
        require(std::none_of(solids.begin(), solids.end(), [&](const auto& other) { return solid.IsSame(other); }),
                "duplicate solid occurrence");
        solids.push_back(solid);
        return;
    }
    require(shape.ShapeType() == TopAbs_COMPOUND, "unexpected lower-dimensional or non-solid Boolean result");
    for (TopoDS_Iterator child(shape); child.More(); child.Next()) collectSolids(child.Value(), solids);
}

TopoDS_Solid oneSolid(const TopoDS_Shape& shape) {
    std::vector<TopoDS_Solid> solids;
    collectSolids(shape, solids);
    require(solids.size() == 1, "expected exactly one reconstructed solid");
    return solids.front();
}

TopoDS_Solid reconstruct(const ExportBodies& imported) {
    TopoDS_Shape result = copy(imported.B);
    for (const auto& r : imported.R) result = booleanOp(result, r, BOPAlgo_CUT, "reconstruction reduction");
    for (const auto& a : imported.A) result = booleanOp(result, a, BOPAlgo_FUSE, "reconstruction addition");
    return oneSolid(result);
}

bool emptyTopology(const TopoDS_Shape& shape) {
    if (shape.IsNull() || shape.ShapeType() != TopAbs_COMPOUND) return false;
    for (TopoDS_Iterator child(shape); child.More(); child.Next()) {
        if (!emptyTopology(child.Value())) return false;
    }
    return true;
}

int coveredFaces(const TopoDS_Solid& from, const TopoDS_Solid& by, std::string_view stage) {
    const auto faces = shapes(from, TopAbs_FACE);
    const auto tools = shapes(by, TopAbs_FACE);
    for (int i = 1; i <= faces.Extent(); ++i) {
        // Sequential bounded face cuts allow equivalent coplanar splits.
        TopoDS_Shape remaining = faces(i);
        for (int j = 1; j <= tools.Extent() && !emptyTopology(remaining); ++j) {
            remaining = booleanOp(remaining, tools(j), BOPAlgo_CUT, stage);
        }
        require(emptyTopology(remaining), std::string(stage) + ": uncovered bounded face " + std::to_string(i));
    }
    return faces.Extent();
}

struct Residual {
    std::size_t count = 0;
    double volume = 0;
};

Residual residual(const TopoDS_Solid& from, const TopoDS_Solid& by, std::string_view stage) {
    std::vector<TopoDS_Solid> solids;
    collectSolids(booleanOp(from, by, BOPAlgo_CUT, stage), solids);
    Residual result{solids.size(), 0};
    for (const auto& solid : solids) result.volume += checkSolid(solid);
    return result;
}

struct Comparison {
    Residual missing;
    Residual extra;
    double volumeDifference;
    int sourceCovered = 0;
    int reconstructedCovered = 0;

    bool equivalent() const {
        return missing.count == 0 && extra.count == 0 && sourceCovered > 0 && reconstructedCovered > 0 &&
            std::abs(volumeDifference) <= reconstructionVolumeToleranceMm3;
    }
};

Comparison compareSolids(const TopoDS_Solid& source, const TopoDS_Solid& reconstructed) {
    const auto sourceVolume = checkSolid(source);
    const auto reconstructedVolume = checkSolid(reconstructed);
    Comparison result{residual(source, reconstructed, "S minus S'"),
                      residual(reconstructed, source, "S' minus S"),
                      reconstructedVolume - sourceVolume};
    if (result.missing.count == 0 && result.extra.count == 0) {
        result.sourceCovered = coveredFaces(source, reconstructed, "cover S by S'");
        result.reconstructedCovered = coveredFaces(reconstructed, source, "cover S' by S");
    }
    return result;
}

struct ExportReference {
    BodyReference S, B;
    std::vector<BodyReference> A, R;
};

ExportReference measureBodies(const ExportBodies& bodies, std::size_t additions, std::size_t reductions) {
    require(bodies.A.size() == additions && bodies.R.size() == reductions, "unexpected upstream A/R counts");
    ExportReference reference{measure(bodies.S, volume(bodies.S)), measure(bodies.B, volume(bodies.B)), {}, {}};
    for (const auto& body : bodies.A) reference.A.push_back(measure(body, volume(body)));
    for (const auto& body : bodies.R) reference.R.push_back(measure(body, volume(body)));
    return reference;
}

ExportReference writeExport(const fs::path& path, const ExportBodies& bodies,
                            std::size_t additions, std::size_t reductions) {
    auto reference = measureBodies(bodies, additions, reductions);
    exportBrep(bodies, path);
    require(fs::is_regular_file(path) && fs::file_size(path) > 0, "missing or empty JSON/BREP export");
    require(std::distance(fs::directory_iterator(path.parent_path()), fs::directory_iterator{}) == 1,
            "expected one standalone JSON/BREP file");
    return reference;
}

ExportBodies readExport(const fs::path& path, const ExportReference& reference) {
    auto imported = importBrep(path);
    require(imported.A.size() == reference.A.size() && imported.R.size() == reference.R.size(),
            "unexpected imported A/R counts");
    compareBody(imported.S, reference.S, "S");
    compareBody(imported.B, reference.B, "B");
    for (std::size_t i = 0; i < imported.A.size(); ++i) {
        compareBody(imported.A[i], reference.A[i], "A[" + std::to_string(i) + "]");
    }
    for (std::size_t i = 0; i < imported.R.size(); ++i) {
        compareBody(imported.R[i], reference.R[i], "R[" + std::to_string(i) + "]");
    }
    return imported;
}

ExportBodies fixtureBodies(const fs::path& fixture) {
    std::ifstream stream(fixture, std::ios::binary);
    require(stream.good(), "fixture is unreadable");
    const std::string text{std::istreambuf_iterator<char>(stream), {}};
    const auto parsed = smd::parseSmd10125(text);
    const auto& source = parsed.geometry.brep;
    const auto s = buildSolid(assembleShell(source, convertFaces(source)));
    const auto b = buildPrincipalPrismCandidate(s, source);
    const auto parts = decomposeBoolean(s, b);
    ExportBodies bodies{s.solid, b.solid, {}, {}};
    for (const auto& a : parts.extensions) bodies.A.push_back(a.solid);
    for (const auto& r : parts.reductions) bodies.R.push_back(r.solid);
    return bodies;
}

void report(const ExportBodies& imported) {
    const auto comparison = compareSolids(imported.S, reconstruct(imported));
    std::cout << std::setprecision(17) << "A/R=" << imported.A.size() << '/' << imported.R.size()
              << " residual solids S-S'/S'-S=" << comparison.missing.count << '/' << comparison.extra.count
              << " covered faces S/S'=" << comparison.sourceCovered << '/' << comparison.reconstructedCovered
              << " dV(mm^3)=" << comparison.volumeDifference << '\n';
    require(comparison.equivalent(), "imported reconstruction is geometrically inconsistent");
}

ExportReference exportSynthetic(const fs::path& path) {
    ExportBodies bodies{{}, BRepPrimAPI_MakeBox(80, 10, 5).Solid(),
        {BRepPrimAPI_MakeBox(gp_Pnt(-4, 1, 0), 4, 3, 5).Solid(),
         BRepPrimAPI_MakeBox(gp_Pnt(80, 5, 0), 6, 4, 5).Solid()},
        {BRepPrimAPI_MakeBox(gp_Pnt(10, 0, 1), 3, 2, 3).Solid(),
         BRepPrimAPI_MakeBox(gp_Pnt(50, 5, 1), 4, 3, 2).Solid()}};

    // Make S from the whole outer footprint, independently of the A solids.
    // The larger open-notch tool extends past the prism boundary.
    BRepBuilderAPI_MakePolygon footprint;
    for (const auto& p : {gp_Pnt(0, 0, 0), gp_Pnt(80, 0, 0), gp_Pnt(80, 5, 0), gp_Pnt(86, 5, 0),
                          gp_Pnt(86, 9, 0), gp_Pnt(80, 9, 0), gp_Pnt(80, 10, 0), gp_Pnt(0, 10, 0),
                          gp_Pnt(0, 4, 0), gp_Pnt(-4, 4, 0), gp_Pnt(-4, 1, 0), gp_Pnt(0, 1, 0)}) {
        footprint.Add(p);
    }
    footprint.Close();
    const auto face = BRepBuilderAPI_MakeFace(footprint.Wire()).Face();
    const auto prism = BRepPrimAPI_MakePrism(face, gp_Vec(0, 0, 5)).Shape();
    const auto notch = BRepPrimAPI_MakeBox(gp_Pnt(10, -1, 1), 3, 3, 3).Solid();
    const auto open = booleanOp(prism, notch, BOPAlgo_CUT, "synthetic open notch");
    bodies.S = oneSolid(booleanOp(open, bodies.R[1], BOPAlgo_CUT, "synthetic internal cavity"));
    const auto reference = writeExport(path, bodies, 2, 2);
    const auto expectVolume = [](const BodyReference& body, double expected) {
        require(std::abs(body.volume - expected) <= reconstructionVolumeToleranceMm3,
                "synthetic volume differs from analytic reference");
    };
    expectVolume(reference.B, 4000);
    expectVolume(reference.A[0], 60);
    expectVolume(reference.A[1], 120);
    expectVolume(reference.R[0], 18);
    expectVolume(reference.R[1], 24);
    expectVolume(reference.S, 4138);
    require(reference.S.shells.size() == 2, "expected outer shell and internal cavity in synthetic S");
    return reference;
}

void negativeControls(const ExportBodies& imported) {
    auto incomplete = imported;
    incomplete.A.pop_back();
    auto check = compareSolids(imported.S, reconstruct(incomplete));
    require(!check.equivalent() && check.missing.count == 1 && check.extra.count == 0,
            "missing addition must leave missing material");
    std::cout << "Rejected missing addition\n";

    incomplete = imported;
    incomplete.R.pop_back();
    check = compareSolids(imported.S, reconstruct(incomplete));
    require(!check.equivalent() && check.missing.count == 0 && check.extra.count == 1,
            "missing reduction must leave extra material");
    std::cout << "Rejected missing reduction\n";

    for (const double offset : {1.0, 0.001}) {
        gp_Trsf shift;
        shift.SetTranslation(gp_Vec(offset, 0, 0));
        auto displaced = oneSolid(BRepBuilderAPI_Transform(imported.B, shift, true).Shape());
        check = compareSolids(imported.B, displaced);
        require(std::abs(check.volumeDifference) <= reconstructionVolumeToleranceMm3,
                "displacement control must agree in volume");
        require(!check.equivalent() && check.missing.count == 1 && check.extra.count == 1,
                "equal-volume displacement must leave both directed residuals");
        // 10 mm × 5 mm × 0.001 mm yields 0.05 mm^3 in each direction.
        if (offset < 1) {
            require(offset > booleanToleranceMm && check.missing.volume < reconstructionVolumeToleranceMm3 &&
                    check.extra.volume < reconstructionVolumeToleranceMm3,
                    "small displacement must exercise positive sub-1 mm^3 residuals above Boolean fuzz");
        }
        std::cout << "Rejected " << offset << " mm displacement, residual volumes (mm^3) "
                  << check.missing.volume << '/' << check.extra.volume << '\n';
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 5) {
            // The CLI already wrote this file in a separate process.
            const auto reference = measureBodies(fixtureBodies(argv[1]), std::stoul(argv[2]), std::stoul(argv[3]));
            report(readExport(argv[4], reference));
            return 0;
        }
        const TestDirectory directory;
        const auto path = directory.path / "qualification.json";
        if (argc == 4) {
            const fs::path fixture = argv[1];
            std::cout << fixture.filename().string() << '\n';
            // Only numeric measurements leave the scope of the upstream pipeline.
            const auto reference = writeExport(path, fixtureBodies(fixture), std::stoul(argv[2]), std::stoul(argv[3]));
            report(readExport(path, reference));
        } else {
            require(argc == 2, "expected fixture, A/R counts and optional export path, or synthetic/negative-controls");
            const std::string_view mode = argv[1];
            require(mode == "synthetic" || mode == "negative-controls", "unknown qualification mode");
            const auto reference = exportSynthetic(path);
            const auto imported = readExport(path, reference);
            if (mode == "synthetic") report(imported);
            else negativeControls(imported);
        }
        directory.verifyCleanup();
        return 0;
    } catch (const Standard_Failure& error) {
        std::cerr << "GEO-EXP-02 OCCT failure: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "GEO-EXP-02 qualification failure: " << error.what() << '\n';
    }
    return 1;
}
