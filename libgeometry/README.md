# GEO-01B: native source polygons to OCCT faces

`smd-geometry` depends on `smd` and **Open CASCADE Technology 8.0.1 exactly**.
The parser, JSON v1 and browser/WASM builds do not depend on OCCT. Nothing is
downloaded by the project build. No source evidence is persisted in a new format.

## Build

Requirements: CMake 3.20+, a C++20 compiler, installed OCCT 8.0.1 development
files for `TKTopAlgo`, `TKBO` and their dependencies, and Python 3.9+ for parser
regressions. An OCCT installation built only for the GEO-01/GEO-02A toolkit
set is insufficient for the geometry-enabled build. See the
[GEO-02B build instructions](../docs/GEO-02B.md#toolchains-and-reproduction)
for adding `TKBO` and its dependencies. The default parser-only build still
does not require OCCT.
OCCT and the application must use compatible compiler, architecture, C++ runtime
and build configurations. The geometry component is a static library; the tested
OCCT installation uses shared libraries, C++20, Release, without TBB or GUI modules.

Linux, from the repository root:

```sh
cmake -S . -B /tmp/smd-geo01b-build -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON \
  -DOpenCASCADE_DIR=/path/to/occt-8.0.1/lib/cmake/opencascade
cmake --build /tmp/smd-geo01b-build --parallel
export LD_LIBRARY_PATH=/path/to/occt-8.0.1/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
ctest --test-dir /tmp/smd-geo01b-build --output-on-failure
```

Use the actual directory containing `OpenCASCADEConfig.cmake` (some installations
have a version suffix), or supply `-DCMAKE_PREFIX_PATH=/path/to/occt-8.0.1`.
The imported OCCT targets carry the library locations; headers come from
`OpenCASCADE_INCLUDE_DIR`. Missing or incompatible packages are configuration errors
when geometry is enabled. Required toolkit targets are checked explicitly.

Windows, in an x64 MSVC developer PowerShell with an LF checkout:

```powershell
cmake -S . -B "$env:TEMP/smd-geo01b-build" -A x64 `
  -DSMD_BUILD_GEOMETRY=ON -DOpenCASCADE_DIR=C:/occt-8.0.1/cmake
cmake --build "$env:TEMP/smd-geo01b-build" --config Release --parallel
# Use the directory containing TKernel.dll in your installation:
$env:PATH = "C:/occt-8.0.1/win64/vc14/bin;$env:PATH"
ctest --test-dir "$env:TEMP/smd-geo01b-build" -C Release --output-on-failure
```

Select the actual DLL directory for the installed OCCT compiler and build
configuration. The [GEO-02B Windows verification](../docs/GEO-02B.md#windows-x64msvc--executed-first)
records the tested toolkit configuration and commands for both build modes.
See `libsmd/README.md` for the LF requirement of oracle fingerprint tests.

Geometry is **off by default**. Both parser-only routes remain supported:

```sh
cmake -S . -B /tmp/smd-native -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/smd-native --parallel
ctest --test-dir /tmp/smd-native --output-on-failure
# The original standalone entry point is unchanged:
cmake -S libsmd -B /tmp/smd-parser -DCMAKE_BUILD_TYPE=Release
```

The original Emscripten entry point remains `libsmd/`; explicitly enabling geometry
with Emscripten at the root is rejected. `BUILD_TESTING=OFF` removes Python/tests
from the native geometry configuration too.

For a private shared OCCT installation, the Linux runtime loader must also find
its transitive libraries: set `LD_LIBRARY_PATH` as above (use `lib64` if applicable),
or configure your system loader. CMake imported targets locate libraries at link
time; an executable's RUNPATH alone does not locate all transitive dependencies.

## API, coordinates and correspondence

```cpp
#include <smd_geometry/faces.hpp>

// parsed is the authoritative, caller-owned smd::ParsedSmd.
auto converted = smd_geometry::convertFaces(parsed.geometry.brep);
// parsed.geometry.brep.faces[i] <-> converted.faces[i]
// parsed.geometry.brep.verticesLocal[j] <-> converted.vertices[j]
```

Link your CMake target against `smd-geometry`. The result owns OCCT handles and
does not borrow input storage. Keep the original `ParsedBrep` for metadata,
original plane coefficients, ordered loops, flags, names and recorded transform.
The two vectors provide ordinal correspondence without copying source metadata
or introducing a second geometry hierarchy. OCCT handles are mutable, so clients
must not mutate returned shapes if they rely on this correspondence.

Only `verticesLocal` is used, interpreted as millimetres. `verticesWorld`, B-rep
transform, element placement and project placement are never read or composed.
All source vertices, including unused ones, receive distinct OCCT vertex entities;
references to the same source index share that entity across faces. Edges are
independent per face. No shared edge incidence, shell, solid, sewing or healing is
performed. No conclusion about solid validity follows from successful conversion.

Before any OCCT geometry is constructed, an ordered map checks the entire vertex
array in O(V log V), O(V) storage. Equality is exact numerical equality; signed
zeros compare equal. Non-finite coordinates are rejected before map insertion.
Duplicate diagnostics identify both zero-based source indices and the coordinate
triple with round-trip precision. No proximity-based deduplication occurs.

## Supported boundary and numerical policy

GEO-01B supports one **simple convex or concave**, single-loop planar polygon per
source face, with at least three distinct vertices and a finite, nonzero plane
normal. An implicit closing edge joins the last index to the first. An explicitly
repeated closing index, other repeated indices, consecutive collinear corners,
adjacent backtracking and nonadjacent edge crossings, touches or overlaps are
rejected. Ordinary shared endpoints of adjacent edges (including last/first) are
valid. Faces are not split or triangulated for conversion.

The fixed distance tolerance remains **1e-7 mm**, matching OCCT's default topology
tolerance. Source boundary edges at or below this length are rejected, not merged.
The same minimum length is required in the temporary projection. Doubled polygon
area at or below `(1e-7 mm)²` is rejected. These are support limits, not a general
tolerance or healing policy for later milestones.

Boundary validation temporarily projects coordinates orthogonally onto an
orthonormal 2D basis of the **original source plane**, using the first loop vertex
as the local numerical origin. This changes neither source nor OCCT vertices.
All 2D distance predicates use **1e-7 mm** in that plane:

- At each consecutive corner, both free endpoints must be farther than this
  distance from the other edge's infinite supporting line. This rejects collinear
  and near-collinear corners, including backtracking; either turn sign is valid.
- Every nonadjacent segment pair is checked for a proper crossing using signed
  line distances. Otherwise, a minimum endpoint-to-closed-segment distance at or
  below the tolerance is treated as contact. This rejects exact touches/overlaps
  and conservatively rejects narrow gaps at or below `1e-7 mm`, without snapping
  or merging anything. Adjacent pairs share their endpoint and are excluded from
  this pairwise contact test; their corner check still applies.

The pairwise test takes O(n²) time per face and O(n) temporary storage. It uses
double arithmetic, not exact predicates; non-finite intermediate results fail
explicitly. Distances have units of millimetres, avoiding an unscaled cross-product
epsilon whose meaning would depend on edge length.

For the original equation `Ax + By + Cz + D = 0`, coefficients are normalized
using the largest absolute normal component first, avoiding overflow/underflow
from squaring unscaled coefficients. A point on this source equation and its
normal define `gp_Pln`. Every source loop vertex must satisfy
`abs((A*x+B*y+C*z+D)/sqrt(A²+B²+C²)) <= 1e-7 mm`, evaluated with the scaled
coefficients. This is **perpendicular geometric distance**, distinct from the
parser's existing unnormalised plane residual. Non-finite intermediate results
are rejected. Extreme coordinate magnitudes remain limited by double-precision
OCCT arithmetic; arbitrary scale robustness is not claimed.

The supporting plane's normal is flipped if necessary to agree with source
polygon winding; its geometric locus remains the original source plane. Stored
source coefficients and vertex sequences are untouched. `MakeFace(..., false)`
disables automatic inside-orientation adjustment. `BRep_Builder` assembles the
wire without `MakeWire`'s proximity-based vertex replacement. Closure is checked;
`BRepCheck_Analyzer(face, true)` validates each bounded face. Wire traversal then
verifies the original start, direction, vertex count and entity identities.
No outward-facing interpretation is made.

Invalid/unsupported input throws `std::runtime_error`; face/vertex diagnostics
use original zero-based ordinals. All face inputs are preflighted before topology
construction, and no partial result escapes. The test executable catches errors
and exits nonzero. Production CLI/framework additions are unnecessary here.

## Evidence and scope

**Sample-verified** behaviour is recorded in [the GEO-01B report](../docs/GEO-01B.md),
with the original integration evidence retained in [GEO-01A](../docs/GEO-01A.md).
The geometry regressions are the accepted `plain_column.smd` (8 vertices, 6 faces)
and `pocket_column.smd` (16 vertices, 10 faces). The pocket's source faces 0 and 3
each remain one face with eight edges, with areas 427500 and 3800000 mm².
Passing synthetic polygons establishes the stated numerical/support policy,
not additional STRAKON format rules. General SMD geometry, multiple loops,
collinear corners, shell/solid topology and downstream feature interpretation
remain unsupported or outside this milestone. Newer corbel specimens have not
been qualified. Windows/MSVC remains pending.
