# GEO-02C — Pocket and corbel solid regressions

Fetched `origin` before editing on 2026-11-13: `main`, `origin/main` and HEAD
matched the accepted `917bccd4239ee0155486581674cde98e0bda2740`.
Task branch: `codex/geo-02c-solid-regressions`. Tracked files were clean;
pre-existing untracked `.vscode/` was preserved and excluded. The parser sources,
oracle inputs and fixtures were verified as LF with `git ls-files --eol`.

Both Windows/MSVC and Linux/GCC results below were executed against exact commit
`18bee0523444ba321009614791573373ed666c7d`. This document is a subsequent
documentation-only addition; the tested code and native registration are unchanged.

## Qualification and scope

The existing native solid test now runs these four additional specimens through
the unchanged native parser → GEO-01 faces → GEO-02A shell → GEO-02B solid pipeline.
The GEO-02B `buildAndCheck` / `verify` helpers require one contextually valid
`TopoDS_Solid` with one closed mapped boundary shell, infinity OUT, finite positive
volume, exact source-local vertex coordinates, unchanged supporting planes and
entity tolerances, ordered source face loops and edge-occurrence correspondence.
They also check private output topology/geometry and unchanged input shell through
BREP serialization, recursive identities/orientations/flags and mapping records.

Each new regression additionally checks actual unique solid vertex/edge/face
counts, zero GEO-02A relative reversals, zero GEO-02B global reversals, and complete
source-record preservation through the pipeline. Source B-rep snapshots include
double bit patterns, loops, planes, flags, names, transform and source token index;
the full parser JSON snapshot is also unchanged. JSON is used only as an additional
test snapshot, not as pipeline input or an archival contract. A failure is prefixed
with the filename and active stage, retaining validator diagnostics and available
source indices without changing the validator.

**Sample-verified on both platforms:**

| Exact fixture under `public/samples/` | Actual vertices / shared edges / faces | Independent reference (mm³) | Measured on both platforms (mm³) |
| --- | --- | --- | --- |
| `pocket_column.smd` | 16 / 24 / 10 | 2895000000.0 | 2895000000 |
| `SingleCorbelColumn.smd` | 16 / 24 / 10 | 3755624999.850474 | 3755624999.8504744 |
| `SingleHalfCorbelColumn.smd` | 16 / 24 / 10 | 3192499999.875395 | 3192499999.8753948 |
| `SinglePocketingSingleCorbelColumn.smd` | 24 / 36 / 14 | 3134885999.875305 | 3134885999.8753047 |

All four have zero relative and global reversals. Volume comparisons use
**1 mm³ absolute tolerance**, independently of the unchanged **1e-7 mm** geometry
policy. No tolerance relaxation, checker change or production geometry change
was needed. The original plain-column regression and all synthetic success,
malformed-input and geometric-interference rejection tests remain passing.

## Independent reference calculations

These are manually derived test references from raw `verticesLocal` dimensions,
in millimetres. The decomposition below is a fixture interpretation for arithmetic
only; it is not production feature recognition, recovered construction history or
an OCCT Boolean calculation. It does not use the solid's measured volume to derive
the expected value. All source indices below are zero-based. No recorded B-rep,
element or project placement is applied. Small source offsets are retained rather
than rounded to nominal dimensions. Printed references are rounded to six decimal
places, far below the 1 mm³ comparison tolerance.

### `pocket_column.smd`

Main prism: X `-375..375`, Y `-325..325`, Z `-1000..5000` (vertices 3, 10).
Pocket prism: X `75..375`, Y `-125..75`, Z `4500..5000` (vertices 0, 15).
There is no corbel term.

```text
V = 750 × 650 × 6000 + 0 - 300 × 200 × 500
  = 2,925,000,000 - 30,000,000
  = 2,895,000,000 mm³
```

### `SingleCorbelColumn.smd`

Main prism: X `-250..250`, Y `-300..300`,
Z `-2649.999999501579..9640` (vertices 0, 6).
Corbel prism: X `-250..250`, Y `-605..-300`, Z `7780..8230`
(vertices 7, 10, 12, 15). There is no pocket term.

