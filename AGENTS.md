# AGENTS.md — BIBimDev/smdviewer

## 1. Purpose and governance

Develop an independent, predominantly headless engineering system for precast concrete elements: STRAKON/DICAD geometry extraction, geometric processing, parametric interpretation, BIM exchange, reinforcement, structural design and workshop-drawing generation.

The browser viewer and native GEO-Visualiser are research and inspection tools, not the engineering system of record. These instructions govern the repository and its subdirectories unless superseded by a deliberately introduced, more specific `AGENTS.md`.

Authoritative repository: `BIBimDev/smdviewer`; integration branch: `main`.

### Working principles

1. **Inspect the actual baseline.** Before proposing or making code changes, inspect the applicable instructions, relevant source, current branch, commit SHA and working-tree status. Fetch/check the remote when currency matters. A dated report is not proof of the current state.
2. **Require an agreed task.** Discuss requirements and acceptance criteria before implementation. Use small, independently testable milestones. No source changes, commits, pushes, merges or rebases without explicit authorization for the specific task.
3. **Protect team work.** Preserve existing changes. Work on a task branch and submit a reviewable pull request. Never force-push, alter another contributor's branch or merge without authorization.
4. **Separate evidence from interpretation.** The SMD format is reverse-engineered from limited specimens, not an authoritative DICAD specification. Distinguish established requirements, sample-verified findings, inferences, proposals and unknowns. Reject unsupported inputs explicitly; do not invent semantics.
5. **Preserve source evidence.** Original SMD B-rep geometry is authoritative. Retain vertex coordinates and order, ordered face loops, flags, names, plane equations, block tags, source identities and recorded transformations. Derived geometry, feature interpretations and parameterizations never replace that evidence.
6. **Keep coordinate systems explicit.** Distinguish raw source-local geometry, recorded B-rep transformations, element placement and project/global placement. Do not infer project-global semantics from `verticesWorld` or silently compose transformations. Specify units, tolerances and numerical policies.
7. **Maintain module boundaries.** Prefer native C++20 for engineering components. Keep processing independent of STRAKON at downstream interfaces, IFC, Qt, OpenGL, Three.js, DOM and viewer state where these are not essential to the component's purpose. Preserve accepted interfaces and both viewers unless separately authorized to change them.
8. **Test before claiming success.** Run focused tests and relevant regression builds; report exact commands and outcomes. Include malformed input and negative controls. Never claim unexecuted platform, application or operator verification.
9. **Respect confidentiality.** Do not publish proprietary `.kon` files, drawings, credentials, internal project information or additional specimens without permission. Generated engineering exports belong outside the public browser asset tree; do not submit confidential geometry to external validation services without authorization.

## 2. Accepted baseline and architectural contracts

Reference baseline verified on 20261126: `main` at `162638f2da8525da8e5caacdfb92d624f97d95ba`, three sample-only commits beyond the GEO-Export closure at `49a424cd82b2acd3f21a9a3f32abac38a4cba2b7`. This is a historical reference, not a substitute for checking the current repository and working tree.

The following milestones are accepted and closed. Historical implementation details, test results and limitations reside in their referenced documents and Git history, not in this governance file.

| Milestone | Accepted scope | Reference |
| --- | --- | --- |
| GEO-00 | Native C++20 SMD parser, native JSON bridge and WASM integration | [libsmd/README.md](libsmd/README.md), [INT01](docs/GEO-00-INT01.md), [INT02](docs/GEO-00-INT02.md) |
| GEO-01 | Validated planar OCCT faces with source correspondence | [GEO-01B](docs/GEO-01B.md), [GEO-01C](docs/GEO-01C.md) |
| GEO-02 | Closed shell and validated connected physical solid | [GEO-02B](docs/GEO-02B.md), [GEO-02C](docs/GEO-02C.md) |
| GEO-03 | Principal prism, Boolean differences and qualified reconstruction | [GEO-03A](docs/GEO-03A.md), [GEO-03B](docs/GEO-03B.md), [GEO-03C](docs/GEO-03C.md) |
| GEO-Visualiser | Optional native read-only geometry inspector | [GEO-VISUALISER.md](docs/GEO-VISUALISER.md) |
| GEO-Export | Native JSON/BREP persistence, headless CLI and viewer export | [GEO-EXPORT.md](docs/GEO-EXPORT.md) |

### Source and operational geometry

