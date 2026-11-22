# GEO-VIS-REFINE-03 — menu, appearance settings, Help and session tags

## Baseline and boundary

Implementation and local testing only, left uncommitted for review.
`origin/fix/geo-vis-refine-02-windows-smoke` was fetched and verified at
`0044402203274063bf93b4fbdf18bb66871df748` on 2026-11-17.
The new branch `feat/geo-vis-refine-03` was created at that exact commit in
`C:/smdviewer-geo-vis-refine-03`, after confirming the branch/path were unused.
The existing Step 2 worktree was clean. No commits, pushes, merges, rebases or PR
operations were performed; main, the existing branches and draft PR #15 were
not modified. Code changes are confined to `geo-visualiser/`.

## Changed files

All paths below are relative to the task worktree.

| Files | Purpose |
| --- | --- |
| `geo-visualiser/window.cpp`, `window.hpp` | Menu, Help/About, preference loading, entity context actions |
| `geo-visualiser/appearance.hpp`, `appearance_settings.cpp`, `appearance_settings.hpp` | Appearance value, settings transaction, sRGB controls and local storage |
| `geo-visualiser/viewport.cpp`, `viewport.hpp` | Preview colours/transparency on existing AIS objects |
| `geo-visualiser/build_info.hpp.in`, `CMakeLists.txt` | Build identification and native test registration |
| `geo-visualiser/tests.cpp`, `display_tests.cpp`, `refine_tests.cpp`, `refine_tests.hpp`, `settings_tests.cpp` | Existing regression adaptation plus focused settings/UI/render tests |
| `docs/GEO-VISUALISER.md`, `GEO-VIS-REFINE-03.md` | Operator guidance and task handoff |

## Operator behavior

The hamburger menu contains Settings and Help. Open SMD, Representation,
Isolate, Pick, Fit, Display style and Show context remain visible, together
with filename/status information and the persistent O/P projection indicator.
The long control hint is now in Help. Help covers S/B/A/R and P, cube tiles
and dragging, orbit/pan/zoom/picking, session tags, raw source-local millimetres,
and the Perspective scale's **at target** camera-target-plane qualification.
About identifies this task build, the Git revision/dirty state and UTC time
captured at CMake configuration, and the compiled OCCT version (8.0.1).
Reconfigure when preparing a new review build so its revision label is current.

Settings provides nine integer sRGB channel controls (0–255), B/A/R swatches,
and a 0–100% **transparency** slider. 0% is opaque; the factory value is 72%.
The factory linear RGB values remain those in `appearance.hpp`, unchanged.
The controls display rounded sRGB values, but opening/accepting a dialog does
not round the underlying palette. Only an edited channel is converted from
sRGB to linear RGB using OCCT's conversion. Defaults and rollback use exact
linear snapshots. Wire accents retain the existing `0.8 * linear + 0.2`
relationship, and filled-face edges remain black.

Preview updates the existing AIS objects. It preserves display style,
representation, isolation, camera, pick mode, selected owner and face highlight.
The transparency preference affects Transparent only. Default values previews
the exact factory palette and 72% without saving. OK saves appearance;
Cancel, Escape and the dialog close button restore the opening appearance.

Preferences use Qt INI-format user-scope storage for organization `BIBimDev`,
application `GEO-Visualiser`, group `appearance/v1`. System fallback is disabled.
Only B/A/R linear RGB triples and transparency are saved; no file paths,
document state, selection or tags. On Windows the normal location is
`%APPDATA%/BIBimDev/GEO-Visualiser.ini`; Qt uses its platform-specific user
configuration directory on Linux. Missing or invalid values use factory values.
Failure to save is reported and leaves Settings open.

Right-click an **entity row** to Add tag (untagged), Edit tag or Remove tag
(tagged). The captured row identity is used, independently of the prior
selection. Grouping rows and blank space offer no tag actions. Cancelled edits
leave the value unchanged. The User tag column is retained. Tags still use the
existing viewer identity, canonical-file-path/exact-byte document scope and
window/run lifetime; they never change SMD metadata or source evidence.

## Verification

Windows x64, MSVC 19.44, installed OCCT **8.0.1** and Qt **6.8.3** were reused.
No dependencies were rebuilt. Tests use temporary settings storage and do not
read or modify the operator's appearance preferences.

The `inspector-settings` CTest uses Qt's offscreen platform, without creating
an OCCT viewport. It checks sRGB conversion, exact defaults and untouched
channels, preview without saving, Cancel/Escape/close, saving/reloading in a
fresh child process, and malformed preferences. Only the four expected
appearance keys may be stored.

Desktop checks exercise the actual menu actions, tag context menu, dialogs,
Help text and shortcut handling. They cover an invoked entity different from
the selected entity, non-entity rows/blank space, cancelled editing, exact
settings rollback and retained viewer state in all three styles. Actual AIS
checks verify surface/wire colours, fixed black face edges, transparency,
selection owner/highlight and clipping. Framebuffers check visible colour
changes and restoration to the appearance before the first edit.

