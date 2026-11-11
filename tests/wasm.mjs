// Real WASM ABI + viewer wrapper, using only the locked compiler and Node builtins.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFileSync, mkdtempSync, rmSync } from 'node:fs';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
import { root, assets, checkWasmAssets } from '../tools/wasm-assets.mjs';

const extract = process.argv[2];
assert.ok(extract && process.argv.length === 3, 'Usage: npm run test:wasm -- /path/to/native/smd-extract');
checkWasmAssets();
const { default: factory } = await import(pathToFileURL(join(assets, 'smd-parser.mjs')));
const wasm = await factory({ wasmBinary: readFileSync(join(assets, 'smd-parser.wasm')) });
const temp = mkdtempSync(join(tmpdir(), 'smd-wasm-test-'));
const encode = text => new TextEncoder().encode(text);
const golden = name => JSON.parse(readFileSync(join(root, `libsmd/tests/golden/${name}.json`), 'utf8'));
const sample = name => readFileSync(join(root, `public/samples/${name}.smd`));
let checks = 0;
async function test(name, fn) { await fn(); console.log(`ok ${++checks} - ${name}`); }

// Same GEO-00 tolerance, with exact expected integer values and field presence.
function compare(actual, expected, path = '$') {
  if (typeof expected === 'number') {
    assert.equal(typeof actual, 'number', path);
    if (Number.isInteger(expected)) assert.equal(actual, expected, path);
    else assert.ok(Number.isFinite(actual) && Math.abs(actual - expected) <= Math.max(1e-9,
      1e-12 * Math.max(Math.abs(actual), Math.abs(expected))), path);
  } else if (expected && typeof expected === 'object') {
    assert.ok(actual && typeof actual === 'object', path);
    assert.equal(Array.isArray(actual), Array.isArray(expected), path);
    assert.deepEqual(Object.keys(actual).sort(), Object.keys(expected).sort(), path);
    for (const key of Object.keys(expected)) compare(actual[key], expected[key], `${path}.${key}`);
  } else assert.equal(actual, expected, path);
}

function abi(bytes, module = wasm) {
  const input = module._malloc(Math.max(1, bytes.length));
  assert.ok(input);
  let result = 0;
  try {
    module.HEAPU8.set(bytes, input);
    result = module._smd_wasm_parse(input, bytes.length);
    assert.ok(result);
    const status = module._smd_wasm_status(result);
    const pointer = module._smd_wasm_data(result), length = module._smd_wasm_length(result);
    assert.ok(pointer && length);
    const text = new TextDecoder('utf-8', { fatal: true }).decode(module.HEAPU8.slice(pointer, pointer + length));
    return { status, text };
  } finally {
    if (result) module._smd_wasm_destroy(result);
    module._free(input);
  }
}

