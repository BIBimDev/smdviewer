# GEO-03B — Boolean decomposition and verified reconstruction

Fetched `origin` before editing on 2026-11-13. `main`, HEAD and `origin/main`
matched accepted PR #12 SHA `9dc99b25a6922d4433a26d8abadee0c146653d64`.
Work is on `codex/geo-03b-boolean-decomposition`. Pre-existing untracked
`.vscode/` is preserved and excluded. No work was performed on `main`.

The final Windows/MSVC and Linux/GCC verification commands below test
**`b53573fa4f47373da049e396306c3b8f28cf05b3`**. This document is a subsequent
documentation-only addition; implementation, tests and build registration are
unchanged. The final PR head is reported in the delivery message.

## Contract and API

```cpp
#include <smd_geometry/boolean_decomposition.hpp>
// Existing unchanged parser -> faces -> shell -> validated solid -> candidate:
auto result = smd_geometry::decomposeBoolean(solidAssembly, principalCandidate);
// result.extensions: A = S \ B
// result.reductions: R = B \ S
// result.reconstructed: (B \ R) union A
// result.verification, result.history
auto verification = smd_geometry::verifyReconstruction(
    solidAssembly, result.reconstructed.solid);
```

Inputs must be unmodified validated GEO-02 and GEO-03A results from the same
pipeline. Source coordinates are **source-local millimetres**; no recorded
B-rep transform, element placement or project placement is applied. Source `S`
and its correspondence remain authoritative. GEO-03B neither calls nor changes
the GEO-03A candidate selection rule. Candidate bounds, dimensions and supporting
cap ordinal are not used to select/rebuild a prism or to assign face lineage.
As with earlier stages, the public structs are not tamper-proof certificates;
this API cannot establish historical pipeline association from OCCT handles.

The API checks the source's mapped shell and complete unique face identities
and orientations, and works only on deep copies of both operands. Input vertex,
edge and loop mappings are not rewritten or reinterpreted. Each Boolean makes
another topology-and-geometry copy using `BRepBuilderAPI_Copy(..., true, false)`
and enables `SetNonDestructive(true)`, serial execution and operation history.
Returned shapes and history do not share topology, surfaces or curves with the
caller's inputs. Internal result/history handles may share private topology;
these are in-memory correspondences, not a persistence format.

`extensions` and `reductions` contain separate `BooleanSolid` records with an
OCCT solid, finite strictly positive signed volume and face lineage. A successful
empty difference is an empty compound (possibly nested empty compounds), returned
as an empty vector. A null shape, failed/inconclusive operation, warning, loose
face/edge/vertex, unexpected container, invalid solid or duplicate solid occurrence
throws. No dimension, volume or sliver filtering converts failures into emptiness.

Each result solid must pass contextual exact-method `BRepCheck_Analyzer`, closed
edge-connected shell checks, and explicit boundary self-interference checking
(`BOPAlgo_CheckerSI`, level 5, on another deep copy). Exactly one outward shell is
required; inward cavity shells must lie inside it and outside one another. Infinity
must classify OUT and analytic signed volume must be finite and positive. Cavity
shell tests use the prior interference exclusion plus point classification of
each connected shell. Multiple outward shells are rejected as disconnected material.

Reconstruction uses the **actual** difference shapes. Empty `R` copies `B`;
otherwise it computes `B \ R`. Empty `A` leaves that result; an empty base with
nonempty `A` copies `A`; otherwise it fuses the base with `A`. Exactly one valid
connected solid must remain. No healing, simplification/unification, sewing,
snapping, retries with larger fuzz or source modification is performed.

## Reconstruction checks and tolerances

1. Validate one source solid and exactly one reconstructed solid on private copies.
2. Compute both directed residuals, `S \ S'` and `S' \ S`. Both operations must
   succeed and return no solid material. **Any positive-volume residual fails**,
   including material below 1 mm³. Unexpected lower-dimensional residuals also fail.
