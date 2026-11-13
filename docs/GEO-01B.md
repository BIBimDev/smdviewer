# GEO-01B — Simple concave source loops to OCCT faces

## Baseline and scope

Fetched `origin` on 2026-11-13 and verified PR
[#6](https://github.com/BIBimDev/smdviewer/pull/6) was merged at
2026-11-13 04:58:12 UTC. Actual implementation baseline:
`56abd9a740b87cac352491af92254cab9c6a9b35` (`origin/main`). The accepted GEO-01A
head `cf8fa7d95b9b8438842f8a9802436389afaa1622` is an ancestor of that merge;
its geometry implementation, root CMake entry point and report match the baseline.
Read `AGENTS.md`, the GEO-01A implementation and `docs/GEO-01A.md` before editing.

Dedicated branch: `codex/geo-01b-concave-faces`. The initial tracked tree was clean;
the pre-existing untracked `.vscode/` directory remains untouched and excluded.
No merge, rebase or force-push is part of this task.

Changed files:

- `libgeometry/src/faces.cpp`: replace strict convexity with explicit boundary validation.
- `libgeometry/include/smd_geometry/faces.hpp`: describe the expanded support boundary.
- `libgeometry/tests/faces_tests.cpp`: concave acceptance and invalid-boundary regressions.
- `libgeometry/CMakeLists.txt`: pass the existing pocket specimen to the native test.
- `libgeometry/README.md`: support, numerical policy and build instructions.
- `docs/GEO-01B.md`: this evidence and acceptance report.

No parser, fixture contents, root native build gate, viewer, WASM, persistence or
shell/solid code changed. The pocket fixture is the existing accepted
`public/samples/pocket_column.smd`; no additional specimen is published.

## Boundary validation and failure semantics

`convertFaces()` now supports simple convex and concave planar straight-edged
single loops. It still constructs exactly one bounded `TopoDS_Face` per source
face. Raw `verticesLocal` coordinates are interpreted as millimetres, copied
exactly and never transformed, snapped, split, deduplicated by proximity or repaired.
Source face/vertex vector ordinals provide correspondence. A source vertex index
shared across faces maps to the same OCCT vertex entity; edges remain per face.
The caller retains the authoritative `ParsedBrep`, including flags, names,
original coefficients and transforms.

Preflight uses an orthonormal 2D basis of the original source plane, with the first
loop vertex as the temporary numerical origin. Only validation uses this
projection. Consecutive corner checks allow either turn sign but reject
collinearity/backtracking. An O(n²) nonadjacent edge-pair check rejects crossings,
touches and overlaps; ordinary adjacent shared endpoints are excluded from that
pairwise check, including the closing pair. Temporary storage is O(n).

The fixed tolerance is **1e-7 mm**:

- The existing source-plane perpendicular-distance and source minimum-edge
  thresholds are unchanged. Projected edges also must exceed this length.
- At each adjacent corner, both free endpoints must be farther than this
  distance from the other edge's infinite supporting line. This also rejects
  near-collinear corners, without an angular epsilon.
- Proper crossings use signs of signed perpendicular line distances. Other
  nonadjacent pairs are rejected when the minimum endpoint-to-closed-segment
  distance is at or below `1e-7 mm`. Thus narrow gaps at or below the tolerance
  are conservatively treated as contact; vertices are never moved or merged.
- The existing minimum doubled area `(1e-7 mm)²` remains. Non-finite 2D predicate
  results fail with a source-face diagnostic. Predicates use double arithmetic;
  arbitrary coordinate scales and exact-arithmetic robustness are not claimed.

The original supporting-plane locus remains unchanged. Its operational normal
may be flipped to represent source winding, without reversing or rotating the
source loop. Construction still uses `BRep_Builder` to avoid proximity-based wire
vertex replacement, then validates each face with `BRepCheck_Analyzer(face, true)`
and checks full ordered vertex-identity traversal.

GEO-01A's complete-array validation is unchanged: before constructing any OCCT
geometry, non-finite coordinates and exact duplicate triples (including unused
vertices) are rejected. Signed zeros compare equal, nearby distinct coordinates
remain separate, and duplicate diagnostics retain both source indices and the
coordinate triple. All faces are preflighted before topology construction.
Invalid input throws indexed diagnostics; no partial conversion escapes.

## Linux/GCC verification — passed

Executed on Linux x86-64 with GCC 15.2.0, CMake 4.2.3 and Python 3.14.4.
Reused the external OCCT **8.0.1**, official `V8_0_1` source commit
`b8f597c677811d1f9f4d8a97f5ae2825c0353a42`, built as C++20 Release shared
libraries without TBB or GUI modules. Checked the source SHA and installed
`OpenCASCADEConfig.cmake` version fields, including the empty development suffix.
Acquisition and kernel build commands are recorded in
[GEO-01A](GEO-01A.md#kernel-and-build-environment). Exact package version checks,
C++20 requirements and the native-only geometry gate are unchanged.

From the repository root:

```sh
cmake -S . -B /tmp/smd-geo01b-build -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON \
  -DOpenCASCADE_DIR=/tmp/smd-occt-8.0.1/lib/cmake/opencascade
cmake --build /tmp/smd-geo01b-build --parallel 6
LD_LIBRARY_PATH=/tmp/smd-occt-8.0.1/lib \
  ctest --test-dir /tmp/smd-geo01b-build --output-on-failure
LD_LIBRARY_PATH=/tmp/smd-occt-8.0.1/lib \
  /tmp/smd-geo01b-build/libgeometry/smd-geometry-tests \
  public/samples/plain_column.smd public/samples/pocket_column.smd
```

**Passed:** build, 3/3 CTests (`native`, `reference-regression`, `geometry-faces`),
and 27 named geometry checks. No compiler warnings were emitted. The full parser
regression suite includes specimen goldens and malformed-input oracle cases.

```sh
cmake -S . -B /tmp/smd-geo01b-no-occt -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/smd-geo01b-no-occt --parallel 6
ctest --test-dir /tmp/smd-geo01b-no-occt --output-on-failure
git diff --check
```

**Passed:** default parser-only build and 2/2 CTests, with no OCCT package path
supplied. `CMAKE_PREFIX_PATH`, `OpenCASCADE_DIR` and `CMAKE_TOOLCHAIN_FILE` were
unset; the fresh cache has `SMD_BUILD_GEOMETRY:BOOL=OFF` and no OCCT entries.
Whitespace check passes. No unexpected build/test failures remain.
Browser/WASM builds were not rerun for this native-only change.

## Verified geometry

**Sample-verified:** plain column remains 8 source vertices and 6 valid faces;
its GEO-01A areas and correspondence checks remain. Pocket column produces
16 source vertices and 10 valid faces. Every face has exactly one closed wire,
with all source vertices in original start/direction order and shared identities.
Coordinates are checked for exact equality. The unchanged authoritative parser
output is checked before/after conversion for both specimens.

| Pocket source face | Source name | Edges | Area, mm² |
| --- | --- | ---: | ---: |
| 0 | top | 8 | 427500 |
| 1 | bottom | 4 | 487500 |
| 2 | sweepface_1_0_1 | 4 | 4500000 |
| 3 | sweepface_1_0_2 | 8 | 3800000 |
| 4 | sweepface_1_0_3 | 4 | 4500000 |
| 5 | sweepface_1_0_4 | 4 | 3900000 |
| 6 | top-1 | 4 | 60000 |
| 7 | sweepface_1_0_1-1 | 4 | 150000 |
| 8 | sweepface_1_0_2-1 | 4 | 100000 |
| 9 | sweepface_1_0_3-1 | 4 | 150000 |

Faces 0 and 3 retain their full eight-edge loops in individual faces, with exact
source index sequences `{0,1,2,3,5,6,7,4}` and `{0,13,12,6,5,11,10,1}`.
Area comparison tolerance is absolute `1e-5 mm²`. Supporting-plane normal-locus
comparison tolerance is `1e-12` in absolute dot product; the original plane's
normalized closest-to-origin point lies on the constructed support within
`1e-10 mm`. Vertex-to-plane distance is checked within `1e-7 mm`, and operational
plane orientation must agree with source winding.

**Synthetic-test-verified:** the former valid-concavity rejection input now passes,
including all four loop starts and both directions on axial and oblique planes.
The preserved suite checks input lifetime independence, ignored world/transform
data, oblique triangles, reversed winding, scaled/negated plane coefficients,
and complete-array failures. A within-plane-tolerance vertex is retained exactly
while the supporting plane is not refitted.

Separate invalid-boundary checks cover zero-area/collinear loops, consecutive
collinear corners, zero/sub-tolerance edges, repeated indices, a bow-tie crossing
with zero and nonzero signed area, a star with consistent local turns, nonadjacent
vertex-to-edge contact, nonadjacent collinear edge overlap, adjacent backtracking
(including the closing corner), and a near contact at `5e-8 mm`. A related
concave loop with `1e-4 mm` clearance passes. Late invalid faces still abort the
entire conversion. These establish conversion policy, not additional format rules.

## Windows/MSVC verification — pending, required before acceptance

No Windows/MSVC environment was available during implementation. **No Windows
test or build is claimed to have passed.** The milestone must remain pending
acceptance until the following are run and their compiler/kernel versions and
results are attached to the PR.

Use an x64 Visual Studio 2022 developer PowerShell, CMake 3.21+, Git and Python
3.9+, with the repository checked out using LF line endings (`core.autocrlf=false`;
see `libsmd/README.md`). Build OCCT and the application with compatible x64 MSVC
Release runtimes. For a new external OCCT installation:

```powershell
$occtSrc = "$env:TEMP/smd-geo01b-occt-8.0.1-src"
$occtBuild = "$env:TEMP/smd-geo01b-occt-8.0.1-build"
$occtPrefix = "$env:TEMP/smd-geo01b-occt-8.0.1"
git clone --depth 1 --branch V8_0_1 `
  https://github.com/Open-Cascade-SAS/OCCT.git $occtSrc
if ((git -C $occtSrc rev-parse HEAD) -ne "b8f597c677811d1f9f4d8a97f5ae2825c0353a42") {
  throw "Unexpected OCCT source SHA"
}
cmake -S $occtSrc -B $occtBuild -G "Visual Studio 17 2022" -A x64 `
  "-DINSTALL_DIR=$occtPrefix" -DBUILD_CPP_STANDARD=C++20 `
  -DBUILD_MODULE_FoundationClasses=OFF -DBUILD_MODULE_ModelingData=OFF `
  -DBUILD_MODULE_ModelingAlgorithms=OFF -DBUILD_MODULE_Visualization=OFF `
  -DBUILD_MODULE_ApplicationFramework=OFF -DBUILD_MODULE_DataExchange=OFF `
  -DBUILD_MODULE_Draw=OFF -DBUILD_ADDITIONAL_TOOLKITS=TKTopAlgo `
  -DUSE_TBB=OFF -DUSE_FREETYPE=OFF -DUSE_TK=OFF
cmake --build $occtBuild --config Release --parallel
cmake --install $occtBuild --config Release
```

From this repository's root, using fresh build directories and no inherited
`CMAKE_PREFIX_PATH`, `OpenCASCADE_DIR` or `CMAKE_TOOLCHAIN_FILE` environment values:

```powershell
$occtConfig = Get-ChildItem $occtPrefix -Recurse -Filter OpenCASCADEConfig.cmake |
  Select-Object -First 1
$occtDll = Get-ChildItem $occtPrefix -Recurse -Filter TKernel.dll |
  Select-Object -First 1
if (!$occtConfig -or !$occtDll) { throw "OCCT installation incomplete" }
cmake -S . -B "$env:TEMP/smd-geo01b-build" -G "Visual Studio 17 2022" -A x64 `
  -DSMD_BUILD_GEOMETRY=ON "-DOpenCASCADE_DIR=$($occtConfig.Directory.FullName)"
cmake --build "$env:TEMP/smd-geo01b-build" --config Release --parallel
$env:PATH = "$($occtDll.Directory.FullName);$env:PATH"
ctest --test-dir "$env:TEMP/smd-geo01b-build" -C Release --output-on-failure

# Default parser-only build, no OCCT package path supplied:
cmake -S . -B "$env:TEMP/smd-geo01b-no-occt" -G "Visual Studio 17 2022" -A x64
cmake --build "$env:TEMP/smd-geo01b-no-occt" --config Release --parallel
ctest --test-dir "$env:TEMP/smd-geo01b-no-occt" -C Release --output-on-failure
```

Expected: 3/3 geometry-enabled CTests, including 27 geometry checks, and 2/2
default parser-only CTests. A compatible preinstalled exact OCCT 8.0.1 package
can replace the kernel build; use its actual config and Release DLL directories.

## Remaining limits and team review

- Review the conservative `1e-7 mm` 2D contact/corner policy and sample evidence.
  Consecutive collinear corners remain unsupported in this milestone.
- Windows/MSVC execution is an outstanding acceptance requirement.
- Inner wires, curved edges, parser semantics, transforms, shells/solids,
  sewing/healing, feature recognition, persistence, viewer/WASM changes and
  qualification of newer corbel specimens remain outside this task.
- Successful face conversion proves neither a valid solid nor outward/shell
  orientation. Source evidence remains authoritative; the result is not a
  persistence contract. Extreme numerical ranges remain limited by double/OCCT
  arithmetic.

Submit one focused PR for review; do not merge. Stop at GEO-01B face conversion.
