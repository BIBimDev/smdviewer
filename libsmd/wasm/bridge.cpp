#include "bridge.h"
#include "smd/json.hpp"
#include "smd/parser.hpp"
#include <cstring>
#include <exception>
#include <new>
#include <string>

struct SmdWasmResult {
    int status = 3;
    std::string text;
    const char* fallback = "WASM bridge: unknown C++ failure.";

    void fail(int code, const char* message) noexcept {
        status = code;
        text.clear();
        // Keep an allocation-free diagnostic even if copying what() fails.
        try { if (message && *message) text = message; }
        catch (...) { text.clear(); }
    }
};

extern "C" SmdWasmResult* smd_wasm_parse(const char* input, size_t length) noexcept {
    auto* result = new (std::nothrow) SmdWasmResult;
    if (!result) return nullptr;
    if (!input && length) {
        result->fail(3, "WASM bridge: null input with nonzero length.");
        return result;
    }
    // Bridge resource policy, not a new SMD semantic rule.
    if (length > 16 * 1024 * 1024) {
        result->fail(2, "WASM bridge: UTF-8 input exceeds 16 MiB limit.");
        return result;
    }
    try {
        result->text = smd::toJsonV1(smd::parseSmd10125({input ? input : "", length}));
        result->status = 0;
    } catch (const std::bad_alloc&) {
        result->fail(2, "WASM bridge: memory allocation failed (256 MiB heap limit).");
    } catch (const std::exception& error) {
        result->fail(1, error.what());
    } catch (...) {
        result->fail(3, nullptr);
    }
    return result;
}

extern "C" int smd_wasm_status(const SmdWasmResult* result) noexcept {
    return result ? result->status : 2;
}
extern "C" const char* smd_wasm_data(const SmdWasmResult* result) noexcept {
    return result ? (result->text.empty() ? result->fallback : result->text.data()) : nullptr;
}
extern "C" size_t smd_wasm_length(const SmdWasmResult* result) noexcept {
    return result ? (result->text.empty() ? std::strlen(result->fallback) : result->text.size()) : 0;
}
extern "C" void smd_wasm_destroy(SmdWasmResult* result) noexcept { delete result; }
