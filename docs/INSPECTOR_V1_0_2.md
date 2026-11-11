# Geometry Inspector - V1.0.2

## Vertices

- **Show vertices OFF**: hover a vertex-table row to temporarily show/highlight that single node and its full world-coordinate label.
- **Show vertices ON**: all vertices appear as compact screen-sized markers. Click a vertex row, marker, or label to select it. The selected vertex is emphasized; others are muted.
- Click empty 3D space to clear vertex selection.
- Hovering either a marker or its label highlights and scrolls the corresponding table row.

## Faces

- Face-table hover is always available. It highlights the face and temporarily shows all vertices contributing to that face with full coordinates.
- Persistent face selection is available only when **Face select** is ON.
- Turning **Face select** OFF clears all persistent face-selection effects.
- Selecting a face highlights its table row, its contributing vertex rows, the corresponding 3D nodes, and the selected-face detail panel.

## Scrolling

The inspector scrolls only the relevant table container. It no longer uses global `scrollIntoView()` behaviour that can move the entire sidebar unexpectedly.

## Compatibility

This is still the V1 single-loop face model (`Face.indices[]`). V2 remains separate and is not modified by this release.
