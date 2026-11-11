import { createHash } from 'node:crypto';
import { readdirSync, readFileSync } from 'node:fs';
import { resolve, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

export const root = fileURLToPath(new URL('../', import.meta.url));
export const assets = resolve(root, 'public/wasm');
export const sdkVersion = readFileSync(resolve(root, '.emscripten-version'), 'utf8').trim();
export const outputs = ['smd-parser.mjs', 'smd-parser.wasm'];
function files(dir) {
  return readdirSync(resolve(root, dir), { withFileTypes: true }).flatMap(entry => {
    const name = `${dir}/${entry.name}`;
    return entry.isDirectory() ? files(name) : [name];
  });
}
export function sourceHash() {
  const hash = createHash('sha256');
  for (const file of ['.emscripten-version', 'libsmd/CMakeLists.txt', 'tools/build-wasm.mjs',
    ...files('libsmd/include'), ...files('libsmd/src'), ...files('libsmd/wasm')].sort()) {
    hash.update(file).update('\0').update(readFileSync(resolve(root, file))).update('\0');
  }
  return hash.digest('hex');
}
export function assetHash(file) {
  return createHash('sha256').update(readFileSync(resolve(assets, file))).digest('hex');
}
export function checkWasmAssets() {
  try {
    const manifest = JSON.parse(readFileSync(resolve(assets, 'build.json'), 'utf8'));
    if (manifest.sdk !== sdkVersion || manifest.source !== sourceHash()
      || outputs.some(file => manifest.outputs[file] !== assetHash(file))) throw Error('stale');
  } catch {
    throw Error(`WASM assets missing or stale in ${relative(root, assets)}. Activate Emscripten ${sdkVersion}, then run npm run build:wasm. No TypeScript fallback is available.`);
  }
}
