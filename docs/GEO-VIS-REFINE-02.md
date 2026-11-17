# GEO-VIS-REFINE-02 — navigation (Task 2 only)

## Baseline and scope

Worktree: `~/Projects/smdviewer-geo-visualiser`. Inspected on 2026-11-17 before
editing: clean `feat/geo-vis-refine-01` at the locally accepted FIX01 commit
`58d6c281615289936b692d900a4632560cc1add6`. Root `AGENTS.md` applies; no nested
instructions were found. Task branch: `feat/geo-vis-refine-02`, created directly
from that exact local commit. No remote tip was substituted or fetched.

The existing Kubuntu Release build and visualization-enabled OCCT **8.0.1**
installation are reused. Only native viewer navigation, its regression tests
and these operator notes change. FIX01 presentation, selection/tag identity,
parser, qualified geometry pipeline, fixtures and browser/WASM sources are
preserved. The regular checkout's source/worktree is untouched; branch and
commit operations necessarily use its shared linked-worktree Git metadata.
No push, merge or PR update is authorized. Task 3 remains outside this task.

## Navigation behavior

- Plain `s`, `b`, `a`, `r` set the existing representation dropdown to S, B, A,
  R, respectively. Its existing signal performs the action, including selection
  clearing, component/isolation reset, comparison-camera retention, any required
  FIX01 eye retreat and empty-group handling. Repeating the active choice is the
  same no-op as the dropdown. Converted faces remains available in the dropdown.
- These keys and the existing `p` projection action share one event filter.
  Auto-repeat is consumed without executing navigation. Modified keys, text
  editors (including read-only details), and dialogs retain normal Qt handling.
  Both event-recipient and focus ancestry are checked for text editors.
- Left-button cube gestures use the same `StartRotation`/`Rotation` operation,
  displacement and direction as ordinary viewport orbit. The existing drag
  threshold is four logical pixels of Manhattan movement. Crossing it latches
  a drag even if the pointer returns to the starting tile. A short click still
  uses the existing 26-direction hit test. The reserved cube footprint includes
  its existing padding. Mouse capture retains the gesture outside the cube and
  widget; release ends capture without snapping or selecting the background.
  Cube clicks/drags retain projection and the exact selected model owner.
- Discrete orientation changes in Perspective call the existing foreground-only
  eye-retreat rule before depth fitting. Direction/up change for the requested
  snap; target and projection remain fixed. Only an unsafe eye retreats, using
  FIX01's existing clearance `max(0.001 mm, foreground diagonal × 1e-6)` and
  framing rule. A safe snap retains exact distance and scale. Continuous free
  orbit retains its existing policy; no collision policy is added.
- The lower-right scale bar chooses a rounded 1/2/5 real-world length in mm or m,
  up to 150 logical pixels. Orthographic measures the viewing plane; Perspective
  measures the camera target plane and explicitly labels it **at target**.
  Camera view dimensions and the physical drawable height determine mm/pixel;
  bar margins, strokes and glyphs account for Qt device pixel ratio. Zoom, Fit,
  projection, retreat and resize update it through viewport redraw. It is an
  infinite, non-selectable screen-space OCCT presentation in the existing
  immediate overlay layer, excluded from foreground bounds and Fit. Small
  vector bitmap glyphs keep labels visible with the existing FreeType-OFF OCCT
  installation. It introduces no widget or mouse interception.

These are sample-verified presentation behaviors on the existing plain and
combined-column viewer fixtures, not expanded parser or geometry qualification.
The scale measures raw source-local millimetres; it does not imply project/global
placement or a new engineering measurement tolerance.

## Reproduce automated verification

```sh
cd ~/Projects/smdviewer-geo-visualiser
export LD_LIBRARY_PATH="$HOME/.local/opt/occt-8.0.1-visual/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake --build build/visualiser-kubuntu --parallel 6
ctest --test-dir build/visualiser-kubuntu --output-on-failure

QT_QPA_PLATFORM=xcb build/visualiser-kubuntu/geo-visualiser/smd-inspector-tests \
  "$PWD/public/samples" --camera
QT_QPA_PLATFORM=xcb QT_FONT_DPI=96 QT_SCALE_FACTOR=1 QT_SCREEN_SCALE_FACTORS=1 \
  build/visualiser-kubuntu/geo-visualiser/smd-inspector-tests "$PWD/public/samples" --camera
QT_QPA_PLATFORM=xcb QT_FONT_DPI=96 QT_SCALE_FACTOR=1 QT_SCREEN_SCALE_FACTORS=2 \
  build/visualiser-kubuntu/geo-visualiser/smd-inspector-tests "$PWD/public/samples" --camera
QT_QPA_PLATFORM=xcb build/visualiser-kubuntu/geo-visualiser/smd-inspector-tests \
  "$PWD/public/samples" --render
git diff --check

# Review the complete focused task against the required local baseline:
git diff 58d6c281615289936b692d900a4632560cc1add6 HEAD -- geo-visualiser docs/GEO-VIS-REFINE-02.md
```

