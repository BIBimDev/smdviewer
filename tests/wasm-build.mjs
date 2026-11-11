import assert from 'node:assert/strict';
import { renameSync } from 'node:fs';
import { resolve } from 'node:path';
import { build, createServer } from 'vite';
import { assets, checkWasmAssets, root } from '../tools/wasm-assets.mjs';

checkWasmAssets();
const manifest = resolve(assets, 'build.json');
renameSync(manifest, `${manifest}.test-backup`);
try {
  await assert.rejects(build({ root, logLevel: 'silent', build: { write: false } }), /WASM assets missing or stale/);
  const server = await createServer({ root, logLevel: 'silent', server: { port: 0 } });
  try { await assert.rejects(server.listen(), /WASM assets missing or stale/); }
  finally { await server.close(); }
} finally { renameSync(`${manifest}.test-backup`, manifest); }
console.log('PASS missing assets stop development and build with generation instructions (no fallback)');

let inspected = false;
await build({ root, logLevel: 'error', base: '/smd-test/', build: { write: false }, plugins: [{
  name: 'inspect-production-imports',
  generateBundle() {
    const modules = [...this.getModuleIds()];
    for (const file of ['parser10125.ts', 'tokenizer.ts', 'brepDetector.ts', 'transform.ts']) {
      assert.ok(!modules.some(id => id.endsWith(`/src/smd/${file}`)), `${file} entered production graph`);
    }
    for (const file of ['wasm.ts', 'wasmBridge.ts', 'nativeJson.ts']) {
      assert.ok(modules.some(id => id.endsWith(`/src/smd/${file}`)), `${file} missing from production graph`);
    }
    inspected = true;
  },
}] });
assert.ok(inspected);
console.log('PASS production graph uses WASM + validator, excludes every TS parser module');
