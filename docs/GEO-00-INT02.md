# GEO-00-INT02 — accepted C++ parser in the browser

## Baseline and scope

Fetched `origin/main` and verified GitHub PR #3 is merged. The actual post-INT01
merge/base is `adf38e06571202f279c9cf23eb72c5299effd03a`; the initial local `main`
matched it with a clean worktree. Work uses
`feature/geo-00-int02-wasm-parser`. The delivery response and PR record the final
head SHA. No merge is authorized or performed. GEO-01 remains untouched.

Changed files: `.emscripten-version`, `.gitignore`, `package.json`, `vite.config.ts`,
`tools/{build-wasm,wasm-assets}.mjs`, `libsmd/CMakeLists.txt`,
`libsmd/wasm/bridge.{h,cpp}`, `src/main.ts`, `src/vite-env.d.ts`,
`src/smd/{wasm,wasmBridge}.ts`, `tests/{wasm,wasm-build}.mjs`,
`tests/firefox-wasm.py`, `README.md`, `libsmd/README.md` and this report.

The production `.smd` selection and drop routes now use the same `libsmd` C++20
sources as `smd-extract`. Pipeline:

```text
File.arrayBuffer -> existing decodeBuffer (Windows-1252)
 -> TextEncoder (UTF-8) -> module-owned input bytes
 -> smd::parseSmd10125 -> smd::toJsonV1
 -> copy JSON into JS -> free result and input
 -> existing parseNativeJson (JSON.parse + validateNativeJson)
 -> existing loadParsed / viewer / inspector
```

There is no intermediate file or parsing service. The only parsing-related HTTP
traffic is normal static retrieval of the generated JS/WASM. Successful SMD input
is labeled **Native C++ (WASM)**; diagnostic input stays **Native C++ (JSON v1)**.
The existing byte decoder, including its UTF-8 fallback if the named decoder is
unavailable, is unchanged. This preserves browser behavior; it does not establish
the general STRAKON encoding. Native tools still require valid UTF-8.

The TS parser/tokenizer/detector/transform remain byte-identical test oracles and
are absent from the production import graph. `types.ts`, JSON v1, validation,
renderer/triangulation/navigation, native parser sources/headers, fixtures,
goldens and package lockfile are unchanged. No dependencies were added.
Tracked `tsconfig.tsbuildinfo` is restored after builds and excluded from delivery.

## Pinned SDK and clean-checkout workflow

Requirements: Node 22+, CMake 3.20+, Python 3, Git and **Emscripten 4.0.20**.
`.emscripten-version` is the pin; `build:wasm` rejects other compiler versions.
Executed on Linux with compiler revision
`6913738ec5371a88c4af5a80db0ab42bad3de681`, release identifier
`c387d7a7e9537d0041d2c3ae71b7538cc978104e`.
The emsdk installer checkout used was
`1693ce7359fc9694f198694f902a2cc061239b86`.

Install the SDK outside this repository (substitute your own SDK directory):

```sh
git clone https://github.com/emscripten-core/emsdk.git /path/to/emsdk
git -C /path/to/emsdk checkout 1693ce7359fc9694f198694f902a2cc061239b86
/path/to/emsdk/emsdk install 4.0.20
/path/to/emsdk/emsdk activate 4.0.20
source /path/to/emsdk/emsdk_env.sh
```

From a clean repository checkout:

```sh
npm ci
npm run build:wasm
npm run dev
# Or production:
npm run build
npm run preview
```

`build:wasm` runs `emcmake cmake -S libsmd -B build/wasm
-DCMAKE_BUILD_TYPE=Release`, then `cmake --build build/wasm --target smd-wasm
--parallel 2`. The Emscripten-only CMake branch links the accepted static library
and small C adapter; normal native CLI/CTest targets retain their configuration.
Windows users activate `emsdk_env.bat` in a command prompt; the Node build helper
invokes SDK Python entry points directly on Windows. These Windows commands and
the native MSVC build were **not executed** for INT02.

Build products are ignored: `build/wasm/` contains compilation output;
`public/wasm/` holds `smd-parser.mjs`, `smd-parser.wasm` and `build.json`.
The manifest hashes the pin, native sources/headers, adapter, CMake/build script
and output pair. Vite development/build reject absent, stale or modified assets
with the generation command. Rerun `build:wasm` after source/build changes and
restart Vite. Preview serves the already-built `dist/`, which includes the assets;
deploy that complete directory. No SDK/binary/generated JSON is committed.

