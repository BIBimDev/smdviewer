# AGENTS.md — BIBimDev/smdviewer

## Purpose and scope

This repository contains an experimental STRAKON/DICAD `.smd` reader and browser-based geometry inspector. The longer-term goal is an independent system for geometric data extraction, parametric interpretation, reinforcement, design, and workshop-drawing generation for precast concrete elements.

**The viewer is a research and diagnostic tool, not the future engineering system of record.** These instructions govern the repository root and its subdirectories unless a more specific `AGENTS.md` is introduced deliberately.

## Non-negotiable working principles

1. **Inspect the actual baseline.** Before proposing or making a code change, identify the current branch, commit SHA, working-tree status, relevant files, and applicable instructions. Fetch/check the remote when currency matters. Do not treat a dated report as proof of today's state.
2. **Require an agreed task.** Discuss requirements and acceptance criteria first. Do not modify source, create commits, push, merge, or rebase without explicit authorization for the specific task. Prefer small, independently testable milestones.
3. **Protect team work.** Never discard uncommitted changes. Work on a task branch, not directly on `main`; submit a reviewable pull request. Do not force-push or alter another contributor's branch without approval. Never merge on behalf of the team without authorization.
4. **Separate evidence from hypotheses.** The `.smd` format has been reverse-engineered from a limited set of specimens, not from an authoritative DICAD specification. Mark behavior as *sample-verified*, *inferred*, *proposed*, or *unknown*. Reject unsupported input explicitly; do not invent semantics.
5. **Preserve geometric evidence.** Treat the source B-rep as authoritative. Preserve original vertex coordinates and order, ordered face loops, face flags/names, plane equations, block tags, source identities, and any detected transformations. Parametric profiles, extrusions, pockets, and Boolean histories are **derived interpretations**, never replacements for the source B-rep.
6. **Keep coordinate systems explicit.** Distinguish raw B-rep coordinates, coordinates after a recorded B-rep transform, element placement, and project/global placement. Do not assume that the existing TypeScript property `verticesWorld` necessarily means project-global coordinates. Define units and numerical tolerances for each operation.
7. **Maintain module boundaries.** Keep parser and geometric processing independent from Three.js, DOM, and browser UI. The viewer must remain functional until a replacement or integration is independently approved.
8. **Test before claiming success.** Run focused tests and relevant builds; report the commands and results. Never claim an unexecuted Windows/Linux test passed. Check failures and malformed input as well as successful specimens.
9. **Respect confidentiality.** Do not publish proprietary `.kon` files, drawings, credentials, internal project information, or additional specimens without explicit permission to share them in the repository.

## Repository snapshot — verified 20261113

- Repository: `BIBimDev/smdviewer`; default branch: `main`; verified HEAD `23c0aeb4acc08e66464ebf3f0a1bc649ce50db3f`.
- GEO-00 and integrations GEO-00-INT01/INT02 are accepted: `libsmd/` implements the authoritative C++20 SMD parser; its sources are also compiled to WebAssembly for browser parsing. The original TypeScript parser under `src/smd/` remains a regression oracle, not the production SMD parser.
- `libsmd/include/smd/types.hpp` defines the native parsed data structures. `libsmd/src/brep_detector.cpp` extracts ordered vertices, single-loop polygon faces, source plane equations, names and optional transform.
- JSON v1 is a diagnostic/viewer interchange format, **not** a lossless source archive or GEO-01 persistence contract.
- `src/viewer/` and `src/main.ts` implement presentation/interaction; the browser viewer remains functional pending any separately authorized replacement.
- `public/samples/plain_column.smd` and `public/samples/pocket_column.smd` are the two accepted GEO-00 geometry regression fixtures. A subsequent commit introduced `SingleCorbelColumn.smd`, `SingleHalfCorbelColumn.smd`, and `SinglePocketingSingleCorbelColumn.smd`; their presence alone does not establish new parser or geometry acceptance.
- `docs/SMD_V1_STRUCTURE.md`, `docs/GEO-00-INT01.md`, `docs/GEO-00-INT02.md`, and `libsmd/README.md` document current accepted scope and limitations.
- Experimental multi-block/subject-selection heuristics are not established general STRAKON/DICAD rules.

