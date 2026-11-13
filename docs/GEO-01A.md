# GEO-01A — OCCT integration and basic face construction

## Baseline and delivered scope

Implementation baseline: `e72b5adb46fbf092f070752cf756fd6d81ed1f91`, fetched
`origin/main` on 2026-11-13. The initial checkout was the roadmap branch at
`eceb074f791600221b15e1dd4ef8a74e2f010c3a`; its tracked tree matched the baseline.
The dedicated branch is `codex/geo-01a-occt-faces`, created from `origin/main`.
The pre-existing untracked `.vscode/` directory was left untouched and excluded.

Added files:

- `CMakeLists.txt`: optional native geometry entry point, off by default.
- `libgeometry/CMakeLists.txt`: exact OCCT version and toolkit requirements.
- `libgeometry/include/smd_geometry/faces.hpp`: owning source-ordinal API.
- `libgeometry/src/faces.cpp`: validation and individual face conversion.
- `libgeometry/tests/faces_tests.cpp`: focused regression/negative checks.
- `libgeometry/README.md`: API, support/tolerance policy, Linux/MSVC commands.
- `docs/GEO-01A.md`: this evidence report.

No existing parser, browser, JSON, WASM, specimen or build configuration files
were changed. The browser build regenerated tracked `tsconfig.tsbuildinfo`; only
that run's generated change was restored to baseline. No housekeeping is included.

## Kernel and build environment

