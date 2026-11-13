# GEO-03A — Source-local rectangular principal-prism candidate

Fetched `origin` before editing on 2026-11-13. `main`, HEAD and `origin/main`
matched the accepted `3cbda8538af00b317f613919e95a7006d12652b4`; tracked files
were clean. Work is on `codex/geo-03a-principal-prism-candidate`. Pre-existing
untracked `.vscode/` is preserved and excluded.

Both platform verification runs below executed against implementation commit
**`2af67027cb2e1f8e67b5863ba6a1b7fa37d1f013`**. This document is a subsequent
documentation-only addition; native code, tests and build registration are unchanged.

## Contract and evidence boundary

`smd_geometry::buildPrincipalPrismCandidate(const SolidAssembly&, const ParsedBrep&)`
requires an **unmodified validated GEO-02 solid and its authoritative source**.
It returns one `PrincipalPrismCandidate`: an independent OCCT `TopoDS_Solid`,
`minimumLocalMm`, `maximumLocalMm`, `dimensionsMm` (XYZ), and the zero-based
`supportingFaceOrdinal`. Bounds and dimensions refer to **source-local
millimetres**, along declared **+Z**. OCCT face traversal indices are not source
identifiers. The returned box has no source-face lineage beyond the supporting
cap ordinal; its faces are newly generated geometry.

The new stage first checks the binding to the validated solid: its one mapped
boundary shell, complete unique mapped faces and used vertices, exact used
source coordinates, and ordered face loops/edge identities. It does not rerun
GEO-02 geometric validation or certify arbitrarily modified `SolidAssembly`
objects. The existing GEO-01/GEO-02 APIs and validation policy are unchanged.
Kernel validity checks are applied only to the newly constructed candidate.

The approved rule is implemented as follows:

1. Derive minimum and maximum Z only from source vertices mapped into the actual
   solid boundary. Unused source vertices do not contribute. Reject nonpositive
   or non-finite height.
2. Require exactly one source face with every ordered-loop vertex at minimum Z.
   Require exactly four distinct XY corner coordinates, positive X/Y spans,
   each corner at the cap's X/Y extrema, and each consecutive side parallel to
   exactly one of X or Y. Loop start and winding may vary.
3. Construct a fresh `BRepPrimAPI_MakeBox` using that cap's XY bounds and the
   boundary's Z extent. Require contextual `BRepCheck_Analyzer` validity,
   infinity OUT, and finite positive volume. Return no partial candidate.

**Source pattern comparisons use exact double equality**, not the geometry
tolerance. There is no fitting, snapping, rotation, axis inference, whole-body
XY bounding-box fallback, or reading of transformed coordinates, recorded
transforms, names, placements or optional profile metadata. Kernel checks use
OCCT 8.0.1; infinity classification uses the existing **1e-7 mm** distance
policy. No tolerance is changed. The **1 mm³ absolute** volume comparison is
only the plain-column test reference, not a general threshold or geometry policy.

Diagnostics are `std::runtime_error` with explicit stage prefixes:

| Prefix | Meaning |
| --- | --- |
| `GEO-03A [input-correspondence]` | Missing/mismatched validated boundary binding; includes source ordinals where known |
| `GEO-03A [unsupported]` | Missing minimum-Z cap, multiple cap faces (listed), nonrectangular cap (ordinal), nonpositive/non-finite height or non-finite dimensions |
| `GEO-03A [kernel-construction]` | OCCT cannot construct the candidate |
| `GEO-03A [kernel-validation]` | Candidate fails OCCT validity, infinity classification or finite positive volume |

OCCT exceptions retain the active stage. Unsupported-rule rejection is distinct
from kernel construction/validation failure. Nonpositive height and degenerate
caps cannot normally reach this stage through a valid GEO-02 volume; the guards
remain defensive. Kernel failure diagnostics are implemented, but no artificial
kernel failure injection is claimed by these regression results.

