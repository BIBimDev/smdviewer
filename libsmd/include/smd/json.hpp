#pragma once
#include "smd/types.hpp"
#include <span>
#include <string>

namespace smd {
// Version 1 envelope: {"schemaVersion":1,"parsed":<TS ParsedSmd shape>}.
// Absent optionals are omitted; non-finite numeric values are null (JSON.stringify).
std::string toJsonV1(const ParsedSmd& parsed);
// Focused tokenizer reference comparison, with the same version envelope.
std::string tokensToJsonV1(std::span<const Token> tokens);
} // namespace smd
