# GEO-VIS-CAMERA-01 — projection and orientation

## Baseline and scope

Worktree `~/Projects/smdviewer-geo-visualiser` was clean on
`feat/geo-vis-open-01` at the explicitly requested local commit
`853ef00ea0775630621d3df2c51069abe3418d36` (including `01d2d0e`).
Created `feat/geo-vis-camera-01` directly from that commit. No remote base was
substituted. Changes are confined to the native viewer, its tests, and these
operator notes. No push, PR update, merge, parser/geometry change, fixture change,
browser change, tag-identity change, or Ctrl-click/source-metadata work is included.

## Controls

- **P** switches Orthographic/Perspective once per key press. Auto-repeat is
  consumed. Text editors and modal dialogs retain normal text input. The status
  bar reports the actual camera mode as **O · Orthographic** or **P · Perspective**.
- Switching projection preserves direction, up and target, and normally scale at
  the target plane. OCCT adjusts perspective eye distance using its existing 45°
  vertical field of view. If that scale places the eye inside/behind the displayed
  bounds, the eye retreats enough to frame those bounds from in front. This safety
  exception enlarges the target-plane scale; switching back to Orthographic then
  retains that enlarged scale. It does not call Fit or change selection/tree/tags.
  Objects at other depths naturally change apparent size in Perspective.
- Startup and every successful Open use fitted Orthographic/default isometric S.
  Cancel and failed opens retain the previous camera, projection and active state.
- The lower-left cube has nine tiles per face. A centre tile chooses one axis,
  an edge tile two axes, and a corner tile three axes: **26 unique directions**.
  Shared tiles on incident faces choose the same direction. Labels are X+/X−,
  Y+/Y−, Z+/Z−; front is −Y, right +X and top +Z.
- Side views use +Z screen up. Top uses +Y up; Bottom uses −Y up. Other views use
  the projection of +Z onto the screen. Cube clicks retain projection, target,
  scale and selected model owner. Use Fit after changing direction if desired.
- Hover is cyan. The cube tracks camera orbit. The 200 × 200 logical-pixel control
  footprint includes tile gaps and axis arrows; clicks/drags there never pick or
  deselect model geometry. Cube dragging does nothing. Picking and the existing
  orbit, pan, wheel zoom and Fit remain available outside the control.

## OCCT evaluation and local adaptation

Inspected the exact installed OCCT **8.0.1** headers and its local source.
Stock `AIS_ViewCube` supplies 26 orientation owners, dynamic highlighting,
infinite-state presentation (excluded from Fit), and screen anchoring. Its stock
bevelled face/edge/corner appearance does not meet the visible 3×3 requirement.
`geo-visualiser/view_cube.*` therefore subclasses it locally:

- Each owner's facets are the corresponding squares on incident faces: 54 outward
  tiles, one owner per direction. OCCT still manages presentation and selection.
- Uniform tile sensitivity and front-facing tile-normal checks replace the stock
  enlarged edge/corner sensitivities. This prevents hidden/grazing facets from
  winning detection through grid gaps.
- Triedron persistence plus `Graphic3d_TMF_OrthoPers` gives the miniature cube an
  orthographic screen representation in both model projections. Dimensions and
  anchor offsets follow Qt's pixel ratio.
- Vector axis labels work with the existing FreeType-OFF install; no dependency
  rebuild is needed. Model clicks are explicitly routed separately from cube
  owners; stock animation/selection callbacks are not invoked. Direction changes
  are immediate and never Fit the current model or selected entity.

The cube never enters the display adapter's model components or inspector tree.
No browser code is imported or changed.

## Launch and tests on Kubuntu

Reuse the Release build and visualization-enabled OCCT install:

```sh
cd ~/Projects/smdviewer-geo-visualiser
export LD_LIBRARY_PATH="$HOME/.local/opt/occt-8.0.1-visual/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake --build build/visualiser-kubuntu --parallel 6
ctest --test-dir build/visualiser-kubuntu --output-on-failure

cd /tmp
QT_QPA_PLATFORM=xcb \
  "$HOME/Projects/smdviewer-geo-visualiser/build/visualiser-kubuntu/geo-visualiser/smd-inspector-tests" \
  "$HOME/Projects/smdviewer-geo-visualiser/public/samples" --render
QT_QPA_PLATFORM=xcb \
  "$HOME/Projects/smdviewer-geo-visualiser/build/visualiser-kubuntu/geo-visualiser/geo-visualiser"
```