3. Independently compare boundaries in both directions. For each bounded face,
   subtract the other solid's bounded faces sequentially with OCCT face CUTs.
   Require an actually empty topological remainder for every face. This tests
   trimming/coverage, not merely support planes, vertex samples, face counts or
   total area. Equivalent split faces are permitted without merging them.
4. Additionally compare analytic volumes within 1 mm³ absolute tolerance.

| Quantity | Policy |
| --- | --- |
| Linear Boolean/classification tolerance | Fixed `booleanToleranceMm = 1e-7 mm` |
| Boolean fuzz | Exactly `1e-7 mm`, the pinned OCCT public API minimum/default; checked after each Boolean; never enlarged |
| Result self-interference checker | Fixed `1e-7 mm`, level 5; errors, warnings or nonempty post-treated interference map fail |
| Volumetric reconstruction comparison | `reconstructionVolumeToleranceMm3 = 1.0 mm³`, additional reporting check only |
| Independent nonempty A/R fixture assertions | `1 mm³` absolute, independently supplied references |
| Residual/sliver discard cutoff | **None**; every returned solid must have strictly positive volume and any residual solid fails |
| Boundary remainder cutoff | **None**; no positive area or lower-dimensional remainder is discarded |

OCCT 8.0.1's public `SetFuzzyValue(0)` clamps to `Precision::Confusion()`
(`1e-7 mm`). This stage explicitly uses that fixed minimum. Intrinsic entity and
intersection tolerances still apply; this is numerical kernel verification, not
exact arithmetic proof. Private Boolean-generated entities may acquire kernel
tolerances; caller tolerances are unchanged. The earlier GEO-02B checker and its
zero top-level fuzzy setting are unchanged. No result is retried or healed to pass.

Failures throw `std::runtime_error("GEO-03B [stage]: reason")` with no partial
`BooleanDecomposition`. Stages identify `input-correspondence`, `input-candidate`,
`input-source`, `extensions`, `reductions`, `reconstruct-base`, `reconstruct-union`,
`reconstruct-solid`, the directed verification operations, `verify-residuals`,
the two boundary directions, or `verify-volume`. OCCT reports/exceptions retain
the active operation/check stage. Boundary traversal indices are explicitly
diagnostic indices, not source identities.

## Face lineage and limits

`BooleanFace` maps an actual output face to one or more `FaceAncestor` records:

- **Source:** the zero-based ordinal comes only from `SolidAssembly.boundary.faces`,
  followed through deep-copy `ModifiedShape` correspondence and Boolean history.
- **Candidate:** derived GEO-03A faces have no source-face ordinal, including faces
  geometrically coincident with a source face or its supporting cap.
- **Unknown:** provenance cannot be established from available identity/history.
  No source identity is invented.

`FaceChange` distinguishes unchanged identity, modified/split descendants,
generated descendants, removed faces, and unknown change. Operation history is
composed through reconstruction; modification remains recorded through subsequent
unchanged operations. Multiple established contributors are retained. Generated
face descendants, if reported, receive unknown origin with `Generated` change and
**no source ordinal**; their history event separately retains the causal ancestor.
Unknown output faces get unknown change, not an unsupported unchanged claim.

`FaceHistory` records the stage, ancestor, immediate change and stage-local
descendant face handles. Removed records have no descendants. These handles need
not occur in the final reconstructed solid. Empty-operand copy branches carry
lineage through copy correspondence and do not invent Boolean events.

OCCT Boolean `Generated(face)` normally reports intersection **edges/vertices**,
while resulting face pieces are reported as modifications. This API tracks faces
only; it does not claim complete edge/vertex lineage or a generated-face event
for these fixtures. All candidate faces are generated geometry in the architectural
sense, represented by **Candidate** origin. Tests exercise candidate/source
separation and modified, unchanged and removed history; no actual OCCT
generated-face history event is claimed as fixture-verified. No lineage is inferred
from coplanarity, proximity, traversal order, names or the candidate cap ordinal.

## Acceptance evidence

