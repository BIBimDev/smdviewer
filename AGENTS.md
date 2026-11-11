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

## Repository snapshot — verified 20261111

- Repository: `BIBimDev/smdviewer`; default branch: `main`.
- At drafting, `main` was `0ef2aaf6561610278dc00d47edbc122886905c1f` and contained **Viewer v1.0.3** (inspector, interaction refinements, and view navigation).
- The existing browser parser lives in `src/smd/`:
  - `tokenizer.ts` — tokenization.
  - `parser10125.ts` — `#10125` metadata, profile parsing, and B-rep orchestration.
  - `brepDetector.ts` — explicit B-rep candidate selection and vertex/plane checks.
  - `transform.ts` and `types.ts` — transforms and data interfaces.
- `src/viewer/` (including `viewCube.ts`) and `src/main.ts` implement presentation/interaction, not the source-format contract.
- `public/samples/` holds the two accepted baseline specimens.
- `docs/SMD_V1_STRUCTURE.md` contains specimen-derived interpretation notes.
- The experimental `experiment/bi301-subject-selection` branch investigates drawing-subject selection, including `#10071` rather than blindly choosing `#10125`. Its heuristics are **not** established general rules and must not be silently folded into a baseline parser port.

**This section is a dated snapshot. Reinspect the repository at the start of every implementation task.**

## Baseline geometry fixtures and limitations

| Specimen | B-rep vertices | Faces | Bounding dimensions |
| --- | ---: | ---: | --- |
| `plain_column.smd` | 8 | 6 | 750 × 650 × 6000 mm |
| `pocket_column.smd` | 16 | 10 | 750 × 650 × 6000 mm |

For both specimens, rendered-coordinate bounds are X `-375..375`, Y `-325..325`, Z `-1000..5000` mm. The pocket specimen includes a rectangular recess near the top. The current browser parser primarily supports the confirmed `#10125` subset and single-loop faces; successful rendering does not prove a watertight, topologically valid solid.

## Engineering development sequence

**GEO-00 — independent C++20 parser (proposed first implementation task)**

- Port the behavior of the **accepted** TypeScript SMD parser into a separate, testable C++20 library, preferably under `libsmd/`, using CMake. Keep the existing TypeScript implementation untouched.
- Preserve data structures, tokens, source-format assumptions, coordinate transforms, topology, and error behavior before changing or generalizing the parser.
- Provide a small command-line extraction utility and documented output format to support comparative tests; the interchange contract must be versioned before downstream consumers depend on it.
- Compare field-by-field against golden outputs from the TypeScript parser for both fixtures, with explicit numerical tolerances. Include malformed-input tests.
- A shared library can be built, but a stable cross-compiler/cross-language C ABI is **not** implied by exporting C++ symbols. Specify that interface separately if required.
- Do not add SQLite, extrusion recognition, Boolean geometry, reinforcement, WebAssembly, or experimental multi-block selection in GEO-00.

**GEO-01 — normalization and SQLite (later)**

Define and review a normalized, versioned geometry contract and persistence schema. Store original B-rep data and provenance separately from any derived parametric model.

**GEO-02 and beyond — interpretation and engineering (later)**

Recognize extrusions and geometric features; verify reconstructed geometry against the original B-rep; then evaluate robust geometric kernels, reinforcement, design, and drawing generation in separate approved milestones.

No roadmap item above is authorization to implement it.

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