Linux x86-64, GCC 15.2.0, CMake 4.2.3, Python 3.14.4, Node 22.22.1.
OCCT **8.0.1**, official tag `V8_0_1`, source commit
`b8f597c677811d1f9f4d8a97f5ae2825c0353a42`:
[upstream release](https://github.com/Open-Cascade-SAS/OCCT/releases/tag/V8_0_1).
Built outside the repository and not vendored. Exact acquisition/configuration:

```sh
git clone --depth 1 --branch V8_0_1 \
  https://github.com/Open-Cascade-SAS/OCCT.git /tmp/smd-occt-8.0.1-src
cmake -S /tmp/smd-occt-8.0.1-src -B /tmp/smd-occt-8.0.1-build \
  -DCMAKE_BUILD_TYPE=Release -DINSTALL_DIR=/tmp/smd-occt-8.0.1 \
  -DBUILD_CPP_STANDARD=C++20 \
  -DBUILD_MODULE_FoundationClasses=OFF -DBUILD_MODULE_ModelingData=OFF \
  -DBUILD_MODULE_ModelingAlgorithms=OFF -DBUILD_MODULE_Visualization=OFF \
  -DBUILD_MODULE_ApplicationFramework=OFF -DBUILD_MODULE_DataExchange=OFF \
  -DBUILD_MODULE_Draw=OFF -DBUILD_ADDITIONAL_TOOLKITS=TKTopAlgo \
  -DUSE_TBB=OFF -DUSE_FREETYPE=OFF -DUSE_TK=OFF
cmake --build /tmp/smd-occt-8.0.1-build --parallel 10
cmake --install /tmp/smd-occt-8.0.1-build
```

OCCT defaults to shared libraries. `TKTopAlgo` pulls in its required foundation,
modeling-data and geometry-algorithm toolkits; no visualization or exchange modules
were built. OCCT's attempt to write a user CMake package registry entry issued a
non-blocking filesystem warning. Explicit package paths were used for testing.

## Commands and results

All commands below ran on Linux from the repository root. Final native builds
completed without compiler warnings from the new component.

```sh
cmake -S . -B /tmp/smd-geo01a-build -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON -DCMAKE_PREFIX_PATH=/tmp/smd-occt-8.0.1
cmake --build /tmp/smd-geo01a-build --parallel 6
LD_LIBRARY_PATH=/tmp/smd-occt-8.0.1/lib \
  ctest --test-dir /tmp/smd-geo01a-build --output-on-failure
LD_LIBRARY_PATH=/tmp/smd-occt-8.0.1/lib \
  /tmp/smd-geo01a-build/libgeometry/smd-geometry-tests public/samples/plain_column.smd
```

**Passed:** 3/3 CTests (`native`, `reference-regression`, `geometry-faces`);
the geometry executable passed 19 named checks. The parser regression includes
the original full specimen goldens and malformed-input oracle cases.

```sh
cmake -S . -B /tmp/smd-geo01a-no-occt -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/smd-geo01a-no-occt --parallel 6
ctest --test-dir /tmp/smd-geo01a-no-occt --output-on-failure
cmake -S libsmd -B /tmp/smd-geo00-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/smd-geo00-build --parallel 6
ctest --test-dir /tmp/smd-geo00-build --output-on-failure
cmake -S . -B /tmp/smd-geo01a-no-tests -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON -DBUILD_TESTING=OFF \
  -DOpenCASCADE_DIR=/tmp/smd-occt-8.0.1/lib/cmake/opencascade
cmake --build /tmp/smd-geo01a-no-tests --parallel 6
```

**Passed:** both parser-only routes, 2/2 CTests each, without discovering OCCT;
geometry with tests disabled also builds and requires no Python discovery.

```sh
npm ci
npm run build
npm run test:native-json
npm run test:wasm -- /tmp/smd-geo01a-no-occt/libsmd/smd-extract
npm run test:wasm-build
git diff --check
```

**Passed:** dependency install (zero audit vulnerabilities), browser production
build, 90 native JSON checks, 105 WASM checks, both WASM build-gate checks, and
diff whitespace check. The existing non-blocking bundle-size warning remains.
WASM tests used the existing locally generated, provenance-checked assets; a new
Emscripten compilation and interactive browser session were **not executed**.

Expected configuration failures were also exercised:

```sh
cmake -S . -B /tmp/smd-geo01a-missing -DSMD_BUILD_GEOMETRY=ON \
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF \
  -DCMAKE_IGNORE_PREFIX_PATH=/tmp/smd-occt-8.0.1-build
cmake -S . -B /tmp/smd-geo01a-wrong-version -DSMD_BUILD_GEOMETRY=ON \
  -DOpenCASCADE_DIR=/tmp/smd-occt-wrong-version \
  -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF
cmake -S . -B /tmp/smd-geo01a-wasm-reject -DEMSCRIPTEN=ON -DSMD_BUILD_GEOMETRY=ON
```

The missing-package configuration failed with the required OCCT 8.0.1/path
diagnostic. The wrong-version check used a temporary synthetic CMake package
advertising 8.0.0 with compatibility/exact flags false, and was rejected; it did
not build or use an alternative kernel. Setting the Emscripten flag tested the
explicit native-only guard, not an actual Emscripten toolchain build.

Initial failures resolved during development: one missing test header and an
OCCT deprecated diagnostic accessor were corrected; the private shared kernel
needed `LD_LIBRARY_PATH` before geometry CTest could load its transitive libraries.
Sandbox DNS prevented the initial kernel clone, and sandbox process restrictions
prevented the initial esbuild installation; both succeeded with permitted command
escalation. No final unexpected test failures remain.

## Verified geometry and errors

**Sample-verified:** `plain_column.smd` produces exactly six valid bounded OCCT
faces, retaining source face order and ordered vertex entity correspondence.
Repeated source indices across different faces use the same OCCT vertex entity.
The eight source coordinates remain exact; the original parser output is unchanged.

| Source face ordinal | Source name | Area, mm² |
| --- | --- | ---: |
| 0 | top | 487500 |
| 1 | bottom | 487500 |
| 2 | sweepface_1_0_1 | 4500000 |
| 3 | sweepface_1_0_2 | 3900000 |
| 4 | sweepface_1_0_3 | 4500000 |
| 5 | sweepface_1_0_4 | 3900000 |

Area regression tolerance: absolute `1e-5 mm²`. Supporting plane normals agree
with the original plane locus within `1e-12` in absolute dot product; source
vertices satisfy plane distance within `1e-7 mm`. Local coordinate bounds remain
X `-750..0`, Y `-650..0`, Z `0..6000` mm. The recorded translation
`(+375,+325,-1000)` is not applied. A test also replaces `verticesWorld` and the
recorded transform with unrelated values and obtains the same local geometry.

**Synthetic-test-verified:** input lifetime independence, oblique triangular
planes, reversed source winding, negated/scaled coefficients and coefficient-scale
independent distance checks. Exact duplicates, signed-zero duplicates, non-finite
coordinates on all axes, invalid indices, missing/degenerate/non-finite planes,
insufficient vertices, zero/short edges, repeated boundary indices, collinear,
concave and crossing/star polygons, and an invalid later face all throw descriptive
exceptions. Duplicate scanning includes unused vertices and precedes face checks.
Nearby distinct unused vertices remain separate entities. No partial result escapes.

## Limitations and review before GEO-01B

- Windows/MSVC build and tests are **not executed**. They remain an acceptance
  requirement; exact verification commands are in `libgeometry/README.md`.
- Only the accepted plain-column specimen establishes geometry regression
  acceptance. No additional SMD specimens were introduced or accepted.
- Strict convexity, no collinear corners, fixed `1e-7 mm` support thresholds and
  O(n²) polygon validation are explicit GEO-01A limits. Review these before GEO-01B.
- Original plane normal sign may oppose loop winding. The operational supporting
  plane is oriented to that loop while the original coefficients stay in `ParsedBrep`.
  This is not an outward-orientation or shell-consistency claim.
- Returned vertex/face vectors provide source ordinal correspondence and own
  topology; they do not preserve source metadata after the caller discards its
  authoritative input, and are not a persistence contract.
- No sewing, shell, solid, healing, feature recognition, Boolean operation,
  persistence, export or viewer replacement is included. Extreme numerical ranges
  may be rejected; arbitrary-scale robustness and generalized polygon support are
  not established.

No scope deviation or substitution of OCCT version was required. The two-vector
owning API and explicit convexity restrictions are implementation choices within
the assignment. Team review and Windows verification remain; the PR must not be
merged automatically. Stop at GEO-01A.
