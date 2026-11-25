# GEO-Export — Working Roadmap and Development Ledger

> **Working GEO-Export roadmap and ledger**
>
> Project: `smdviewer` · Repository: `BIBimDev/smdviewer`
>
> Established: **2026-11-23** · Status: **Architecture accepted. GEO-EXP-01 accepted and closed on 2026-11-25.**
>
> Intended lifecycle: maintain across GEO-EXP-01–04, then consolidate verified results into permanent project documentation and retire this ledger.

## 0. Purpose and use

This is the *continuity document* for successive development conversations, Codex assignments, code reviews and milestone closeouts. It records **accepted decisions**, **verified repository facts**, **planned work**, **open implementation details** and **test outcomes** without conflating them.

**Rule for successor sessions:** Read this ledger, then inspect the *current* GitHub `main`, relevant code and applicable `AGENTS.md`. The ledger is an orientation aid, not proof that an earlier repository snapshot is still current. Record changes only after confirming the implemented code and test results.

GEO-EXP-01 adds this ledger at the repository root. The assignment authorized
the implementation and pull request.

## 1. Verified starting position

| Item | Position at ledger creation |
| --- | --- |
| Repository | `BIBimDev/smdviewer` |
| Default / integration branch | `main` |
| Accepted baseline commit | `81f3a8d5f62c874a896e2d4cb6d08f2953d2d461` |
| GitHub `main` checked on 2026-11-23 | **Identical to accepted baseline** |
| PR #15 / GEO-Visualiser | Merged into `main` |
| Primary platform | Kubuntu 26.04, C++20, OCCT 8.0.1 |
| Relevant native code | `libgeometry/` |
| Applicable governance | Repository-root `AGENTS.md` |
| Local Kubuntu branch / working-tree cleanliness | **Not remotely verifiable; check before coding** |
| Changes made during architectural discussion | **None** |

Source paths inspected at this baseline:

- `libgeometry/include/smd_geometry/solid.hpp`
- `libgeometry/include/smd_geometry/principal_prism.hpp`
- `libgeometry/include/smd_geometry/boolean_decomposition.hpp`
- `libgeometry/CMakeLists.txt`; repository-root `CMakeLists.txt`
- `geo-visualiser/model.hpp`, `geo-visualiser/model.cpp`
- `docs/GEO-03B.md`, `docs/GEO-03C.md`, `AGENTS.md`

The current `libgeometry` library already links OCCT 8.0.1 and exposes all required `TopoDS_Solid` objects. The visualiser presently **orchestrates** the established parser → faces → shell → solid → prism → Boolean stages, but the geometry functions themselves are independent of the UI.

## 2. Objective and preserved geometry contract

GEO-Export is a **headless geometric persistence/interchange facility**, not a new geometry-recognition or validation stage. It transports already-established solids from GEO-02/GEO-03 to downstream engineering modules without requiring STRAKON parsing, the original `.smd` file or a viewer at the receiving end.

Authoritative nomenclature (unchanged):

| Role | Meaning | Existing C++ source |
| --- | --- | --- |
| **S** | Complete validated physical element solid reconstructed from source B-rep | `SolidAssembly::solid` |
| **B** | Derived principal constant-cross-section prism | `PrincipalPrismCandidate::solid` |
| **A** | Zero or more independently represented additions: `S \ B` | `BooleanDecomposition::extensions[*].solid` |
| **R** | Zero or more independently represented reductions: `B \ S` | `BooleanDecomposition::reductions[*].solid` |

`S = (B \ R) ∪ A` is the accepted reconstruction relationship. `BooleanDecomposition::reconstructed` remains an upstream verification result, **not** a fifth exported body category.

**Authority boundary:** Original source B-rep evidence stays authoritative upstream. Exported solids preserve geometric/topological processing results; they are **not** a complete archive of source ordinals, flags, names, plane equations, source mappings or Boolean face lineage. Parametric interpretations remain derived.

**Coordinates and units:** Source-local millimetres; no B-rep transform, element placement or project/global transform is applied by GEO-Export. Do not round, repair, reclassify, mesh, simplify, refit, re-extrude or reinterpret solids during export/import.

## 3. Accepted architectural decisions

