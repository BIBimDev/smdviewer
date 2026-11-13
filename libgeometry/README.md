# GEO-01A: native source polygons to OCCT faces

`smd-geometry` depends on `smd` and **Open CASCADE Technology 8.0.1 exactly**.
The parser, JSON v1 and browser/WASM builds do not depend on OCCT. Nothing is
downloaded by the project build. No source evidence is persisted in a new format.

## Build

Requirements: CMake 3.20+, C++20 compiler, installed OCCT 8.0.1 development files
(at least TKTopAlgo and its dependencies), and Python 3.9+ for parser regressions.
OCCT and the application must use compatible compiler, architecture, C++ runtime
and build configurations. The geometry component is a static library; the tested
OCCT installation uses shared libraries, C++20, Release, without TBB or GUI modules.

Linux, from the repository root:

```sh
cmake -S . -B /tmp/smd-geo01a-build -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON \
  -DOpenCASCADE_DIR=/path/to/occt-8.0.1/lib/cmake/opencascade
cmake --build /tmp/smd-geo01a-build --parallel
export LD_LIBRARY_PATH=/path/to/occt-8.0.1/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
ctest --test-dir /tmp/smd-geo01a-build --output-on-failure
```

Use the actual directory containing `OpenCASCADEConfig.cmake` (some installations
have a version suffix), or supply `-DCMAKE_PREFIX_PATH=/path/to/occt-8.0.1`.
The imported OCCT targets carry the library locations; headers come from
`OpenCASCADE_INCLUDE_DIR`. Missing or incompatible packages are configuration errors
when geometry is enabled. Required toolkit targets are checked explicitly.

Windows, in an x64 MSVC developer PowerShell with an LF checkout:

```powershell
cmake -S . -B "$env:TEMP/smd-geo01a-build" -A x64 `
  -DSMD_BUILD_GEOMETRY=ON -DOpenCASCADE_DIR=C:/occt-8.0.1/cmake
cmake --build "$env:TEMP/smd-geo01a-build" --config Release --parallel
# Use the directory containing TKernel.dll in your installation:
$env:PATH = "C:/occt-8.0.1/win64/vc14/bin;$env:PATH"
ctest --test-dir "$env:TEMP/smd-geo01a-build" -C Release --output-on-failure
```

Select the installed OCCT compiler/configuration's actual DLL directory. Windows
has **not** been executed here; these commands require independent verification.
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

GEO-01A supports one **strictly convex**, single-loop planar polygon per source
face, with at least three distinct vertices and a finite, nonzero plane normal.
An implicit closing edge joins the last index to the first. An explicitly repeated
closing index, other repeated indices, collinear corners, concave boundaries and
self-intersections are rejected. Non-convex acceptance is deferred to GEO-01B.
The strict convexity check tests all other vertices against each directed edge
(O(n²) per face), also rejecting star loops with locally consistent turns.

The fixed distance tolerance is **1e-7 mm**, matching OCCT's default topology
tolerance. Edges at or below this length are rejected, not merged. Each non-edge
vertex must be more than that distance to the interior side of each boundary
edge. Doubled polygon area at or below `(1e-7 mm)²` is rejected. These are support
limits, not a general tolerance or healing policy for later milestones.

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

**Sample-verified** behaviour is recorded in [the technical report](../docs/GEO-01A.md).
The only geometry regression specimen is the accepted `plain_column.smd`.
Passing synthetic polygons establishes the stated numerical/support policy,
not additional STRAKON format rules. General SMD geometry, multiple loops,
non-convex polygons, shell/solid topology and downstream feature interpretation
remain unsupported or outside this milestone.
