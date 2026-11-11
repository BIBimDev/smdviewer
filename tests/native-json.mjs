// Compile with the existing TypeScript toolchain; no test dependency or rebaseline.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFileSync, mkdtempSync, rmSync } from 'node:fs';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const temp = mkdtempSync(join(tmpdir(), 'smd-native-json-test-'));
const nativePaths = process.argv.slice(2);
assert.ok(nativePaths.length === 0 || nativePaths.length === 2,
  'Usage: npm run test:native-json -- [plain.native.json pocket.native.json]');
let checks = 0;
function test(name, fn) {
  fn();
  checks++;
  console.log(`ok ${checks} - ${name}`);
}

// GEO-00 comparison tolerances: exact field presence, strings and array order;
// integers exact; floating numbers within max(absolute 1e-9, relative 1e-12).
function compare(actual, expected, path = 'parsed') {
  if (typeof expected === 'number') {
    assert.equal(typeof actual, 'number', path);
    if (Number.isInteger(actual) && Number.isInteger(expected)) {
      assert.equal(actual, expected, path);
    } else {
      assert.ok(Math.abs(actual - expected) <= Math.max(1e-9,
        1e-12 * Math.max(Math.abs(actual), Math.abs(expected))), path);
    }
  } else if (expected && typeof expected === 'object') {
    assert.ok(actual && typeof actual === 'object', path);
    assert.equal(Array.isArray(actual), Array.isArray(expected), path);
    assert.deepEqual(Object.keys(actual).sort(), Object.keys(expected).sort(), path);
    for (const key of Object.keys(expected)) compare(actual[key], expected[key], `${path}.${key}`);
  } else {
    assert.equal(actual, expected, path);
  }
}