```text
V = 500 × 600 × (9640 - (-2649.999999501579))
    + 500 × 305 × 450 - 0
  ≈ 3,755,624,999.850474 mm³
```

### `SingleHalfCorbelColumn.smd`

Main prism: X and Y `-250..250`, Z `-2649.999999501579..10060`
(vertices 0, 2, 5, 10).
Corbel prism: X `49.99999999999818..250`, Y `250..500`,
Z `3740.0000000000023..4040.0000000000023` at its outer edge
(vertices 12..15); corresponding attachment vertices 4, 7..9 have Z ending
in `000002`. Both source height differences are 300 mm; the sub-picometre
offset translates the lower and upper supports together. There is no pocket term.

```text
V = 500 × 500 × (10060 - (-2649.999999501579))
    + (250 - 49.99999999999818) × 250 × 300 - 0
  ≈ 3,192,499,999.875395 mm³
```

### `SinglePocketingSingleCorbelColumn.smd`

Main prism: X and Y `-250..250`, Z `-2649.999999501579..10000`
(vertices 0..2, 8, 12).
Corbel prism: X `50..250`, Y `-500..-250`,
Z `3740.0000000000023..4040.0000000000023` (vertices 14..17, 20..23).
Pocket prism: X `-250..10.000000000549335`, Y `-110..110`,
Z `9255..10000` (vertices 3..6, 10, 11, 18, 19). The source contains
Y `±110.00000000000003` on two pocket vertices, an approximately `3e-14 mm`
offset; its volume contribution is below `1e-8 mm³`. The arithmetic reference
uses the 220 mm span of vertices 18 and 19 and retains the non-nominal X depth.

```text
V = 500 × 500 × (10000 - (-2649.999999501579))
    + 200 × 250 × 300
    - (10.000000000549335 - (-250)) × 220 × (10000 - 9255)
  ≈ 3,134,885,999.875305 mm³
```

The original filename has **no space before `.smd`**. The newer
`SinglePocketingSingleCorbelColumn .smd` and all six other newer uploads are
excluded from this task and from this solid test registration.

## Toolchains and kernel