Success is evidence that this rule produced a **candidate**. It does not prove
constant section, identify the intended mould, or recover construction history.
The synthetic flared body deliberately succeeds with a narrower candidate than
the upper body, demonstrating this boundary. No Boolean operation is performed.

## Acceptance and preservation

**Sample-verified: plain column only.** The unchanged native parser → GEO-01
faces → GEO-02A shell → GEO-02B solid pipeline feeds the new stage:

| Property | Checked value |
| --- | --- |
| Supporting source face | **1**, zero-based |
| Source-local X | **-750..0 mm** |
| Source-local Y | **-650..0 mm** |
| Source-local Z | **0..6000 mm** |
| XYZ dimensions | **750 × 650 × 6000 mm** |
| Independent reference volume | **750 × 650 × 6000 = 2,925,000,000 mm³**, passes within **1 mm³ absolute** |
| Kernel result | One valid OCCT solid, infinity OUT, eight corners at the reported bounds |

The test snapshots all `ParsedBrep` records, including double bit patterns,
ordered loops, planes, flags, names, source token index and transform. Additional
parser JSON snapshots cover the complete parsed fixture without making JSON a
pipeline input or persistence contract. Solid and boundary snapshots include
BREP serialization, recursive identities/orientations/flags, all correspondence
records, reversal history and recorded volume. Inputs remain unchanged on
success and rejection. The result shares no topology, surface or curve handles
with the source solid; candidate mutation and input-lifetime tests also pass.

**Synthetic-test-verified:** the 16 named GEO-03A checks cover the plain fixture;
an X-longest box (80 × 10 × 5 mm); unused vertices outside both Z ends; recorded
rotation/translation with changed world coordinates and names; translated raw
coordinates; changed cap start/winding; a flared upper body; missing cap on a
pointed solid; a split minimum cap; triangular, trapezoidal and rotated-square
caps; 1e-10 mm XY skew; sub-tolerance bottom Z slope; independent output ownership;
and mismatched source/mapping rejection. Each unsupported-rule specimen first
passes the unchanged native face/shell/solid pipeline. Synthetic volume checks
use 1e-9 mm³ absolute tolerance independently of the plain fixture's tolerance.

Other original specimens still run in the **unchanged earlier regression
suites**; they are not passed to or qualified for the new candidate stage.
GEO-03C remains separate.

## Toolchains and executed commands