Loading uses a modular ES-module factory, document-relative Vite `BASE_URL`, and
an explicit WASM `locateFile`. Root development and production `/smd-test/` were
tested. Relative base URLs are resolved against the document; deployment on
arbitrary hosting/CDN/cache configurations is not claimed verified. Keep the JS
loader and WASM from the same build together.

## Private WASM ABI and ownership

`libsmd/wasm/bridge.h` defines a **private wasm32** C ABI, not a stable native or
cross-compiler interface:

- `_malloc(length)` / `_free(pointer)` own input in the module. JS allocates at
  least one byte for empty input, copies UTF-8, passes explicit byte length and
  releases the input in `finally`. No terminator is required or inspected.
- `_smd_wasm_parse(pointer, length)` borrows input for the synchronous call and
  returns an owned opaque result handle. A null result means handle allocation
  failed. No success JSON is exposed until parsing and serialization finish.
- `_smd_wasm_status(handle)` returns 0 success, 1 parser/serializer exception,
  2 resource failure, or 3 invalid ABI/internal error.
- `_smd_wasm_data(handle)` / `_smd_wasm_length(handle)` borrow result bytes until
  `_smd_wasm_destroy(handle)`. Status 0 contains only JSON v1; other statuses
  contain only a non-empty diagnostic. Length excludes any terminator.
- JS acquires the current `HEAPU8` after allocations/parsing (memory may grow),
  copies the output bytes, decodes UTF-8, destroys the handle and frees input
  on success/error/validation rejection. It never retains a WASM typed-array view.

All C entry points are `noexcept`; parsing/serialization exceptions are caught.
`bad_alloc` has resource status; diagnostic-copy allocation failure still has a
static fallback diagnostic. Invalid pointers/handles remain caller errors; the
private ABI is not an arbitrary-pointer validation API. Result accessors require
live handles. Tests deliberately exercise only defined boundary arguments.

The wrapper shares one lazy initialization promise. Parse failures keep the
healthy module; failed initialization or runtime/bridge failures discard it so
the next request can retry. Initialization, runtime, parse and resource failures
have distinct visible messages. Existing request numbering is checked after
file reads and after the async parser, including the error route; a late result
cannot replace a newer choice. Inputs reset to permit same-file selection.

Resource policy: **16 MiB UTF-8 input**, **16 MiB initial / 256 MiB maximum WASM
memory**, **1 MiB stack**. These are adapter limits, not SMD semantics. JS file
decoding, UTF-8 encoding, JSON text and parsed objects require additional browser
memory. The parser runs synchronously on the UI thread; no bounded parse time,
cancellation, hostile-input resource isolation or arbitrary large-file support
is promised. Traps, stack exhaustion and browser termination are runtime failures,
not catchable C++ parse exceptions. The two accepted small specimens are verified.

## Reproduce regression and integration checks

```sh
cmake -S libsmd -B /tmp/smd-int02-native -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/smd-int02-native -j 2
ctest --test-dir /tmp/smd-int02-native --output-on-failure
node libsmd/tests/export_reference.mjs
/tmp/smd-int02-native/smd-extract public/samples/plain_column.smd > /tmp/smd-int02-plain.native.json
/tmp/smd-int02-native/smd-extract public/samples/pocket_column.smd > /tmp/smd-int02-pocket.native.json
npm run test:native-json -- /tmp/smd-int02-plain.native.json /tmp/smd-int02-pocket.native.json
npm run test:wasm -- /tmp/smd-int02-native/smd-extract
npm run test:wasm-build
npm run build
```

The WASM suite compares both specimens against freshly extracted native output,
unchanged TS execution and committed goldens, field by field: key/optional
presence, strings, array order and expected integer values exact; floating-point
tolerances remain absolute `1e-9`, relative `1e-12`. All 94 existing parser oracle
cases run through the raw WASM ABI, including accepted outputs unsuitable for
the viewer; separate wrapper checks verify validator rejection without repair.
The existing native/TS suites retain all six tokenizer cases.

