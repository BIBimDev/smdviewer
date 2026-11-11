# GEO-00: independent native SMD parser

This C++20 static library ports the accepted TypeScript parser at
`88d996f9056edf6d8b48a40263a7891d33a1ac68`. GEO-00-INT02 also compiles the same
library sources to WASM for the viewer; see [the integration report](../docs/GEO-00-INT02.md).
The native library and extractor have no Node, TypeScript, Three.js, browser,
STRAKON, or third-party native runtime dependency. There is no stable C ABI or
cross-compiler binary compatibility promise.

`wasm/bridge.h` specifies a private wasm32 C buffer ABI for that browser build.
It does not establish a stable native or cross-compiler C ABI. Native CMake and
CTest targets retain their previous configuration when not using Emscripten.

## Build and extraction

Requirements: CMake 3.20+, a C++20 compiler, and Python 3.9+ for regression tests.
Python is only required when `BUILD_TESTING=ON` (the default); it is not a library
or CLI runtime dependency.

```sh
cmake -S libsmd -B /tmp/smd-geo00-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/smd-geo00-build
ctest --test-dir /tmp/smd-geo00-build --output-on-failure
/tmp/smd-geo00-build/smd-extract public/samples/plain_column.smd
/tmp/smd-geo00-build/smd-extract public/samples/pocket_column.smd
```

For an MSVC developer terminal, the corresponding commands are:

```powershell
cmake -S libsmd -B "$env:TEMP/smd-geo00-build"
cmake --build "$env:TEMP/smd-geo00-build" --config Release
ctest --test-dir "$env:TEMP/smd-geo00-build" -C Release --output-on-failure
& "$env:TEMP/smd-geo00-build/Release/smd-extract.exe" public/samples/plain_column.smd
```

Oracle provenance checks require exact committed source/fixture bytes. Use an LF
checkout (for example, clone with `git -c core.autocrlf=false clone ...`) when
running these tests on Windows; automatic checkout CRLF conversion changes the
SHA-256 fingerprints. The parser itself handles both LF and CRLF inputs.

GCC/Linux Release is tested; **MSVC/Windows is not tested**. The code uses standard
C++20 types, explicit UTF-8 handling, locale-independent decimal conversion, and
compiler options preventing fused floating-point expressions from changing the
reference operation order. Those design choices are not Windows verification.
Use `-DBUILD_TESTING=OFF` to build just the library and extractor.

An additional Linux Debug build with AddressSanitizer and UndefinedBehaviorSanitizer
passes the same suite with `ASAN_OPTIONS=detect_leaks=0`. LeakSanitizer cannot run
under this execution environment's ptrace facility, so leak detection is untested.

`smd-extract <file.smd>` reads one UTF-8 file in binary mode. Success writes one
JSON v1 object and a newline to stdout, exit code 0. Parsing, file, encoding or
serialization errors write a diagnostic to stderr and exit 1. Argument-count
errors write usage to stderr and exit 2. JSON is constructed before writing, so
parse/serialization failures do not write partial JSON. An output-device failure
can still leave partial bytes. CLI file access is outside the parser library.

## Public API and ownership

```cpp
#include <smd/parser.hpp>
#include <smd/json.hpp>

smd::ParsedSmd parsed = smd::parseSmd10125(utf8Text);
std::string json = smd::toJsonV1(parsed);
// Reusable independently of #10125 metadata/selection:
auto tokens = smd::tokenizeSmd(utf8Text);
auto brep = smd::detectBrep(tokens);
```

`include/smd/types.hpp` defines counterparts for `Token`, `Vec2`, `Vec3`,
`ProfileEdge`, `Plane`, `Face`, `Transform3x4`, `ParsedBrep`, `Smd10125Geometry`,
and `ParsedSmd`. Results own all strings and arrays; input storage may be released
after the call. `Token::value` is `std::variant<double,std::string>`, distinguished
by `Token::type`. Callers constructing tokens must keep type and variant consistent.
Counts/indices validated and bounded by the reference use `std::size_t`. Profile
indices and numeric flags remain doubles because the TS parser does not constrain
them to native integers. `std::optional` represents absent values; empty strings
and numeric zero are present values. Successful B-rep detection fills each face's
optional plane/name in source order. Errors use `std::runtime_error`, including
the three verbatim reference parser diagnostics. Allocation failures remain
standard C++ exceptions. The API performs no filesystem I/O or logging.

