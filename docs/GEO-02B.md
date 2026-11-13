# GEO-02B — One finite, outward-oriented validated solid

Baseline fetched before editing: `main` and `origin/main` both at
`6b8eb3ef9210e720095b65f446793e9970c01f6c`. Task branch:
`codex/geo-02b-validated-solid`. Tracked files were clean; the pre-existing
untracked `.vscode/` is untouched and excluded. The checkout is LF, including
the parser oracle sources and fixtures. The PR records the final tested SHA.

## API and boundary

```cpp
#include <smd_geometry/solid.hpp>
auto faces = smd_geometry::convertFaces(parsed.geometry.brep);
auto shell = smd_geometry::assembleShell(parsed.geometry.brep, faces);
auto result = smd_geometry::buildSolid(shell);
// result.solid, result.boundary, result.globallyReversed, result.volumeMm3
```

Input is an unmodified GEO-02A `ShellAssembly`, optionally globally reversed
together with its mapped face and edge-occurrence orientations. No parser,
GEO-01 or GEO-02A semantics change. Only source-local millimetre coordinates are
used: the plain column remains X `-750..0`, Y `-650..0`, Z `0..6000` mm.
No B-rep, element or project placement is applied.

The result owns private topology **and geometry**. `boundary.vertices` and
`boundary.faces` retain source ordinals. Canonical `boundary.edges` remain
FORWARD and retain their source endpoint pairs and both source use records.
`boundary.loopEdges[f][j]` remains in original source occurrence order, with
the cumulative orientation actually used by the final solid. The boundary
shell itself can be REVERSED. `reversedFaceOrdinals` retains GEO-02A's relative
correction history; it is **not** a list of final reversed faces.
`globallyReversed` separately records exactly one whole-shell reversal in this
stage. Source flags, names, original plane coefficients, order, transform and
other evidence remain in the unchanged authoritative `ParsedBrep`; this sidecar
is not a persistence/archive format.

Failure throws `std::runtime_error` prefixed `GEO-02B [stage]`, with a reason and
zero-based source face/vertex ordinals or endpoint pairs/loop positions where
available. There is no optional partially accepted shape: no result escapes a
failed check. OCCT exceptions include the active stage. Checker errors/warnings
without identifiable subshapes carry the kernel report rather than invented
source ordinals.

## Validation and isolation

1. Read-only input correspondence checks require one local nonempty shell,
   unique mapped identities, exact actual face and edge-occurrence orientations,
   canonical edge endpoints, two opposite uses on distinct faces, complete
   correspondence, one edge-connected face set, finite coordinates, planar
   faces, straight edges and the unchanged `1e-7 mm` entity tolerance ceiling.
2. `BRepBuilderAPI_Copy(..., true, false)` copies topology and geometry without
   mesh reuse. Unused source vertices are copied separately. Caller shapes are
   never added to temporary containers: OCCT `BRep_Builder::Add` can change a
   child's shared `Free` flag even when its wrapper is passed as const.
3. On the private shell, require `BRepCheck_Shell::Closed()` and `Orientation()`,
   `BRep_Tool::IsClosed`, and contextual exact-method
   `BRepCheck_Analyzer(shell, true, false, true)`.
4. Run the explicit geometric interference checker described below on another
   deep copy. Neither the caller nor the eventual result can inherit checker
   working topology, temporary intersections or tolerance updates.
5. Construct a solid with `BRepBuilderAPI_MakeSolid`. Classify infinity with
   `BRepClass3d_SolidClassifier::PerformInfinitePoint(1e-7)`. IN reverses the
   whole private shell wrapper, updates every mapped face/occurrence orientation
   and reconstructs the solid. UNKNOWN/ON fails. Require final infinity OUT.
   There are no individual-face orientation repairs in GEO-02B.
6. Require exactly one boundary shell as the solid's only child, recheck actual
   correspondence/closure/orientation, and require contextual exact-method
   `BRepCheck_Analyzer` validity for the solid. Use
   `BRepGProp::VolumeProperties(solid, properties, true, false, false)` (closed
   boundary, analytic geometry, no triangulation) and require a finite, strictly
   positive signed mass in mm³. No absolute-value correction is permitted.