try {
  execFileSync(process.execPath, [join(root, 'node_modules/typescript/bin/tsc'),
    '--target', 'ES2022', '--module', 'commonjs', '--moduleResolution', 'node',
    '--strict', '--skipLibCheck', '--outDir', temp, join(root, 'src/smd/wasmBridge.ts'),
    join(root, 'src/smd/parser10125.ts')], { stdio: 'inherit' });
  const require = createRequire(import.meta.url);
  const { createWasmParser, MAX_SMD_UTF8_BYTES } = require(join(temp, 'wasmBridge.js'));
  const { parseSmd10125 } = require(join(temp, 'parser10125.js'));
  const parse = createWasmParser(async () => wasm);

  for (const [name, vertices, faces, transform] of [
    ['plain_column', 8, 6, true], ['pocket_column', 16, 10, false],
  ]) await test(`${name}: WASM/native/golden/TS field fidelity, topology, transform and bounds`, async () => {
    const bytes = sample(name);
    const result = abi(bytes);
    assert.equal(result.status, 0);
    const output = JSON.parse(result.text);
    const native = JSON.parse(execFileSync(resolve(extract), [join(root, `public/samples/${name}.smd`)], { encoding: 'utf8' }));
    compare(output, native);
    compare(output, golden(name));
    const text = new TextDecoder('windows-1252').decode(bytes);
    compare(output.parsed, JSON.parse(JSON.stringify(parseSmd10125(text))));
    compare(await parse(text), output.parsed);
    const b = output.parsed.geometry.brep;
    assert.equal(b.verticesLocal.length, vertices);
    assert.equal(b.faces.length, faces);
    assert.equal(b.planes.length, faces);
    assert.equal('transform' in b, transform);
    assert.deepEqual(['x', 'y', 'z'].map(axis => [Math.min(...b.verticesWorld.map(v => v[axis])),
      Math.max(...b.verticesWorld.map(v => v[axis]))]), [[-375, 375], [-325, 325], [-1000, 5000]]);
    if (!transform) assert.ok(b.faces.some(face => face.indices.length === 8));
  });

  const behavior = golden('behavior');
  for (const c of behavior.cases) await test(`ABI oracle ${c.id}`, () => {
    const result = abi(encode(c.input));
    if ('error' in c.expected) {
      assert.equal(result.status, 1);
      assert.equal(result.text, c.expected.error);
    } else {
      assert.equal(result.status, 0);
      compare(JSON.parse(result.text), c.expected);
    }
  });

  await test('ABI rejects invalid UTF-8; diagnostics survive freeing; healthy next parse', () => {
    for (const bytes of [[0xc0, 0xaf], [0xed, 0xa0, 0x80], [0xf4, 0x90, 0x80, 0x80], [0xc2], [0xff]]) {
      const result = abi(Uint8Array.from(bytes));
      assert.equal(result.status, 1);
      assert.equal(result.text, 'SMD text must be valid UTF-8.');
    }
    assert.equal(abi(sample('plain_column')).status, 0);
  });
  await test('ABI null argument and explicit input-size limit', () => {
    for (const [pointer, length, status] of [[0, 1, 3], [0, 0, 1]]) {
      const result = wasm._smd_wasm_parse(pointer, length);
      try { assert.equal(wasm._smd_wasm_status(result), status); assert.ok(wasm._smd_wasm_length(result)); }
      finally { wasm._smd_wasm_destroy(result); }
    }
    const result = abi(new Uint8Array(MAX_SMD_UTF8_BYTES + 1));
    assert.equal(result.status, 2);
    assert.match(result.text, /16 MiB/);
  });
  await test('browser Windows-1252 -> UTF-8, including high bytes, not UTF-8 reinterpretation', async () => {
    const text = new TextDecoder('windows-1252').decode(sample('plain_column'));
    const oldName = golden('plain_column').parsed.geometryBlockLines[3];
    const prefix = text.indexOf(oldName);
    assert.ok(prefix >= 0);
    const bytes = Buffer.concat([Buffer.from(text.slice(0, prefix), 'ascii'),
      Buffer.from([0x22, 0x53, 0x74, 0xfc, 0x74, 0x7a, 0x65, 0x20, 0x80, 0x20, 0x93, 0xe9, 0x94, 0x22]),
      Buffer.from(text.slice(prefix + oldName.length), 'ascii')]);
    const decoded = new TextDecoder('windows-1252').decode(bytes);
    const output = await parse(decoded);
    assert.equal(output.geometry.name, 'Stütze € “é”');
    compare(output, JSON.parse(JSON.stringify(parseSmd10125(decoded))));
  });
  await test('JSON v1 validation rejects nonfinite C++ output without repair', async () => {
    const input = behavior.cases.find(c => c.id === 'nonfinite-transform-json-null').input;
    assert.equal(abi(encode(input)).status, 0);
    await assert.rejects(parse(input), /Native JSON:.*finite number/);
    await parse(new TextDecoder('windows-1252').decode(sample('plain_column')));
  });

  // Track the actual wrapper's ownership separately from internal allocator reuse.
  let allocations = 0, results = 0;
  const tracked = new Proxy(wasm, { get(target, key) {
    if (key === '_malloc') return n => { const p = target._malloc(n); if (p) allocations++; return p; };
    if (key === '_free') return p => { allocations--; target._free(p); };
    if (key === '_smd_wasm_parse') return (p, n) => { const r = target._smd_wasm_parse(p, n); if (r) results++; return r; };
    if (key === '_smd_wasm_destroy') return r => { results--; target._smd_wasm_destroy(r); };
    return target[key];
  } });
  const trackedParse = createWasmParser(async () => tracked);
  await test('wrapper frees input and output on success, parse failure and validation failure', async () => {
    for (const text of [sample('plain_column').toString(), '', behavior.cases.find(c => c.id === 'nonfinite-transform-json-null').input]) {
      try { await trackedParse(text); } catch { /* Two intentional failures. */ }
      assert.equal(allocations, 0); assert.equal(results, 0);
    }
  });
  await test('module init is shared, failure retries, runtime trap frees input and retries', async () => {
    let attempts = 0;
    const init = createWasmParser(async () => { if (++attempts === 1) throw Error('synthetic init'); return tracked; });
    await assert.rejects(init(''), /WASM initialization failed: synthetic init/);
    const text = sample('plain_column').toString();
    await Promise.all([init(text), init(text)]);
    assert.equal(attempts, 2);
    let trap = true, loads = 0;
    const runtime = createWasmParser(async () => {
      loads++;
      return new Proxy(tracked, { get(target, key) {
        if (key === '_smd_wasm_parse' && trap) return () => { trap = false; throw Error('synthetic trap'); };
        return target[key];
      } });
    });
    await assert.rejects(runtime(text), /WASM runtime failed: synthetic trap/);
    assert.equal(allocations, 0); assert.equal(results, 0);
    await runtime(text);
    assert.equal(loads, 2);
    const noInput = createWasmParser(async () => new Proxy(wasm, {
      get: (target, key) => key === '_malloc' ? () => 0 : target[key],
    }));
    await assert.rejects(noInput(text), /Cannot allocate WASM input/);
    const noResult = createWasmParser(async () => new Proxy(tracked, {
      get: (target, key) => key === '_smd_wasm_parse' ? () => 0 : target[key],
    }));
    await assert.rejects(noResult(text), /Cannot allocate WASM result/);
    assert.equal(allocations, 0); assert.equal(results, 0);
  });
  await test('sizable input and memory growth refresh views; UTF-8 limit and heap exhaustion recover', async () => {
    const before = wasm.HEAPU8.buffer;
    const pointer = wasm._malloc(48 * 1024 * 1024);
    assert.ok(pointer);
    assert.notEqual(wasm.HEAPU8.buffer, before);
    wasm._free(pointer);
    const text = sample('plain_column').toString().replace('"DICAD ASC"\n',
      '"DICAD ASC"\n"' + 'a'.repeat(1024 * 1024) + '"\n');
    compare(await trackedParse(text), JSON.parse(JSON.stringify(parseSmd10125(text))));
    assert.equal(allocations, 0); assert.equal(results, 0);
    await assert.rejects(parse('€'.repeat(MAX_SMD_UTF8_BYTES / 2)), /16 MiB/);
    assert.equal(wasm._malloc(268435456), 0);
    await parse(sample('plain_column').toString());
  });
  await test('2,000 alternating successful/failed loads retain no handles and heap plateaus', async () => {
    const text = sample('pocket_column').toString();
    const sizes = [];
    for (let batch = 0; batch < 2; batch++) {
      for (let i = 0; i < 1000; i++) {
        if (i % 2) await assert.rejects(trackedParse(''), /WASM parse error:/);
        else await trackedParse(text);
        assert.equal(allocations, 0); assert.equal(results, 0);
      }
      sizes.push(wasm.HEAPU8.length);
    }
    assert.equal(sizes[1], sizes[0]);
    console.log(`WASM heap plateau: ${sizes[0]} bytes (retained capacity, not live allocations)`);
  });
  await test('real C++ allocation exception stays inside ABI, returns resource diagnostic, then recovers', async () => {
    const limited = await factory({ wasmBinary: readFileSync(join(assets, 'smd-parser.wasm')) });
    const reserve = limited._malloc(250 * 1024 * 1024);
    assert.ok(reserve);
    try {
      const text = sample('plain_column').toString().replace('"DICAD ASC"\n',
        '"DICAD ASC"\n"' + 'a'.repeat(2 * 1024 * 1024) + '"\n');
      const result = abi(encode(text), limited);
      assert.equal(result.status, 2);
      assert.ok(result.text.length > 0);
      assert.doesNotThrow(() => new TextDecoder('utf-8', { fatal: true }).decode(encode(result.text)));
    } finally { limited._free(reserve); }
    assert.equal(abi(sample('plain_column'), limited).status, 0);
  });
  console.log(`Passed ${checks} WASM checks; field tolerances abs=1e-9, rel=1e-12, integers exact.`);
} finally { rmSync(temp, { recursive: true, force: true }); }