| ID | Accepted decision | Consequence |
| --- | --- | --- |
| **GEO-EXP-D01** | Preserve one native OCCT BREP serialization **per solid**, even though multiple serializations are embedded in a container. | `S` and `B` each correspond to one `TopoDS_Solid`; each member of `A[]` and `R[]` is one `TopoDS_Solid`. No compounds are required. |
| **GEO-EXP-D02** | Use OCCT **ASCII BREP format VERSION_3**, omitting triangulations and triangulation normals. | Use explicit `BRepTools::Write` settings; do not rely on default format versions. |
| **GEO-EXP-D03** | Use a minimal **C++20 / OCCT-only public geometry interface**; no Qt, OpenGL, viewer types or viewer-to-export reverse dependency. | Accept and return S/B/A/R solids directly; an upstream caller supplies already-computed results. |
| **GEO-EXP-D04** | **Exactly one self-contained JSON file per exported element**, with the ASCII BREP serializations embedded as JSON strings. | No sidecar BREP files, per-element directory trees or filename manifests. Header and S/B/A/R are in a single document. |
| **GEO-EXP-D05** | Keep the export implementation **inside the existing `libgeometry/` library**. | Avoid a separate library target or unnecessary additional directory hierarchy at this stage. |
| **GEO-EXP-D06** | GEO-Visualiser's future default export directory is **`public/exports/`**, resolved relative to the repository/project root rather than the process working directory. | Implement only in the viewer-integration milestone, not as a dependency of the export library. |

D01 and D04 are compatible: a BREP is a single solid's serialization, **not** necessarily a separate filesystem file.

### 3.1 Version-1 JSON/BREP container

Canonical conceptual shape (the BREP bodies below are *illustrative placeholders*, not valid geometry):

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
  "A": [
    "CASCADE Topology V3, (c) Open Cascade\n..."
  ],
  "R": []
}
```

- `head`: required object containing at minimum `format`, `version`, `coordinates`, `units`, `brepVersion`.
- `S`, `B`: **required nonempty strings**, each containing an entire ASCII BREP for one valid solid.
- `A`, `R`: **required arrays of strings**, allowing zero, one or multiple solids.
- Values are opaque BREP text. JSON handles quoting/escaping, including newlines; it must not interpret/reformat numeric coordinates embedded in BREP text.
- On read, reject unsupported container versions and mismatching required header fields, malformed JSON, null/empty/non-solid shapes, and incomplete BREP streams. Keep diagnostics stage-specific.
- JSON key order and pretty-print layout are not geometry semantics. Stable, human-readable formatting is nevertheless preferable for diagnostics.
- The exact C++ function names and JSON parser dependency are **implementation details**, not yet fixed public API commitments.

### 3.2 OCCT stream transport

Use `std::ostringstream` / `std::istringstream` with `BRepTools::Write` / `BRepTools::Read` and a `BRep_Builder` on reading. The 8.0.1 reference confirms the stream overload and the explicit `theWithTriangles`, `theWithNormals`, `TopTools_FormatVersion` arguments. No standalone `.brep` files are created as intermediate storage.

The export/import code must not modify the supplied topology or source geometries. Reimport creates new OCCT objects; **object identity, memory addresses and OCCT traversal ordinals are not portable identifiers**.

### 3.3 Module placement

Planned *minimal* addition, subject to GEO-EXP-01 review:

```text
libgeometry/
  include/smd_geometry/export.hpp
  src/export.cpp
  tests/export_tests.cpp
  CMakeLists.txt                (small update for source and tests)
```

Potential later modules `toJSON.cpp` and `toIFC.cpp` are conceptual only; **do not create them pre-emptively**. The JSON container required for GEO-Export belongs to the initial exporter and does not imply a general geometric JSON format or IFC export implementation.

Conceptual public contract (function names provisional):

```cpp
struct ExportBodies
{
    TopoDS_Solid S;
    TopoDS_Solid B;
    std::vector<TopoDS_Solid> A;
    std::vector<TopoDS_Solid> R;
};

