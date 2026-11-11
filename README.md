# STRAKON SMD Viewer V1.0.3

Experimental browser-based 3D viewer and geometry inspector for the currently reverse-engineered STRAKON SMD V1 subset.

## V1.0.3 highlights

### Interactive view cube

A STRAKON-style view cube is shown in the lower-left corner of the 3D viewport.

- The control is a true **3 x 3 Rubik-style cube** on every side, matching the STRAKON navigation concept.
- Click a **center tile** for Top / Bottom / Front / Back / Left / Right orthographic views.
- Click an **edge tile** for a two-axis oblique view.
- Click a **corner tile** for a three-axis isometric view.
- Together the surface tiles cover the 26 canonical camera directions around the model.
- Drag the cube to orbit the main model using the same directional sense as the main 3D view.
- The cube uses the same **Z-up** world basis as the rendered STRAKON element.
- The active view tile is highlighted in orange; no separate dot overlay or surrounding card is used.
- Use **View cube** under View options to show/hide it.

### Grouped toolbar

The top toolbar is divided into:

- **Commands** - Open `.smd`, Open native JSON, Fit model.
- **View options** - Show vertices, Select faces, Face colors, Transparent, View cube.
- **Status** - current solid/transparent, vertex, and face-selection modes plus load status.

### Solid / transparent vertex visibility

V1.0.3 distinguishes visible and occluded vertices.

- **Solid mode** hides back-side vertices and labels that are occluded by the solid geometry from the current camera direction.
- **Transparent mode** makes the faces translucent and allows all requested vertices to be inspected through the model.
- The visibility updates when the camera changes.

### Cleaner selected-face inspection

Face selection no longer expands XYZ text for every contributing vertex over the 3D model.

- Face-related vertices use compact `V#` tags in 3D.
- Full `V# (x, y, z)` text is reserved for a directly hovered/selected vertex.
- Selected-face coordinates are organized in the inspector using compact vertex chips plus a coordinate table.

## Preserved V1.0.2 behaviour

- **Show vertices OFF**: vertex-table hover temporarily requests the matching vertex; in solid mode it is shown only if visible from the camera.
- **Show vertices ON**: visible vertices use screen-sized markers and compact `V#` labels.
- Click a vertex row, marker, or label to select/toggle it while vertices are ON.
- Click empty 3D space to clear persistent vertex selection.
- Face-table hover works in every mode.
- Persistent face selection exists only while **Select faces** is ON.
- Turning **Select faces** OFF clears persistent face state.
- **Face colors** keeps deterministic face coloring.
- Inspector auto-scroll stays within the appropriate table.

## Preserved V1 parser scope

This release intentionally keeps the existing V1 parser model unchanged:

- `"DICAD ASC"` files
- one main `#10125` geometry block
- explicit BREP vertices
- single-loop polygon faces (`Face.indices[]`)
- plane equations
- face names
- optional confirmed 3x4 BREP transform

V2 multi-loop/hole parsing is intentionally not included.

## Native JSON v1 diagnostic input (GEO-00-INT01)

**Open native JSON** accepts the native `smd-extract` envelope
`{"schemaVersion":1,"parsed":<ParsedSmd>}`. The status identifies the active
source as TypeScript (SMD) or Native C++ (JSON v1). Both use the existing renderer
and inspectors. JSON is validated before loading; recorded coordinates and
transforms are preserved without applying any additional transform.

This is a diagnostic bridge, not a GEO-01 persistence contract. See
[GEO-00-INT01 verification and manual steps](docs/GEO-00-INT01.md) for generation,
automated tests, supported structure and browser verification limits.

## Prerequisites

Install Node.js 22 LTS or newer. npm is included.

```powershell
node --version
npm --version
```

## Run

```powershell
npm install
npm run dev
```

Or double-click:

```text
start-viewer.cmd
```

Open the local address printed by Vite, normally `http://localhost:5173/`.

## Build

```powershell
npm run build
```

See `docs/CHANGELOG_V1_0_3.md` and `docs/INSPECTOR_V1_0_3.md` for the new interaction details.
