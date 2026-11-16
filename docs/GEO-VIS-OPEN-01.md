# GEO-VIS-OPEN-01 — Open SMD and reset the inspector

## Baseline and delivery

On 2026-11-16, fetched `refs/pull/15/head` and checked the remote
`feat/geo-visualiser`: both matched the reviewed baseline
`f23940da6c25d63df334cc7fb64036d0eb163ccd`. The clean Kubuntu test worktree
started detached at that commit. Work is on `feat/geo-vis-open-01`.
PR #15, its branch, `main`, and the regular checkout's working files are untouched.
The linked worktree necessarily shares Git object/ref storage with that checkout.

Commit `01d2d0e` is the separate test-only fix qualifying `inspector::Display`
in `first`, `navigation` and `render` to resolve the X11 type-name collision.
The subsequent feature commit contains only native viewer code/tests and notes.

## Operator handoff

Reuse the installed visualization-enabled OCCT **8.0.1** and existing build:

```sh
cd ~/Projects/smdviewer-geo-visualiser
export LD_LIBRARY_PATH="$HOME/.local/opt/occt-8.0.1-visual/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
cmake --build build/visualiser-kubuntu --parallel 6
ctest --test-dir build/visualiser-kubuntu --output-on-failure

# Deliberately launch from outside the checkout.
cd /tmp
QT_QPA_PLATFORM=xcb \
  "$HOME/Projects/smdviewer-geo-visualiser/build/visualiser-kubuntu/geo-visualiser/smd-inspector-tests" \
  "$HOME/Projects/smdviewer-geo-visualiser/public/samples" --render
QT_QPA_PLATFORM=xcb \
  "$HOME/Projects/smdviewer-geo-visualiser/build/visualiser-kubuntu/geo-visualiser/geo-visualiser"
```

The existing CMake cache uses Release, GCC 15.2.0, Qt 6.10.2 and
`OpenCASCADE_DIR=$HOME/.local/opt/occt-8.0.1-visual/lib/cmake/opencascade`.
No OCCT rebuild or configuration change is required. Running CTest without the
library-path export fails to resolve indirect OCCT shared libraries on this host.

The initial file is `plain_column.smd`, showing validated S. **Open SMD…**
opens one existing file and shows the successful filename beside the button.
Every dialog starts in this checkout's `public/samples`, regardless of launch
working directory or the last opened file. Paths with spaces and Unicode work.
Open is synchronous; processing a large file can temporarily block interaction.

Successful opens always reread and recompute, including the same pathname.
Success selects S / All / Face, resets and fits the camera, removes previous AIS
objects/selection owners, and clears tree selection, highlight, entity details
and unsaved tag-editor text. A/R are rebuilt from the new document; plain A/R
are empty. No cache of the old specimen's geometry remains.

Cancel preserves the active state. Failed reads, parses, qualified geometry
stages, or initial display preparation retain the active document, camera,
controls, selection and tag editor. The warning identifies the attempted path
and stage. Valid S with unsupported B fails the whole load. A missing initial
plain fixture instead leaves the inspector unavailable with a visible diagnostic;
Open can still load a supported file. Graphics-device failures retain the
existing draft's separate fail-closed viewport handling.

Saved tags live only in the current window's memory. The document namespace is
SHA-256 over canonical absolute path, a NUL separator, and exactly the bytes
passed to the parser. Same path + same bytes may recover tags; a different
canonical path or different bytes cannot inherit them. Symlink aliases resolving
to the same canonical path share tags; hard-link paths remain separate. Restoring
exact previous contents can recover that content version's in-memory tags.
Topology suffixes distinguish representation/component/kind/entity. This is a
viewer policy, not durable source identity or a persistence contract. Tags are
not written to files, settings or the source document.

## Verification and review

The focused adapter checks cover fresh native pipeline objects on same-file
reopen, path/content tag isolation, Unicode/space paths, read/parse failures,
source preservation, and synthetic valid S with unsupported triangular B.
The synthetic prism is test-generated; original fixtures are unchanged.

The explicit `--render` test uses the logged-in Kubuntu X11/XWayland desktop via
`QT_QPA_PLATFORM=xcb`, with `/tmp` as cwd. It exercises actual AIS rendering,
hit testing and Qt Window callbacks, then drives Qt's widget file-open dialog
through the Open button. Each invocation checks the starting samples directory,
including after opening outside it. It verifies combined R face selection,
changed camera, cancel/failure retention, combined-to-plain reset, empty plain
A/R, removal of old pick owners, same-file reset, saved-tag retention, same-name
and changed-content tag isolation. A child process starts a fresh inspector on
a file tagged by the parent and verifies no tags survived restart. Tests choose
Qt's widget dialog for deterministic automation; OS-native dialog integration
is a separate operator check.

The focused dialog suite can also be run with `--open` instead of `--render`;
`--render` includes it along with all existing adapter/rendering checks.

| Check on Kubuntu | Outcome |
| --- | --- |
| Existing native build with installed OCCT | Passed |
| CTest (including focused adapter regressions) | Passed 8/8 |
| Focused `xcb` dialog/reset/tag suite from `/tmp` | Passed, including new-process tag expiry |
| Windows follow-up | Pending |

The complete final `--render` result and exact tested feature SHA are recorded
in the delivery report.
Windows checks for this follow-up are **pending, not executed**. Prior draft
Windows results do not validate this change. User-operated desktop/remote-desktop
acceptance remains separate from automated desktop evidence.

Repository hygiene: `npm ci` passed (zero audit vulnerabilities). `npm run build`
was attempted and failed because `public/wasm` assets are missing/stale in this
worktree; the build requests Emscripten 4.0.20 and `npm run build:wasm`.
No WASM rebuild was performed for this viewer-only task. The regenerated tracked
`tsconfig.tsbuildinfo` was restored to its original bytes and is excluded.

Team review: file/content identity policy, reset/failure UI, and pending Windows
and operator checks. Parser semantics, qualified geometry algorithms/tolerances,
coordinates, fixtures, browser/WASM sources, Ctrl-click behaviour and deferred
source-metadata review items are outside this diff. No PR merge or advancement
is included.