void exportBrep(const ExportBodies&, const std::filesystem::path& filename);
ExportBodies importBrep(const std::filesystem::path& filename);
```

The existing library may expose the interface in namespace `smd_geometry`. The library never decides where to put a file by inspecting a viewer-specific document or widget state.

## 4. Milestone roadmap

Development procedure: **agree focused task and acceptance criteria → Codex assignment on dedicated branch → test/review PR → team decision → record closeout in this ledger → proceed**. Do not merge without approval.

### GEO-EXP-01 — Single-file JSON/BREP round-trip

**Scope**

- Add the minimal S/B/A/R in-memory body transport interface within `libgeometry`.
- Serialize each `TopoDS_Solid` as OCCT ASCII BREP VERSION_3 with triangulations/normals excluded; embed the resulting strings in one JSON document.
- Deserialize a document from the same format, reconstructing the same four categories as `TopoDS_Solid` objects.
- Handle empty A/R arrays; correctly report malformed files, unsupported version, invalid shapes and filesystem failures.
- Add focused automated tests including a simple solid round-trip, at least one nonempty collection, empty collections, and invalid input.

**Outside scope:** new geometry algorithms, source metadata persistence, GUI, a command-line application, IFC, generic JSON geometry, bulk export, elaborate transaction infrastructure.

**Acceptance:** Headless C++20 build succeeds with the pinned OCCT 8.0.1; focused round-trip and rejection tests pass; source objects remain unchanged; geometry library remains independent of Qt/viewer. Record exact commands, compiler/platform, test SHA and resulting PR.

**Status:** **Accepted and closed on 2026-11-25.** [PR #16](https://github.com/BIBimDev/smdviewer/pull/16)
was merged into `main` as `73779deef01650c0d9d4f090f1839dc289cf4c98`.
The reviewed PR HEAD was `afe6b84686cbe7a263a11d520cb2df5c8c0aaa59`.
The reviewed implementation and previously tested commit was
`3070ef6219cb05398ba9e14c3b50ee6ca4b1fd7b`.

The public API is `smd_geometry::ExportBodies`, `exportBrep`, and `importBrep`
in `<smd_geometry/export.hpp>`. The implementation uses `BRepTools::Write`
with `VERSION_3` and both mesh flags disabled, then stores the exact stream
bytes as JSON strings. The reader checks the required header values, the
embedded BREP version, stream completeness and the shape type. Diagnostics
identify the failed field or collection index. Shape validation at this
boundary checks for a non-null solid with geometric faces. Callers supply
validated solids and the S/B/A/R relationship. The interchange uses OCCT's
native ASCII precision. A face backed only by a triangulation is rejected
because OCCT would otherwise write mesh data for it.

The private build dependency is the header-only `nlohmann_json` CMake package
(3.11 or newer within the 3.x series). The existing SMD JSON code only writes
diagnostic output. The installed package provides parsing and string encoding
without a build-time download. Geometry-enabled builds now require the
package, including builds that enable the native visualiser.

The exporter serializes in memory, then exclusively creates the destination
file and checks write and close results. It attempts to remove a partial
output on write failure. There is no application-level input size limit, and
decoding delegates native BREP syntax to OCCT.

**Verification** on Kubuntu 26.04 x86-64, GCC 15.2.0, CMake 4.2.3, OCCT 8.0.1
and nlohmann/json 3.11.3 from the Ubuntu `nlohmann-json3-dev` package:

```sh
taskOcct="$HOME/.local/opt/occt-8.0.1-visual"
taskJson=/tmp/smd-geo-exp-01-deps/usr/share/cmake/nlohmann_json
cmake -S . -B /tmp/smd-geo-exp-01-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSMD_BUILD_GEOMETRY=ON -DSMD_BUILD_GEO_VISUALISER=OFF \
  -DOpenCASCADE_DIR="$taskOcct/lib/cmake/opencascade" -Dnlohmann_json_DIR="$taskJson"
cmake --build /tmp/smd-geo-exp-01-build --parallel 6
LD_LIBRARY_PATH="$taskOcct/lib" ctest --test-dir /tmp/smd-geo-exp-01-build --output-on-failure
LD_LIBRARY_PATH="$taskOcct/lib" ldd /tmp/smd-geo-exp-01-build/libgeometry/smd-export-tests