- `libsmd/` is the authoritative production SMD parser, also compiled to WASM for the browser. The original TypeScript parser is a regression oracle, not a production fallback. Parser JSON v1 is diagnostic interchange, not a lossless source archive.
- `libgeometry/` uses native OCCT topology and geometry, not a parallel half-edge model. The accepted build requires OCCT **8.0.1 exactly**. Changes to this baseline require explicit agreement and qualification.
- The accepted initial source scope is planar polygonal faces with straight edges and one connected physical solid per supported dataset. General STRAKON input, assemblies, curved source geometry and arbitrary construction histories are not established capabilities.
- Preserve source-to-kernel correspondence and distinguish source, derived and generated faces. OCCT traversal ordinals are not persistent source identifiers. No silent healing, snapping, topology simplification or tolerance relaxation.
- All accepted geometry is expressed in raw source-local millimetres, with recorded STRAKON, element and project transforms unapplied. Feature-local and future project placements must be defined explicitly.

These provisions consolidate the enduring GEO-01-D01 through GEO-01-D10 architectural decisions; their original record remains in Git history.

### Constructive decomposition (GEO-03)

| Symbol | Meaning |
| --- | --- |
| `S` | Validated complete physical element solid |
| `B` | Derived principal constant-cross-section prism |
| `A[]` | Individual additions |
| `R[]` | Individual reductions |

```text
A = S \ B
R = B \ S
S = (B \ R) ∪ A

Here A and R denote the unions of their respective body collections.
```

The accepted principal-prism recognition rule is restricted to a source-local +Z extrusion with a rectangular XY section. Neither the decomposition nor a successful reconstruction establishes the original construction history, unique family interpretation, mould design or manufacturing sequence.

Use the established `1e-7 mm` Boolean/classification tolerance and `1 mm³` absolute reconstruction-volume comparison for the qualified GEO-03/GEO-Export scope. Volume agreement alone is insufficient: require empty directed solid residuals and complete bidirectional bounded-face coverage. Do not discard positive-volume residuals or enlarge tolerances silently. Any new operation must have an explicitly agreed numerical policy.

### Native persistence and inspection

- Preserve `smd_geometry::ExportBodies` (`S`, `B`, `A[]`, `R[]`) and the existing `exportBrep()` / `importBrep()` interface.
- GEO-Export uses one version-1 JSON file per element containing OCCT ASCII BREP `VERSION_3` solids, without cached presentation meshes or normals. It preserves processed geometry, not the complete original source evidence or its provenance.
- Exporters and downstream engineering modules must remain independent of viewer selections and display geometry. Existing viewers remain functional; native session tags are transient annotations.
- The operational default export directory is repository-root `exports/`, excluded from ordinary Git staging and the browser production asset tree. This is not a filesystem security boundary. Libraries receive explicit destinations; preserve existing output files on failure.

See [GEO-EXPORT.md](docs/GEO-EXPORT.md) and [libgeometry/README.md](libgeometry/README.md) for the qualified specimens, API, build instructions and outstanding platform limitations. GEO-Export Windows/MSVC qualification remains outstanding at this baseline. Additional specimens in the repository are not automatically qualified.

## 3. Accepted development roadmap — revised 20261127

The next development objective is a reusable parametric engineering model, not another representation of final solid geometry.

Use consecutive short GEO milestone identifiers for the next geometric stages. GEO-IFC and REINF are distinct downstream development streams.

| Milestone | Objective | Status |
| --- | --- | --- |
| GEO-04 | Family.Type contract, column classification and rectangular principal body `B` | Architecture accepted; next |
| GEO-05 | Parametric interpretation of additions `A[]` and reductions `R[]`; investigate haunched corbels and tapered pockets | Planned |
| GEO-06 | Complete parametric regeneration and geometric qualification against `S` | Planned |
| GEO-IFC | Parametric IFC exchange | Planned, following parametric qualification |
| REINF | Independent headless parametric reinforcement modules | Future |

The roadmap and GEO-04 architectural decisions are agreed. They do not authorize implementation or settle detailed APIs, algorithms, dependencies or milestone acceptance criteria beyond the explicit decisions below.

### GEO-04 — accepted architectural decisions (20261127)

GEO-04 establishes the internal parametric engineering contract and its first qualified geometric family. It consumes the accepted GEO-03A result; it does not replace or extend the existing recognition rule.

