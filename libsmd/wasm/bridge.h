#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#define SMD_NOEXCEPT noexcept
#else
#define SMD_NOEXCEPT
#endif

/* Private wasm32 ABI, not a stable native/cross-compiler interface.
 * Input: caller-owned UTF-8 bytes, explicit length, no terminator required.
 * Allocate/free input with this module's malloc/free; parse never retains it.
 * Result: opaque owned handle; destroy exactly once after copying data out.
 * Data: borrowed bytes, terminator excluded from length; valid until destruction.
 * Status: 0 JSON v1, 1 parser/serializer exception, 2 resource limit, 3 ABI/internal.
 * Nonzero status contains diagnostic only. A null handle means allocation failed.
 * No exception crosses this boundary. Calls are synchronous, single-threaded.
 */
typedef struct SmdWasmResult SmdWasmResult;
SmdWasmResult* smd_wasm_parse(const char* input, size_t length) SMD_NOEXCEPT;
int smd_wasm_status(const SmdWasmResult* result) SMD_NOEXCEPT;
const char* smd_wasm_data(const SmdWasmResult* result) SMD_NOEXCEPT;
size_t smd_wasm_length(const SmdWasmResult* result) SMD_NOEXCEPT;
void smd_wasm_destroy(SmdWasmResult* result) SMD_NOEXCEPT;

#ifdef __cplusplus
}
#endif
#undef SMD_NOEXCEPT
