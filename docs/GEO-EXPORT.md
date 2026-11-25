# GEO-Export

## 1. Purpose and scope

GEO-Export provides native geometric persistence and interchange for an element
that has passed the SMD and OCCT geometry pipeline. A downstream program can
read the resulting solids without STRAKON, an SMD parser or a viewer. The
original STRAKON SMD and its B-rep remain authoritative evidence. Keep them
when source coordinates and order, face loops, ordinals, flags, names, plane
equations, block tags, recorded transforms, source mappings or Boolean
lineage need to survive. A GEO-Export file preserves the processed geometry
and topology for interchange.

## 2. Geometric contract

| Role | Meaning | Upstream result |
| --- | --- | --- |
| S | Validated physical element solid reconstructed from the source B-rep | `SolidAssembly::solid` |
| B | Derived principal constant-cross-section prism | `PrincipalPrismCandidate::solid` |
| A | Zero or more individual additions | `BooleanDecomposition::extensions[*].solid` |
| R | Zero or more individual reductions | `BooleanDecomposition::reductions[*].solid` |

```text
A = S \ B
R = B \ S
S = (B \ R) ∪ A
```

Coordinates are raw source-local millimetres. Export and import leave the
provided geometry in that frame, with the upstream B-rep, element and project
placement transforms unapplied. Geometry uses OCCT's native ASCII precision.
Reconstruction from imported B/A/R is a qualification step. The accepted
container carries four body categories, and the upstream reconstructed solid
has no separate field. See [GEO-03C](GEO-03C.md) for the Boolean method.

## 3. Architectural decisions

| Decision | Accepted contract and rationale |
| --- | --- |
| GEO-EXP-D01 | Use one native OCCT BREP serialization for each solid so OCCT carries the geometric surfaces and topology without a second shape model. Each entry is a single `TopoDS_Solid`. |
| GEO-EXP-D02 | Write OCCT ASCII BREP `VERSION_3` with explicit flags excluding triangulations and triangulation normals. Geometry consumers receive the surfaces and topology independently of cached presentation meshes. |
| GEO-EXP-D03 | Expose a native C++20 and OCCT geometry interface. The caller supplies the already-computed S/B/A/R. Qt, OpenGL, viewer state and directory policy stay outside the public geometry interface. |
| GEO-EXP-D04 | Store one self-contained JSON file per element with complete ASCII BREP strings. JSON supplies the header, body roles and collections in a single transport file without additional BREP files or manifests. |
| GEO-EXP-D05 | Place the exporter in the existing `libgeometry/` library and link `smd-geometry`. This reuses the established geometry target and dependency direction. |
| GEO-EXP-D06, Revision 01 | Default GEO-Visualiser to repository-root `exports/` on demand, independently of the working directory, with an alternate destination available. The library and CLI receive explicit filenames. |

D04 changed from multiple files to one JSON/BREP container on 2026-11-23.
The original D06 choice on that date was `public/exports/`. The owner
accepted D06-R01 on 2026-11-26. The default moved to `exports/` to keep
potentially confidential generated geometry outside Vite's public asset tree,
without special browser-build copy exclusions. The root `/exports/` Git rule
excludes the operational directory from ordinary staging. A temporary
non-confidential sentinel was ignored by Git and absent from a fresh
`npm ci` / `npm run build` distribution, which still contained the public WASM
asset. The sentinel was removed after verification.

## 4. Interchange file specification

The writer emits the following object. The BREP strings shown here are
illustrative and omit the native shape records.

```json
{
  "head": {
    "format": "smdviewer.geo-export",
    "version": 1,
    "coordinates": "source-local",
    "units": "mm",
    "brepVersion": 3
  },
  "S": "CASCADE Topology V3, (c) Open Cascade\n...",
  "B": "CASCADE Topology V3, (c) Open Cascade\n...",
  "A": ["CASCADE Topology V3, (c) Open Cascade\n..."],
  "R": []
}
```

