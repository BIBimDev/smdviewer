#include "smd/parser.hpp"
#include "smd_geometry/boolean_decomposition.hpp"
#include "smd_geometry/export.hpp"

#include <Standard_Failure.hxx>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {
std::string readInput(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        throw std::runtime_error("Cannot open input file: " + (error ? error.message() : "expected a regular file"));
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open input file for reading");
    }
    try {
        return {std::istreambuf_iterator<char>(file), {}};
    } catch (const std::ios_base::failure& error) {
        throw std::runtime_error(std::string("Cannot read input file: ") + error.what());
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: smd-geo-export <input.smd> <output.json>\n";
        return 2;
    }

    try {
        const auto parsed = smd::parseSmd10125(readInput(argv[1]));
        const auto& source = parsed.geometry.brep;
        const auto faces = smd_geometry::convertFaces(source);
        const auto shell = smd_geometry::assembleShell(source, faces);
        const auto solid = smd_geometry::buildSolid(shell);
        const auto prism = smd_geometry::buildPrincipalPrismCandidate(solid, source);
        const auto parts = smd_geometry::decomposeBoolean(solid, prism);

        smd_geometry::ExportBodies bodies{solid.solid, prism.solid, {}, {}};
        for (const auto& extension : parts.extensions) {
            bodies.A.push_back(extension.solid);
        }
        for (const auto& reduction : parts.reductions) {
            bodies.R.push_back(reduction.solid);
        }
        smd_geometry::exportBrep(bodies, argv[2]);
        return 0;
    } catch (const Standard_Failure& error) {
        std::cerr << "smd-geo-export: OCCT failure: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "smd-geo-export: " << error.what() << '\n';
    }
    return 1;
}