| Decision | Accepted requirement |
| --- | --- |
| GEO-04-D01 | Separate geometric family, constructive role (principal/addition/reduction) and engineering classification. Reuse geometry families across roles. Permit future nonrectangular families, including corbels with an inclined underside (*Schmiege*), without implementing them now. |
| GEO-04-D02 | Implement only `RectangularPrism` in GEO-04. |
| GEO-04-D03 | Use section-centred canonical coordinates: origin at the centre of the starting section; longitudinal axis +Z; source-aligned orientation for GEO-04. Preserve the translation to source-local coordinates. |
| GEO-04-D04 | Extract parameters from the existing `PrincipalPrismCandidate`; do not duplicate principal-prism recognition. Do not change `ExportBodies` or its JSON/BREP contract. Recognition from independently supplied solids is deferred. |
| GEO-04-D05 | Use strongly typed, dependency-free C++20 parameter structures, independent of OCCT, SMD parsing, IFC and presentation technology. |
| GEO-04-D06 | Preserve recovered dimensions without nominal rounding. Defer automatic type consolidation, nominal catalogue matching and persistent type administration. |
| GEO-04-D07 | Establish an independent `libparametric/` module with a dependency-free parameter contract and an OCCT-dependent adapter for extraction, regeneration and qualification. Preserve existing module interfaces and make the new build optional. |
| GEO-04-D08 | Regenerate `B` from parameters alone; independently qualify the regenerated solid against the accepted reference `B`, including validity, placement, volume, empty directed solid residuals and complete bidirectional bounded-face coverage. Preserve inputs. |
| GEO-04-D09 | The project-owner-confirmed filename prefixes `COL` (Column) and `ST` (Stütze) identify columns. Keep filename-based engineering classification separate from geometric recognition; unrecognized names remain unclassified. Do not infer feature counts or other semantics from the remaining filename. |
| GEO-04-D10 | Cross-sectional dimensions are type parameters; extrusion length and placement are instance parameters. |

For `RectangularPrism`, the canonical domain in millimetres is `-bx/2 <= x <= bx/2`, `-by/2 <= y <= by/2`, `0 <= z <= L`. The type defines `bx, by`; the instance defines `L` and the canonical origin in source-local coordinates. The initial orientation is identity. Recover the origin from the XY midpoint and minimum Z of the GEO-03A candidate; do not apply recorded STRAKON transformations. Preserve non-nominal dimensions and offsets.

Store type parameters by value initially. No central family/type catalogue, deduplication, persistence scheme or Revit integration is required or authorized in GEO-04. A future, separate import-mapping module shall assign the appropriate internal Family.Type from the existing Revit family; its mapping rules, interface and technology remain deferred.

GEO-04 qualification compares regenerated `B` with reference `B`, not `S`. Use the established `1e-7 mm` Boolean tolerance and `1 mm³` absolute volume comparison; volume alone is insufficient. Verify independent generation after deliberate parameter changes, reject invalid or inconsistent inputs, and preserve the accepted geometry pipeline and all prior regressions. Exact APIs, diagnostics, prefix matching details and the test matrix are to be specified in the focused implementation assignment. Do not treat architectural acceptance as implementation authorization.

### GEO-05 and GEO-06 — subsequent parametric processing

- Extend the internal model to individually identifiable additive and subtractive features, with explicit feature-local placements relative to the principal body. Reuse geometric families rather than duplicating construction algorithms by role. Investigate haunched corbels (*Schmiege*) and tapered reductions when supported by geometric evidence; do not presume a unique engineering interpretation or subdivide a feature merely for geometric convenience.
- Establish supported family parameterizations for `A[]` and `R[]` and qualify each regenerated constituent against its corresponding reference body. Explicitly reject unsupported or ambiguous parameterizations; do not substitute B-rep geometry for missing parameters.
- GEO-06 assembles the complete parametric element and independently verifies its reconstructed solid against `S`, retaining the established numerical qualification policy. Keep the internal model reusable by IFC and reinforcement without either becoming a dependency of parametric processing.
- Detailed family definitions, feature recognition, orientation handling, identities, complete-element interfaces and acceptance criteria require separate agreement in the corresponding milestone.

### GEO-IFC — external parametric BIM exchange