`--camera` runs the actual native OCCT rendering/hit-testing, synthetic Qt pointer
events and Window/dialog callbacks. It checks all 26 discrete directions from
54 tiles in both projections, exact selected-owner retention, small clicks,
same-sense cube/viewport drags, capture/release outside the widget, and a drag
returning to its original tile. Ordinary viewport navigation/picking tests remain.

The combined B regression fits each end in Orthographic, changes to a side
without Fit, performs the same action as P, then invokes the discrete Top/Bottom
snap action. It checks eye and near-plane clearance against foreground vertices,
unchanged target/projection/owner, visible selected-end framebuffer fill, full
end-boundary framing and actual end-face picking. A subsequent safe snap must
retain distance and scale. Default-scale preliminary measurements were about
639.767 → 6964.77 mm eye distance, with 639.767 mm nearest depth, for each end.

Scale tests compare a known projected 1000 mm target-plane distance with the bar,
then independently measure its bright horizontal stroke in captured pixels.
They cover Orthographic Fit/zoom, Perspective, resize, and Top/Bottom retreat.
Raster tolerance is four logical pixels (4 × DPR physical pixels); projected
distance comparison allows 1.5 physical pixels for integer projection rounding.
Separate hide/show tests compare Fit from identical cameras, and pan a real
foreground face behind the bar to verify picking through the drawn stroke.

Window tests compare shortcuts with the dropdown in both projections on plain
and combined columns, including empty A/R, camera, selection, tree, details,
isolation and tag state. They check repeat/modifier suppression and ordinary
`sbarp` text in the tag editor and real Qt file-dialog filename field, plus the
read-only details editor. Existing document reset/failure/cancel, session-tag,
presentation/FIX01 and source-evidence preservation regressions remain included
in the full `--render` smoke. No browser/WASM code is changed or rebuilt.

The delivery report records the exact tested commit and final results. Logs:
`/tmp/geo-vis-refine-02-{build,ctest,camera,camera-1x,camera-2x,render}.log`.
Scale framebuffer captures: `/tmp/geo-vis-scale-*.png` (later runs overwrite
earlier captures). Full review diff: `/tmp/geo-vis-refine-02.diff`.

An initial CTest invocation without `LD_LIBRARY_PATH` failed to load OCCT shared
libraries; the documented runtime path resolves that environment prerequisite.
An initial sandboxed xcb launch could not access display `:0`; desktop tests need
local X11/XWayland access. Automated tests are distinct from human pointer
acceptance. Windows and remote-desktop checks remain pending.

## Short real-pointer Plasma checklist — pending operator execution

1. Open the combined column, select and tag a face. With the viewport focused,
   use `s/b/a/r`, compare with the dropdown, hold each key, and press `p` twice.
   Confirm normal selection clearing on representation changes, retained camera,
   and retained face/tag on projection changes. Type `sbarp` in the tag editor
   and Open SMD filename field; confirm normal text entry. Open plain and check
   A/R remain visibly empty by either route.
2. Select a model face. Short-click cube faces, edges and corners in O and P;
   make a tiny shaky click, then left-drag horizontally/vertically. Check orbit
   sense matches the viewport, projection and selected row/tag stay intact.
   Drag past the cube onto the viewport, and past the widget edge; release.
   Also drag out and back onto the starting tile. Confirm no snap, background
   pick or deselection on release, and ordinary viewport picking still works.
3. With combined B active, use Top/Bottom in O, Fit, change to a side without
   Fit, then P. Reveal the required cube cap with a slight free orbit if needed
   and click Top/Bottom. Check the eye retreats and the end remains visible and
   pickable. Repeat for the other end. From a safe fitted Perspective view,
   snap between safe directions and check scale stays unchanged.
4. Watch the lower-right bar while zooming, fitting, toggling P and resizing.
   Confirm mm/m labels are legible at Plasma's normal and scaled display
   settings; Perspective says “at target”. Pan a face behind it and click the
   stroke; the foreground face should still pick. Check Fit stays foreground-only.

Team review: human Plasma gesture/visual acceptance, Windows and remote desktop.
No Task 3 acceptance, new geometry qualification, push, merge or PR change is implied.