- `head` is an object. All five fields are required with the exact values
  shown. The two version fields are JSON integers.
- `S` and `B` are required strings, each containing the entire native BREP
  stream for one solid. `A` and `R` are required arrays of the same strings.
  Either array may be empty. Collection order is preserved.
- JSON quoting and line escapes transport the exact BREP text. Numeric
  coordinates are part of that text. Key order and indentation carry no
  geometry semantics. The reader checks the required members and ignores
  additional keys.
- OCCT streams use `BRepTools::Write` and `BRepTools::Read` with a
  `BRep_Builder` on read. The writer supplies `VERSION_3` and disables both
  mesh flags. A face with no geometric surface is rejected because OCCT can
  otherwise emit mesh data even when triangulation output is disabled.

Import rejects a missing, non-regular or unreadable input, malformed JSON,
unexpected data after its document, a non-object root, missing or incompatible
required fields, and bad collection or member types. The BREP check rejects
an incompatible signature, an embedded NUL byte, a failed or incomplete
stream, trailing non-whitespace shape data, null or non-solid shapes, and
solids without faces or with surface-less faces. Diagnostics identify the
stage, field or collection index. See [the implementation](../libgeometry/src/export.cpp).

## 5. Native API

Include [`<smd_geometry/export.hpp>`](../libgeometry/include/smd_geometry/export.hpp)
and link `smd-geometry`. Namespace `smd_geometry` exposes the following
C++20 contract.

```cpp
struct ExportBodies {
    TopoDS_Solid S;
    TopoDS_Solid B;
    std::vector<TopoDS_Solid> A;
    std::vector<TopoDS_Solid> R;
};

void exportBrep(const ExportBodies& bodies, const std::filesystem::path& filename);
ExportBodies importBrep(const std::filesystem::path& filename);
```

For example, an application with validated bodies can export and reopen them
at a caller-selected, unused path.

```cpp
#include <smd_geometry/export.hpp>

smd_geometry::ExportBodies roundTrip(
    const smd_geometry::ExportBodies& validated,
    const std::filesystem::path& newFile)
{
    smd_geometry::exportBrep(validated, newFile);
    return smd_geometry::importBrep(newFile);
}
```

The caller owns the inputs. Export preserves the supplied topology and surfaces.
Import returns new OCCT shape handles with the stored topology and locations.
Memory addresses, shape identity and OCCT traversal ordinals are process-local
details. Compare meaningful topology and geometry after serialization.

The library serializes in memory before exclusively creating the output.
It checks writes and close, and attempts to remove a partial file on write
failure. Supply a new filename with an existing parent directory. An existing
destination is preserved. Export and import report format, shape and file I/O
failures with `std::runtime_error`. The structural import checks provide a
readable solid for each role. The caller supplies the validated geometric
relationship, and full qualification uses the checks in section 8. The private
JSON implementation dependency is the installed header-only
`nlohmann_json` CMake package (3.11 or later in the 3.x series).
The public header exposes standard-library and OCCT types.

## 6. Headless CLI

```sh
smd-geo-export input.smd output.json
```

