#pragma once
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "smd/types.hpp"

namespace smd::detail {
void validateUtf8(std::string_view text);
std::vector<std::string> splitLines(std::string_view text);
std::string_view trim(std::string_view text);
double numberValue(std::string_view text);
inline std::optional<double> number(std::span<const Token> tokens, std::size_t i) {
    if (i < tokens.size() && tokens[i].type == TokenType::number)
        return std::get<double>(tokens[i].value);
    return std::nullopt;
}
inline std::optional<std::string> string(std::span<const Token> tokens, std::size_t i) {
    if (i < tokens.size() && tokens[i].type == TokenType::string)
        return std::get<std::string>(tokens[i].value);
    return std::nullopt;
}
} // namespace smd::detail