7. Before returning, compare every copied vertex coordinate, support plane
   equation and vertex/edge/face tolerance exactly against the input. No
   tolerance change, fitting, healing, sewing, splitting or merging is returned.

The tests additionally serialize the complete input shell and correspondence
and record recursive OCCT identities/orientations/flags before and after success
and failure. All `ParsedBrep` fields use exact double-bit snapshots; the real
fixture also uses the full parser JSON snapshot. Result vertex, surface, curve
and flag mutations leave the input unchanged, and results survive input lifetime.

## Explicit geometric self-interference

Checker: **OCCT 8.0.1 `BOPAlgo_CheckerSI`**, toolkit **TKBO**, level **5**:
vertex/vertex, vertex/edge, edge/edge, vertex/face, edge/face and face/face.
There are no solid subshapes at this stage. Run serially, non-destructively,
on an independent deep shell copy. Reject errors, **all warnings**, missing
checker data or unexpected result indices. Reject every pair in the checker's
post-treated `DS().Interferences()` map. Map original checker subshapes back
through the copy history to source face/vertex ordinals and source edge endpoint
pairs plus both uses. Reports are sorted for deterministic diagnostics.

This uses the checker's topological identity exclusions and post-treatment;
it does not blanket-exempt adjacent face pairs. An established shared edge or
vertex is allowed, but extra overlap/crossing/contact between the same faces is
still rejected. Synthetic edge-contact and coplanar-overlap tests exercise this
distinction. A valid `BRepCheck_Analyzer` result or successful MakeSolid call
alone is never sufficient.

**Tolerance policy:** all source and returned entity tolerances remain at or
below the inherited **1e-7 mm** ceiling and must be unchanged. Additional fuzzy
tolerance is explicitly **zero**. In pinned OCCT 8.0.1, the public
`SetFuzzyValue(0)` clamps to `Precision::Confusion()`; the private
`NoFuzzChecker` subclass assigns the protected `myFuzzyValue = 0.0` and the stage
checks the reported value after execution. There is no fallback, retry at a
larger tolerance, proximity-based correspondence, or OCCT source patch.

The checker still uses OCCT's intrinsic intersection precision, operand
tolerances and their combined contact thresholds. Zero additional fuzz does
not turn OCCT into exact arithmetic, nor does `1e-7 mm` promise one universal
pairwise contact threshold. Conservative near-contact rejection is permitted;
the algorithm never responds by inflating source/result tolerances. Empty
post-treated interferences with no errors/warnings constitutes completion under
this pinned API; it has no separate `IsDone()` method. This is kernel-based
numerical validation, not a proof for arbitrary SMD geometry.

Pinned implementation references:

