# GEO-VIS-REFINE-02-FIX01 — cube readability, projection and input

## Baseline and scope

On 2026-11-17, before editing, `~/Projects/smdviewer-geo-visualiser` was clean on
`feat/geo-vis-refine-02` at the exact requested local baseline
`922331596c99e47eafdebd6b3ecd98c6be2b0dea`. Root `AGENTS.md` applies; no nested
instructions were found. This is one focused follow-up commit on that task
branch. The existing Kubuntu Release build and visualization-enabled OCCT 8.0.1
installation are reused. No fetch, push, merge or PR update is part of the fix.
The regular checkout's source/worktree remains untouched; Git operations use
shared linked-worktree metadata.

Changes are confined to native cube presentation, viewport input/raster defaults,
tests/capture tooling, and these notes. Parser, geometry pipeline, fixtures,
browser/WASM, B/A/R palette, lighting, face highlighting, origin/context overlays,
selection/tags and foreground-only framing are preserved. The scale calculation
and “at target” label are unchanged; its explanation remains Task 3 work.

## Implementation

- Six fixed, unlit face tones distinguish adjacent cube faces independently of
  model lights. X uses blue, Y green, and Z neutral tones, with separate positive
  and negative variants. Explicit back-face culling keeps rear colours out of
  the tile gaps. The existing 54 tiles, vector axis labels, stock trihedron and
  native hover ownership remain; no model material/light setting changes.
- `CubePersistence` keeps OCCT's original triedron appearance in O. In P it uses
  an affine view transform with a focal distance of six cube-side lengths,
  cancelling the lower-left corner's off-axis skew. Its x/y scale maps cube
  units to physical pixels; its depth scale supplies mild perspective. The
  exact same OCCT transform-persistence object is used for drawing, hover,
  bounding/depth calculations and selection BVHs/sensitive triangles. There is
  no separate screen-space hit-test approximation or secondary model camera.
  The origin stays 100 logical pixels from the lower-left corner; the side is
  still 84 logical pixels. Model pan/zoom/target do not move or resize the cube.
  Perspective strength is independent of display scaling and model eye distance.
- `cubeLeftGesture_` records left-button gesture ownership independently of the
  existing reserved cube footprint. Further button presses and non-left releases
  return without changing the press point, drag latch, rotation origin or mouse
  grab. Left release alone ends that gesture. Existing short-click, drag threshold,
  outside release, return-to-start behavior and ordinary viewport pan/orbit remain.
- Raster rendering requests **4× MSAA**. OCCT clamps to the driver's limit and
  disables multisampling when its framebuffer/blit path cannot support it; no
  capability check becomes a launch requirement. Installed OCCT's fallback path
  was inspected. Tests exercise rendering with zero samples as well. This does
  not claim an unsupported physical GPU was tested. Default line feathering stays
  **1.0**, adaptive/ray-tracing AA is untouched, and there is no image filtering,
  render-resolution scaling or new Settings control.

These are sample-verified presentation/input behaviors, not new geometry or
parser qualification. Perspective snap eye clearance still uses the existing
foreground-only retreat rule; neither that rule nor continuous free orbit changes.

## Matched captures and measurements

Baseline production code was captured before editing with a small external
harness linked against the existing baseline static libraries. Its source,
compile command and executable are retained in `/tmp/geo-vis-refine-02-fix01-capture*`.
The committed `smd-inspector-captures` executable reproduces the same scenes.
Both use the existing public `pocket_column.smd`, 900 × 800 logical pixels,
`V3d_XnegYnegZpos` (Z+ and X− visible), normal Plasma DPR **1.6979166667**,
and the same selected pocket face. No specimen bytes are changed or published.

All following paths have prefix `/tmp/geo-vis-refine-02-fix01-`:

| Comparison | Before | After |
| --- | --- | --- |
| Z+/X− cube tones in O | `before-cube-O-cube.png` | `after-cube-O-cube.png` |
| Same oblique cube in P | `before-cube-P-cube.png` | `after-cube-P-cube.png` |
| Oblique pocket/selection | `before-pocket.png` | `after-pocket.png` |
| Isolated MSAA comparison | `after-pocket-msaa-off.png` | `after-pocket-msaa-on.png` |
| Native-pixel pocket detail | `after-pocket-msaa-off-detail.png` | `after-pocket-msaa-on-detail.png` |

`before-camera.txt` and `after-camera.txt` agree exactly on DPR, eye, target and
scale; only requested samples change from 0 to 4. Eye is
`(-108.33333333333343, -358.33333333333343, 5083.333333333333)`, target
`(225, -25, 4750)`, and scale `649.1147818375423` mm.

