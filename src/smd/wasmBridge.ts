import { parseNativeJson } from "./nativeJson";
import type { ParsedSmd } from "./types";

/** Only the private wasm32 C ABI and current heap view are exposed by the glue. */
export interface SmdWasmModule {
  HEAPU8: Uint8Array;
  _malloc(length: number): number;
  _free(pointer: number): void;
  _smd_wasm_parse(pointer: number, length: number): number;
  _smd_wasm_status(result: number): number;
  _smd_wasm_data(result: number): number;
  _smd_wasm_length(result: number): number;
  _smd_wasm_destroy(result: number): void;
}

export const MAX_SMD_UTF8_BYTES = 16 * 1024 * 1024;
export type WasmLoader = (onAbort: (reason: unknown) => void) => Promise<SmdWasmModule>;

function diagnostic(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

/** Input/output buffers always belong to this module, never the JS heap.
 * Module creation is shared across concurrent requests; failed creation retries
 * on the next request. Parse errors retain the healthy module. No TS fallback.
 */
export function createWasmParser(load: WasmLoader): (text: string) => Promise<ParsedSmd> {
  let ready: Promise<SmdWasmModule> | undefined;
  function module(): Promise<SmdWasmModule> {
    if (!ready) {
      ready = load(() => { ready = undefined; }).catch((error: unknown) => {
        ready = undefined;
        throw new Error(`WASM initialization failed: ${diagnostic(error)}`);
      });
    }
    return ready;
  }

  return async (text) => {
    // A UTF-16 code unit needs at least one UTF-8 byte. Avoid encoding huge input.
    if (text.length > MAX_SMD_UTF8_BYTES) throw new Error("WASM input exceeds 16 MiB UTF-8 limit.");
    const bytes = new TextEncoder().encode(text);
    if (bytes.length > MAX_SMD_UTF8_BYTES) throw new Error("WASM input exceeds 16 MiB UTF-8 limit.");
    const wasm = await module();
    let input = 0;
    let result = 0;
    let status: number;
    let output: string;
    try {
      try {
        input = wasm._malloc(Math.max(1, bytes.length));
        if (!input) throw new Error("Cannot allocate WASM input buffer.");
        // malloc/parse can grow memory. Always acquire the module's current view.
        wasm.HEAPU8.set(bytes, input);
        result = wasm._smd_wasm_parse(input, bytes.length);
        if (!result) throw new Error("Cannot allocate WASM result handle.");
        status = wasm._smd_wasm_status(result);
        const pointer = wasm._smd_wasm_data(result);
        const length = wasm._smd_wasm_length(result);
        if (!pointer || length <= 0 || pointer + length > wasm.HEAPU8.length) {
          throw new Error("Invalid WASM result buffer.");
        }
        // Copy before destroying the result, including error diagnostics and NULs.
        output = new TextDecoder("utf-8", { fatal: true }).decode(wasm.HEAPU8.slice(pointer, pointer + length));
      } finally {
        try { if (result) wasm._smd_wasm_destroy(result); }
        finally { if (input) wasm._free(input); }
      }
    } catch (error) {
      ready = undefined;
      throw new Error(`WASM runtime failed: ${diagnostic(error)}`);
    }
    if (status !== 0) {
      if (status === 3) ready = undefined;
      throw new Error(`WASM ${status === 1 ? "parse" : status === 2 ? "resource" : "bridge"} error: ${output}`);
    }
    // Existing JSON.parse + structural validator; no repair or transformation.
    return parseNativeJson(output);
  };
}
