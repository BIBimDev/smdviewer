#include "smd/parser.hpp"
#include "text.hpp"
#include <charconv>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace smd::detail {
void validateUtf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) continue;
        const unsigned length = first >= 0xc2 && first <= 0xdf ? 2 :
            first >= 0xe0 && first <= 0xef ? 3 : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (!length || text.size() - i < length - 1)
            throw std::runtime_error("SMD text must be valid UTF-8.");
        unsigned code = first & (0x7f >> length);
        for (unsigned j = 1; j < length; ++j) {
            const auto c = static_cast<unsigned char>(text[i++]);
            if ((c & 0xc0) != 0x80) throw std::runtime_error("SMD text must be valid UTF-8.");
            code = (code << 6) | (c & 0x3f);
        }
        if ((length == 3 && code < 0x800) || (length == 4 && code < 0x10000)
            || (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff)
            throw std::runtime_error("SMD text must be valid UTF-8.");
    }
}
std::vector<std::string> splitLines(std::string_view text) {
    validateUtf8(text);
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        const auto end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            return lines;
        }
        auto length = end - start;
        if (length && text[end - 1] == '\r') --length;
        lines.emplace_back(text.substr(start, length));
        start = end + 1;
    }
}

std::string_view trim(std::string_view text) {
    // ECMAScript String.trim white space and line terminators, in UTF-8.
    static constexpr std::string_view spaces[] = {
        "\x09", "\x0a", "\x0b", "\x0c", "\x0d", " ", "\xc2\xa0",
        "\xe1\x9a\x80", "\xe2\x80\x80", "\xe2\x80\x81", "\xe2\x80\x82",
        "\xe2\x80\x83", "\xe2\x80\x84", "\xe2\x80\x85", "\xe2\x80\x86",
        "\xe2\x80\x87", "\xe2\x80\x88", "\xe2\x80\x89", "\xe2\x80\x8a",
        "\xe2\x80\xa8", "\xe2\x80\xa9", "\xe2\x80\xaf", "\xe2\x81\x9f",
        "\xe3\x80\x80", "\xef\xbb\xbf"
    };
    bool changed = true;
    while (changed && !text.empty()) {
        changed = false;
        for (auto space : spaces) {
            if (text.starts_with(space)) { text.remove_prefix(space.size()); changed = true; }
            if (text.ends_with(space)) { text.remove_suffix(space.size()); changed = true; }
        }
    }
    return text;
}

double numberValue(std::string_view text) {
    // Caller has checked the JS decimal grammar. from_chars is locale independent.
    if (text.starts_with('+')) text.remove_prefix(1);
    double value = 0;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec == std::errc{}) return value;
    if (result.ec != std::errc::result_out_of_range)
        throw std::runtime_error("Native decimal conversion failed.");
    // Number() yields signed infinity/zero on decimal overflow/underflow.
    const bool negative = text.starts_with('-');
    if (negative) text.remove_prefix(1);
    const auto e = text.find_first_of("eE");
    auto mantissa = text.substr(0, e);
    long long exponent = 0;
    if (e != std::string_view::npos) {
        auto exp = text.substr(e + 1);
        const bool minus = exp.starts_with('-');
        if (exp.starts_with('-') || exp.starts_with('+')) exp.remove_prefix(1);
        constexpr auto cap = std::numeric_limits<long long>::max() / 4;
        for (const char c : exp) {
            const auto digit = c - '0';
            exponent = exponent > (cap - digit) / 10 ? cap : exponent * 10 + digit;
        }
        if (minus) exponent = -exponent;
    }
    const auto dot = mantissa.find('.');
    const auto decimal = dot == std::string_view::npos ? mantissa.size() : dot;
    const auto first = mantissa.find_first_of("123456789");
    if (first == std::string_view::npos) return negative ? -0.0 : 0.0;
    exponent += static_cast<long long>(decimal) - static_cast<long long>(first)
                - (first < decimal ? 1 : 0);
    value = exponent < 0 ? 0.0 : std::numeric_limits<double>::infinity();
    return negative ? -value : value;
}
} // namespace smd::detail

namespace smd {
namespace {
bool digits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(),text.end(),[](char c){return c >= '0' && c <= '9';});
}
bool numeric(std::string_view text) {
    // Exact NUMBER_RE grammar, scanned iteratively to avoid regex stack limits.
    if (text.starts_with('+') || text.starts_with('-')) text.remove_prefix(1);
    const auto exponent = text.find_first_of("eE");
    if (exponent != std::string_view::npos) {
        auto exp = text.substr(exponent+1);
        if (exp.starts_with('+') || exp.starts_with('-')) exp.remove_prefix(1);
        if (!digits(exp)) return false;
        text = text.substr(0,exponent);
    }
    const auto dot = text.find('.');
    if (dot == std::string_view::npos) return digits(text);
    const auto before = text.substr(0,dot), after = text.substr(dot+1);
    return (before.empty() ? digits(after) : digits(before) && (after.empty() || digits(after)));
}
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::string decode(std::string_view inner) {
    std::string value;
    for (std::size_t i = 0; i < inner.size(); ++i) {
        if (inner[i] == '\\' && i + 3 < inner.size() && inner[i+1] == 'x'
            && hex(inner[i+2]) >= 0 && hex(inner[i+3]) >= 0) {
            const int code = hex(inner[i+2]) * 16 + hex(inner[i+3]);
            if (code >= 128) {
                value += static_cast<char>(0xc0 | (code >> 6));
                value += static_cast<char>(0x80 | (code & 63));
            } else value += static_cast<char>(code);
            i += 3;
        } else value += inner[i];
    }
    return value;
}
}
std::vector<Token> tokenizeSmd(std::string_view text) {
    const auto lines = detail::splitLines(text);
    std::vector<Token> tokens;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto raw = detail::trim(lines[i]);
        if (raw.empty()) continue;
        const std::string owned(raw);
        if (raw.starts_with('#') && digits(raw.substr(1)))
            tokens.push_back({TokenType::block, owned, owned, i + 1});
        else if (raw.starts_with('"') && raw.ends_with('"'))
            tokens.push_back({TokenType::string,
                decode(raw.size() < 2 ? std::string_view{} : raw.substr(1, raw.size() - 2)), owned, i + 1});
        else if (numeric(raw))
            tokens.push_back({TokenType::number, detail::numberValue(raw), owned, i + 1});
        else tokens.push_back({TokenType::string, owned, owned, i + 1});
    }
    return tokens;
}
} // namespace smd