**This section is a dated snapshot. Reinspect the repository at the start of every implementation task.**

## Baseline geometry fixtures and limitations

| Specimen | B-rep vertices | Faces | Bounding dimensions |
| --- | ---: | ---: | --- |
| `plain_column.smd` | 8 | 6 | 750 × 650 × 6000 mm |
| `pocket_column.smd` | 16 | 10 | 750 × 650 × 6000 mm |

For both specimens, rendered-coordinate bounds are X `-375..375`, Y `-325..325`, Z `-1000..5000` mm. The pocket specimen includes a rectangular recess near the top. The current browser parser primarily supports the confirmed `#10125` subset and single-loop faces; successful rendering does not prove a watertight, topologically valid solid.

## Engineering development sequence — accepted architectural roadmap 20261113

The accepted staged workflow is **source B-rep → OCCT faces → valid OCCT solid → partial-body interpretation**. Target **Open CASCADE Technology (OCCT) 8.x** as the operational geometric/topological kernel. Pin the exact 8.x release and build configuration in an authorized implementation task, not by assumption.

**GEO-00 — independent C++20 SMD parser (accepted and closed)**

- Preserve the accepted native `libsmd` parser, command-line extractor, JSON v1 diagnostic interface, and WebAssembly integration.
- The TypeScript parser remains a regression oracle; there is no production TypeScript parser fallback.
- No subsequent geometry milestone implicitly authorizes changes to GEO-00 parsing semantics.

**GEO-01 — source B-rep to OCCT `TopoDS_Face` collection (planned)**

- Input: the accepted native `smd::ParsedSmd` / `ParsedBrep` representation, not rendered triangles or JSON v1 as a required persistence contract.
- Construct **one OCCT `TopoDS_Face` per supported source polygonal face**, with its bounded planar surface and the necessary `TopoDS_Wire`, `TopoDS_Edge`, and `TopoDS_Vertex` objects. Preserve the original ordered loop, source face/vertex ordinals, names, flags, plane coefficients, and provenance through an explicit sidecar/correspondence mechanism.
- Keep raw source coordinates distinct from coordinates after the recorded B-rep transform. Define and document the coordinate frame used to construct OCCT faces; do not compose unverified element or project placement transforms.
- Use OCCT as the operational topology kernel. No independent relational half-edge implementation or prescribed face-edge hub tables.
- Check each constructed face against source geometry with explicit tolerances; report unsupported, degenerate, or inconsistent input without overwriting or healing authoritative source evidence.
- **Boundary:** no sewing into shells, no construction of `TopoDS_Solid`, no extrusion/feature recognition, no Boolean decomposition, and no viewer replacement. Source-evidence persistence remains a separate design decision.

**GEO-02 — OCCT `TopoDS_Face` collection to `TopoDS_Solid` (planned)**

- Input: the GEO-01 faces and their source-to-kernel correspondence.
- Establish shared edge incidences, consistent orientations, and a closed shell using appropriate OCCT topology/sewing tools; construct and validate one connected `TopoDS_Solid`.
- Confirm that the supported boundary is planar-faced, closed, orientable, non-self-intersecting, and volumetrically valid under an explicitly stated tolerance policy. A successful SMD parse or successful face conversion is not proof of a valid solid.
- Track and report sewing/healing/splitting changes independently of the source evidence. Reject or diagnose conversion failures without silently modifying source vertices, loops, planes, flags, or names.
- **Boundary:** no identification of main extrusion, corbels, pockets, cut-outs, or general Boolean reconstruction.

**GEO-03 — OCCT `TopoDS_Solid` to principal extrusion and partial bodies (planned)**

- Input: one validated GEO-02 solid `S`.
- Use precast-mould domain knowledge, geometric evidence, source profile metadata, and—where meaningful—element designation to identify a **main constant-cross-section extrusion** `B`; do not presume a unique interpretation where evidence is insufficient.
- Classify Boolean differences as **extensions** `A = S \ B` (mathematical set difference) and **reductions** `R = B \ S`; evaluate using OCCT Boolean operations and identify connected components as candidate corbels, pockets, cut-outs, or unclassified features.
- Keep original source faces distinct from OCCT-generated/split/removed faces. Record derived correspondences when established; OCCT face traversal indices are not durable source identifiers.
- Verify that `(B \ R) ∪ A` reproduces `S` geometrically within defined tolerances; compare shape/volume/boundary, not only face counts.
- Initial scope excludes tapered main sections, arbitrary CSG histories, and construction/manufacturing-joint inference.

