#pragma once
#include "smd/types.hpp"
#include <span>
#include <string_view>

namespace smd {
// Text and strings use UTF-8. All results own their storage.
std::vector<Token> tokenizeSmd(std::string_view text);
// Throws std::runtime_error with the reference diagnostic on unsupported input.
ParsedBrep detectBrep(std::span<const Token> tokens);
ParsedSmd parseSmd10125(std::string_view text);
Vec3 applyTransform(Vec3 vertex, const Transform3x4& transform);
} // namespace smd