cmake -S . -B /tmp/smd-geo-exp-01-parser -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/smd-geo-exp-01-parser --parallel 6
ctest --test-dir /tmp/smd-geo-exp-01-parser --output-on-failure
```

- Headless Release build and **8/8 CTests** passed, including `geometry-export`
  and all existing native regressions. `ldd` showed no Qt or OpenGL libraries
  in the export test executable's dependency tree.
- Parser-only Release build and **2/2 CTests** passed with no JSON package
  discovery.
- Synthetic round-trips verified S/B, empty arrays, one addition and one
  reduction, source-local bounds, volume, shared topology, stored location
  and unmodified source shapes with mesh and normals. Rejection cases covered
  malformed JSON, incompatible headers, partial BREP, null and non-solid
  shapes, missing paths and existing output.
- Windows/MSVC verification remains outstanding. The five-specimen export
  qualification is the next milestone, GEO-EXP-02.

**Non-blocking review observations**

- Include the affected file path in file I/O diagnostics.
- Clarify how to provision the `nlohmann_json` development package for
  geometry-enabled builds across platforms, including Windows/MSVC.

### GEO-EXP-02 — Qualified specimen and geometric-equivalence regression

**Scope**

- Use the existing five original qualified GEO-03 column specimens end-to-end.
- Export each S/B/A/R set; discard the original output objects; reimport from the JSON/BREP document.
- Compare each imported solid with its original for validity, topology/geometric content, volume, bounds and equivalence under the established policy.
- Reconstruct `S' = (B \ R) ∪ A` from **reimported** bodies; verify equivalence with reimported `S` by the established GEO-03 methodology.
- Include synthetic cases with **two or more** additions or reductions, because the five original specimens do not cover collection multiplicity.

**Original qualified fixture names currently registered in native CMake** (under `public/samples/Columns/`):

1. `COL_PLAIN.smd`
2. `COL_SINGLE-POCKET.smd`
3. `COL_PLAIN_SINGLE-CORBEL.smd`
4. `COL_PLAIN_HALF-CORBEL.smd`
5. `COL_SINGLE-POCKET_SINGLE-CORBEL_02.smd`

**Acceptance:** Existing upstream suites remain passing; export/import changes no source coordinates or geometry interpretation; imported solids are valid and equivalent; no new tolerance relaxation, healing or sliver filtering. Keep fixed GEO-03 Boolean/classification tolerance **`1e-7 mm`** and the additional **`1 mm³`** reconstruction-volume comparison; **any positive-volume residual fails**, regardless of size. Report results separately from source-validation responsibilities.

**Status:** Not started.

### GEO-EXP-03 — Headless command-line export

**Scope**

- Add a small native CLI that reads a supported `.smd` file, invokes the **unchanged** accepted parser/geometry pipeline, and emits the single GEO-Export JSON document.
- Accept an explicit output filename and return meaningful error codes/messages.
- Exercise the CLI independently from Qt, the browser viewer and OpenGL.

**Acceptance:** A supported specimen can be exported on the primary Kubuntu platform, independently reimported, and verified; failure handling is testable. No new SMD interpretation or geometry stages.

**Status:** Not started.

### GEO-EXP-04 — Optional GEO-Visualiser integration

**Scope**

- Add a minimal GEO-Visualiser **Export** action that passes the already-available S/B/A/R solids to GEO-Export.
- Default destination directory: **`public/exports/`**.
- Report success and errors without altering viewer geometry, selection, tags or rendering.
- Ensure exported files are not accidentally committed or distributed via the browser build.

**Acceptance:** One action produces the same single-file JSON/BREP interchange format as the headless facility; the dependency direction remains viewer → geometry/export, never reverse; existing viewer tests remain passing.

**Status:** Not started.

### Milestone tracking register

