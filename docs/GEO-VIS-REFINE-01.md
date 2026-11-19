# GEO-VIS-REFINE-01 — rendering and correctness

## Baseline and boundary

Task 1 only, based on `feat/geo-vis-display-01` at
`022b7353f59facf021f8b86fbedfa65d68cfea2e`. On 2026-11-17 the
`~/Projects/smdviewer-geo-visualiser` worktree was clean at that commit; after
`git fetch origin`, `origin/feat/geo-vis-display-01` matched exactly.
Implementation branch: `feat/geo-vis-refine-01`. The regular checkout's source
files were not changed; linked-worktree Git metadata was used for fetch,
branch creation and commit. No push, merge or PR update is part of this task.

The installed visualization-enabled OCCT **8.0.1** and existing Kubuntu Release
build are reused. Changes stay in the native viewer, presentation regressions
and these notes. Parser, geometry pipeline, specimen files, browser/WASM code
and tag identity policy are unchanged. Tasks 2–3, Perspective cube-snap
clearance, Ctrl-click and source-metadata review remain separate work.

## Behavior and implementation

- Selected faces have a 48%-transparent amber-yellow fill and a brighter yellow
  boundary in all three styles. OCCT shades the actual display-copy face,
  including its concave boundaries and inner wires. Unselected Wireframe faces
  stay unfilled. The passive `FaceHighlight` builds no sensitive entities and
  never becomes an inspector entity. Style changes retain the original AIS
  owner, fill presentation, pick mode, inspector row, details and session tag.
- The muted origin arrows remain anchored at raw source-local `(0,0,0)` mm,
  with the existing 36 logical-pixel zoom persistence. They overlay opaque
  faces and selected fill, move with zero and can leave the frame. Their layer
  uses OCCT's immediate pass: normal-pass transparency is otherwise composited
  after opaque arrows, even with depth testing disabled. This layer neither
  clears nor writes depth. The model's Shaded opacity is unchanged.
- Context uses two matching dashed wire strokes: a dark 3.5-pixel halo and a
  lighter 1.8-pixel core, with a muted red accent for reductions. The native
  passive `ContextOutline` constructs no selection. Existing B→A/R, A→B/R and
  R→B/A combinations and Show context behavior remain. R context still draws
  behind the opaque active pocket; B/A context reveals contained features.
- A representation/isolation change without Fit now applies the existing
  Perspective eye-retreat rule before depth fitting. It uses only active
  foreground geometric bounds. When clearance is already safe, the comparison
  camera is retained. When unsafe, only distance increases: projection,
  direction, up and target are preserved. The existing numerical clearance is
  `max(0.001 mm, bounds diagonal × 1e-6)`, independent of modelling tolerances.
  Cube snaps are intentionally unchanged for Task 2.
- `geo-visualiser/appearance.hpp` centralizes the original B/A/R filled palette
  and 72% factory transparency for Task 3. Defaults are linear RGB; future sRGB
  fields must convert at the UI boundary. Wire colours use an 80% palette /
  20% white mix in linear RGB: B `(0.656,0.672,0.688)`, A
  `(0.456,0.472,0.488)`, R `(0.776,0.616,0.624)`. Yellow stays independent.

Origin, selected fill and context have infinite presentation state and never
participate in Fit or Perspective-clearance bounds. Source coordinates,
topology and qualified pipeline tolerances are not changed. Rendering behavior
is sample-verified on the existing plain and combined-column viewer fixtures;
inner-wire behavior is separately verified with a synthetic native OCCT face,
not claimed as expanded SMD parser acceptance.

## Reproduce verification

```sh
cd ~/Projects/smdviewer-geo-visualiser
export LD_LIBRARY_PATH="$HOME/.local/opt/occt-8.0.1-visual/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake --build build/visualiser-kubuntu --parallel 6
ctest --test-dir build/visualiser-kubuntu --output-on-failure

QT_QPA_PLATFORM=xcb build/visualiser-kubuntu/geo-visualiser/smd-inspector-tests \
  "$PWD/public/samples" --display
QT_QPA_PLATFORM=xcb build/visualiser-kubuntu/geo-visualiser/smd-inspector-tests \
  "$PWD/public/samples" --render
git diff --check
```

`--display` is the focused smoke and includes Window controls, owner/tree/tag
retention, file-open failures/cancellation and session-tag isolation. `--render`
also exercises the full existing adapter, geometry-preservation, camera/cube
and rendered-picking suite. Evidence snapshots bracket the rendering checks.
New framebuffer assertions cover actual specimen fill, synthetic inner hole
and concave recess, an unselected Wireframe face, brighter selected boundary,
arrows over an enclosing opaque box and selected fill without colour tinting,
R foreground priority, and foreground picking through passive presentations.
All three wire colours are checked explicitly. Existing origin zoom, pan,
orbit, projection, offscreen and foreground-only bounds checks are retained.

The combined Perspective R → Bottom → Fit → B regression requires an eye
retreat (approximately 690.516 → 13119 mm on this build), preserves target/up/
direction/projection, and checks B visibility, picking and near-plane clearance.
Separate safe Perspective B/A/R switches must preserve comparison framing.

Logs are `/tmp/geo-vis-refine-01-{display,render}.log`; framebuffer captures use
`/tmp/geo-vis-display-*.png`. The delivery report records the exact tested SHA
and final build/CTest/smoke results. Agent inspection of framebuffer captures
is separate from human pointer acceptance on Plasma. Windows and remote
desktop verification have not been performed.

Repository hygiene: `npm ci` passed with zero reported vulnerabilities.
`npm run build` failed at the existing missing/stale `public/wasm` asset guard,
which requests Emscripten 4.0.20 and `npm run build:wasm`. No WASM rebuild was
performed. Generated `tsconfig.tsbuildinfo` was restored byte-for-byte.

## Short human Plasma check

1. Open the combined column. Select and tag a face with a recess; cycle Shaded,
   Transparent and Wireframe. Check amber fill follows its bounded region,
   yellow outline stays bright, row/tag/pick mode stay selected, and other
   Wireframe faces remain empty.
2. Orbit/pan/zoom around zero. Check muted arrows draw over the face and amber
   fill, move with zero, can leave the view, and do not intercept a face click
   or change Fit. Toggle context in B/A/R; confirm dashed ghosts are legible
   while R reads first and foreground picks still work. Compare wire colours.
3. In Perspective, use R → Bottom → Fit → B. Check B appears and is pickable
   after eye retreat. From a safe fitted B view, switch B/A/R and check framing
   is retained. This does not test or accept the deferred cube-snap fix.

Team review remains necessary for visual preference and human Plasma pointer
acceptance. No new parser/geometry qualification or follow-on task acceptance
is implied by the automated rendering results.
