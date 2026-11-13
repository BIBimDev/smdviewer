# GEO-03C — Half-corbel and combined pocket/corbel qualification

Fetched `origin` before editing on 2026-11-13. `main`, HEAD and `origin/main`
matched accepted PR #13 SHA `265a54a22385faf9778fb9365875205c0428a3e7`.
Task branch: `codex/geo-03c-decomposition-regressions`. Tracked files were clean;
pre-existing untracked `.vscode/` was preserved and excluded.

Both Windows/MSVC and Linux/GCC verification runs tested exact commit
**`22b2b7afb9cf5d9c37efad1a6deb3ee1f7d6696d`**. This report is the subsequent
documentation-only change; tested code and registration are unchanged. The final
PR head is recorded in the PR description and delivery message.

## Scope and independent references

**Sample-verified:** the two remaining original GEO-02 fixtures now pass through
the unchanged native parser → GEO-01 faces → GEO-02 shell and solid `S` →
GEO-03A principal-prism candidate `B` → GEO-03B differences and reconstruction
`S' = (B \ R) union A`. The three existing real regressions retain their original
meaning and assertions. Changes are limited to the existing native Boolean test,
its fixture arguments in `libgeometry/CMakeLists.txt`, and this report.

The exact added files under `public/samples/` are `SingleHalfCorbelColumn.smd`
and **`SinglePocketingSingleCorbelColumn.smd`**. The original combined filename
has **no space before `.smd`**. The similarly named newer upload and all other
newer specimens are excluded.

All coordinates below are **raw source-local millimetres**, with zero-based
source ordinals. No recorded B-rep transform, element placement or project/global
placement is applied. The following calculations use the raw vertex dimensions
documented in [GEO-02C](GEO-02C.md#independent-reference-calculations), rechecked
with the native extractor and asserted against selected raw vertices in the test.
They are independent arithmetic references, not OCCT measurements or values
derived from `V(S) - V(B)`. This arithmetic does not establish manufacturing
sequence, mould intent or recovered Boolean history.

### Half-corbel

Candidate section: X and Y `-250..250`. Boundary Z:
`-2649.999999501579..10060` (vertices 0, 2, 5, 10). Supporting source face **5**
has ordered loop `[5, 11, 10, 6]` at minimum Z.

Extension reference: X `49.99999999999818..250`, Y `250..500`;
outer-edge Z `3740.0000000000023..4040.0000000000023` (vertices 12..15).
Attachment vertices 4, 7..9 have Z `3740.000000000002..4040.000000000002`.
The tiny common translation between the attachment and outer supports preserves
the 300 mm height; neither support is rounded or rewritten.

```text
V(A) = (250 - 49.99999999999818) × (500 - 250)
       × (4040.0000000000023 - 3740.0000000000023)
     ≈ 15,000,000.000000136 mm³
R = empty
```

### Combined pocket/corbel

Candidate section: X and Y `-250..250`. Boundary Z:
`-2649.999999501579..10000` (vertices 0..2, 8, 12). Supporting source face **5**
has ordered loop `[8, 13, 12, 9]` at minimum Z.

Extension reference: X `50..250`, Y `-500..-250`,
Z `3740.0000000000023..4040.0000000000023` (vertices 14..17, 20..23).
Reduction reference: X `-250..10.000000000549335`, Y `-110..110`,
Z `9255..10000` (vertices 3..6, 10, 11, 18, 19).

```text
V(A) = (250 - 50) × (-250 - (-500))
       × (4040.0000000000023 - 3740.0000000000023)
     = 15,000,000 mm³
V(R) = (10.000000000549335 - (-250)) × (110 - (-110)) × (10000 - 9255)
     ≈ 42,614,000.00009004 mm³
```

As in GEO-02C, the reduction calculation uses the 220 mm span of vertices 18
and 19. Source vertices 4 and 10 retain Y `110.00000000000003` and
`-110.00000000000003`; their contribution is below `1e-8 mm³`. These offsets
are explicitly checked and remain in the source snapshot. The non-nominal X
depth is retained in the reference. **A and R have separate 1 mm³ absolute
volume assertions**, so their errors cannot cancel in a net-volume check.

## Candidate and reconstruction results

The existing GEO-03A minimum-Z rectangular-cap rule constructs every candidate;
there is no manual substitution or fixture-specific production logic. For the
two new fixtures, tests require exact candidate bounds, dimensions and supporting
ordinal, and independently compare its Z extrema with all raw boundary-loop
vertices. Source-local offsets are retained. The shortest decimal source bottom
`-2649.999999501579` prints as `-2649.9999995015792` at 17 significant digits;
these denote the same stored double, not a coordinate change.

Measured identically on both platforms:

| Fixture | Supporting source face | B X bounds | B Y bounds | B Z bounds | B dimensions (mm) |
| --- | ---: | --- | --- | --- | --- |
| `plain_column.smd` | 1 | -750..0 | -650..0 | 0..6000 | 750 × 650 × 6000 |
| `pocket_column.smd` | 1 | -375..375 | -325..325 | -1000..5000 | 750 × 650 × 6000 |
| `SingleCorbelColumn.smd` | 6 | -250..250 | -300..300 | -2649.999999501579..9640 | 500 × 600 × 12289.999999501579 |
| `SingleHalfCorbelColumn.smd` | 5 | -250..250 | -250..250 | -2649.999999501579..10060 | 500 × 500 × 12709.999999501579 |
| `SinglePocketingSingleCorbelColumn.smd` | 5 | -250..250 | -250..250 | -2649.999999501579..10000 | 500 × 500 × 12649.999999501579 |

| Fixture | A solids / measured volume (mm³) | R solids / measured volume (mm³) | Covered S / S' faces | V(S') - V(S) (mm³) |
| --- | --- | --- | --- | --- |
| `plain_column.smd` | 0 / 0 | 0 / 0 | 6 / 6 | 0 |
| `pocket_column.smd` | 0 / 0 | 1 / 30000000 | 10 / 10 | 0 |
| `SingleCorbelColumn.smd` | 1 / 68625000 | 0 / 0 | 10 / 12 | -0.000000476837158203125 |
| `SingleHalfCorbelColumn.smd` | 1 / 15000000.000000136 | 0 / 0 | 10 / 11 | 0 |
| `SinglePocketingSingleCorbelColumn.smd` | 1 / 15000000 | 1 / 42614000.00009004 | 14 / 15 | 0 |