**Sample-verified on both platforms**, using exactly these three real fixtures in
the new unchanged parser → faces → shell → validated solid → GEO-03A → GEO-03B
pipeline. Other specimens still occur only in unchanged earlier-stage suites;
this does not qualify them for GEO-03B.

| Fixture | A solids / volume mm³ | R solids / volume mm³ | Covered source / reconstructed faces | `V(S') - V(S)` mm³ |
| --- | --- | --- | --- | --- |
| `plain_column.smd` | 0 / 0 | 0 / 0 | 6 / 6 | 0 |
| `pocket_column.smd` | 0 / 0 | 1 / 30,000,000 | 10 / 10 | 0 |
| `SingleCorbelColumn.smd` | 1 / 68,625,000 | 0 / 0 | 10 / 12 | -0.000000476837158203125 |

Independent fixture references: pocket `300 × 200 × 500 = 30,000,000 mm³`;
corbel `500 × 305 × 450 = 68,625,000 mm³` (source-local dimensions documented
in GEO-02C). Both directed residual solid counts are **zero for every fixture**.
The corbel's split reconstructed boundary passes without face-count equality.

Each fixture checks complete parser JSON and bitwise source-record snapshots,
BREP serialization, recursive OCCT identity/orientation/flag snapshots, every
source correspondence record, candidate metadata and private output ownership.
Input geometry, mappings and source evidence remain unchanged. Output lineage
tables cover all actual result faces without duplicate identities.

**15 named GEO-03B checks** include the three real pipelines; valid emptiness
versus null/invalid candidate rejection; incomplete/duplicate correspondence;
equal-volume displacement rejection; `0.05 mm³` directed residual rejection;
lower-dimensional/null/empty reconstruction rejection; equivalent six-versus-seven
split-face coverage; two connected extension components; a connected hollow
extension; an empty reconstruction base; reordered source ordinals; and result
lifetime/mutation isolation. Synthetic narrower, interior and disjoint box operands
exercise Boolean mechanics defensively; they are manually supplied kernel inputs,
**not** new GEO-03A candidate rules or newly qualified real specimens. The actual
kernel error/warning guards are implemented, but no artificial kernel-failure or
generated-face-history injection is claimed.

## Toolchains and exact verification commands

