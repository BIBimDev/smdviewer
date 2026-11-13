# GEO-01C — Corbel face-conversion regressions

## Baseline and status

Fetched `origin` on 2026-11-13 before editing. `main`, `HEAD` and `origin/main`
all matched the accepted baseline `b3c5335ae27976020ce0191311c7466bc6342bd5`.
Read `AGENTS.md`, `libgeometry/README.md`, `docs/GEO-01A.md` and `docs/GEO-01B.md`.
Created `codex/geo-01c-corbel-regressions`; the tracked tree was initially clean.
The pre-existing untracked `.vscode/` directory remains untouched and excluded.

**Linux/GCC verification passed. Windows/MSVC verification is pending; GEO-01C
is not accepted while that gate is open.** Submit one focused PR for team review;
do not merge.

Only `libgeometry/tests/faces_tests.cpp`, native test arguments in
`libgeometry/CMakeLists.txt`, and this record change. The parser, GEO-01B converter,
API, tolerances, default build configuration, viewer/WASM and existing specimens
are unchanged. No additional specimens are introduced.

## Source-derived expectations and sample-verified results

Each existing specimen is read by `smd::parseSmd10125`; its native `ParsedBrep`
is passed directly to `convertFaces()`. All ordinals below are zero-based.

| Specimen | Local vertices | Valid faces | Eight-edge concave source faces |
| --- | ---: | ---: | --- |
| `SingleCorbelColumn.smd` | 16 | 10 | 2, 5 |
| `SingleHalfCorbelColumn.smd` | 16 | 10 | 1, 4 |
| `SinglePocketingSingleCorbelColumn.smd` | 24 | 14 | 0, 2, 3, 4 |

The full ordered index sequences are pinned:

| Specimen | Face | Source loop |
| --- | ---: | --- |
| SingleCorbelColumn | 2 | `{1,5,6,7,13,14,10,2}` |
| SingleCorbelColumn | 5 | `{0,3,11,15,12,9,8,4}` |
| SingleHalfCorbelColumn | 1 | `{1,7,9,8,4,5,6,2}` |
| SingleHalfCorbelColumn | 4 | `{0,11,5,4,13,14,7,1}` |
| SinglePocketingSingleCorbelColumn | 0 | `{0,1,2,3,4,5,6,7}` |
| SinglePocketingSingleCorbelColumn | 2 | `{2,9,12,7,6,10,11,3}` |
| SinglePocketingSingleCorbelColumn | 3 | `{0,7,12,13,14,16,17,15}` |
| SinglePocketingSingleCorbelColumn | 4 | `{0,15,23,20,14,13,8,1}` |

Expected areas are derived independently from the raw source polygons using the
planar shoelace formula, without OCCT. Drop the dominant source-plane normal axis
`k`, accumulate `sum(u[j]*v[j+1] - u[j+1]*v[j])` cyclically, and multiply its
absolute half by `length(normal)/abs(normal[k])`. These specimens have axial
planes, so the last factor is one. The test recomputes these areas and compares
them with the pinned constants below; the existing face check independently
compares OCCT `BRepGProp::SurfaceProperties` against the same constants. Both use
the existing absolute tolerance **1e-5 mm²**. Fractional source values are retained;
rounding to nominal integer dimensions would exceed that tolerance on some faces.

Area vectors in source-face order, mm² (tiny sub-tolerance roundoff is abbreviated):

```text
SingleCorbelColumn:
[300000, 6144999.999750789, 7511249.999700948, 5214999.999750789, 705000,
 7511249.999700948, 300000, 152500, 152500, 225000]

SingleHalfCorbelColumn:
[250000, 6294999.999750789, 6354999.999750789, 6354999.999750789,
 6429999.999750789, 250000, 50000, 50000, 75000, 60000]

SinglePocketingSingleCorbelColumn:
[192799.99999987916, 6324999.999750789, 6161099.999750789, 6264999.999750789,
 6399999.999750789, 250000, 57200.000000120854, 193700.0000004093,
 163900, 193700.0000004093, 50000, 50000, 60000, 75000]
```

**Sample-verified:** all 34 corbel faces pass the reused checks for a valid
`TopoDS_Face`, exactly one closed wire, full edge count, original loop start and
direction, original supporting plane and winding, exact local coordinates, and
shared OCCT vertex identity for every occurrence of a source index. Each distinct
source vertex maps to a distinct entity. `SingleCorbelColumn` faces 3 and 4 both
retain `sweepface_1_0_3`, remain distinct OCCT faces, and are individually checked
against their respective source loops and areas.

Before/after `toJsonV1(parsed)` equality reuses the existing test snapshot check
for unchanged parser output: ordered vertices/loops, planes, flags, names,
transform data and source metadata. This is a test comparison, not a new lossless
persistence contract. Local coordinates are interpreted as mm; no B-rep, element
or project placement transform is applied. The original GEO-01B test poisoning
`verticesWorld` and the recorded transform still passes. Failures now include the
specimen/test name and source-face index for face checks, or the converter's
original indexed diagnostic. No fixture failed or required repair.

## Linux/GCC commands and results — passed