try {
  execFileSync(process.execPath, [join(root, 'node_modules/typescript/bin/tsc'),
    '--target', 'ES2022', '--module', 'commonjs', '--moduleResolution', 'node',
    '--strict', '--skipLibCheck', '--outDir', temp,
    join(root, 'src/smd/nativeJson.ts'), join(root, 'src/smd/parser10125.ts')],
  { stdio: 'inherit' });
  const require = createRequire(import.meta.url);
  const { parseNativeJson, validateNativeJson } = require(join(temp, 'nativeJson.js'));
  const { parseSmd10125 } = require(join(temp, 'parser10125.js'));
  const specimens = ['plain_column', 'pocket_column'];
  const envelopes = specimens.map(name => JSON.parse(readFileSync(
    join(root, `libsmd/tests/golden/${name}.json`), 'utf8')));

  for (const [i, name] of specimens.entries()) {
    test(`${name}: accepted golden shape and current browser SMD parser agree`, () => {
      const text = new TextDecoder('windows-1252').decode(readFileSync(join(root, `public/samples/${name}.smd`)));
      compare(parseNativeJson(JSON.stringify(envelopes[i])), JSON.parse(JSON.stringify(parseSmd10125(text))));
    });
    test(`${name}: unchanged data, ordered loops, metadata and transform`, () => {
      const envelope = structuredClone(envelopes[i]);
      const snapshot = structuredClone(envelope);
      assert.equal(validateNativeJson(envelope), envelope.parsed);
      assert.deepEqual(envelope, snapshot);
      const b = envelope.parsed.geometry.brep;
      assert.equal(b.verticesWorld.length, i === 0 ? 8 : 16);
      assert.equal(b.faces.length, i === 0 ? 6 : 10);
      assert.equal(b.planes.length, b.faces.length);
      assert.equal('transform' in b, i === 0);
      assert.deepEqual(['x', 'y', 'z'].map(key => [
        Math.min(...b.verticesWorld.map(v => v[key])),
        Math.max(...b.verticesWorld.map(v => v[key])),
      ]), [[-375, 375], [-325, 325], [-1000, 5000]]);
      if (i === 1) assert.ok(b.faces.some(face => face.indices.length === 8));
    });
    if (nativePaths.length) {
      test(`${name}: freshly extracted C++ JSON matches TS field by field`, () => {
        const native = parseNativeJson(readFileSync(nativePaths[i], 'utf8'));
        compare(native, envelopes[i].parsed);
      });
    }
  }

  test('optional metadata may be absent; empty strings and zero stay present', () => {
    const envelope = structuredClone(envelopes[0]);
    const g = envelope.parsed.geometry;
    for (const key of ['objectId', 'name', 'partType', 'placementOrigin', 'xAxis', 'yAxis']) delete g[key];
    delete g.brep.transform;
    g.profileEdges = [];
    delete g.brep.faces[0].name;
    validateNativeJson(envelope);
    Object.assign(g, { name: '', objectId: '', partType: 0 });
    assert.equal(validateNativeJson(envelope), envelope.parsed);
    assert.equal(g.name, '');
    assert.equal(g.partType, 0);
  });
  test('no topology, residual, flag meaning, or placement acceptance heuristics', () => {
    const envelope = structuredClone(envelopes[0]);
    const b = envelope.parsed.geometry.brep;
    b.faces[0].indices = [0, 0, 0];
    b.faces[0].flag = -2.5;
    b.faces[0].plane = { a: 0, b: 0, c: 0, d: 0 };
    b.planes[0].d = 123;
    envelope.parsed.geometry.placementOrigin = { x: 10000, y: 20000, z: 30000 };
    b.extraEvidence = { source: 'preserve me' };
    const snapshot = structuredClone(envelope);
    assert.equal(validateNativeJson(envelope), envelope.parsed);
    assert.deepEqual(envelope, snapshot);
  });

  const invalid = [
    ['missing parsed', e => { delete e.parsed; }, /parsed must be an object/],
    ['missing marker', e => { delete e.parsed.isDicadAsc; }, /isDicadAsc/],
    ['invalid marker type', e => { e.parsed.isDicadAsc = 'true'; }, /isDicadAsc/],
    ['missing source lines', e => { delete e.parsed.geometryBlockLines; }, /geometryBlockLines/],
    ['invalid header line', e => { e.parsed.headerLines[0] = null; }, /headerLines\[0\]/],
    ['missing geometry', e => { delete e.parsed.geometry; }, /geometry must be an object/],
    ['null brep', e => { e.parsed.geometry.brep = null; }, /brep must be an object/],
  ];
  function bad(path, value, pattern = path.at(-1)) {
    invalid.push([`${path.join('.')} = ${String(value)}`, e => {
      let target = e.parsed.geometry;
      for (const key of path.slice(0, -1)) target = target[key];
      target[path.at(-1)] = value;
    }, new RegExp(String(pattern).replace(/[.*+?^${}()|[\]\\]/g, '\\$&'))]);
  }
  for (const field of ['objectId', 'name', 'partType', 'placementOrigin', 'xAxis', 'yAxis']) bad([field], null);
  bad(['profileEdges'], {});
  for (const [field, value] of [['id', 1], ['index', null], ['type', '1'], ['start', {}], ['end', null]])
    bad(['profileEdges', 0, field], value);
  for (const field of ['verticesLocal', 'verticesWorld']) {
    for (const value of [null, {}, []]) bad(['brep', field], value);
    bad(['brep', field, 0], [1, 2, 3], field);
    for (const value of [null, '0', NaN, Infinity, -Infinity]) bad(['brep', field, 0, 'x'], value);
  }
  bad(['brep', 'verticesLocal'], [{ x: 0, y: 0, z: 0 }], 'verticesWorld');
  for (const value of [null, {}, []]) bad(['brep', 'faces'], value);
  bad(['brep', 'faces', 0], null, 'faces');
  bad(['brep', 'faces', 0, 'flag'], '0');
  for (const value of [null, {}, [0, 1], Array(9).fill(0)]) bad(['brep', 'faces', 0, 'indices'], value);
  for (const value of [-1, 8, 0.5, null, '0', Infinity]) bad(['brep', 'faces', 0, 'indices', 0], value, 'indices');
  bad(['brep', 'faces', 0, 'plane'], null);
  bad(['brep', 'faces', 0, 'plane', 'd'], null);
  bad(['brep', 'faces', 0, 'name'], 1);
  bad(['brep', 'planes'], []);
  bad(['brep', 'planes', 0, 'a'], null);
  bad(['brep', 'faceNames'], []);
  bad(['brep', 'faceNames', 0], 1, 'faceNames');
  for (const value of [null, -1, 0.5]) bad(['brep', 'vertexCountTokenIndex'], value);
  bad(['brep', 'transform'], null);
  for (const value of [null, [1, 2], [...Array(12).fill(0), 0], Array(12).fill(null)])
    bad(['brep', 'transform', 'values'], value);
  for (const [name, mutate, pattern] of invalid) {
    test(`reject ${name}`, () => {
      const envelope = structuredClone(envelopes[0]);
      mutate(envelope);
      assert.throws(() => validateNativeJson(envelope), pattern);
    });
  }
  for (const value of [null, [], true, 'json']) {
    test(`reject root ${JSON.stringify(value)}`, () => assert.throws(() => validateNativeJson(value), /root/));
  }
  for (const version of [undefined, null, '1', 0, 2, 1.1]) {
    test(`reject schemaVersion ${String(version)}`, () => assert.throws(
      () => validateNativeJson({ ...envelopes[0], schemaVersion: version }), /schemaVersion/));
  }
  for (const text of ['', '{', '{"schemaVersion":1,}', 'NaN']) {
    test(`reject malformed JSON ${JSON.stringify(text)}`, () => assert.throws(() => parseNativeJson(text), /malformed JSON/));
  }
  test('JSON number overflow and serialized non-finite null are rejected', () => {
    const envelope = structuredClone(envelopes[0]);
    envelope.parsed.geometry.brep.verticesWorld[0].x = 'OVERFLOW';
    assert.throws(() => parseNativeJson(JSON.stringify(envelope).replace('"OVERFLOW"', '1e999')), /finite number/);
    envelope.parsed.geometry.brep.verticesWorld[0].x = Infinity;
    assert.throws(() => parseNativeJson(JSON.stringify(envelope)), /finite number/);
  });
  test('valid load after rejected input still succeeds', () => {
    assert.throws(() => parseNativeJson('{}'));
    assert.deepEqual(parseNativeJson(JSON.stringify(envelopes[1])), envelopes[1].parsed);
  });
  console.log(`Passed ${checks} native JSON checks${nativePaths.length ? ' including both generated native inputs' : ' (native CLI inputs not supplied)'}.`);
} finally {
  rmSync(temp, { recursive: true, force: true });
}