- **GEO-IFC-D01 accepted:** IFC4 ADD2 TC1, STEP physical file format (IFC-SPF). Other schemas are deferred. Do not claim MVD compliance without qualification.
- Export the parametric principal body and identifiable additive/subtractive features, preserving their parameters, type/occurrence relationships and placements. B-rep and tessellated production representations or fallbacks are excluded from this development objective.
- Investigate parameterized profiles, swept solids, type assignment, additive/subtractive IFC feature relationships and constructive Boolean alternatives. Their precise mapping and receiving-application support remain open. Derived bodies must not be counted as separate physical precast elements or as duplicate material.
- Preserve source-local millimetres; no unverified project placements, material properties or manufacturing semantics. The semantic context, spatial scaffold and GUID policy require detailed agreement.
- Keep IFC serialization separate from parametric recognition and native JSON/BREP persistence. Investigate an IfcOpenShell entity-only build versus a restricted native writer; no technology has been selected. Do not compromise the accepted OCCT baseline to accommodate an IFC dependency.
- Qualify schema, entity semantics, parameters, feature relationships and independent geometric reconstruction. Test actual interoperability with selected applications; rendering alone is insufficient.

### REINF — independent engineering processing

Develop our own headless parametric reinforcement modules consuming the internal parametric element model. IFC is an intermediate development deliverable and a useful external interface, **not** a required input, internal data format or runtime dependency of reinforcement processing. Third-party reinforcement generation is not a GEO-IFC acceptance requirement. Reinforcement rules, engineering design criteria and their qualification are separate future tasks.

```text
Source SMD -> GEO-00/01/02 -> S -> GEO-03 -> S, B, A[], R[]
                                                     |
                         +---------------------------+------------------+
                         |                           |                  |
                     Inspection                 GEO-Export      GEO-04/05/06
                                                  JSON/BREP            |
                                                              Parametric model
                                                                 /         \
                                                             GEO-IFC      REINF
                                                                |           |
                                                               IFC    Reinforcement
```

Native geometry persistence, inspection and the two downstream engineering streams remain independently usable within their respective accepted contracts.

## 4. Development, build and verification procedure

- Discuss requirements -> agree architecture and acceptance criteria -> issue a focused Codex assignment -> implement and test -> independently review -> authorize integration -> close the milestone.
- Record architectural decisions explicitly. For multi-milestone work, maintain an agreed working ledger and consolidate the permanent acceptance record in `docs/` at closure. Do not reopen completed stages implicitly.
- Primary environment: Kubuntu/Linux, GCC, C++20 and OCCT 8.0.1. Windows/MSVC is a supplementary target. Pin actual dependency versions, architecture, compiler, runtime and build configuration for each task. Qt is needed only for native viewer work.
- Preserve the default parser-only build and independent WASM path; native geometry and viewer builds remain opt-in. Optional parametric and future IFC dependencies must not leak into builds where the corresponding modules are disabled. No implicit dependency downloads.
- Use focused CTests and relevant fresh build configurations; preserve existing assertions and reference fixture bytes (including LF line endings). Distinguish test counts, automated smoke tests and manual operator acceptance. Document outstanding checks rather than claiming them complete.
- For browser viewer changes, run `npm ci` and `npm run build` in addition to relevant regressions. Do not mix generated-file housekeeping or unrelated repository changes into engineering tasks.
- Consult the milestone references for detailed commands, specimen lists, regression evidence and limitations; avoid maintaining duplicate historical test matrices here.

## 5. Response header, numbering and RIC convention

For substantive conversational responses by an assistant or coding agent, use:

```text
N // YYYY.MM.DD - HH:MM // RIC NN% (IST)
```

- `N`: consecutive answer number within the current conversation; restart at 1 in a new conversation.
- Timestamp: actual local date and time in Asia/Kolkata, 24-hour format; do not invent a time if it cannot be determined.
- `RIC`: Remaining Intellectual Capacity, a subjective estimate of context continuity, not a measured resource counter, token quota or probability of correctness.
- Below an estimated 25%, include **“Cognitive Sanity Warning: RIC below 25%. Further branching is unsafe.”** and propose a concise handover into a fresh conversation.
- The header does not apply to code, program output, commit messages, PR titles, tests, generated files or other artifacts unless explicitly requested.

## 6. Task closure report

At the end of each authorized task, report the task identifier, branch and commit, files changed, build/test commands and results, verified behavior, limitations and unresolved questions, and decisions requiring team review. Stop at the agreed boundary. No silent scope expansion.
