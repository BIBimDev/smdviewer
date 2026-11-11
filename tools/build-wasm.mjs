import { execFileSync } from 'node:child_process';
import { copyFileSync, mkdirSync, statSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { assets, assetHash, outputs, root, sdkVersion, sourceHash } from './wasm-assets.mjs';

// Use the activated emsdk toolchain; no implicit installation or SDK download.
function sdkTool(name, args, options) {
  // Emscripten's Windows launchers are .bat files. Invoke the Python entry
  // points directly rather than passing shell-interpreted paths/arguments.
  if (process.platform === 'win32') {
    if (!process.env.EMSDK) throw Error('Activate emsdk_env.bat first.');
    return execFileSync('python', [resolve(process.env.EMSDK, 'upstream/emscripten', `${name}.py`), ...args], options);
  }
  return execFileSync(name, args, options);
}
const version = sdkTool('emcc', ['--version'], { encoding: 'utf8' });
if (!version.split('\n')[0].match(new RegExp(` ${sdkVersion.replaceAll('.', '\\.')}(?: |$)`))) {
  throw Error(`Expected Emscripten ${sdkVersion}; activate the pinned SDK. Got: ${version.split('\n')[0]}`);
}
const build = resolve(root, 'build/wasm');
sdkTool('emcmake', ['cmake', '-S', resolve(root, 'libsmd'), '-B', build,
  '-DCMAKE_BUILD_TYPE=Release'], { stdio: 'inherit' });
execFileSync('cmake', ['--build', build, '--target', 'smd-wasm', '--parallel', '2'], { stdio: 'inherit' });
mkdirSync(assets, { recursive: true });
for (const file of outputs) copyFileSync(resolve(build, file), resolve(assets, file));
writeFileSync(resolve(assets, 'build.json'), JSON.stringify({ sdk: sdkVersion, source: sourceHash(),
  outputs: Object.fromEntries(outputs.map(file => [file, assetHash(file)])) }, null, 2) + '\n');
for (const file of outputs) console.log(`${file}: ${statSync(resolve(assets, file)).size} bytes`);
