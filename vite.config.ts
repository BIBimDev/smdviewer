import { defineConfig } from "vite";
// @ts-expect-error Build-time Node helper, not part of the browser TS project.
import { checkWasmAssets } from "./tools/wasm-assets.mjs";

export default defineConfig({
  plugins: [{
    name: "require-generated-smd-wasm",
    buildStart() { checkWasmAssets(); },
  }],
});