Modules separate tokenizer/text handling, `#10125` metadata/profile parsing,
B-rep detection, affine transformation, JSON serialization, and CLI file I/O.
No geometry kernel, topology repair, meshing, subject selection or engineering
interpretation is included.

## JSON comparison/interchange contract v1

The contract is **proposed** for extraction/regression, not an authoritative SMD
schema or the native memory layout. Version changes require review before consumer
adoption. Root keys, in deterministic order, are `schemaVersion` (integer `1`) and
`parsed` (the existing TS `ParsedSmd` JSON shape). Object fields are emitted in the
following order; absent optional fields are omitted rather than written as null:

| Object | Fields in order |
| --- | --- |
| `parsed` | `isDicadAsc`, `headerLines`, `geometryBlockLines`, `geometry` |
| `geometry` | optional `objectId`, `name`, `partType`, `placementOrigin`, `xAxis`, `yAxis`; `profileEdges`, `brep` |
| `profileEdges[]` | `id`, `index`, `type`, `start`, `end` |
| `brep` | `verticesLocal`, `verticesWorld`, `faces`, `planes`, `faceNames`, optional `transform`, `vertexCountTokenIndex` |
| `faces[]` | `flag`, `indices`, optional `plane`, optional `name` |
| `transform` | `values` (12 numbers, row-major 3x4) |
| vectors / planes | `x`, `y` (and `z`); `a`, `b`, `c`, `d` |

Header and geometry lines retain untrimmed text, blank lines and the empty final
line when present. Line splitting is exactly `\r?\n`: CRLF terminators are
normalized, embedded/lone CR bytes remain in the source lines. Geometry lines
retain the entire tail starting at the first trimmed `#10125`, including later
block tags. Thus the original block tags and source text survive without inventing
additional identity fields. `vertexCountTokenIndex` is zero-based into tokenized
geometry-tail text, not the full document. Token line numbers are one-based relative
to the text passed to the tokenizer. `tokensToJsonV1` is a focused test API with
`{"schemaVersion":1,"tokens":[{"type":...,"value":...,"raw":...,"line":...}]}`.
Tokens are available independently; the extraction envelope mirrors `ParsedSmd`
and does not add a duplicate token array.

Arrays and face loops preserve original order; indices are zero-based. Face planes
are repeated in `planes` and `faces[].plane`, as in TS. Strings are UTF-8 with JSON
quotes, backslashes and control characters escaped. Finite doubles use up to
`max_digits10` significant digits for round-trip precision with locale-independent
formatting. Numeric formatting/whitespace need not match `JSON.stringify` bytes.
Non-finite doubles become JSON `null`, matching `JSON.stringify`; negative zero
becomes `0`. These values remain infinity/NaN/signed zero in memory. Consequently
v1 JSON is lossy for these exceptional values and is not a lossless persistence
contract. Omitted numeric optionals differ from present numeric fields encoded as
null. JSON field comparison ignores object key order, checks array order, and
separately checks that repeated extraction is byte-deterministic.

## TypeScript oracle and regression

Reference provenance is the approved baseline commit above. `tests/golden/provenance.json`
records SHA-256 hashes of all five accepted parser sources and both authorized
specimens. No new proprietary specimens are included. `plain_column.json` and
`pocket_column.json` were generated by invoking the actual TS parser; `behavior.json`
contains generated mutations of that accepted plain-column input and tokenizer
strings, with actual TS results/errors. It uses one compact JSON record per line.
Tests read these files; they never regenerate or overwrite them.

With Node 22+ and the repository's locked npm dependencies installed:

```sh
npm ci
node libsmd/tests/export_reference.mjs
```

The exporter checks source/fixture bytes against the approved Git commit, compiles
only the five existing TS files with the installed TypeScript compiler into a
temporary directory as CommonJS, invokes `parseSmd10125` and `tokenizeSmd`, then
compares regenerated results with committed goldens. It deletes its temporary
output and never changes TypeScript sources or repository compiler settings. A Git
checkout containing the baseline commit is required for generation. The CTest
comparison only needs source files, fixtures, goldens and Python, not Git or Node.

The explicit regeneration command is `node libsmd/tests/export_reference.mjs --write`.
Use it only for an intentionally reviewed golden change; it still rejects source
bytes that differ from the approved baseline. Do not silently rebaseline.

CTest runs native API/non-finite/ownership/transform checks and a Python field-by-field
comparison. The latter checks exact field presence, types, strings, identity, ordered
topology and integers, and floating results with absolute tolerance **1e-9** and
relative tolerance **1e-12** (`math.isclose`, maximum of the two thresholds). All
integer-valued numbers are compared exactly, a stricter condition than the floating
tolerances. These output tolerances differ from the unchanged B-rep acceptance
threshold **1e-5** on the absolute, unnormalized plane residual.

