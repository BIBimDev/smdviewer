# GEO-VIS-DISPLAY-01 — native display styles and context

## Baseline and boundary

On 2026-11-16 the clean worktree `~/Projects/smdviewer-geo-visualiser`
was on `feat/geo-vis-camera-01` at
`3f370b85af5818f1fecc2609c8fc4d3cdc09ab75`. After fetching origin,
`origin/feat/geo-vis-camera-01` matched that exact SHA. Created
`feat/geo-vis-display-01` there. The regular checkout was not used for source
changes. Shared Git metadata was accessed for fetch, branch creation and commit.

Changes are confined to the native viewer, its regression tests and these notes.
OCCT remains the installed visualization-enabled **8.0.1**. Parser, qualified
geometry pipeline, fixtures, browser/WASM code, tag identities and the cube's
presentation/selection implementation are unchanged. No push, merge or PR update
is part of this delivery. Perspective cube-click eye clearance and the deferred
Ctrl-click/source-metadata items remain separate follow-ups.

## Operator behavior

- **Display style** starts at **Shaded**: opaque surfaces with black boundary
  edges. **Transparent** uses 72% surface transparency with boundary edges.
  **Wireframe** uses OCCT topological edges, with plane isoparametric lines
  disabled; no shaded triangles or mesh diagonals are displayed.
- Style is independent of representation. Style changes retain the actual
  selected owner, highlight, pick mode, tree row, details, tag and camera.
  Yellow selection remains visible in all three styles.
- B is concrete grey, A is dark concrete grey and R is pale, muted red.
  **Show context** starts checked. B shows A/R outlines, A shows B/R outlines,
  and R shows B/A outlines. These are faint dashed edges, without surface fill.
  In B/A these faint outlines overlay the foreground so contained reductions
  remain visible. In R, context is drawn before foreground without writing depth,
  so the background B outline cannot cover an opaque R pocket surface.
- Turning context off restores foreground-only presentation. Context changes
  retain selection and camera. Isolate affects only foreground components;
  the contextual group set stays the same. S and Converted faces have no context.
  Empty A/R remain labelled empty and have no active/selectable body, even if B
  context is visible.
- Representation and Isolate changes retain eye, target, direction, up, scale
  and projection. Use **Fit** to frame the current foreground. Successful Open
  still resets/Fits Orthographic S. Style/context preferences persist on Open.
- Three muted +X/+Y/+Z arrows mark actual raw source-local **(0, 0, 0)**.
  They use 36 logical-pixel lengths with zoom persistence and world anchoring.
  Orbit rotates them with the axes; pan moves their anchor with zero. They can
  be occluded by opaque surfaces or leave the view. Specimen zero is a boundary
  corner, so exposed arrows beside a specimen are expected. The lower-left
  interactive cube retains its existing role.

## Presentation and bounds

Foreground and context use independent deep display copies. Context has no
entity IDs, inspector rows, source correspondence or tags. It is displayed with
selection mode `-1`, never loaded or activated with the selector. The marker
builds no sensitive entities. Both have infinite presentation state, excluding
them from ordinary framing bounds. None of these presentations transform the
source geometry or compose placement transforms: coordinates remain raw
source-local millimetres.

Fit, resize framing and Perspective eye clearance explicitly use geometric
bounds of active foreground copies, without triangulation or changes to qualified
kernel tolerances. Fit calls the camera's `FitMinMax` directly: even an explicit
box passed to `V3d_View::FitAll` otherwise adds zoom-persistent marker framing.
The existing 6% margin and OCCT viewer resolution (`10 × Precision::Confusion()`)
are retained. OCCT's automatic ZFit also considers infinite graphical structures;
it is disabled here in favor of explicit depth fitting. The primary depth box
uses foreground geometry. The graphical depth box additionally retains the
existing cube's transformed presentation bounds so the cube remains visible.
Context and origin enter neither depth box. Camera operations update this range;
changing foreground representation can change near/far clipping, while retaining
comparison framing. Style/context changes retain both framing and clipping.
No new eye retreat is added to cube clicks.

## Launch and verification

Reuse the existing Kubuntu Release build:

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

Use absolute sample paths: the dialog tests deliberately assert their initial
directory. `--display` runs focused presentation tests plus the Window/Open
suite; `--render` includes these tests, the existing adapter/preservation suite
and all camera/cube regressions. The existing eight CTest registrations remain
headless. Temporary captures and logs use `/tmp/geo-vis-display-*`.

The new checks cover private context topology and untransformed vertices,
context group routing, empty foreground, unregistered context/marker selection,
all four pick modes' owner retention through styles, actual opacity/modes,
foreground-only Fit/depth/clearance, B/A/R camera retention, context toggles and
isolation, ordinary face picking in each style, an empty wireframe face interior,
and marker framebuffer behavior on zoom/pan/orbit/projection/occlusion/offscreen.
The marker occlusion test uses an independent test box around zero. Marker pixel
comparisons exclude the cube's reserved footprint and its independent hover
presentation. Window tests additionally retain the selected tree row, details,
pick control and saved tag while operating the actual controls. Complete source
evidence snapshots bracket the new render suite as well as the existing tests.

Repository hygiene checks: `npm ci` passed, with zero reported vulnerabilities.
`npm run build` reached Vite but failed on the baseline's missing/stale
`public/wasm` assets, requesting Emscripten 4.0.20 and `npm run build:wasm`.
No WASM rebuild was performed. `tsconfig.tsbuildinfo` was restored byte-for-byte.

For review, use `git diff 3f370b8..HEAD`. The delivery report records the exact
tested commit and final native results. Windows and remote-desktop checks remain
**pending**. Visual inspection evidence is separate from automated assertions;
neither substitutes for human operator acceptance on those platforms.

On Kubuntu/Plasma the agent launched the actual application and inspected its
window capture (`/tmp/geo-vis-display-plasma-plain.png`): control layout, Shaded
default, context checkbox, source origin and orientation cube were visible.
The agent also inspected the combined-column Shaded/Transparent/Wireframe and
B/A/R framebuffer captures for colours, faint outlines and yellow selection.
This is an agent visual check, separate from the automated control/picking
assertions. Human pointer acceptance was not performed; synthetic desktop input
did not establish reliable interaction, so it is not counted as manual evidence.