Existing camera/cube, picking, highlighting, context, source-preservation,
failed-open and tag identity/lifetime regressions remain. The baseline's
normalized failed-open path and native-drawable/framebuffer assertions are
preserved. The old always-visible tag editor tests now drive context actions.

Final Windows results, verified on 2026-11-18 (IST):

| Command/check | Outcome |
| --- | --- |
| CMake configure with the installed OCCT/Qt paths below | Passed; optional Vulkan headers unavailable, non-blocking |
| `cmake --build build/visualiser-windows --config Release --parallel 6` | Passed; final build log has no compiler warnings/errors |
| `ctest --test-dir build/visualiser-windows -C Release --output-on-failure` | Passed **9/9**, including isolated `inspector-settings` |
| `smd-inspector-tests <absolute samples path> --open` | Passed, exit 0 |
| First fresh `--render`, cwd `C:/` | Passed, exit 0, both specimens and all window checks |
| Second fresh `--render`, cwd `C:/` | Passed, exit 0, both specimens and all window checks |
| `git diff --check` | Passed |
| Protected Step 2 worktree | Clean; baseline ref/branch still at `0044402203274063bf93b4fbdf18bb66871df748` |
| Settings/Help screenshots and appearance framebuffer review | Reviewed at local display scale 175% |
| Kubuntu | **Pending / not tested** |
| Remote Desktop | **Pending / not tested** |

Development compile errors in a new string literal and a missing test include
were corrected. Review also caught OCCT `SetColor` recolouring face boundaries;
preview now explicitly retains black filled-face edges, covered by actual AIS
and before-first-edit framebuffer checks. The table records the final build.

Browser/WASM tests were not run for this native-only task. No parser, fixture
or geometry source changes are included. Automated local desktop success is
not Kubuntu or Remote Desktop acceptance.

## Reproduce on this Windows machine

From `C:/smdviewer-geo-vis-refine-03` in PowerShell:

```powershell
$taskCmake = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$taskCtest = Join-Path (Split-Path $taskCmake) ctest.exe
$taskVisual = 'C:/smdviewer/build/geo-visualiser-deps/occt-install'
$taskQt = 'C:/smdviewer/build/geo-visualiser-deps/Qt/6.8.3/msvc2022_64'
& $taskCmake -S . -B build/visualiser-windows -G 'Visual Studio 17 2022' -A x64 `
  -DPython3_EXECUTABLE=C:/Python314/python.exe `
  -DSMD_BUILD_GEOMETRY=ON -DSMD_BUILD_GEO_VISUALISER=ON `
  "-DOpenCASCADE_DIR=$taskVisual/cmake" "-DCMAKE_PREFIX_PATH=$taskQt"
& $taskCmake --build build/visualiser-windows --config Release --parallel 6
$env:PATH = "$taskVisual/win64/vc14/bin;$taskQt/bin;$env:PATH"
$env:QT_QPA_PLATFORM_PLUGIN_PATH = "$taskQt/plugins/platforms"
& $taskCtest --test-dir build/visualiser-windows -C Release --output-on-failure
$taskTests = 'C:/smdviewer-geo-vis-refine-03/build/visualiser-windows/geo-visualiser/Release/smd-inspector-tests.exe'
$taskSamples = 'C:/smdviewer-geo-vis-refine-03/public/samples'
& $taskTests $taskSamples --open
# Each invocation is a fresh process; also run from C:/ to verify cwd independence.
& $taskTests $taskSamples --render
& $taskTests $taskSamples --render
git diff --check
```

Stop on a nonzero command exit code. Build/test logs remain local under ignored
`build/step3-*.log` and `build/visualiser-windows/Testing/Temporary/LastTest.log`.
Desktop render captures and Settings/Help previews are in the OS temporary
directory, not repository artifacts.

## Manual review checklist

1. Launch `build/visualiser-windows/geo-visualiser/Release/start-geo-visualiser.cmd`.
   Check visible controls, menu, load status and O/P indicator at normal display scaling.
2. In B/A/R and each style, preview colours/transparency with a selected face.
   Confirm camera, selection, isolation and style stay put. Try Default values,
   Cancel, Escape and the close button; then OK and restart to check persistence.
3. Right-click a different entity from the selected row. Add/edit/remove its
   tag; cancel an edit. Check grouping rows/blank space and tag expiry on restart.
4. Read Help/About. Exercise S/B/A/R/P in the viewer and type those letters in
   tag/file dialogs. Check the Perspective **at target** scale explanation.
5. Perform Kubuntu and Remote Desktop checks separately and record evidence.

Team review is required for UI usability, the local preference location and
pending platform checks. No later development milestone is included.
