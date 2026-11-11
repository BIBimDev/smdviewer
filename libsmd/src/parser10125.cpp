#include "smd/parser.hpp"
#include "text.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace smd {
namespace {
std::string join(const std::vector<std::string>& lines) {
    std::string result;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i) result += '\n';
        result += lines[i];
    }
    return result;
}
std::optional<Vec3> vec(std::span<const Token> tokens, std::size_t offset) {
    auto x = detail::number(tokens,offset), y = detail::number(tokens,offset+1), z = detail::number(tokens,offset+2);
    if (x && y && z) return Vec3{*x,*y,*z};
    return std::nullopt;
}
struct EdgeMarker { std::string_view id, index; };
std::optional<EdgeMarker> edgeMarker(std::string_view s) {
    // Equivalent to /^edge\[(.+)\]\[(\d+)\]$/ including greedy ID matching.
    // JS dot excludes CR, LF, U+2028 and U+2029.
    if (!s.starts_with("edge[") || !s.ends_with(']')
        || s.find_first_of("\r\n") != std::string_view::npos
        || s.find("\xe2\x80\xa8") != std::string_view::npos
        || s.find("\xe2\x80\xa9") != std::string_view::npos) return std::nullopt;
    const auto separator = s.rfind("][");
    if (separator == std::string_view::npos || separator <= 5) return std::nullopt;
    const auto index = s.substr(separator+2,s.size()-separator-3);
    if (index.empty() || !std::all_of(index.begin(),index.end(),[](char c){return c >= '0' && c <= '9';}))
        return std::nullopt;
    return EdgeMarker{s.substr(5,separator-5),index};
}
std::vector<ProfileEdge> profiles(std::span<const Token> tokens) {
    std::size_t first = 0;
    for (; first < tokens.size(); ++first) {
        if (auto id = detail::string(tokens,first)) {
            const auto marker = edgeMarker(*id);
            if (marker && marker->index == "0") break;
        }
    }
    if (first < 1 || first == tokens.size()) return {};
    const auto count = detail::number(tokens,first-1);
    if (!count || !std::isfinite(*count) || std::trunc(*count) != *count || *count < 1) return {};
    // Cannot read more complete records than available; TS would return [] on truncation.
    if (*count > static_cast<double>((tokens.size()-first)/6)) return {};
    std::vector<ProfileEdge> edges;
    auto cursor = first;
    for (std::size_t i = 0; i < static_cast<std::size_t>(*count); ++i, cursor += 6) {
        const auto id = detail::string(tokens,cursor);
        const auto type = detail::number(tokens,cursor+1), x1 = detail::number(tokens,cursor+2),
                   y1 = detail::number(tokens,cursor+3), x2 = detail::number(tokens,cursor+4), y2 = detail::number(tokens,cursor+5);
        if (!id || !type || !x1 || !y1 || !x2 || !y2) return {};
        const auto marker = edgeMarker(*id);
        if (!marker) return {};
        edges.push_back({std::string(marker->id), detail::numberValue(marker->index), *type, {*x1,*y1}, {*x2,*y2}});
    }
    return edges;
}
}
ParsedSmd parseSmd10125(std::string_view text) {
    const auto lines = detail::splitLines(text);
    const auto start = std::find_if(lines.begin(),lines.end(), [](const auto& line) { return detail::trim(line) == "#10125"; });
    if (start == lines.end()) throw std::runtime_error("#10125 main geometry block was not found.");
    ParsedSmd result{};
    result.headerLines.assign(lines.begin(),start);
    result.geometryBlockLines.assign(start,lines.end());
    const auto header = tokenizeSmd(join(result.headerLines));
    const auto first = std::find_if(header.begin(),header.end(), [](const auto& t) { return t.type == TokenType::string; });
    if (first == header.end() || std::get<std::string>(first->value) != "DICAD ASC")
        throw std::runtime_error("File does not begin with the expected \"DICAD ASC\" marker.");
    result.isDicadAsc = true;
    const auto tokens = tokenizeSmd(join(result.geometryBlockLines));
    auto& g = result.geometry;
    // geometry starts at the first trimmed #10125, so its block token is index zero.
    g.objectId = detail::string(tokens,2);
    g.name = detail::string(tokens,3);
    g.partType = detail::number(tokens,11);
    g.placementOrigin = vec(tokens,17);
    g.xAxis = vec(tokens,20);
    g.yAxis = vec(tokens,23);
    g.profileEdges = profiles(tokens);
    g.brep = detectBrep(tokens);
    return result;
}
} // namespace smd