The unchanged pocket-only reference is `300 × 200 × 500 = 30,000,000 mm³`;
the unchanged corbel-only reference is `500 × 305 × 450 = 68,625,000 mm³`.
The plain fixture requires both differences empty.

Every fixture reconstructs **one valid connected solid**, with **zero solid
material in each directed residual**, `S \ S'` and `S' \ S`. Every bounded face
is completely covered in both directions under the existing GEO-03B policy.
Face-count equality is deliberately not required. All nonempty outputs pass the
existing contextual exact-method validity checks and positive-volume assertions.

The fixed `1e-7 mm` Boolean/classification policy and the additional `1 mm³`
reconstruction volume comparison are unchanged. No tolerance was relaxed, no
shape healed, no face unified, no sliver discarded, and no reconstruction step
changed. Any positive-volume residual or nonempty bounded-face remainder still
fails, independently of the 1 mm³ reporting tolerance. See
[GEO-03B](GEO-03B.md#reconstruction-checks-and-tolerances) for numerical limits.

## Preservation and observed lineage

Both new fixtures reuse the GEO-03B preservation and lineage checks. Complete
parser JSON and bitwise B-rep snapshots preserve coordinates/order, face loops,
planes, names, flags, transform and source token identity. JSON is only an
additional test snapshot. BREP serialization, recursive identities/orientations/
flags, correspondence records and reversal history preserve `S`; its snapshot
now also precedes candidate construction. Candidate topology, geometry and
metadata remain unchanged after decomposition. Output topology, surfaces and
curves are independent of both operands.

Every actual output face has exactly one lineage-table entry with nonempty
ancestor records. Only source-origin ancestors may have an in-range source
ordinal. Candidate, generated and unknown faces cannot acquire invented source
ordinals. Removed history entries have no descendants. The original reordered
source-ordinal and coincident-candidate tests remain unchanged and passing.

Observed on both platforms (counts are faces with each origin/change, so
categories could overlap; these observations are not new semantic assertions):

| Fixture / output | Faces | Source / candidate / unknown origin | Unchanged / modified / generated change |
| --- | ---: | --- | --- |
| Plain S' | 6 | 0 / 6 / 0 | 6 / 0 / 0 |
| Pocket R | 6 | 4 / 2 / 0 | 0 / 6 / 0 |
| Pocket S' | 10 | 4 / 6 / 0 | 4 / 6 / 0 |
| Corbel A | 6 | 5 / 1 / 0 | 1 / 5 / 0 |
| Corbel S' | 12 | 5 / 7 / 0 | 4 / 8 / 0 |
| Half-corbel A | 6 | 5 / 1 / 0 | 1 / 5 / 0 |
| Half-corbel S' | 11 | 5 / 6 / 0 | 5 / 6 / 0 |
| Combined A | 6 | 5 / 1 / 0 | 1 / 5 / 0 |
| Combined R | 6 | 4 / 2 / 0 | 0 / 6 / 0 |
| Combined S' | 15 | 9 / 6 / 0 | 3 / 12 / 0 |

Both new fixtures exercise source and candidate history with removed, modified
and unchanged events. No generated or unknown output-face lineage was observed;
their no-invented-ordinal checks remain in place but this is not evidence of an
actual generated-face history event. Candidate faces are derived geometry even
when coincident with source faces. Intended mould and manufacturing sequence
remain **unknown**.

## Toolchains and executed verification commands

Both builds use **exact OCCT 8.0.1**, no development suffix, source `V8_0_1`
SHA `b8f597c677811d1f9f4d8a97f5ae2825c0353a42`, Release shared libraries and
TBB OFF, reusing the GEO-02B installations and TKBO/TKPrim dependency closure.
The kernel cache and generated build files were rechecked for C++20 (Windows
`stdcpp20`, Linux `gnu++20`); project geometry uses C++20 with extensions OFF.
Kernel acquisition/build instructions remain in
[GEO-02B](GEO-02B.md#toolchains-and-reproduction).

| Platform | Compiler and build tools | CTest Python |
| --- | --- | --- |
| Windows x64 | MSVC 19.44.35230.0; MSBuild 17.14.60; CMake 3.31.6-msvc6; SDK 10.0.26100.0 | 3.14.6 |
| WSL2 Ubuntu 22.04.5 LTS x86_64 | GCC 11.4.0; CMake 3.31.6; Ninja 1.11.1.git.kitware.jobserver-1 | 3.12.13 |

Windows/MSVC, from `C:/smdviewer` at the tested SHA, using fresh directories:

```powershell
git rev-parse HEAD
git ls-files --eol libsmd public/samples src/smd libgeometry
$taskBin = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$taskRoot = "$env:TEMP/smdviewer-geo01a-cf8fa7d-20261113"
$taskOcct = "$taskRoot/occt-install"
git -C "$taskRoot/occt-source" rev-parse HEAD
& "$taskBin/cmake.exe" -S . -B build/geo03c-windows -G 'Visual Studio 17 2022' -A x64 `
  -DSMD_BUILD_GEOMETRY=ON "-DOpenCASCADE_DIR=$taskOcct/cmake"
& "$taskBin/cmake.exe" --build build/geo03c-windows --config Release --parallel 6
$env:PATH = "$taskOcct/win64/vc14/bin;$env:PATH"
& "$taskBin/ctest.exe" --test-dir build/geo03c-windows -C Release --output-on-failure

Remove-Item Env:CMAKE_PREFIX_PATH,Env:OpenCASCADE_DIR,Env:CMAKE_TOOLCHAIN_FILE -ErrorAction SilentlyContinue
& "$taskBin/cmake.exe" -S . -B build/geo03c-windows-default-22b2b7a -G 'Visual Studio 17 2022' -A x64 `
  --debug-find-pkg=OpenCASCADE
& "$taskBin/cmake.exe" --build build/geo03c-windows-default-22b2b7a --config Release --parallel 6
& "$taskBin/ctest.exe" --test-dir build/geo03c-windows-default-22b2b7a -C Release --output-on-failure
Select-String build/geo03c-windows-default-22b2b7a/CMakeCache.txt -Pattern 'SMD_BUILD_GEOMETRY|OpenCASCADE'
git diff --check
```

Linux/GCC in WSL Ubuntu, same LF checkout; CMake/Ninja resolve from
`/home/dhu/.local/bin` and outputs are under `/tmp`:

```sh
cd /mnt/c/smdviewer
git rev-parse HEAD
git -C /tmp/smd-geo02b-occt-source rev-parse HEAD
cmake -S . -B /tmp/smd-geo03c-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON \
  -DOpenCASCADE_DIR=/tmp/smd-geo02b-occt-install/lib/cmake/opencascade
cmake --build /tmp/smd-geo03c-linux --parallel 6
LD_LIBRARY_PATH=/tmp/smd-geo02b-occt-install/lib \
  ctest --test-dir /tmp/smd-geo03c-linux --output-on-failure

env -u CMAKE_PREFIX_PATH -u OpenCASCADE_DIR -u CMAKE_TOOLCHAIN_FILE \
  cmake -S . -B /tmp/smd-geo03c-linux-default-22b2b7a -G Ninja -DCMAKE_BUILD_TYPE=Release \
  --debug-find-pkg=OpenCASCADE
cmake --build /tmp/smd-geo03c-linux-default-22b2b7a --parallel 6
ctest --test-dir /tmp/smd-geo03c-linux-default-22b2b7a --output-on-failure
grep -E 'SMD_BUILD_GEOMETRY|OpenCASCADE' /tmp/smd-geo03c-linux-default-22b2b7a/CMakeCache.txt
```

Configure/build/test output was retained locally; per-test results are in each
build's `Testing/Temporary/LastTest.log`. Logging wrappers and fail-fast exit
guards are omitted above. To reproduce, check out the tested SHA, preserve LF
inputs and use unused default-build directories, never a geometry-enabled cache.

## Results, limitations and review boundary

Both platforms passed **7/7 geometry-enabled CTests**: `native`,
`reference-regression`, `geometry-boolean-decomposition`,
`geometry-principal-prism`, `geometry-solid`, `geometry-shell`, `geometry-faces`.
The Boolean suite now contains **17 checks** (15 existing plus two new real
pipelines); the **16 principal-prism, 15 solid, 16 shell and 30 face checks**
remain passing. The parser reference test includes both full fixture goldens,
94 parser cases, six tokenizer cases and CLI errors. Existing rejection tests,
including the 0.05 mm³ residual and malformed-input checks, remain passing.

Both fresh default builds passed **2/2 CTests**, with
`SMD_BUILD_GEOMETRY:BOOL=OFF`, no OpenCASCADE cache entries and no OCCT package
discovery in `--debug-find-pkg=OpenCASCADE` diagnostics. No platform verification
is pending. No project compiler warnings or unresolved fixture failures remain.
`git diff --check` passes.

Production geometry, parser, fixtures, viewer and WASM are unchanged. No viewer
or WASM test result is claimed. There was no production defect requiring a rule
change. Qualification applies **only to these five original specimens under the
existing numerical policy**, not general STRAKON/DICAD input. This work does not
establish exact-arithmetic equivalence, durable face identifiers, placement
semantics, manufacturing sequence or a persistence format.

Team review is required for the independent reference calculations and specimen
qualification. The delivery is one focused PR, **not merged**; stop at GEO-03C.