Both builds use C++20 and exact OCCT **8.0.1**, including **TKBO**. Rechecked
kernel source SHA `b8f597c677811d1f9f4d8a97f5ae2825c0353a42` (`V8_0_1`),
installed version fields (no development suffix), C++20 kernel build settings
and TKBO linkage. Reused the GEO-02B Release shared-library installations with
no TBB or GUI modules. Windows requested `TKTopAlgo;TKBO`; Linux requested
`TKBO` with its dependency closure. See [GEO-02B](GEO-02B.md#toolchains-and-reproduction)
for kernel acquisition/build commands.

| Platform | Compiler/build tools | Regression Python |
| --- | --- | --- |
| Windows x64 | MSVC 19.44.35230.0; MSBuild 17.14.60; CMake 3.31.6-msvc6; SDK 10.0.26100.0 | 3.14.6 |
| WSL2 Ubuntu 22.04.5 LTS x86_64 | GCC 11.4.0; CMake 3.31.6; Ninja 1.11.1.git.kitware.jobserver-1 | 3.12.13 |

### Windows/MSVC commands — executed

From `C:/smdviewer`, using fresh build directories and the verified LF checkout:

```powershell
git rev-parse HEAD
git ls-files --eol libsmd public/samples src/smd libgeometry
$taskBin = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$taskRoot = "$env:TEMP/smdviewer-geo01a-cf8fa7d-20261113"
$taskOcct = "$taskRoot/occt-install"
git -C "$taskRoot/occt-source" rev-parse HEAD
& "$taskBin/cmake.exe" -S . -B build/geo02c-windows -G 'Visual Studio 17 2022' -A x64 `
  -DSMD_BUILD_GEOMETRY=ON "-DOpenCASCADE_DIR=$taskOcct/cmake"
& "$taskBin/cmake.exe" --build build/geo02c-windows --config Release --parallel 6
$env:PATH = "$taskOcct/win64/vc14/bin;$env:PATH"
& "$taskBin/ctest.exe" --test-dir build/geo02c-windows -C Release --output-on-failure

Remove-Item Env:CMAKE_PREFIX_PATH,Env:OpenCASCADE_DIR,Env:CMAKE_TOOLCHAIN_FILE -ErrorAction SilentlyContinue
& "$taskBin/cmake.exe" -S . -B build/geo02c-windows-default -G 'Visual Studio 17 2022' -A x64 `
  --debug-find-pkg=OpenCASCADE
& "$taskBin/cmake.exe" --build build/geo02c-windows-default --config Release --parallel 6
& "$taskBin/ctest.exe" --test-dir build/geo02c-windows-default -C Release --output-on-failure
Select-String build/geo02c-windows-default/CMakeCache.txt -Pattern 'SMD_BUILD_GEOMETRY|OpenCASCADE'
git diff --check
```

### Linux/GCC commands — executed

In WSL Ubuntu, same LF checkout at `/mnt/c/smdviewer`; build outputs are under
`/tmp`. CMake and Ninja resolve from `/home/dhu/.local/bin`.

```sh
cd /mnt/c/smdviewer
git rev-parse HEAD
git -C /tmp/smd-geo02b-occt-source rev-parse HEAD
cmake -S . -B /tmp/smd-geo02c-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON \
  -DOpenCASCADE_DIR=/tmp/smd-geo02b-occt-install/lib/cmake/opencascade
cmake --build /tmp/smd-geo02c-linux --parallel 6
LD_LIBRARY_PATH=/tmp/smd-geo02b-occt-install/lib \
  ctest --test-dir /tmp/smd-geo02c-linux --output-on-failure

env -u CMAKE_PREFIX_PATH -u OpenCASCADE_DIR -u CMAKE_TOOLCHAIN_FILE \
  cmake -S . -B /tmp/smd-geo02c-linux-default -G Ninja -DCMAKE_BUILD_TYPE=Release \
  --debug-find-pkg=OpenCASCADE
cmake --build /tmp/smd-geo02c-linux-default --parallel 6
ctest --test-dir /tmp/smd-geo02c-linux-default --output-on-failure
grep -E 'SMD_BUILD_GEOMETRY|OpenCASCADE' /tmp/smd-geo02c-linux-default/CMakeCache.txt
```

For a new checkout, use `git -c core.autocrlf=false clone --branch
codex/geo-02c-solid-regressions https://github.com/BIBimDev/smdviewer.git`, then
check out the exact tested SHA above. Use new build directories when repeating
the default-build check; do not reuse a geometry-enabled CMake cache.

## Results and review boundary

Both platforms passed **5/5 geometry-enabled CTests**: `native`,
`reference-regression`, `geometry-solid`, `geometry-shell`, `geometry-faces`.
The detailed CTest logs report **15 solid checks** (11 existing + 4 new),
**16 unchanged GEO-02A checks** and **30 unchanged GEO-01 checks**.
Both fresh default builds passed **2/2 CTests** with
`SMD_BUILD_GEOMETRY:BOOL=OFF`, no OpenCASCADE cache entries and no OCCT package
search in `--debug-find-pkg=OpenCASCADE` diagnostics. No compiler warnings or
specimen failures remain; `git diff --check` passes. No platform gate is pending.

Changes are confined to `libgeometry/tests/solid_tests.cpp`, native solid test
registration in `libgeometry/CMakeLists.txt`, and this document. Production
geometry, checker and tolerance policy, parser, fixtures, viewer and WASM are
unchanged. No browser or WASM test result is claimed.

Team review remains required for the fixture-derived reference calculations and
regression qualification. This is sample verification under the pinned numerical
kernel and current validator, not a general guarantee for STRAKON/DICAD solids.
No new production geometry semantics are inferred. Submit one focused PR and
**do not merge**. Stop at solid qualification: feature recognition, Boolean
decomposition, newer fixtures, placements and persistence remain outside GEO-02C.
