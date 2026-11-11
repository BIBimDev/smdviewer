#include "smd/parser.hpp"
#include "text.hpp"
#include <cmath>
#include <stdexcept>

namespace smd {
namespace {
bool integer(double x) { return std::isfinite(x) && std::trunc(x) == x; }
std::optional<Transform3x4> transformBefore(std::span<const Token> tokens, std::size_t start) {
    if (start < 15) return std::nullopt;
    const auto marker = start - 15;
    if (detail::number(tokens, marker) != 5 || detail::number(tokens, marker+13) != 1
        || detail::number(tokens, marker+14) != 0) return std::nullopt;
    Transform3x4 t{};
    for (std::size_t j = 0; j < 12; ++j) {
        const auto value = detail::number(tokens, marker + 1 + j);
        if (!value) return std::nullopt;
        t.values[j] = *value;
    }
    return t;
}
std::optional<ParsedBrep> candidate(std::span<const Token> tokens, std::size_t start) {
    const auto n = detail::number(tokens, start);
    if (!n || !integer(*n) || *n < 4 || *n > 100000) return std::nullopt;
    auto cursor = start + 1;
    ParsedBrep result{};
    for (std::size_t i = 0; i < static_cast<std::size_t>(*n); ++i) {
        const auto x = detail::number(tokens,cursor), y = detail::number(tokens,cursor+1), z = detail::number(tokens,cursor+2);
        if (!x || !y || !z) return std::nullopt;
        result.verticesLocal.push_back({*x,*y,*z});
        cursor += 3;
    }
    const auto count = detail::number(tokens,cursor++);
    if (!count || !integer(*count) || *count < 4 || *count > 100000) return std::nullopt;
    const auto faceCount = static_cast<std::size_t>(*count);
    for (std::size_t i = 0; i < faceCount; ++i) {
        const auto flag = detail::number(tokens,cursor), k = detail::number(tokens,cursor+1);
        if (!flag || !k || !integer(*k) || *k < 3 || *k > *n) return std::nullopt;
        cursor += 2;
        Face face{*flag, {}, std::nullopt, std::nullopt};
        for (std::size_t j = 0; j < static_cast<std::size_t>(*k); ++j) {
            const auto index = detail::number(tokens,cursor++);
            if (!index || !integer(*index) || *index < 0 || *index >= *n) return std::nullopt;
            face.indices.push_back(static_cast<std::size_t>(*index));
        }
        result.faces.push_back(std::move(face));
    }
    if (detail::number(tokens,cursor++) != *count) return std::nullopt;
    for (std::size_t i = 0; i < faceCount; ++i) {
        const auto a = detail::number(tokens,cursor), b = detail::number(tokens,cursor+1),
                   c = detail::number(tokens,cursor+2), d = detail::number(tokens,cursor+3);
        if (!a || !b || !c || !d) return std::nullopt;
        result.planes.push_back({*a,*b,*c,*d});
        cursor += 4;
    }
    if (detail::number(tokens,cursor) == 0 && detail::number(tokens,cursor+1) == *count) ++cursor;
    if (detail::number(tokens,cursor++) != *count) return std::nullopt;
    for (std::size_t i = 0; i < faceCount; ++i) {
        const auto prefix = detail::number(tokens,cursor), suffixA = detail::number(tokens,cursor+2), suffixB = detail::number(tokens,cursor+3);
        const auto name = detail::string(tokens,cursor+1);
        if (!prefix || !name || !suffixA || !suffixB) return std::nullopt;
        result.faceNames.push_back(*name);
        result.faces[i].plane = result.planes[i];
        result.faces[i].name = *name;
        cursor += 4;
    }
    for (std::size_t i = 0; i < faceCount; ++i) {
        const auto& p = result.planes[i];
        for (const auto index : result.faces[i].indices) {
            const auto& v = result.verticesLocal[index];
            if (std::abs(p.a*v.x + p.b*v.y + p.c*v.z + p.d) > 1e-5) return std::nullopt;
        }
    }
    result.transform = transformBefore(tokens,start);
    for (const auto& v : result.verticesLocal)
        result.verticesWorld.push_back(result.transform ? applyTransform(v,*result.transform) : v);
    result.vertexCountTokenIndex = start;
    return result;
}
}
ParsedBrep detectBrep(std::span<const Token> tokens) {
    for (std::size_t i = 0; i < tokens.size(); ++i)
        if (auto result = candidate(tokens,i)) return std::move(*result);
    throw std::runtime_error("No supported explicit BREP was found in #10125. Viewer V1 currently expects vertices + faces + planes + face names.");
}
} // namespace smd