Additional WASM checks cover malformed/unsupported/empty input, five invalid
UTF-8 sequences, Windows-1252 high bytes, embedded NULs through the existing
Unicode oracle case, explicit lengths, buffer cleanup, null/size limits, missing
input/result allocations, simulated runtime traps, initialization sharing/retry,
memory-view invalidation, a 1 MiB synthetic header, exhausted malloc and a real
C++ allocation exception. A 2,000-load alternating success/failure loop checks
external input/result counts return to zero each time and retained heap capacity
plateaus. This is practical leak evidence, not a formal leak proof.

`test:wasm-build` checks missing assets fail development startup and build, and inspects Rollup's
actual production module graph to exclude all four TS parser implementation
modules while requiring WASM and the existing validator.

For automated Firefox, start geckodriver and a Vite server in separate terminals:

```sh
geckodriver --port 4445 --log warn
npm run dev -- --host 127.0.0.1 --port 5174 --strictPort
python3 tests/firefox-wasm.py --extract /tmp/smd-int02-native/smd-extract
# Stop the dev server or use another terminal for production preview:
npm run build -- --base=/smd-test/
npm run preview -- --host 127.0.0.1 --port 4174 --strictPort --base=/smd-test/
python3 tests/firefox-wasm.py --url http://127.0.0.1:4174/smd-test/ --extract /tmp/smd-int02-native/smd-extract
```

The Python harness uses only the standard library; it creates/closes its own
headless session unless `--session` reuses one. Its temporary JSON lives in an
automatically removed workspace directory accessible to Firefox Snap. PNGs are
written to `/tmp` by default (`--screenshots` overrides). Only the two authorized
specimens and synthetic mutations are used.

## Executed results and review gate

Executed on Linux, 2026-11-12 IST: Node 22.22.1, GCC 15.2.0, CMake 4.2.3,
Python 3.14.4, locked TypeScript/Vite, Emscripten 4.0.20. `npm ci` passed with zero
audit vulnerabilities. Native Release build and CTest **2/2**, unchanged TS
oracle **2 specimens / 94 parser / 6 tokenizer cases**, INT01 **92 checks**, WASM
**105 checks**, missing-assets/import-graph checks and TypeScript/Vite builds passed.
The existing non-blocking bundle-size warning remains. Initial sandboxed npm
and Node child-process checks hit EPERM; authorized retries outside the sandbox
succeeded. These were execution restrictions, not parser failures.

Generated release sizes: `smd-parser.mjs` **13,191 bytes**;
`smd-parser.wasm` **197,463 bytes**. The stress check retained **51,511,296 bytes**
of heap capacity across both 1,000-load batches with no outstanding wrapper-owned
input/result handles.

Firefox **160.0**, geckodriver **0.37.1**, headless: full harness passed on
development `/` and production preview `/smd-test/`. Verified both SMD/JSON
specimens, repeated same-file selection and switching, matching inspector markup,
selection/details, fit, face-color/transparency controls, solid occlusion versus
all transparent vertices for both samples, cube pointer snap/drag, drop, actual
high-bit decoding, malformed SMD/JSON recovery, escaped names/errors, delayed
file-read success/failure, delayed WASM-initialization success/failure against a
newer JSON choice, current initialization failure with stale-state clearing and
successful retry, no TS fallback, no parsing network requests after static
initialization, and no window errors/unhandled rejections. Visually inspected
screenshots retain the pocket recess in solid and transparent modes.

**Sample-verified:** plain 8 vertices / 6 faces / 6 planes with transform;
pocket 16 / 10 / 10 without transform, including its eight-index top face.
Both bounds remain X `-375..375`, Y `-325..325`, Z `-1000..5000` mm. Source B-rep
data remains authoritative. `verticesWorld` is after the recorded B-rep transform,
not assured project-global placement; the bridge applies no additional transform.
General format semantics, encodings and topology validity remain **unknown**.

**Manual Firefox acceptance is still a user/team gate**: inspect both specimens
and native JSON interactively; verify 3D picking, table/label linkage, orbit/zoom,
all cube center/edge/corner directions and dragging, fit, face colors, solid and
transparent occlusion, repeated switching and recovery from invalid input.
Automation covers exercised interactions, not exhaustive navigation/picking.
Windows/MSVC and other browsers are untested. Review the private ABI/resource
policy and generated-asset workflow before a separately authorized merge. No
semantic portability changes or implementation scope deviations were needed.

Reference: [Emscripten C/JS interaction](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html),
[exception support](https://emscripten.org/docs/porting/exceptions.html) and
[compiler settings](https://emscripten.org/docs/tools_reference/settings_reference.html).