Measured at normal scaling: cube opposite-edge length ratio is **1.00000 in O**
and **1.04449 in P** at the same oblique orientation. Actual framebuffer draw
callbacks report **4 samples**, with driver maximum **32**. With AA disabled,
after-image model pixels outside the cube/scale regions match the baseline
exactly. Enabling MSAA changes **7,024** model-view pixels in those regions;
the inspected pocket detail has 10 colours versus 4 without AA. Native-pixel
inspection shows modest smoothing of the model silhouette, pocket edges, cube
grid boundaries and thin yellow selection outline; flat fills stay sharp. This
was sufficient without changing line-edge controls. These are agent inspections
and automated measurements; operator visual preference/acceptance remains pending.

## Reproduce verification

```sh
cd ~/Projects/smdviewer-geo-visualiser
export LD_LIBRARY_PATH="$HOME/.local/opt/occt-8.0.1-visual/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake --build build/visualiser-kubuntu --parallel 6
ctest --test-dir build/visualiser-kubuntu --output-on-failure
QT_QPA_PLATFORM=xcb build/visualiser-kubuntu/geo-visualiser/smd-inspector-tests "$PWD/public/samples" --camera
QT_QPA_PLATFORM=xcb QT_FONT_DPI=96 QT_SCALE_FACTOR=1 QT_SCREEN_SCALE_FACTORS=1 \
  build/visualiser-kubuntu/geo-visualiser/smd-inspector-tests "$PWD/public/samples" --camera
QT_QPA_PLATFORM=xcb QT_FONT_DPI=96 QT_SCALE_FACTOR=1 QT_SCREEN_SCALE_FACTORS=2 \
  build/visualiser-kubuntu/geo-visualiser/smd-inspector-tests "$PWD/public/samples" --camera
QT_QPA_PLATFORM=xcb build/visualiser-kubuntu/geo-visualiser/smd-inspector-tests "$PWD/public/samples" --render
QT_QPA_PLATFORM=xcb build/visualiser-kubuntu/geo-visualiser/smd-inspector-captures \
  "$PWD/public/samples" /tmp/geo-vis-refine-02-fix01-after
git diff --check
git diff 922331596c99e47eafdebd6b3ecd98c6be2b0dea HEAD
```

The focused suite keeps all 26 directions from 54 principal-view tiles in both
projections, and additionally clicks their geometrically projected tile centres
and all 27 visible tiles at the matching three-face oblique orientation. Actual
face/edge/corner clicks must agree with their expected source direction and retain
the exact selected model owner. Existing hover, small-click, outside-release and
return-to-start checks remain. New right/middle chords check capture before/after
the extra release, unchanged rotation origin, and no final left-release snap,
camera-direction change, pick, deselection or selection notification.

Tests also check distinct rendered Z+/X− tones, O/P edge convergence, cube anchor
and size invariance under pan/zoom, actual draw-time MSAA samples and rendering
with zero samples. The scale stroke test uses a hidden-bar negative-control frame
to include partial MSAA coverage at 1×; the existing stroke tolerance and independent
known-distance check remain. Cube-overlap tests use a currently projected tile
centre rather than assuming that an O hit pixel is still inside a P tile.
Existing shortcut/editor, scale, eye-clearance and FIX01/source
preservation checks remain in `--camera`/`--render`. Logs use prefix
`/tmp/geo-vis-refine-02-fix01-` with `build`, `ctest`, `camera`, `camera-1x`,
`camera-2x`, `render`, and `after` `.log` suffixes. Final delivery records the
exact tested SHA, outcomes and `/tmp/geo-vis-refine-02-fix01.diff`.

Synthetic mouse events service non-input Qt events between gestures; UI startup
explicitly waits for the queued initial document load. Test output flushes live.
Earlier normal-scale runs waited in DRM synchronization and were interrupted,
not counted as passes. A VBlank-disabled retry did not resolve that wait. Final
commands retain default presentation synchronization; no VBlank or driver-setting
workaround is added to the application. Real Plasma pointer acceptance remains
separate from these synthetic event tests.

## Short Plasma operator check — pending

1. At an oblique angle showing Z+ and X−, compare adjacent face tones, all 3×3
   tiles, labels and hover feedback. Toggle P at the same orientation; check mild
   convergence, roughly unchanged apparent size and fixed lower-left anchor.
   Pan/zoom the model and check the cube stays put.
2. Select/tag a model face. In O and P, click cube faces/edges/corners; then left
   drag and press/release right while holding left. Continue dragging and release
   left, including outside the cube/widget and after returning to the initial
   tile. Check grab/gesture continuity and no release snap or selected-row/tag loss.
   Repeat with middle; check ordinary viewport right/middle pan remains normal.
3. Compare matched normal-scale pocket images at native pixels. Inspect silhouette,
   recess edges, cube grid and yellow outline for smoother strokes without blur.
   Confirm origin/context overlays, selected fill, scale label and foreground Fit
   still behave as before. Recheck the existing unsafe B Top/Bottom snap sequence.

Team review remains for real-pointer Plasma behavior and visual acceptance.
Windows, remote-desktop and unsupported physical-GPU verification remain pending.
No Task 3, geometry/persistence change, push, merge or PR update is implied.
