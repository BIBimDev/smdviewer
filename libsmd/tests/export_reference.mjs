// GEO-00 oracle: compile and invoke the accepted TypeScript sources unchanged.
// Default verifies committed outputs. --write is an explicit, review-required rebaseline.
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import { execFileSync } from 'node:child_process';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const writing = process.argv.length === 3 && process.argv[2] === '--write';
if (process.argv.length > 2 && !writing) throw new Error('Usage: node libsmd/tests/export_reference.mjs [--write]');
const sources = ['tokenizer.ts', 'parser10125.ts', 'brepDetector.ts', 'transform.ts', 'types.ts'];
const samples = ['plain_column', 'pocket_column'];
const hashes = {};
const referenceCommit = '88d996f9056edf6d8b48a40263a7891d33a1ac68';
for (const file of [...sources.map(f => `src/smd/${f}`), ...samples.map(f => `public/samples/${f}.smd`)]) {
  hashes[file] = createHash('sha256').update(readFileSync(join(root,file))).digest('hex');
  const baselineHash = createHash('sha256').update(execFileSync('git', ['show',`${referenceCommit}:${file}`], { cwd: root })).digest('hex');
  if (hashes[file] !== baselineHash) throw new Error(`Reference differs from approved baseline: ${file}`);
}
const temp = mkdtempSync(join(tmpdir(), 'smd-ts-reference-'));
function record(name, value) {
  // One record per line keeps the generated mutation corpus compact and diffable.
  const output = name === 'behavior.json'
    ? '{\n  "schemaVersion": 1,\n  "cases": [\n' + value.cases.map(c => '    ' + JSON.stringify(c)).join(',\n')
      + '\n  ],\n  "tokenCases": [\n' + value.tokenCases.map(c => '    ' + JSON.stringify(c)).join(',\n') + '\n  ]\n}\n'
    : JSON.stringify(value, null, 2) + '\n';
  const path = join(here, 'golden', name);
  if (writing) writeFileSync(path, output);
  else if (readFileSync(path, 'utf8') !== output) throw new Error(`Reference differs: ${name}; investigate, do not silently rebaseline`);
}
try {
  execFileSync(process.execPath, [join(root,'node_modules/typescript/bin/tsc'),
    '--target','ES2022','--module','commonjs','--moduleResolution','node',
    '--strict','--skipLibCheck','--outDir',temp,
    ...sources.map(f => join(root,'src/smd',f))], { stdio: 'inherit' });
  const require = createRequire(import.meta.url);
  const { parseSmd10125 } = require(join(temp,'parser10125.js'));
  const { tokenizeSmd } = require(join(temp,'tokenizer.js'));
  const envelope = text => ({ schemaVersion: 1, parsed: parseSmd10125(text) });
  record('provenance.json', {
    task: 'GEO-00', referenceCommit,
    generator: 'libsmd/tests/export_reference.mjs', sha256: hashes,
  });
  for (const name of samples)
    record(`${name}.json`,envelope(readFileSync(join(root,`public/samples/${name}.smd`),'utf8')));

  const plain = readFileSync(join(root,'public/samples/plain_column.smd'),'utf8');
  const parsed = parseSmd10125(plain);
  const tokens = tokenizeSmd(parsed.geometryBlockLines.join('\n'));
  const start = parsed.geometry.brep.vertexCountTokenIndex;
  const faceCount = start + 1 + 3 * 8;
  const firstFace = faceCount + 1;
  const planeCount = firstFace + 6 * 6;
  const firstPlane = planeCount + 1;
  const separator = firstPlane + 6 * 4;
  const nameCount = separator + 1;
  const firstName = nameCount + 1;
  // Restrict mutations to the known first B-rep and remove unrelated trailer.
  const base = tokens.slice(0,firstName + 6*4).map(t => t.raw);
  const text = array => '"DICAD ASC"\n' + array.join('\n') + '\n';
  const cases = [];
  function add(id, input, succeeds) {
    let expected;
    try { expected = envelope(input); }
    catch (e) { expected = { error: e.message }; }
    if (succeeds === ('error' in expected)) throw new Error(`Incorrect test expectation: ${id}`);
    cases.push({ id, input, expected });
  }
  function mutate(id, offset, value, succeeds = false) {
    const array = base.slice(); array[offset] = value; add(id,text(array),succeeds);
  }
  add('missing-block-error-before-header', '', false);
  add('invalid-header-marker', '"OTHER"\n#10125\n', false);
  add('missing-header-marker', '#10125\n', false);
  add('header-first-string-is-wrong', '"OTHER"\n"DICAD ASC"\n#10125\n', false);
  add('missing-10125', '"DICAD ASC"\n#10071\n', false);
  add('absent-brep', '"DICAD ASC"\n#10125\n1\n', false);
  add('incomplete-brep', text(base.slice(0,start+9)), false);
  add('truncated-names', text(base.slice(0,-1)), false);
  for (const value of ['3','100001','8.5','1e999','eight']) mutate(`vertex-count-${value}`,start,value);
  for (const value of ['3','100001','6.5','1e999','six']) mutate(`face-count-${value}`,faceCount,value);
  mutate('invalid-coordinate',start+1,'0x10');
  for (const value of ['2','9','4.5','1e999','four']) mutate(`face-size-${value}`,firstFace+1,value);
  for (const value of ['-1','8','0.5','1e999','index']) mutate(`vertex-index-${value}`,firstFace+2,value);
  mutate('invalid-face-flag',firstFace,'face');
  mutate('uninterpreted-face-flag',firstFace,'-2.5',true);
  mutate('mismatched-plane-count',planeCount,'5');
  mutate('invalid-plane-coefficient',firstPlane,'NaN');
  mutate('plane-vertex-mismatch',firstPlane+3,'-5999');
  mutate('plane-tolerance-inside',firstPlane+3,'-5999.999999',true);
  mutate('plane-tolerance-outside',firstPlane+3,'-5999.99998');
  mutate('mismatched-name-count',nameCount,'5');
  mutate('numeric-face-name',firstName+1,'123');
  mutate('invalid-name-prefix',firstName,'prefix');
  mutate('invalid-name-suffix-a',firstName+2,'suffix');
  mutate('invalid-name-suffix-b',firstName+3,'suffix');
  mutate('uninterpreted-name-prefix',firstName,'42.5',true);
  mutate('uninterpreted-name-suffix',firstName+2,'-8.25',true);
  mutate('nonzero-separator',separator,'1');
  add('absent-separator',text(base.filter((_,i) => i !== separator)),true);
  for (const [id,index,value] of [
    ['transform-marker',start-15,'4'], ['transform-trailing-one',start-2,'2'],
    ['transform-trailing-zero',start-1,'1'], ['transform-value',start-14,'unknown']])
    mutate(`ignored-invalid-${id}`,index,value,true);
  const matrix = base.slice();
  matrix.splice(start-14,12, ...['1.5','2','3','4','5','6','7','8','9','10','11','12']);
  add('row-major-nonidentity-transform',text(matrix),true);
  mutate('nonfinite-transform-json-null',start-14,'1e999',true);
  mutate('nonfinite-local-coordinate-rejected-by-plane-test',start+1,'1e999');
  const nonfiniteVertex = base.slice();
  nonfiniteVertex[start+1] = nonfiniteVertex[start+2] = '1e999';
  add('nonfinite-local-coordinates-nan-plane-test-accepts',text(nonfiniteVertex),true);
  const nonfinitePlane = base.slice();
  nonfinitePlane[firstPlane] = '1e999'; nonfinitePlane[firstPlane+1] = '-1e999';
  add('nonfinite-plane-nan-test-reference-accepts',text(nonfinitePlane),true);
  mutate('absent-object-id',2,'42',true);
  mutate('absent-name',3,'42',true);
  mutate('empty-name',3,'""',true);
  mutate('absent-part-type',11,'unknown',true);
  mutate('nonfinite-part-type',11,'-1e999',true);
  for (const offset of [17,18,19,20,21,22,23,24,25])
    mutate(`incomplete-placement-${offset}`,offset,'unknown',true);
  const edge = base.findIndex(s => s.startsWith('"edge['));
  mutate('invalid-profile-count',edge-1,'1.5',true);
  mutate('zero-profile-count',edge-1,'0',true);
  mutate('huge-profile-count',edge-1,'1e100',true);
  mutate('missing-profile-first-marker',edge,'"edge[id][2]"',true);
  mutate('invalid-profile-coordinate',edge+2,'coordinate',true);
  mutate('malformed-later-profile-id',edge+6,'"edge[id][-1]"',true);
  mutate('greedy-profile-id',edge,'"edge[a][b][0]"',true);
  mutate('profile-nonfinite-coordinate',edge+2,'-1e999',true);
  mutate('profile-large-index',edge+6,'"edge[id][999999999999999999999999]"',true);
  mutate('profile-decoded-newline-id',edge,'"edge[a\\x0ab][0]"',true);
  mutate('profile-unicode-line-separator',edge,'"edge[a\u2028b][0]"',true);
  mutate('profile-index-leading-zero-first-marker',edge,'"edge[id][00]"',true);
  mutate('profile-later-index-leading-zeros',edge+6,'"edge[id][001]"',true);
  mutate('profile-empty-id',edge,'"edge[][0]"',true);
  mutate('profile-long-id',edge,'"edge[' + 'a'.repeat(5000) + '][0]"',true);
  const absent = base.slice(); absent.splice(edge-1,25);
  add('absent-profile',text(absent),true);
  const noMetadata = ['#10125', ...Array(26).fill('unknown'), ...base.slice(start)];
  add('malformed-optional-metadata',text(noMetadata),true);
  add('header-numbers-before-marker-reference-accepts','1\n#9\n' + text(base),true);
  add('dicad-unquoted-reference-accepts',text(base).replace('"DICAD ASC"','DICAD ASC'),true);
  add('crlf-and-final-newline',text(base).replaceAll('\n','\r\n'),true);
  add('whitespace-block-marker',text(base).replace('#10125',' \t#10125\u00a0'),true);
  add('first-valid-candidate-after-rejection',text([...base.slice(0,start),...base.slice(start,start+8), 'invalid', ...base.slice(start)]),true);
  add('first-valid-candidate-wins',text([...base, ...base.slice(start)]),true);
  add('tail-crosses-other-block-marker',text([...base.slice(0,start),'#10071',...base.slice(start)]),true);
  add('first-10125-metadata-tail-brep',text(['#10125','1','"first-id"','"first-name"',...base]),true);
  add('control-and-unicode-name',text(base.map((s,i) => i === 3 ? '"quote\\x22 slash\\x5c tab\\x09 nul\\x00 ü 😀"' : s)),true);
  const tokenInputs = [
    ['lines-escapes-and-grammar', '\n \t\r\n #10125 \n#12x\n"St\\xfctze\\x00\\x22\\x5c"\n"\\x0G\\n\\u1234"\n"\n""\nunquoted\\x41\n+1\n-.5\n1.\n1e-3\n0x10\nNaN\nInfinity\n1e\n1 2\n-0\r'],
    ['unicode-trim', '\ufeff#10125\u00a0\n\u1680\u2000\u200a+.5\u2028\u2029\u202f\u205f\u3000\n"😀 ü\\xff"\n\u0085fallback\u0085'],
    ['overflow-underflow', '1e999\n-1e999\n1e-999\n-1e-999\n1.7976931348623157e308\n5e-324\n2e-324\n-0e9999\n.0001e-9999\n1e+000000000000000000000001\n'],
    ['hex-no-generic-unescape', '"\\\\x41"\n"\\x414\\X41\\x7f\\x80\\xff"\n\rinside\rline\n#١\n'],
    ['empty', ''],
    ['long-numeric-and-block-tokens', '0'.repeat(5000) + '1\n' + '9'.repeat(5000) + '\n#' + '1'.repeat(5000) + '\n'],
  ];
  record('behavior.json',{ schemaVersion: 1, cases,
    tokenCases: tokenInputs.map(([id,input]) => ({ id,input,expected: { schemaVersion: 1,tokens: tokenizeSmd(input) } })) });
  console.log(`${writing ? 'Wrote' : 'Verified'} 2 specimen goldens, ${cases.length} parser cases and ${tokenInputs.length} tokenizer cases against unchanged TypeScript`);
} finally { rmSync(temp,{recursive:true,force:true}); }