Build with CMake 3.20+, a C++20 compiler, OCCT 8.0.1 modeling toolkits,
`nlohmann_json` 3.11+, and `SMD_BUILD_GEOMETRY=ON`. The executable is under
the build tree's `libgeometry/` directory, with a configuration subdirectory
for multi-config generators. Build and loader setup are below and in the
[geometry README](../libgeometry/README.md#build).

Both paths may be absolute or relative to the caller's working directory.
The input must be a supported regular SMD file. The CLI runs the existing
parser, face, shell, solid, prism and Boolean stages, then writes one JSON/BREP
file to the explicit path. The output filename must be unused and its parent
directory must already exist. The CLI writes usage and errors to standard
error. Exit codes are `0` for success, `1` for input, geometry or output
failure, and `2` for the wrong number of arguments.

From the repository root, reserve a directory for an example export.

```sh
example_dir="$(mktemp -d)"
/tmp/smd-geo-export-headless/libgeometry/smd-geo-export \
  public/samples/Columns/COL_PLAIN.smd "$example_dir/COL_PLAIN.json"
```

GEO-EXP-03 verified relative and absolute paths, all five specimens and
nine automated failure cases. These covered wrong argument counts, missing
or non-regular input, a missing geometry block, a degenerate plane, an
existing destination and a missing output directory. Additional Kubuntu
checks covered permission-denied reads and writes. Existing output bytes
were preserved on failure.

## 7. GEO-Visualiser integration

With a successfully loaded, validated document, choose **File → Export…**.
The save dialog uses the `GEO-Export JSON (*.json)` filter and proposes the
SMD basename with its final extension replaced by `.json`. CMake configures
an absolute checkout-root `exports/` path and the viewer creates the
directory on demand. Each invocation starts with the default. Choose a
different folder or filename in the dialog when needed. If directory creation
fails, the viewer reports the path and diagnostic and starts from the active
SMD's folder, or the user's home directory if that folder is gone.

The viewer uses the original solids owned by the active document and calls
`exportBrep()` directly. Export works with empty A/R groups and includes the
complete S/B/A/R set even with an individual component isolated or an entity
selected. The parser and geometry pipeline run when loading the document.
The save operation serializes the existing bodies directly. A changed or
cleared document invalidates the pending export. A failed Open leaves export
available while the previous document stays active. Cancellation writes no
file, and an empty directory created on demand may remain. An existing path
is rejected with an opportunity to select a new one. Success identifies the
full path, and failures identify the path and error
while preserving the active document, load status, representation, selection,
tags and camera. See [native viewer operation](GEO-VISUALISER.md#export-geometry).

### Operator smoke test

1. Open `public/samples/Columns/COL_SINGLE-POCKET_SINGLE-CORBEL_02.smd`,
   select representation A, isolate its extension, then select and tag an
   entity. Note the camera and selection.
2. Open **File → Export…** and confirm the starting directory is the
   checkout's `exports/` and the proposed filename is
   `COL_SINGLE-POCKET_SINGLE-CORBEL_02.json`. Save to a new destination and
   record its checksum.
3. Confirm the selected entity, tag, representation, isolation and camera
   are as before the export.
4. Attempt to save to the same destination and check that the application
   rejects it. Cancel the reopened dialog and compare the file with its
   original checksum.

To independently verify that output, run the test executable against the
original SMD and the new JSON file. This imports the file, compares every role
and checks reconstruction using one addition and one reduction.

```sh
/tmp/smd-geo-export-viewer/libgeometry/smd-export-qualification-tests \
  public/samples/Columns/COL_SINGLE-POCKET_SINGLE-CORBEL_02.smd \
  1 1 "/path/to/new-export.json"
```

## 8. Geometric qualification

The GEO-EXP-02 verifier independently imports the file with `importBrep()`
and compares each body to measurements from the qualified pipeline. It checks
positive finite volume, source-local bounds, meaningful per-shell topology,
bounded polygon loop geometry, OCCT contextual validity, boundary
self-interference, closed edge-connected shells and outward material
orientation. Polygon loop starts and face traversal order can vary after
serialization.

The verifier builds S′ by cutting imported R from B and fusing imported A
into the result. It requires one valid connected solid, empty directed
Boolean residuals in both directions and full bounded-face coverage in both
directions. It uses the established **1e-7 mm** Boolean tolerance and **1 mm³**
absolute reconstruction-volume comparison. Any positive-volume residual
fails. Equal net volume can occur with material missing and added in different
positions, so volume alone is insufficient.

| Case | A / R | Residual solids S minus S′ / S′ minus S | Covered faces S / S′ | V(S′) − V(S), mm³ |
| --- | ---: | ---: | ---: | ---: |
| `COL_PLAIN.smd` | 0 / 0 | 0 / 0 | 6 / 6 | 0 |
| `COL_SINGLE-POCKET.smd` | 0 / 1 | 0 / 0 | 10 / 10 | 0 |
| `COL_PLAIN_SINGLE-CORBEL.smd` | 1 / 0 | 0 / 0 | 10 / 12 | 0 |
| `COL_PLAIN_HALF-CORBEL.smd` | 1 / 0 | 0 / 0 | 10 / 11 | 0 |
| `COL_SINGLE-POCKET_SINGLE-CORBEL_02.smd` | 1 / 1 | 0 / 0 | 14 / 15 | 0 |
| Synthetic notch, internal cavity and multiple bodies | 2 / 2 | 0 / 0 | 25 / 29 | −9.09 × 10⁻¹³ |

The synthetic case has analytic volumes in mm³ of B 4000, additions 60 and
120, reductions 18 and 24, and S 4138. The negative controls reject a missing
addition, a missing reduction, and two equal-volume X translations. The
1 mm translation leaves 50 mm³ of residual material in each direction.
The 0.001 mm translation leaves 0.05 mm³ in each direction. The CLI and viewer
adapter both qualified all five STRAKON fixtures against this policy.

## 9. Build and verification evidence

The integrated revision is `f6162c525b79958b8f6006228eab753ce14b877c`.
Its tree matches the reviewed PR head. The only difference from the fully
tested implementation revision `2f93b94df93224f80c418d680ce701cb8c03e04c`
was the verification ledger.

Reproduce the Release matrix from the checkout root. Set the OCCT install
prefix and, when needed, the path to the installed JSON CMake package.

```sh
occt_prefix="$HOME/.local/opt/occt-8.0.1-visual"
json_cmake=/path/to/nlohmann_json/cmake
export LD_LIBRARY_PATH="$occt_prefix/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

cmake -S . -B /tmp/smd-geo-export-viewer -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON -DSMD_BUILD_GEO_VISUALISER=ON \
  -DOpenCASCADE_DIR="$occt_prefix/lib/cmake/opencascade" -Dnlohmann_json_DIR="$json_cmake"
cmake --build /tmp/smd-geo-export-viewer --parallel
ctest --test-dir /tmp/smd-geo-export-viewer --output-on-failure

cmake -S . -B /tmp/smd-geo-export-headless -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON -DSMD_BUILD_GEO_VISUALISER=OFF \
  -DOpenCASCADE_DIR="$occt_prefix/lib/cmake/opencascade" -Dnlohmann_json_DIR="$json_cmake"
cmake --build /tmp/smd-geo-export-headless --parallel
env -u DISPLAY -u WAYLAND_DISPLAY ctest --test-dir /tmp/smd-geo-export-headless --output-on-failure

cmake -S . -B /tmp/smd-geo-export-parser -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/smd-geo-export-parser --parallel
ctest --test-dir /tmp/smd-geo-export-parser --output-on-failure
```

The viewer requires Qt 6 Widgets and visualization-enabled OCCT. Executed
closure builds used Kubuntu 26.04.1 x86-64, Linux 7.0.0-44-generic, GCC 15.2.0,
C++20, CMake 4.2.3, Ninja 1.13.2, Python 3.14.4, shared OCCT 8.0.1 with
visualization, Qt 6.10.2 and nlohmann/json 3.11.3.

Fresh builds and CTests on the merged revision passed with the following
configuration matrix. The headless CLI linked OCCT modeling and standard
runtime libraries. The parser-only CMake cache contained no OCCT, Qt or JSON
package discovery.

| Merged-main build | Geometry | Viewer | CTest |
| --- | --- | --- | ---: |
| Viewer-enabled | ON | ON | 24/24 |
| Headless geometry | ON | OFF | 21/21 |
| Parser-only | OFF | OFF | 2/2 |

The reviewed implementation separately passed `--export`, `--open`,
`--camera`, `--display` and `--render` under Xvfb/xcb with Mesa 26.0.8
llvmpipe (software OpenGL 4.5). The display and full render modes passed
at Qt scale 2. The focused UI run exported a combined specimen with an
extension isolated and the loaded SMD copy absent from disk. Its independent
import verified the complete body set.

On merged main, the actual executable launched with the plain specimen on
Kubuntu's X11 RDP display `:10.0` using Qt `xcb`, scale factor 1 and device
pixel ratio 1. It was launched from another working directory and left the
default export directory uncreated. NVIDIA PRIME offload was selected with
`__NV_PRIME_RENDER_OFFLOAD=1` and `__GLX_VENDOR_LIBRARY_NAME=nvidia`. The
renderer was NVIDIA GeForce RTX 3080 Ti/PCIe/SSE2, OpenGL 4.6.0, driver
580.178.04. `nvidia-smi` recorded graphics contexts from the actual viewer
and the focused UI test. This exercised the physical GPU through RDP. The
local physical console had no active graphical session and was inaccessible.

The merged-main focused `--export` run used that same NVIDIA-backed session.
It covered default and alternate destinations, on-demand directory creation,
Unicode filenames, valid empty groups, failed open, cancellation, no
overwrite, destination errors, state preservation and stale-dialog cleanup.
The independent GEO-EXP-02 verifier imported the file produced by the actual
Qt export action with the combined specimen's extension isolated. It
confirmed A/R 1/1, zero directed residual solids, complete bounded-face
coverage (14/15) and a 0 mm³ reconstruction-volume difference.

Manual owner acceptance and qualification of an owner-produced export remain
pending. The automated UI export is separately qualified. Windows/MSVC
verification of GEO-Export remains outstanding.

## 10. Known limitations

- Qualification covers the five named STRAKON specimens and the synthetic
  planar case. General STRAKON inputs and other geometric interpretations are
  outside that evidence. The source B-rep remains the evidence record.
- The implementation is pinned to OCCT 8.0.1. Format qualification covers
  this kernel version. The importer delegates native BREP syntax to OCCT and
  supplies no application-level input-size limit.
- A GEO-Export file carries processed geometry and topology. Preserve the
  original SMD and separate provenance records for a complete source archive.
- Windows/MSVC builds and the local physical-console operator session remain
  outstanding for GEO-Export.
- The reviewed baseline also exhibited two scale-sensitive assertions with
  llvmpipe. The origin-arrow pixel check can fail at Qt scale 1 and the
  projected-distance scale-bar check at 1.25. The same display and full
  render suites passed at scale 2 with their original assertions.
- GEO-EXP-03 review recorded two non-blocking CLI observations. Input stream
  completeness could be diagnosed more explicitly, and input errors could
  include the affected filename. GEO-EXP-01 also noted that library file I/O
  diagnostics can omit the affected path and that Windows provisioning of the
  JSON dependency warrants verification.
- Git ignore and Vite production distribution exclusion only cover ordinary
  staging and that build output. They provide no filesystem access control
  and establish no Vite development-server security boundary. Access and
  handling of any exported engineering data depend on the destination.

## 11. Development and acceptance history

The work began on 2026-11-22 after native viewer PR #15 merged at
`81f3a8d5f62c874a896e2d4cb6d08f2953d2d461`. The accepted
architecture and initial ledger were issued on 2026-11-23. GEO-EXP-01 was
implemented and tested that day. GEO-EXP-01 through GEO-EXP-03 were accepted
and closed on 2026-11-25 following independent review. GEO-EXP-D06-R01 and
GEO-EXP-04 passed independent technical review before their normal merge on
2026-11-26 IST. The authorized merge preserved the reviewed amendment as a
separate first commit of the viewer-export branch.

| Milestone and implementation PR | Implementation commit | Reviewed PR head | Merge commit |
| --- | --- | --- | --- |
| GEO-EXP-01, single-file round-trip [#16](https://github.com/BIBimDev/smdviewer/pull/16) | `3070ef6219cb05398ba9e14c3b50ee6ca4b1fd7b` | `afe6b84686cbe7a263a11d520cb2df5c8c0aaa59` | `73779deef01650c0d9d4f090f1839dc289cf4c98` |
| GEO-EXP-02, independent qualification [#18](https://github.com/BIBimDev/smdviewer/pull/18) | `f9243cdc3eddec7c6afce33029c324a8aeabd1f9` | `d87a31b72a031dbc46206ac083a6517fc6a86d3f` | `631f11d8eb3947247a1dca2b42c42d9717862773` |
| GEO-EXP-03, headless CLI [#20](https://github.com/BIBimDev/smdviewer/pull/20) | `a3c21e2ae959f9e45db3c36a511e00aa89952747` | `83d53a038d17377314d47b8101c8151ba9eb52a3` | `9bc1459dee7177ee7367ddb08f1e79184272050e` |
| GEO-EXP-D06-R01, default directory [#22](https://github.com/BIBimDev/smdviewer/pull/22) | `f3902ac6eb5607b743f5f6c691e9da3787b367b9` | `f09c96fc979593b45a8d3c1a8380c4dfd5552b56` | `f6162c525b79958b8f6006228eab753ce14b877c` |
| GEO-EXP-04, viewer export [#22](https://github.com/BIBimDev/smdviewer/pull/22) | `a52d4b9eb1f251c96347616016b28f4b77e95a0c` | `f09c96fc979593b45a8d3c1a8380c4dfd5552b56` | `f6162c525b79958b8f6006228eab753ce14b877c` |

| Historical validation | Executed evidence |
| --- | --- |
| GEO-EXP-01 at its implementation commit | Kubuntu 26.04 x86-64, geometry 8/8 and parser-only 2/2. Synthetic round-trip and rejection cases checked stored locations, shared topology, retained input meshes/normals, invalid files, and output integrity. |
| GEO-EXP-02 at its reviewed head | Kubuntu 26.04.1 x86-64, qualification 7/7, full geometry 15/15 and parser-only 2/2. All cases in section 8 qualified. |
| GEO-EXP-03 at its reviewed head | Kubuntu 26.04.1 x86-64, focused CLI 6/6, full geometry 21/21 and parser-only 2/2. |
| GEO-EXP-04 at its implementation commit and test-only revision `2f93b94df93224f80c418d680ce701cb8c03e04c` | Kubuntu 26.04.1 x86-64, viewer 24/24, headless geometry 21/21, parser-only 2/2 and all five desktop modes. Viewer 24/24 was rerun at the reviewed PR head. |

The administrative closure PRs recorded these commits.

| Milestone | Closure PR | Closure commit | Merge commit |
| --- | --- | --- | --- |
| GEO-EXP-01 | [#17](https://github.com/BIBimDev/smdviewer/pull/17) | `40994434d3631ee174f2b08d56148d5371d78b66` | `a08ea0bee1ce9e746fd4d2220e3b483860783663` |
| GEO-EXP-02 | [#19](https://github.com/BIBimDev/smdviewer/pull/19) | `0f1332f400fc0dec8aaf5ec1b1d66dfb641937df` | `50450e8bc3f00c05f7fda4b60095ea2c825ff277` |
| GEO-EXP-03 | [#21](https://github.com/BIBimDev/smdviewer/pull/21) | `9dd20684702b48ebc80be4a80acf86de47098510` | `1fc79b04dc55555590a8282883b81dd129b8e7b7` |

All four GEO-Export development milestones are implemented, accepted and
integrated. This reference is the permanent successor to the temporary
development ledger. Ledger retirement and formal documentation closure are
submitted for independent review. The former root ledger remains available in
[Git history](https://github.com/BIBimDev/smdviewer/blob/f6162c525b79958b8f6006228eab753ce14b877c/GEO-EXPORT-LEDGER.md).
Governing repository instructions remain in [AGENTS.md](../AGENTS.md).
