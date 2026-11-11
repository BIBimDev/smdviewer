# STRAKON SMD Viewer V1.0.3

V1.0.3 is a viewer/interaction release. The V1 SMD parser and BREP decoding model are unchanged.

## Added

- Interactive 3D view cube in the lower-left viewport corner.
- True 3 x 3 Rubik-style navigation tiles on all six cube sides.
- Center tiles snap to Top/Bottom/Front/Back/Left/Right face views.
- Edge tiles snap to two-axis oblique views; corner tiles snap to isometric views.
- View-cube drag interaction for orbiting the main camera.
- View-cube orientation synchronized with the main model camera.
- Grouped top toolbar with **Commands**, **View options**, and **Status** areas.
- **Transparent** view option.
- **View cube** visibility toggle.
- Camera-dependent solid-mode vertex occlusion.

## Changed

- Solid mode hides vertices/labels located behind the solid geometry from the current camera direction.
- Transparent mode shows requested back-side vertices through translucent faces.
- Face-related vertex labels remain compact `V#` tags instead of expanding every vertex to XYZ text.
- Full coordinate labels are now reserved for directly hovered/selected vertices.
- Selected-face details use compact vertex chips plus the existing coordinate table.
- Face-select mode prevents vertex labels from intercepting face picking.

## Unchanged parser scope

- DICAD ASC header handling
- `#10125` parsing
- optional 3x4 BREP transform handling
- explicit world vertices
- single-loop `Face.indices[]`
- plane/name handling

Multi-loop faces remain a Viewer V2 capability.

## View cube refinement

- Corrected the navigation cube to use the same Z-up world basis as the main STRAKON viewer.
- Corrected view-cube drag direction to match the main OrbitControls interaction.
- Removed the separate framed cube card and the temporary nine-dot overlay.
- Rebuilt the navigator from 26 surface cubelets so every visible side is a real 3 x 3 clickable grid.
- Added 26 canonical camera directions: 6 face, 12 edge/oblique, and 8 corner/isometric views.
- The cubelet corresponding to the current nearest canonical view is highlighted in orange.