**Accepted architectural decisions GEO-01-D01 through GEO-01-D10**

- **D01:** Distinguish authoritative SMD source geometry, OCCT geometric/topological processing, and derived constructive interpretation.
- **D02:** One connected physical solid per supported dataset; no assemblies or independent multi-solid input in the initial scope.
- **D03:** Closed, consistently oriented planar polygonal boundary with straight edges; validate rather than presume geometric correctness.
- **D04:** OCCT 8.x is the preferred C++ geometry kernel for B-rep topology, modelling, operations, and validation.
- **D05:** Use OCCT native vertices, edges, wires, faces, shells, and solids rather than implementing a separate operational half-edge topology.
- **D06:** Original SMD B-rep evidence (coordinates/order, loops, flags, names, plane equations, transforms, source metadata) remains authoritative and independently preserved; physical persistence format undecided.
- **D07:** Principal extrusion, extensions, and reductions are derived OCCT bodies, with source and reconstructed faces distinguishable.
- **D08:** Source-to-OCCT conversion is explicit, reproducible, validated, and non-destructive to original evidence.
- **D09:** Interpret the initial supported precast geometry through one principal constant-section extrusion with local extensions/reductions; tapered and general complex profiles deferred.
- **D10:** For original solid `S` and identified main extrusion `B`, define extensions `A = S \ B` and reductions `R = B \ S` as geometric differences.

**Deferred topics:** SQLite source persistence and logical schema, OCCT B-rep versus OCAF persistence, FreeCAD testing/showcasing, visualization replacement, and downstream reinforcement/design/drawing systems. Selecting OCCT does **not** itself decide the persistence format. Revisit these through separate explicit requirements and acceptance criteria.

The three GEO stages are roadmap boundaries, not a single omnibus implementation authorization. Discuss focused milestones, acceptance criteria, testing, and code review before each coding assignment; use a task branch and PR. No roadmap item above authorizes modifications by itself.

## Build and repository hygiene

For viewer changes, the baseline checks are:

```sh
npm ci
npm run build
```

The confirmed local v1.0.3 build on Kubuntu succeeded on 20261111, with zero reported npm audit vulnerabilities and a non-blocking bundle-size warning. This is **not** a substitute for automated regression tests.

`tsconfig.tsbuildinfo` is a generated TypeScript build artifact currently tracked on `main`; removing it from tracking and adding `*.tsbuildinfo` to `.gitignore` requires a separate approved housekeeping change. Do not mix that change into an unrelated geometry milestone without agreement.

## Response header, numbering, and RIC convention

For substantive **conversational responses** by an assistant or coding agent, use the header:

```text
N // YYYY.MM.DD - HH:MM // RIC NN% (IST)
```

- `N`: consecutive answer number **within the current conversation**; restart at 1 in a new conversation.
- Timestamp: actual local date and time in **Asia/Kolkata**, 24-hour format; do not invent a current time if it cannot be determined.
- `RIC`: **Remaining Intellectual Capacity**, a *subjective context-continuity indicator*. It is a rough working estimate, **not** a measured model-resource counter, a token quota, or a probability of correctness. Do not claim precision that is unavailable.
- Below an estimated 25%, include the warning **“Cognitive Sanity Warning: RIC below 25%. Further branching is unsafe.”** and propose a concise handover/snapshot into a fresh conversation.
- This header convention does **not** apply to code, program output, commit messages, pull-request titles, tests, generated files, or other artifacts unless explicitly requested.

## Task closure report

At the end of each authorized development task, report: task identifier, branch and commit; files changed; build/test commands and outcomes; verified behavior; limitations and unresolved questions; and decisions requiring team review. Stop at the agreed milestone boundary. No silent scope expansion.