`--camera` runs the focused camera/render and Window/Open suites; `--render`
includes them and all existing rendering/adapter checks. Default CTest stays
headless with the existing eight test registrations. Its adapter test also
checks the explicit direction contract, all 54 tile positions and winding.

Camera desktop regressions click all 54 tiles from their six incident face views
in both projections, covering every direction twice or more where shared.
They check actual camera direction/up, projection/selection retention, no model
selection callbacks, hover framebuffer changes, cube rotation during orbit,
target-plane pixel scale, disabled cube dragging, interception over panned
geometry, normal picking outside the cube, and exclusion from Fit bounds.
Window tests select a combined S face and save a tag, send P twice and repeat
key events, type p into a tag and file dialog, then check Perspective/non-default
camera retention on Cancel/failure and fitted Orthographic/isometric plain reset.
Existing source-evidence preservation snapshots remain in the full render run.
Temporary captures/logs are outside the repository under `/tmp/geo-vis-camera-*`.

The tested build uses Ubuntu/Kubuntu 26.04.1, Plasma with XWayland/xcb,
GCC 15.2.0, Qt 6.10.2 and the existing OCCT 8.0.1 installation. Exact tested
source commit: `88bff0d8126d847427d71ae6d15e802babb0a88b`. Native build passed,
CTest passed 8/8, focused `--camera` passed and full `--render` passed, including
the final repeat at that commit. A local Plasma keyboard/indicator visual check
was also performed.

On 2026-11-16 the user reported **Passed** for the short human Plasma pointer
check: selected-face P/P round trip, cube centre/edge/corner clicks in both
projections, Top/Bottom, and cube tracking during orbit. This is operator-reported
acceptance, separate from automated viewport evidence. XWayland ignored the
agent's synthetic XTest pointer motion, so it was not used as manual evidence.
Windows and remote-desktop checks remain **pending**.

Repository-required hygiene: `npm ci` passed with zero reported vulnerabilities.
`npm run build` failed on the baseline's missing/stale `public/wasm` assets and
requested Emscripten 4.0.20 / `npm run build:wasm`. No WASM rebuild was performed.
The tracked `tsconfig.tsbuildinfo` was restored byte-for-byte after the attempt.

Review items: local cube presentation/hit policy, principal screen-up convention,
and outstanding platform checks. This ends at GEO-VIS-CAMERA-01.

## End-view eye clearance follow-up — 2026-11-16

Starting clean at `60b9f1cf15eefaff41626bf329c5f2bf2513c388` on the same topic
branch, added regressions for combined-column Top → Fit → P and Bottom → Fit → P.
The original Top regression failed: scale 795 mm placed the Perspective eye at
959.65 mm from the target, 5365.35 mm behind the nearest model geometry.

The native viewport now checks a conservative axis-aligned box of only the
currently displayed model copies. No cube, hidden component, source shape,
selection-only bounds or triangulation participates. Camera-only clearance is
`max(0.001 mm, 1e-6 × box diagonal)`; qualified kernel tolerances are unchanged.
If the nominal eye is already in front of the box by that clearance, the original
scale-preserving switch remains unchanged. Otherwise only the eye distance
increases, retaining orientation, up, target and selected owner. Retreat is the
larger of front-plane clearance and the distance needed to contain the bounds
within the existing frustum with the native Fit convention's 6% framing margin.
No Fit call or target recentering occurs during the switch.

For each end, the regression finds an actual end-face interior point, selects
that face, performs Orthographic end view → Fit → Perspective, and verifies
positive eye-to-geometry depth beyond the near plane for all displayed vertices,
retained camera direction/up/target/selection, a projected end-face boundary
inside the viewport, framebuffer capture, and picking of the same end face.
The existing ordinary-view target-plane pixel-scale and O/P/O checks remain.
Captures: `/tmp/geo-vis-camera-top-fit-perspective.png` and
`/tmp/geo-vis-camera-bottom-fit-perspective.png`. Follow-up test SHA and outcomes
are recorded in its delivery report. Prior human acceptance above predates this
follow-up; Windows and remote-desktop verification remain pending.