Both builds use exact **OCCT 8.0.1**, no development suffix, source `V8_0_1`
SHA `b8f597c677811d1f9f4d8a97f5ae2825c0353a42`, Release shared libraries, TBB OFF,
and the existing TKBO/TKPrim dependency closure. Kernel build instructions remain
in [GEO-02B](GEO-02B.md#toolchains-and-reproduction). No toolkit registration was
added. Project compilation is C++20 with extensions OFF; the reused kernel builds
use MSVC `stdcpp20` and GCC `gnu++20`, checked in their generated build files.

| Platform | Tools | CTest Python |
| --- | --- | --- |
| Windows x64 | MSVC 19.44.35230.0, MSBuild 17.14.60, CMake 3.31.6-msvc6, SDK 10.0.26100.0 | 3.14.6 |
| WSL2 Ubuntu 22.04.5 LTS x86_64 | GCC 11.4.0, CMake 3.31.6, Ninja 1.11.1.git.kitware.jobserver-1 | 3.12.13 |

Windows/MSVC, from `C:/smdviewer` at the tested SHA:

```powershell
git rev-parse HEAD
git ls-files --eol libsmd public/samples src/smd libgeometry
$taskBin = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$taskRoot = "$env:TEMP/smdviewer-geo01a-cf8fa7d-20261113"
$taskOcct = "$taskRoot/occt-install"
git -C "$taskRoot/occt-source" rev-parse HEAD
& "$taskBin/cmake.exe" -S . -B build/geo03b-windows -G 'Visual Studio 17 2022' -A x64 `
  -DSMD_BUILD_GEOMETRY=ON "-DOpenCASCADE_DIR=$taskOcct/cmake"
& "$taskBin/cmake.exe" --build build/geo03b-windows --config Release --parallel 6
$env:PATH = "$taskOcct/win64/vc14/bin;$env:PATH"
& "$taskBin/ctest.exe" --test-dir build/geo03b-windows -C Release --output-on-failure

Remove-Item Env:CMAKE_PREFIX_PATH,Env:OpenCASCADE_DIR,Env:CMAKE_TOOLCHAIN_FILE -ErrorAction SilentlyContinue
& "$taskBin/cmake.exe" -S . -B build/geo03b-windows-default-b53573f -G 'Visual Studio 17 2022' -A x64 `
  --debug-find-pkg=OpenCASCADE
& "$taskBin/cmake.exe" --build build/geo03b-windows-default-b53573f --config Release --parallel 6
& "$taskBin/ctest.exe" --test-dir build/geo03b-windows-default-b53573f -C Release --output-on-failure
Select-String build/geo03b-windows-default-b53573f/CMakeCache.txt -Pattern 'SMD_BUILD_GEOMETRY|OpenCASCADE'
git diff --check
```

Linux/GCC in WSL Ubuntu, same LF checkout, outputs under `/tmp`:

```sh
cd /mnt/c/smdviewer
git rev-parse HEAD
git -C /tmp/smd-geo02b-occt-source rev-parse HEAD
cmake -S . -B /tmp/smd-geo03b-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON \
  -DOpenCASCADE_DIR=/tmp/smd-geo02b-occt-install/lib/cmake/opencascade
cmake --build /tmp/smd-geo03b-linux --parallel 6
LD_LIBRARY_PATH=/tmp/smd-geo02b-occt-install/lib \
  ctest --test-dir /tmp/smd-geo03b-linux --output-on-failure

env -u CMAKE_PREFIX_PATH -u OpenCASCADE_DIR -u CMAKE_TOOLCHAIN_FILE \
  cmake -S . -B /tmp/smd-geo03b-linux-default-b53573f -G Ninja -DCMAKE_BUILD_TYPE=Release \
  --debug-find-pkg=OpenCASCADE
cmake --build /tmp/smd-geo03b-linux-default-b53573f --parallel 6
ctest --test-dir /tmp/smd-geo03b-linux-default-b53573f --output-on-failure
grep -E 'SMD_BUILD_GEOMETRY|OpenCASCADE' /tmp/smd-geo03b-linux-default-b53573f/CMakeCache.txt
```

For reproduction, check out the tested SHA, retain LF files and use unused
default-build directories. CMake/Ninja resolve from `/home/dhu/.local/bin` in WSL.
The shell's default Python is 3.10.12; CMake selects **3.12.13** for CTest.

## Results and review boundary

Both platforms pass **7/7 geometry-enabled CTests**: `native`,
`reference-regression`, `geometry-boolean-decomposition`,
`geometry-principal-prism`, `geometry-solid`, `geometry-shell`, `geometry-faces`.
This includes the 15 new checks plus the unchanged 16 principal-prism, 15 solid,
16 shell and 30 face checks. Both fresh default builds pass **2/2 CTests**,
with `SMD_BUILD_GEOMETRY:BOOL=OFF`, no OpenCASCADE cache entries and no OCCT
package search under `--debug-find-pkg=OpenCASCADE`. No platform gate is pending.
No project compiler warnings remain. `git diff --check` passes.

Changes are confined to the new native header, implementation and tests,
`libgeometry/CMakeLists.txt` registration and this document. Parser,
GEO-01/02/03A behavior, fixtures, viewer and WASM remain unchanged. No viewer/WASM
test result is claimed. `.vscode/` remains untracked and excluded.

Team review is required for the API, numerical verification policy, bounded-face
coverage and lineage limits. A successful round trip verifies geometry under
these numerical checks; intended mould, manufacturing sequence and semantic
labels for partial bodies remain **unknown**. Half-corbel and combined pocket/corbel
GEO-03C qualification, newer uploads, placements and persistence are excluded.
Open one focused PR for review and **do not merge**.