The suite includes malformed counts, missing/truncated data, invalid coordinates,
flags/indices/names, count mismatches, plane residual rejection and boundary-near
acceptance, invalid/missing transforms, optional metadata/profile fallbacks, first
candidate ordering, continued scanning across later markers, line/raw/escape handling,
Unicode, huge numeric lexemes, non-finite reference quirks, and CLI failures.
See `tests/export_reference.mjs` for individual case construction and expected
success/failure assertions. Native byte-encoding errors are tested separately from
the faithful TS error comparisons.

To run extraction comparisons directly:

```sh
python3 libsmd/tests/compare.py --extract /tmp/smd-geo00-build/smd-extract --tokens /tmp/smd-geo00-build/smd-token-probe
npm run build
```

The last command checks the unchanged viewer and may update the tracked generated
`tsconfig.tsbuildinfo`. Keep that build artifact out of the GEO-00 diff; do not
change its tracking policy as part of this task.

## Evidence, behavior and limitations

**Sample-verified:** plain column: 8 vertices, 6 faces, detected transform;
pocket column: 16 vertices, 10 faces, no detected transform. Both transformed-coordinate
bounds in mm are X `-375..375`, Y `-325..325`, Z `-1000..5000`. The source units
are retained; no conversion is performed. Profile edges, metadata, ordered source
vertices/faces, original planes, names, and optional transform match the TS goldens.
These results do not prove a watertight or topologically valid solid.

**Inferred/specimen-derived:** metadata offsets +2/+3/+11/+17..25, the meaning of
part type, edge fields, name association, face flags, the `0` separator, and the
`5` + 12 numbers + `1` + `0` transform signature. Preserve the reference's handling
without assigning further semantics. Local planes satisfy `a*x+b*y+c*z+d=0` and
are never transformed or normalized. `verticesWorld` means only local B-rep
vertices after the detected transform, not guaranteed project-global coordinates.
Placement origin/X/Y axes are separate evidence and are not composed with that
transform. No new units or relative geometric tolerance are inferred.

**Reference behavior retained:** the first string token in the pre-`#10125` header
must equal `DICAD ASC` (even unquoted); preceding numeric/block tokens are tolerated.
Missing `#10125` is diagnosed before marker validation. The first fully accepted
B-rep candidate wins anywhere in the geometry tail, potentially after a later block
marker. Counts are finite integers in `4..100000`; loops have `3..vertexCount`
indices; repeated indices and zero/degenerate planes are not independently rejected.
Each face has one plane and one name record; prefix/suffix numeric values are
checked as numeric but otherwise uninterpreted. Profiles are optional and malformed
ones become an empty array. Metadata with the wrong token type becomes absent.
Invalid optional transforms become absent, rather than invalidating the B-rep.
Numeric overflow/underflow follows JS Number behavior. NaN plane residuals fail no
`abs(error)>1e-5` check in the reference, so some non-finite data is accepted; this
port preserves that behavior and the JSON null representation. No broad finite-value
restriction, repair, or additional geometric acceptance heuristic is introduced.

**Intentional boundary difference / proposed restriction:** the native API accepts
well-formed UTF-8 text and rejects ill-formed UTF-8 bytes with
`SMD text must be valid UTF-8.`; the TS API instead receives an already-decoded JS
string. Arbitrary JS strings containing lone UTF-16 surrogates have no well-formed
UTF-8 counterpart. `\\xHH` decoding produces the same U+0000..U+00FF characters,
encoded as UTF-8, rather than treating escape values as raw bytes. Only these hex
escapes inside quoted lines are decoded; generic JSON/C-style unescaping is absent.
Very large/incomplete profile counts can be rejected early as an empty array when
the remaining token count proves that TS would fail. Other source grammar, parser
error messages and ordering remain unchanged for representable text.

**Unknown:** general STRAKON grammar, other object classes, placement composition,
project-global coordinates, topology validity, all real-world encodings and
cross-platform numeric identity outside tested cases. Reading the experimental
BI301 `patch.md` confirmed that subject selection is distinct from B-rep decoding;
its designation matching is experimental and excluded. No BI301 fixture, `#10071`
offsets, multiple-block selection, GEO-01 schema or downstream engineering work is
included. Team review should decide acceptance of v1, the UTF-8 boundary and retained
reference quirks; Windows validation remains outstanding.
