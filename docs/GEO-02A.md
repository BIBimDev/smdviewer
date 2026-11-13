# GEO-02A — Exact source-index shell assembly

Fetched `origin` before editing on 2026-11-13: `HEAD`, `main` and `origin/main`
matched the accepted `9292a8be76fdba85e8121cadbcb3f8394a10ed4b`. Rechecked the
remote before delivery; unchanged. Work branch:
`codex/geo-02a-exact-shell-assembly`. The tracked tree was initially clean;
pre-existing untracked `.vscode/` remains untouched and excluded.

**Linux/GCC verification passed. Windows/MSVC is pending and remains an
acceptance gate.** This task ends at a shell; no solid is constructed.

## API and assembly policy

```cpp
#include <smd_geometry/shell.hpp>
auto faces = smd_geometry::convertFaces(parsed.geometry.brep);
auto assembled = smd_geometry::assembleShell(parsed.geometry.brep, faces);
```

`vertices[v]` and `faces[f]` retain source ordinal correspondence. `edges` is
sorted by increasing unordered source endpoint pair; each entry contains one
canonical forward OCCT edge and both source uses (face ordinal, loop position,
source traversal direction). `loopEdges[f][j]` maps each original loop occurrence
to its edge **as oriented in the shell**. The vector retains source occurrence
order even when the derived face is reversed. `reversedFaceOrdinals` is sorted.
Keep the authoritative `ParsedBrep` for original metadata and plane coefficients;
the sidecar is neither a source archive nor a persistence contract.

Only the source index pair determines edge identity. Preflight requires exactly
two uses on distinct faces, a single continuous face fan at every used vertex,
one edge-connected face set and satisfiable relative orientation constraints.
The temporary incidence/constraint graphs are validation data, not a replacement
operational topology kernel. Face 0 remains unchanged; other derived faces may
reverse. Outward orientation is **unknown**, never selected or claimed.

Assembly reuses the unchanged GEO-01 converter to validate the source under its
existing geometry policy and obtain fresh private vertices. It checks the
supplied GEO-01 collection's sizes, exact local coordinates, ordered vertex
identities, straight edges, original supporting plane equations and tolerances.
GEO-01 face validation runs on deep topology/geometry copies. New shared edges
and bounded faces use private vertices and value-copied supporting planes; no
source or input GEO-01 mutable topology/geometry handle is reused in the result.
This deliberately repeats face conversion/validation, rather than introducing
a second source-geometry validation policy. Caller inputs must be an unmodified
GEO-01 conversion of the same source.

Coordinates are exact `verticesLocal`, in **millimetres**. `verticesWorld`,
recorded B-rep transform, element and project placement are not applied. The
existing **1e-7 mm** policy remains; output vertex, edge and face tolerances may
not exceed it. There is no proximity matching, snapping, sewing, bridging,
splitting, face merging or healing. Unused source vertices retain correspondence
but do not enter the shell.

Before returning, assembly checks actual unique vertex/edge/face counts, mapped
shell children, every face's edge identities/orientations, shared edge endpoints
and opposite uses, exact coordinates, unchanged supports and tolerance bounds.
It then requires `BRepCheck_Shell::Closed()` and `Orientation()`, topological
`BRep_Tool::IsClosed`, and contextual `BRepCheck_Analyzer(shell, true, false, true)`.
The shell's `Closed` flag is set only after these checks pass. Exceptions carry
source indices; incidence/orientation errors include endpoint indices, affected
face ordinals and loop positions. No partial shell escapes.

## Verified evidence

**Sample-verified:** `plain_column.smd` yields one closed, edge-connected,
consistently oriented shell with **8 vertices, 12 shared edges and 6 mapped
faces**, and **zero relative reversals**. Each actual shared edge has exactly
two opposite uses. Original local bounds remain X `-750..0`, Y `-650..0`,
Z `0..6000` mm; the recorded placement is not applied.

Tests check all source B-rep fields before/after assembly using exact double bit
patterns and explicit optional/sequence metadata, plus the full parser JSON
snapshot for the specimen. GEO-01 shapes are compared through BREP serialization,
recursive identities, orientations and flags. Checks confirm exact local
coordinates, ordered source loops, original plane loci and private output
topology/surface handles. Subsequent result vertex/surface/flag mutations leave
GEO-01 inputs unchanged; the shell survives input destruction.

**Synthetic-test-verified:** cube assembly; every non-reference face individually
reversed and corrected exactly; reversed reference/global winding cases; ignored
world/transform data; unused nearby distinct vertices; a `5e-8 mm` off-plane
vertex retained without refitting or tolerance inflation. Rejections cover a
missing mate (including a closing occurrence), more than two edge uses, two uses
on one face, disconnected closed cubes, two touching cubes with separate fans
at one vertex, mismatched GEO-01 input, inflated tolerance, invalid indices and
empty input. Focused private-preflight tests cover an edge-connected pinched
surface and a triangulated projective plane with contradictory orientation
constraints; these are combinatorial tests, not geometric SMD specimens.

## Kubuntu/GCC commands and results