Both installations were checked as exact **OCCT 8.0.1**, no development suffix,
source `V8_0_1` commit `b8f597c677811d1f9f4d8a97f5ae2825c0353a42`, C++20,
Release shared libraries, TBB OFF. Existing GEO-02B installations were reused.
**TKPrim** is now an explicit required/link target for box construction; it was
already installed in the TKBO dependency closure. Kernel build/acquisition
commands are in [GEO-02B](GEO-02B.md#toolchains-and-reproduction).

| Platform | Compiler / build tools | Regression Python |
| --- | --- | --- |
| Windows x64 | MSVC 19.44.35230.0; MSBuild 17.14.60; CMake 3.31.6-msvc6; SDK 10.0.26100.0 | 3.14.6 |
| WSL2 Ubuntu 22.04.5 LTS x86_64 | GCC 11.4.0; CMake 3.31.6; Ninja 1.11.1.git.kitware.jobserver-1 | 3.12.13 |

Windows/MSVC, from `C:/smdviewer`:

```powershell
git rev-parse HEAD
$taskBin = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$taskRoot = "$env:TEMP/smdviewer-geo01a-cf8fa7d-20261113"
$taskOcct = "$taskRoot/occt-install"
git -C "$taskRoot/occt-source" rev-parse HEAD
& "$taskBin/cmake.exe" -S . -B build/geo03a-windows -G 'Visual Studio 17 2022' -A x64 `
  -DSMD_BUILD_GEOMETRY=ON "-DOpenCASCADE_DIR=$taskOcct/cmake"
& "$taskBin/cmake.exe" --build build/geo03a-windows --config Release --parallel 6
$env:PATH = "$taskOcct/win64/vc14/bin;$env:PATH"
& "$taskBin/ctest.exe" --test-dir build/geo03a-windows -C Release --output-on-failure

Remove-Item Env:CMAKE_PREFIX_PATH,Env:OpenCASCADE_DIR,Env:CMAKE_TOOLCHAIN_FILE -ErrorAction SilentlyContinue
& "$taskBin/cmake.exe" -S . -B build/geo03a-windows-default-2af6702 -G 'Visual Studio 17 2022' -A x64 `
  --debug-find-pkg=OpenCASCADE
& "$taskBin/cmake.exe" --build build/geo03a-windows-default-2af6702 --config Release --parallel 6
& "$taskBin/ctest.exe" --test-dir build/geo03a-windows-default-2af6702 -C Release --output-on-failure
Select-String build/geo03a-windows-default-2af6702/CMakeCache.txt -Pattern 'SMD_BUILD_GEOMETRY|OpenCASCADE'
git diff --check
```

Linux/GCC in WSL, using the same LF checkout:

```sh
cd /mnt/c/smdviewer
git rev-parse HEAD
git -C /tmp/smd-geo02b-occt-source rev-parse HEAD
cmake -S . -B /tmp/smd-geo03a-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON \
  -DOpenCASCADE_DIR=/tmp/smd-geo02b-occt-install/lib/cmake/opencascade
cmake --build /tmp/smd-geo03a-linux --parallel 6
LD_LIBRARY_PATH=/tmp/smd-geo02b-occt-install/lib \
  ctest --test-dir /tmp/smd-geo03a-linux --output-on-failure

env -u CMAKE_PREFIX_PATH -u OpenCASCADE_DIR -u CMAKE_TOOLCHAIN_FILE \
  cmake -S . -B /tmp/smd-geo03a-linux-default-2af6702 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  --debug-find-pkg=OpenCASCADE
cmake --build /tmp/smd-geo03a-linux-default-2af6702 --parallel 6
ctest --test-dir /tmp/smd-geo03a-linux-default-2af6702 --output-on-failure
grep -E 'SMD_BUILD_GEOMETRY|OpenCASCADE' /tmp/smd-geo03a-linux-default-2af6702/CMakeCache.txt
```

For reproduction, check out the exact implementation SHA above. Use unused
build directories for default-build verification; never reuse a geometry-enabled
cache. Existing parser reference-regression inputs require the verified LF checkout.

## Results and review boundary

Both platforms pass **6/6 geometry-enabled CTests**: `native`,
`reference-regression`, `geometry-principal-prism`, `geometry-solid`,
`geometry-shell`, `geometry-faces`. This includes **16 new GEO-03A checks**,
**15 unchanged solid checks**, **16 unchanged shell checks** and **30 unchanged
face checks**. No project compiler warnings or unresolved test failures remain.

Both fresh default builds pass **2/2 CTests**, report
`SMD_BUILD_GEOMETRY:BOOL=OFF`, contain no OpenCASCADE cache entries, and show no
OCCT package search under `--debug-find-pkg=OpenCASCADE`. No platform gate is
pending. `git diff --check` passes. No viewer/WASM test result is claimed.

Changes are confined to `libgeometry/include/smd_geometry/principal_prism.hpp`,
`libgeometry/src/principal_prism.cpp`, `libgeometry/tests/principal_prism_tests.cpp`,
`libgeometry/CMakeLists.txt`, and this document. Parser, GEO-01/GEO-02 behavior,
fixtures, viewer and WASM remain unchanged; `.vscode/` is excluded.

Team review remains required for the candidate API, exact approved-rule
implementation, diagnostics and acceptance evidence. No scope extension is
proposed. Stop at the candidate: Boolean
differences, reconstruction and lineage belong to **GEO-03B**; qualification of
other original specimens belongs to **GEO-03C**. Submit one focused PR for review;
**do not merge**.