| Milestone | Task branch | PR | Tested SHA/platform | Evidence | Decision / status |
| --- | --- | --- | --- | --- | --- |
| GEO-EXP-01 | `codex/geo-exp-01-json-brep` | [#16](https://github.com/BIBimDev/smdviewer/pull/16) | `3070ef6219cb05398ba9e14c3b50ee6ca4b1fd7b` / Kubuntu 26.04 x86-64 | Geometry-enabled 8/8 and parser-only 2/2 CTests | Accepted and closed 2026-11-25 |
| GEO-EXP-02 | — | — | — | — | Next milestone, awaiting implementation assignment |
| GEO-EXP-03 | — | — | — | — | Planned |
| GEO-EXP-04 | — | — | — | — | Planned |

## 5. Verification policy and known constraints

- OCCT build is pinned to **8.0.1**; do not silently substitute another kernel version.
- Native geometry code must build without enabling the Qt/OCCT visualiser.
- Preserve `libsmd` parsing, GEO-01 faces, GEO-02 solid construction, GEO-03 prism selection and Boolean decomposition unless a separately established defect warrants a separately approved change.
- BREP persistence is **geometric/topological**, not source-provenance persistence; never claim lossless SMD archiving.
- Never compare OCCT object identity after a serialization boundary; compare geometry and meaningful topology instead.
- A valid JSON file containing malformed BREP text is a **failed import**, not evidence that GEO-02/GEO-03 contracts should be changed.
- An importer may check container structure, shape type and readable BREP data without rerunning the entire upstream analysis or inventing a new classification system.
- Build and tests on any platform must be reported with actual commands and outcomes. No unexecuted tests may be reported as passing.

### Open implementation choices (not new architectural decisions)

1. **JSON implementation dependency:** inspect the repository's current JSON facilities, choose the smallest maintainable production reader/writer and document the decision in GEO-EXP-01. The existing diagnostic JSON output is not automatically a suitable parser.
2. **Overwrite/error semantics:** keep simple and explicit. In particular, avoid silent destruction of an existing export; select and test precise behavior in GEO-EXP-01, without introducing a broad filesystem transaction framework.
3. **Failure and parse limits:** decide reasonable error handling for malformed/oversized input in GEO-EXP-01. Do not reinterpret BREP internals.
4. **Public export directory:** `/public/exports/` is currently **not** ignored in `.gitignore`, and Vite commonly copies `public/` into the web distribution. Ignoring it in Git alone does **not** prevent publication. At GEO-EXP-04, verify and address both risks without broad unrelated housekeeping; treat industrial geometry as potentially confidential.
5. **Naming of exported JSON documents:** use a predictable source-stem-based default in viewer/CLI adapters while allowing the library to accept an explicit path. Final naming can be decided with the corresponding milestone.

## 6. Handoff protocol for each successor session

Before making a Codex assignment:

1. Read this ledger and the latest `AGENTS.md`.
2. Check current GitHub `main`/accepted baseline and local worktree, including tracked and untracked changes; never overwrite someone else's work.
3. Inspect **actual source**, build options and relevant tests at the new baseline.
4. Select **one** milestone and agree on concrete acceptance criteria.
5. Issue a narrow Codex task on its own branch and PR. Do not expand scope or merge without team approval.

Suggested **local** non-destructive checks (adjust path if the workstation checkout differs):

```sh
cd ~/Projects/smdviewer
git status --short --branch
git rev-parse HEAD
git branch --show-current
git fetch origin
git rev-parse origin/main
git status --porcelain=v1 --untracked-files=all
```

At review/closeout, update the tracking register and record:

```text
Milestone: GEO-EXP-0X
Branch / PR / tested commit:
Files changed:
Build and test commands (Kubuntu and other actually tested platforms):
Tests and outcomes:
Verified behavior:
Known limitations / rejected cases:
Decisions changed or still open:
Merge status / accepted main SHA:
Next authorized milestone:
```

**Session handoff statement template:**

> Project `BIBimDev/smdviewer`; working stage GEO-Export. Read the root `GEO-EXPORT-LEDGER.md` and `AGENTS.md`. Retain D01–D06 and the single-file JSON/BREP schema. Verify today's `main`, working tree and interfaces; use the milestone register for completed work. Proposed next task: **[identify milestone]**. Do not change parser/geometry contracts, begin work on `main` or initiate viewer integration outside the approved milestone.

## 7. Eventual documentation closeout

This ledger is intentionally **ephemeral**. After GEO-EXP-04 and all agreed follow-up fixes are accepted:

1. Convert **implemented, tested** architecture, JSON schema, API, usage instructions and limitations into permanent documentation, e.g. `docs/GEO-EXPORT.md`.
2. Incorporate any stable architectural decisions into the project's enduring architecture/decision records as appropriate.
3. Confirm permanent documentation agrees with the final merged code and test evidence.
4. Mark the ledger superseded and remove/retire it **only as an explicitly reviewed documentation change**; do not erase the milestone history before it has been transferred.

---

### Ledger event history

| Date | Event |
| --- | --- |
| 2026-11-22 | GEO-Export discussion opened from merged GEO-Visualiser baseline. |
| 2026-11-23 | D01–D03 accepted; D04 revised from multiple files to **one self-contained JSON containing BREP strings**; in-tree `libgeometry` placement and `public/exports/` viewer default accepted. |
| 2026-11-23 | Working roadmap/ledger issued. No repository or implementation modifications performed. |
| 2026-11-23 | GEO-EXP-01 implemented and tested. PR #16 opened for review. |
| 2026-11-25 | GEO-EXP-01 accepted after independent review. PR #16 merged as `73779deef01650c0d9d4f090f1839dc289cf4c98`. |

**Current handoff status:** GEO-EXP-01 is closed. GEO-EXP-02 is the next
milestone and requires a separate implementation assignment.