Executed on the supplied Kubuntu workspace (Ubuntu 26.04.1 LTS base), GCC 15.2.0,
CMake 4.2.3, Python 3.14.4. External OCCT **8.0.1 exactly**, official `V8_0_1`
commit `b8f597c677811d1f9f4d8a97f5ae2825c0353a42`; source SHA and installed
version fields rechecked. Kernel: C++20, Release, shared libraries, TKTopAlgo and
dependencies, no TBB or GUI modules; acquisition/configuration is recorded in
[GEO-01A](GEO-01A.md#kernel-and-build-environment).

```sh
cmake -S . -B /tmp/smd-geo02a-build -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON \
  -DOpenCASCADE_DIR=/tmp/smd-occt-8.0.1/lib/cmake/opencascade
cmake --build /tmp/smd-geo02a-build --parallel 6
LD_LIBRARY_PATH=/tmp/smd-occt-8.0.1/lib \
  ctest --test-dir /tmp/smd-geo02a-build --output-on-failure
LD_LIBRARY_PATH=/tmp/smd-occt-8.0.1/lib \
  /tmp/smd-geo02a-build/libgeometry/smd-shell-tests public/samples/plain_column.smd

env -u CMAKE_PREFIX_PATH -u OpenCASCADE_DIR -u CMAKE_TOOLCHAIN_FILE \
  cmake -S . -B /tmp/smd-geo02a-no-occt -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/smd-geo02a-no-occt --parallel 6
ctest --test-dir /tmp/smd-geo02a-no-occt --output-on-failure
git diff --check
```

**Passed:** geometry build and **4/4 CTests** (`native`, `reference-regression`,
`geometry-shell`, `geometry-faces`), **16 named shell checks**, and all **30
unchanged GEO-01 checks**, including the pocket and three corbel face regressions.
Fresh default parser build: **2/2 CTests**, with geometry OFF and no OCCT cache
entries or package path supplied. Final build has no compiler warnings; whitespace
check passes. Initial test compilation needed an OCCT shape-type correction and
deprecated container aliases were replaced; no unexpected failures remain.

## Windows/MSVC — pending, required before acceptance

No Windows environment is available here; **no Windows build or test was run**.
Use an x64 VS 2022 developer PowerShell, CMake 3.21+, Python 3.9+ and compatible
x64 MSVC Release OCCT **8.0.1 exactly**. Kernel acquisition/build commands and
the pinned source SHA are in
[GEO-01B](GEO-01B.md#windowsmsvc-verification--pending-required-before-acceptance).
Use a fresh LF checkout (required for the parser oracle fingerprints), and record
the tested commit, compiler/kernel versions and results in the PR:

```powershell
$checkout = "$env:TEMP/smd-geo02a-windows"
git -c core.autocrlf=false clone --branch codex/geo-02a-exact-shell-assembly `
  https://github.com/BIBimDev/smdviewer.git $checkout
Set-Location $checkout
git rev-parse HEAD
git ls-files --eol libsmd public/samples src/smd libgeometry
cl
Remove-Item Env:CMAKE_PREFIX_PATH,Env:OpenCASCADE_DIR,Env:CMAKE_TOOLCHAIN_FILE `
  -ErrorAction SilentlyContinue
$occtPrefix = "C:/occt-8.0.1" # Set to the actual compatible installation.
$occtConfig = Get-ChildItem $occtPrefix -Recurse -Filter OpenCASCADEConfig.cmake |
  Select-Object -First 1
$occtDll = Get-ChildItem $occtPrefix -Recurse -Filter TKernel.dll |
  Select-Object -First 1
if (!$occtConfig -or !$occtDll) { throw "OCCT installation incomplete" }
cmake -S . -B "$env:TEMP/smd-geo02a-build" -G "Visual Studio 17 2022" -A x64 `
  -DSMD_BUILD_GEOMETRY=ON "-DOpenCASCADE_DIR=$($occtConfig.Directory.FullName)"
cmake --build "$env:TEMP/smd-geo02a-build" --config Release --parallel
$env:PATH = "$($occtDll.Directory.FullName);$env:PATH"
ctest --test-dir "$env:TEMP/smd-geo02a-build" -C Release --output-on-failure
cmake -S . -B "$env:TEMP/smd-geo02a-no-occt" -G "Visual Studio 17 2022" -A x64
cmake --build "$env:TEMP/smd-geo02a-no-occt" --config Release --parallel
ctest --test-dir "$env:TEMP/smd-geo02a-no-occt" -C Release --output-on-failure
Select-String -Path "$env:TEMP/smd-geo02a-no-occt/CMakeCache.txt" `
  -Pattern "SMD_BUILD_GEOMETRY|OpenCASCADE"
```

Expected: 4/4 geometry-enabled tests, 16 shell checks and 30 GEO-01 checks;
2/2 parser-only tests, geometry OFF, no OCCT cache entries.

## Review boundary

Changed files are the new public shell API, private shell/preflight implementation,
shell tests, native build registration and this record. Parser, GEO-01 conversion,
fixtures, viewer and WASM are unchanged; browser/WASM checks were not rerun.
Submit one focused PR; do not merge. Windows execution and team review remain.
This establishes shell assembly for the plain column, not general SMD topology
rules or qualification of other specimens as solids. Explicit geometric
self-interference checks, outward orientation, solid construction and volume
validation remain GEO-02B; pocket/corbel solid qualification remains GEO-02C.