- [Checker and post-treatment](https://github.com/Open-Cascade-SAS/OCCT/blob/b8f597c677811d1f9f4d8a97f5ae2825c0353a42/src/ModelingAlgorithms/TKBO/BOPAlgo/BOPAlgo_CheckerSI.cxx)
- [Shared-subshape exclusions](https://github.com/Open-Cascade-SAS/OCCT/blob/b8f597c677811d1f9f4d8a97f5ae2825c0353a42/src/ModelingAlgorithms/TKBO/BOPDS/BOPDS_IteratorSI.cxx)
- [Fuzzy option minimum](https://github.com/Open-Cascade-SAS/OCCT/blob/b8f597c677811d1f9f4d8a97f5ae2825c0353a42/src/ModelingAlgorithms/TKBO/BOPAlgo/BOPAlgo_Options.cxx)

## Acceptance evidence

**Sample-verified, plain column only:** parser → GEO-01 → GEO-02A → GEO-02B
returns one solid, one closed boundary, **8 vertices, 12 shared edges, 6 mapped
faces**, infinity OUT and **2,925,000,000 mm³**. The independent reference is
`750 × 650 × 6000`; the fixture uses **1 mm³ absolute** comparison tolerance.
This is not a general minimum volume and does not change any distance tolerance.
The original shell is already outward. Reversing its whole boundary and mappings
produces exactly one reported global reversal, the same volume, and correct final
face/occurrence orientations. All inputs remain unchanged.

**Synthetic-test-verified:** a valid triangular octahedron; an inward reference
face with GEO-02A relative correction history; private unused vertex handling;
positive volume below 1 mm³; empty/mismatched mappings, inconsistent shell
orientation and inflated tolerances. Three interference fixtures share the
octahedral incidence (one connected closed oriented sphere) with four equatorial
vertices `(±10, ±10, 0)`, top `(0,0,10)` and a moved bottom pole:

| Bottom pole | Geometric defect | Required evidence |
| --- | --- | --- |
| `(20,0,5)` | Transverse face/edge crossings | Valid individual faces, GEO-02A success, then GEO-02B self-interference rejection with face and edge ordinals |
| `(5,-5,5)` | Vertex on a nonincident edge and overlapping faces/edges | Same; diagnostic includes SourceVertex[5] and edge (1,4) |
| `(0,-5,5)` | Vertex on a nonincident face and coplanar face overlap | Same; diagnostic includes SourceVertex[5] / SourceFace[0] and faces 0 / 1 |

There are **11 named GEO-02B checks**. Existing **16 GEO-02A checks** and **30
GEO-01 checks** run unchanged, including earlier face-only pocket/corbel
regressions. Those regressions do not qualify any additional specimen as a solid.

## Toolchains and reproduction

Kernel source: official `V8_0_1`, commit
`b8f597c677811d1f9f4d8a97f5ae2825c0353a42`; installed version fields verified as
8.0.1 with no development suffix. C++20, Release, shared libraries, no TBB,
no GUI modules. CMake continues to require this version **EXACT** only inside
`SMD_BUILD_GEOMETRY`. Default parser-only builds never discover OCCT.

### Windows x64/MSVC — executed first

Reused the verified installation under
`$env:TEMP/smdviewer-geo01a-cf8fa7d-20261113/occt-install` and its pinned source
and build directory. Extended `BUILD_ADDITIONAL_TOOLKITS` from `TKTopAlgo` to
`TKTopAlgo;TKBO`. Verified installed TKBO headers/import target/library/DLL;
the dependency closure additionally installs **TKPrim** and **TKShHealing**.
Linking that closure does not invoke healing; this stage calls none.

Toolchain: MSVC **19.44.35230.0**, MSBuild **17.14.60**, CMake
**3.31.6-msvc6**, Python **3.14.6**, x64 Windows SDK **10.0.26100.0**.
Use the Visual Studio CMake executable, not an MSYS generator:

```powershell
$bin = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$root = "$env:TEMP/smdviewer-geo01a-cf8fa7d-20261113"
git -C "$root/occt-source" rev-parse HEAD
& "$bin/cmake.exe" -S "$root/occt-source" -B "$root/occt-build" `
  '-DBUILD_ADDITIONAL_TOOLKITS=TKTopAlgo;TKBO'
& "$bin/cmake.exe" --build "$root/occt-build" --config Release --target INSTALL --parallel 6
git ls-files --eol libgeometry libsmd public/samples src/smd
& "$bin/cmake.exe" -S . -B build/geo02b-windows -G 'Visual Studio 17 2022' -A x64 `
  -DSMD_BUILD_GEOMETRY=ON "-DOpenCASCADE_DIR=$root/occt-install/cmake"
& "$bin/cmake.exe" --build build/geo02b-windows --config Release --parallel 6
$env:PATH = "$root/occt-install/win64/vc14/bin;$env:PATH"
& "$bin/ctest.exe" --test-dir build/geo02b-windows -C Release --output-on-failure
& build/geo02b-windows/libgeometry/Release/smd-solid-tests.exe public/samples/plain_column.smd

Remove-Item Env:CMAKE_PREFIX_PATH,Env:OpenCASCADE_DIR,Env:CMAKE_TOOLCHAIN_FILE -ErrorAction SilentlyContinue
& "$bin/cmake.exe" -S . -B build/geo02b-windows-default -G 'Visual Studio 17 2022' -A x64
& "$bin/cmake.exe" --build build/geo02b-windows-default --config Release --parallel 6
& "$bin/ctest.exe" --test-dir build/geo02b-windows-default -C Release --output-on-failure
Select-String build/geo02b-windows-default/CMakeCache.txt -Pattern 'SMD_BUILD_GEOMETRY|OpenCASCADE'
git diff --check
```

**Passed:** 5/5 geometry-enabled CTests, all 11 named solid checks; fresh default
build 2/2 CTests, `SMD_BUILD_GEOMETRY:BOOL=OFF`, no OCCT cache entries. No project
compiler warnings. Earlier development runs caught and resolved shared `Free`
flag mutation; no failed checks remain in this Windows result.

### Linux/GCC — passed after Windows

WSL2 Ubuntu 22.04.5 LTS, GCC 11.4.0, CMake 3.31.6, Ninja 1.11.1;
CMake selected Python **3.12.13** for regression tests (the shell's `python3`
used to install build tools is 3.10.12). Executed against the same LF checkout
at `/mnt/c/smdviewer`, with Linux binaries/build directories under `/tmp`.
The kernel source was cloned locally from the verified Windows source and its
SHA rechecked before building. The equivalent public-source reproduction is:

```sh
python3 -m pip install --user cmake==3.31.6 ninja==1.11.1.4
export PATH="$HOME/.local/bin:$PATH"
git clone --branch V8_0_1 --depth 1 https://github.com/Open-Cascade-SAS/OCCT.git /tmp/smd-geo02b-occt-source
git -C /tmp/smd-geo02b-occt-source rev-parse HEAD
cmake -S /tmp/smd-geo02b-occt-source -B /tmp/smd-geo02b-occt-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/tmp/smd-geo02b-occt-install \
  -DBUILD_CPP_STANDARD=C++20 -DBUILD_LIBRARY_TYPE=Shared -DBUILD_ADDITIONAL_TOOLKITS=TKBO \
  -DBUILD_MODULE_FoundationClasses=OFF -DBUILD_MODULE_ModelingData=OFF \
  -DBUILD_MODULE_ModelingAlgorithms=OFF -DBUILD_MODULE_Visualization=OFF \
  -DBUILD_MODULE_ApplicationFramework=OFF -DBUILD_MODULE_DataExchange=OFF \
  -DBUILD_MODULE_Draw=OFF -DBUILD_USE_PCH=OFF -DBUILD_RESOURCES=OFF \
  -DBUILD_GTEST=OFF -DUSE_TBB=OFF
cmake --build /tmp/smd-geo02b-occt-build --parallel 6
cmake --install /tmp/smd-geo02b-occt-build
cmake -S . -B /tmp/smd-geo02b-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON \
  -DOpenCASCADE_DIR=/tmp/smd-geo02b-occt-install/lib/cmake/opencascade
cmake --build /tmp/smd-geo02b-linux --parallel 6
LD_LIBRARY_PATH=/tmp/smd-geo02b-occt-install/lib \
  ctest --test-dir /tmp/smd-geo02b-linux --output-on-failure
LD_LIBRARY_PATH=/tmp/smd-geo02b-occt-install/lib \
  /tmp/smd-geo02b-linux/libgeometry/smd-solid-tests public/samples/plain_column.smd
env -u CMAKE_PREFIX_PATH -u OpenCASCADE_DIR -u CMAKE_TOOLCHAIN_FILE \
  cmake -S . -B /tmp/smd-geo02b-linux-default -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/smd-geo02b-linux-default --parallel 6
ctest --test-dir /tmp/smd-geo02b-linux-default --output-on-failure
grep -E 'SMD_BUILD_GEOMETRY|OpenCASCADE' /tmp/smd-geo02b-linux-default/CMakeCache.txt
```

**Passed:** 5/5 geometry-enabled CTests, all 11 named solid checks and exact
`2925000000 mm³` plain-column volume; fresh default build 2/2 CTests, geometry
OFF and no OCCT cache entries. No project compiler warnings. Installed kernel
version fields were rechecked as exactly 8.0.1. Both operating-system build/test
gates have executed; no platform verification is pending.

## Review boundary and limitations

Changed files: native solid API, implementation, tests, geometry build
registration and this document. Parser, GEO-01 conversion, GEO-02A assembly,
specimens, viewer and WASM are unchanged. No browser/WASM tests are claimed.
Only the plain column is sample-verified for this stage. The original pocket
and three corbel specimens remain GEO-02C; newer uploads, feature recognition,
Boolean decomposition, placements and persistence remain outside this task.

Team review should confirm the source-correspondence API, the conservative
checker policy and its pinned zero-additional-fuzz adapter, and both toolchain
results. No proof of general SMD geometric validity or kernel completeness is
claimed. Submit one focused PR; **do not merge**.
