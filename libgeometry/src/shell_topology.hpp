#pragma once

#include "smd_geometry/shell.hpp"

// Private source-index preflight, exposed here only for focused combinatorial
// tests (e.g. nonorientable surfaces that cannot be valid geometric fixtures).
namespace smd_geometry::detail {
struct SourceEdgePair {
    std::size_t firstVertex, secondVertex;
    std::array<SourceEdgeUse, 2> uses;
};
struct ShellTopology {
    std::vector<SourceEdgePair> edges;
    std::vector<bool> reversed;
};
ShellTopology analyzeShellTopology(const smd::ParsedBrep& source);
} // namespace smd_geometry::detail
