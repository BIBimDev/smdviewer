import { createWasmParser } from "./wasmBridge";
import type { SmdWasmModule } from "./wasmBridge";

// Generated public assets stay together and work with Vite --base /subpath/.
// No ambient Module, file paths or generated source-tree imports.
const assetBase = new URL(`${import.meta.env.BASE_URL}wasm/`, document.baseURI).href;
export const parseSmdWasm = createWasmParser(async (onAbort) => {
  const url = `${assetBase}smd-parser.mjs`;
  const { default: createModule } = await import(/* @vite-ignore */ url);
  const module: SmdWasmModule = await createModule({
    locateFile: (file: string) => `${assetBase}${file}`,
    onAbort,
  });
  return module;
});
