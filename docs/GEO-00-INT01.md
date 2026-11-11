# GEO-00-INT01 — native JSON v1 viewer bridge

Approved baseline: `af23abcd1d4ab6c67c73a079fba677d8fbf296d9`.
Task branch: `feature/geo-00-int01-json-bridge`, targeting `main` for independent
review. The baseline working tree was clean and fetched `origin/main` matched it.

## Implementation and boundaries

**Open native JSON** reads UTF-8 native C++ extractor output. Runtime validation
accepts `schemaVersion: 1` with the viewer's `ParsedSmd` shape, then passes the
original `parsed` object directly through the shared load/inspector/status path.
Status identifies TypeScript (SMD) versus Native C++ (JSON v1). Source names use
HTML escaping or `textContent`; diagnostics are escaped as well.

The validator checks required fields, metadata/profile types, finite numeric
values, non-empty vertex/face arrays, equal local/transformed vertex counts,
integer face indices within bounds, 3..vertexCount loop lengths, finite planes,
plane/name list lengths matching faces, token-index shape and an optional
12-number transform. Face planes are required because the existing triangulator
requires them; optional face names and geometry metadata may be absent. Present
optionals cannot be null. Missing fields are not synthesized. Unknown extra
fields remain intact.

This is structural validation for viewer consumption. There is no plane-residual,
normal, winding, uniqueness, manifoldness, watertightness or placement test.
Repeated indices, degenerate planes and uninterpreted numeric flags are retained.
The bridge performs no coordinate conversion, repair, triangulation change or
transform application. Original loops/order/names/flags, identities, raw text,
planes, profile/placement metadata, local vertices, transformed vertices and the
exact transform record survive unchanged. `verticesWorld` remains the existing
rendering input: coordinates after the recorded B-rep transform, **not** guaranteed
project-global coordinates. Source units remain mm for the two accepted samples.

Invalid JSON, unsupported versions, malformed geometry and file-read/decode errors
clear the previous model and inspectors through the existing recovery behavior.
The newest file request wins if reads overlap. File inputs reset to allow choosing
the same file again. The SMD drag/drop route and Windows-1252 decoder are retained.

The TypeScript SMD parser, renderer/triangulator, native C++ library/CLI, `libsmd/`
and sample bytes are unchanged. No dependencies were added. JSON v1 remains a
diagnostic/extraction interface; GEO-01 has not started. No PR merge is authorized.

## Reproduce automated verification

Run from the repository root, with Node 22+, CMake, C++20 and Python 3:

```sh
npm ci
cmake -S libsmd -B /tmp/smd-geo00-int01-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/smd-geo00-int01-build
/tmp/smd-geo00-int01-build/smd-extract public/samples/plain_column.smd > /tmp/plain_column.native.json
/tmp/smd-geo00-int01-build/smd-extract public/samples/pocket_column.smd > /tmp/pocket_column.native.json
npm run test:native-json -- /tmp/plain_column.native.json /tmp/pocket_column.native.json
ctest --test-dir /tmp/smd-geo00-int01-build --output-on-failure
node libsmd/tests/export_reference.mjs
npm run build
```

The focused tests use the locked TypeScript compiler and Node assertions with
temporary CommonJS output, following the existing oracle approach. No test
dependency is required. `npm run test:native-json` alone runs against the committed
TypeScript goldens; the two optional arguments additionally exercise freshly
generated native outputs. Tests never overwrite goldens or commit generated JSON.
Numeric comparisons preserve GEO-00 tolerances: absolute `1e-9`, relative `1e-12`
(maximum threshold), with integer-valued pairs exact; key presence, strings and
array order are exact. Identity/deep-equality assertions verify no validator
mutation or transform application.

Actual Linux execution on 2026-11-11:

- `npm ci`: passed, zero audit vulnerabilities. Sandbox initially blocked the
  esbuild child-process check; execution outside that sandbox passed.
- Release CMake configure/build and both extraction commands: passed.
- Focused JSON tests with both generated inputs: **92 checks passed**, including
  baseline fidelity, optional/extra data preservation, malformed envelopes,
  geometry/metadata/planes/transforms, null/non-finite coordinates, indices,
  schema versions and success after rejection.
- Native CTest: **2/2 passed**, including field-by-field reference regression.
- TypeScript oracle: **2 specimen goldens, 94 parser cases and 6 tokenizer cases
  verified**. Its sandbox child-process restriction was resolved by running
  outside the sandbox.
- `npm run build`: passed; existing non-blocking bundle-size warning remains.
  The tracked generated `tsconfig.tsbuildinfo` is excluded from this task's diff.

Sample-verified counts: plain column 8 vertices / 6 faces / 6 planes with a B-rep
transform; pocket column 16 / 10 / 10 without one, including an eight-index face.
Both retain bounds X `-375..375`, Y `-325..325`, Z `-1000..5000` mm. These checks
establish specimen fidelity, not general SMD semantics or topological validity.

## Firefox verification

Firefox **160.0**, Linux, headless WebDriver **0.37.1**, against the local Vite
server: automated smoke checks passed for both specimens through both file
controls, repeated SMD/JSON switching and same-file reloads, identical metadata,
vertex/face inspector markup and bounds, fit, table selection, face colors,
transparency, vertices and view-cube visibility. Malformed JSON, unsupported
version, out-of-range index and null coordinate cases cleared stale tables,
counts, selections and details, then recovered on a valid load. Filename HTML
stayed text; delayed successful/failed reads could not overwrite a newer load.
No window errors or unhandled promise rejections were observed. Screenshots were
visually inspected: the pocket recess is present in solid and transparent views.
The temporary WebDriver harness used Python's standard library; it is not a new
project dependency. Firefox needed execution outside the task sandbox; its Snap
isolation required temporary workspace copies of the generated JSON, removed
after testing.

**Full hands-on Firefox smoke testing remains pending user verification.** The
automation and screenshot inspection do not establish all mouse navigation,
occlusion and picking behavior. No Windows or other-browser test was executed.

1. Run `npm run dev`. In Firefox, open each authorized `.smd` with **Open .smd**,
   then its generated JSON with **Open native JSON**. Confirm source labels,
   counts/bounds, face/vertex tables and the visible pocket recess agree. Firefox
   Snap users may need to copy `/tmp/*.native.json` into an accessible folder.
2. Switch repeatedly, including choosing the same file again; verify that only
   one model remains and previous selections/details clear on each load.
3. For both sources, use fit, orbit/zoom, view-cube center/edge/corner snaps and
   cube dragging. Toggle face colors, transparency and vertex visibility; check
   solid occlusion, face/vertex table hover and 3D selection/inspector linkage.
4. Open a malformed JSON file, a copy with `schemaVersion: 2`, and copies with an
   invalid face index or null vertex coordinate. Confirm understandable errors,
   empty geometry/tables, and successful loading afterward. Verify SMD drag/drop
   still works.

Independent team review must decide acceptance of this diagnostic bridge and
complete the hands-on check before any separately authorized merge. GEO-00 stays
closed and GEO-01 is outside this task. There were no implementation scope
deviations and no changes to native source or format semantics.