Executed on Linux x86-64 on 2026-11-13 with GCC 15.2.0 (Ubuntu 15.2.0-16ubuntu1),
CMake 4.2.3 and Python 3.14.4. Reused external OCCT **8.0.1 exactly**, official
`V8_0_1` source commit `b8f597c677811d1f9f4d8a97f5ae2825c0353a42`; checked the
source SHA and installed package's 8/0/1 version fields and empty development
suffix. Kernel configuration: C++20, Release shared libraries, TKTopAlgo and its
dependencies, no TBB or GUI modules; acquisition/build commands remain in
[GEO-01A](GEO-01A.md#kernel-and-build-environment).

Fresh build directories, from the repository root:

```sh
git fetch origin
git rev-parse HEAD origin/main
git status --short --branch
git -C /tmp/smd-occt-8.0.1-src rev-parse HEAD
rg 'OpenCASCADE_(MAJOR|MINOR|MAINTENANCE|DEVELOPMENT)_VERSION' \
  /tmp/smd-occt-8.0.1/lib/cmake/opencascade/OpenCASCADEConfig.cmake
cmake -S . -B /tmp/smd-geo01c-build -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON \
  -DOpenCASCADE_DIR=/tmp/smd-occt-8.0.1/lib/cmake/opencascade
cmake --build /tmp/smd-geo01c-build --parallel 6
LD_LIBRARY_PATH=/tmp/smd-occt-8.0.1/lib \
  ctest --test-dir /tmp/smd-geo01c-build --output-on-failure

env -u CMAKE_PREFIX_PATH -u OpenCASCADE_DIR -u CMAKE_TOOLCHAIN_FILE \
  cmake -S . -B /tmp/smd-geo01c-no-occt -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/smd-geo01c-no-occt --parallel 6
ctest --test-dir /tmp/smd-geo01c-no-occt --output-on-failure
git diff --check
```

**Passed:** geometry-enabled build and **3/3 CTests** (`native`,
`reference-regression`, `geometry-faces`), including **30 named geometry checks**.
The unchanged plain/pocket regressions and all existing rejection checks pass,
including consecutive collinear corners. Parser regression reports both full
specimen goldens, 94 parser cases, 6 tokenizer cases and CLI errors passing.
Fresh default parser-only build: **2/2 CTests passed**, no OCCT package path
supplied; cache has `SMD_BUILD_GEOMETRY:BOOL=OFF` and no OCCT entries. No compiler
warnings or unexpected test failures. Concurrent Make builds emitted one
non-blocking jobserver FIFO-exists message. Browser/WASM builds were not rerun
for this native test/documentation task.

## Windows/MSVC commands — pending, required before acceptance

No Windows/MSVC environment is available here. **No Windows build or test has
been executed.** Use x64 Visual Studio 2022 developer PowerShell, CMake 3.21+,
Python 3.9+ and a compatible x64 MSVC Release OCCT **8.0.1 exactly** installation.
For kernel acquisition/build, use the exact commands and source SHA in
[GEO-01B](GEO-01B.md#windowsmsvc-verification--pending-required-before-acceptance).
The following assumes that installation is at `C:/occt-8.0.1`; set `$occtPrefix`
to the actual prefix. Use fresh checkout/build directories. The LF checkout is
required by the [parser oracle fingerprints](../libsmd/README.md).

```powershell
$checkout = "$env:TEMP/smd-geo01c-windows"
git -c core.autocrlf=false clone --branch codex/geo-01c-corbel-regressions `
  https://github.com/BIBimDev/smdviewer.git $checkout
Set-Location $checkout
git rev-parse HEAD
git ls-files --eol libsmd public/samples src/smd
cl
cmake --version
python --version
Remove-Item Env:CMAKE_PREFIX_PATH,Env:OpenCASCADE_DIR,Env:CMAKE_TOOLCHAIN_FILE `
  -ErrorAction SilentlyContinue
$occtPrefix = "C:/occt-8.0.1"
$occtConfig = Get-ChildItem $occtPrefix -Recurse -Filter OpenCASCADEConfig.cmake |
  Select-Object -First 1
$occtDll = Get-ChildItem $occtPrefix -Recurse -Filter TKernel.dll |
  Select-Object -First 1
if (!$occtConfig -or !$occtDll) { throw "OCCT installation incomplete" }
cmake -S . -B "$env:TEMP/smd-geo01c-build" -G "Visual Studio 17 2022" -A x64 `
  -DSMD_BUILD_GEOMETRY=ON "-DOpenCASCADE_DIR=$($occtConfig.Directory.FullName)"
cmake --build "$env:TEMP/smd-geo01c-build" --config Release --parallel
$env:PATH = "$($occtDll.Directory.FullName);$env:PATH"
ctest --test-dir "$env:TEMP/smd-geo01c-build" -C Release --output-on-failure

# Default parser-only build: no OCCT package path supplied.
cmake -S . -B "$env:TEMP/smd-geo01c-no-occt" -G "Visual Studio 17 2022" -A x64
cmake --build "$env:TEMP/smd-geo01c-no-occt" --config Release --parallel
ctest --test-dir "$env:TEMP/smd-geo01c-no-occt" -C Release --output-on-failure
Select-String -Path "$env:TEMP/smd-geo01c-no-occt/CMakeCache.txt" `
  -Pattern "SMD_BUILD_GEOMETRY|OpenCASCADE"
```

Attach compiler/kernel versions, tested commit and results to the PR. Expected:
3/3 geometry-enabled CTests including 30 geometry checks, and 2/2 default
parser-only CTests with geometry OFF and no OCCT cache entries.

## Review boundary

Fixture qualification is sample evidence, not a general STRAKON format claim.
Consecutive collinear corners remain unsupported. Windows execution and team
review remain open; do not label GEO-01C accepted yet. Stop at face conversion:
shared-edge incidence, sewing, shell assembly and solid validation belong to
GEO-02. No healing, interpretation, persistence or production behavior changes
are included.
