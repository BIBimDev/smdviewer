#include "smd_geometry/export.hpp"

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdio>
#include <fstream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#endif

namespace smd_geometry {
namespace {
using Json = nlohmann::ordered_json;
constexpr std::string_view brepSignature = "CASCADE Topology V3, (c) Open Cascade";
constexpr int containerVersion = 1;
constexpr auto brepVersion = TopTools_FormatVersion_VERSION_3;

[[noreturn]] void fail(std::string_view field, const std::string& message) {
    throw std::runtime_error("GEO-EXP-01 [" + std::string(field) + "]: " + message);
}

Json expectedHeader() {
    return {{"format", "smdviewer.geo-export"}, {"version", containerVersion},
            {"coordinates", "source-local"}, {"units", "mm"}, {"brepVersion", static_cast<int>(brepVersion)}};
}

const Json& requiredField(const Json& object, std::string_view key, const std::string& label) {
    const auto field = object.find(key);
    if (field == object.end()) {
        fail(label, "missing required field");
    }
    return *field;
}

void validateHeader(const Json& head) {
    if (!head.is_object()) {
        fail("head", "expected an object");
    }
    const auto expected = expectedHeader();
    for (const auto& [key, value] : expected.items()) {
        const auto label = "head." + key;
        const auto& field = requiredField(head, key, label);
        const bool expectsInteger = value.is_number_integer();
        if (expectsInteger ? !field.is_number_integer() : !field.is_string()) {
            fail(label, expectsInteger ? "expected an integer" : "expected a string");
        }
        if (field != value) {
            fail(label, "unsupported value");
        }
    }
}

void requireSolid(const TopoDS_Shape& shape, std::string_view label) {
    if (shape.IsNull() || shape.ShapeType() != TopAbs_SOLID) {
        fail(label, "expected one non-null solid");
    }
    TopExp_Explorer faces(shape, TopAbs_FACE);
    if (!faces.More()) {
        fail(label, "solid has no faces");
    }
    for (; faces.More(); faces.Next()) {
        // OCCT writes a triangulation for a surface-less face even when mesh
        // output is disabled. Such a face cannot be exported as geometry alone.
        TopLoc_Location location;
        if (BRep_Tool::Surface(TopoDS::Face(faces.Current()), location).IsNull()) {
            fail(label, "solid contains a face without a geometric surface");
        }
    }
}

std::string writeSolid(const TopoDS_Solid& solid, std::string_view label) {
    std::ostringstream stream;
    try {
        requireSolid(solid, label);
        BRepTools::Write(solid, stream, false, false, brepVersion);
    } catch (const Standard_Failure& error) {
        fail(label, std::string("OCCT BREP write failed: ") + error.what());
    }
    if (!stream) {
        fail(label, "BREP stream write failed");
    }
    return std::move(stream).str();
}

TopoDS_Solid readSolid(const Json& value, std::string_view label) {
    if (!value.is_string()) {
        fail(label, "expected a BREP string");
    }
    const auto& bytes = value.get_ref<const std::string&>();
    const std::string_view text(bytes);
    const auto start = text.find_first_not_of("\r\n");
    const auto newline = text.find('\n', start);
    if (newline == text.npos || text.find('\0') != text.npos) {
        fail(label, "expected complete OCCT ASCII BREP VERSION_3");
    }
    auto signature = text.substr(start, newline - start);
    if (signature.ends_with('\r')) {
        signature.remove_suffix(1);
    }
    if (signature != brepSignature) {
        fail(label, "expected complete OCCT ASCII BREP VERSION_3");
    }

    std::istringstream stream(bytes);
    stream.imbue(std::locale::classic());
    // The OCCT stream overload returns void. Stop immediately if an
    // extraction fails, including in the middle of a truncated shape table.
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    TopoDS_Shape shape;
    try {
        BRepTools::Read(shape, stream, BRep_Builder{});
        if (!stream.eof()) {
            stream >> std::ws;
        }
        if (!stream.eof()) {
            fail(label, "unexpected data after BREP shape");
        }
        requireSolid(shape, label);
    } catch (const std::ios_base::failure&) {
        fail(label, "invalid or incomplete BREP stream");
    } catch (const Standard_Failure& error) {
        fail(label, std::string("OCCT BREP read failed: ") + error.what());
    }
    return TopoDS::Solid(shape);
}

std::string itemLabel(std::string_view key, std::size_t index) {
    return std::string(key) + "[" + std::to_string(index) + "]";
}

Json writeCollection(const std::vector<TopoDS_Solid>& solids, std::string_view key) {
    auto array = Json::array();
    for (std::size_t i = 0; i < solids.size(); ++i) {
        array.push_back(writeSolid(solids[i], itemLabel(key, i)));
    }
    return array;
}

std::vector<TopoDS_Solid> readCollection(const Json& array, std::string_view key) {
    if (!array.is_array()) {
        fail(key, "expected an array of BREP strings");
    }
    std::vector<TopoDS_Solid> solids;
    solids.reserve(array.size());
    for (std::size_t i = 0; i < array.size(); ++i) {
        solids.push_back(readSolid(array[i], itemLabel(key, i)));
    }
    return solids;
}

void writeNewFile(const std::filesystem::path& filename, const std::string& data) {
#ifdef _WIN32
    int fd;
    const auto error = _wsopen_s(&fd, filename.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
                                _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (error != 0) {
        fail("output", std::error_code(error, std::generic_category()).message());
    }
    auto* file = _wfdopen(fd, L"wb");
    if (!file) {
        _close(fd);
    }
#else
    auto* file = std::fopen(filename.c_str(), "wbx");
    if (!file) {
        fail("output", std::error_code(errno, std::generic_category()).message());
    }
#endif
    const bool written = file && std::fwrite(data.data(), 1, data.size(), file) == data.size();
    const bool closed = file && std::fclose(file) == 0;
    if (!written || !closed) {
        std::error_code cleanup;
        std::filesystem::remove(filename, cleanup);
        fail("output", cleanup ? "write failed and cleanup failed: " + cleanup.message() : "write failed");
    }
}
} // namespace

void exportBrep(const ExportBodies& bodies, const std::filesystem::path& filename) {
    const Json document = {{"head", expectedHeader()}, {"S", writeSolid(bodies.S, "S")},
                           {"B", writeSolid(bodies.B, "B")}, {"A", writeCollection(bodies.A, "A")},
                           {"R", writeCollection(bodies.R, "R")}};
    writeNewFile(filename, document.dump(2) + '\n');
}

ExportBodies importBrep(const std::filesystem::path& filename) {
    std::error_code statusError;
    if (!std::filesystem::is_regular_file(filename, statusError)) {
        fail("input", statusError ? statusError.message() : "expected a regular file");
    }
    std::ifstream input(filename, std::ios::binary);
    if (!input) {
        fail("input", "could not open file for reading");
    }
    Json document;
    try {
        document = Json::parse(input);
    } catch (const std::ios_base::failure&) {
        fail("input", "could not read file");
    } catch (const Json::exception& error) {
        fail("JSON", error.what());
    }
    // The JSON lexer can treat a literal NUL as end-of-input. Require file EOF.
    if (!input.eof()) {
        fail("JSON", "unexpected data after JSON document");
    }
    if (!document.is_object()) {
        fail("JSON", "expected an object");
    }
    validateHeader(requiredField(document, "head", "head"));
    return {readSolid(requiredField(document, "S", "S"), "S"),
            readSolid(requiredField(document, "B", "B"), "B"),
            readCollection(requiredField(document, "A", "A"), "A"),
            readCollection(requiredField(document, "R", "R"), "R")};
}
} // namespace smd_geometry
