# STRAKON SMD Viewer V1.0.2

This release refines the V1.0.1 geometry-inspection workflow while keeping the original V1 #10125 parser unchanged.

## Behaviour changes

- Turning **Face select** OFF now clears the selected face, selected-face vertex highlighting, and selected-face details.
- Face-table clicks create persistent selection only when **Face select** is ON.
- Face-table hover works in every mode and temporarily shows the face's contributing vertices and full XYZ labels.
- Vertex-table hover works even when **Show vertices** is OFF; only the hovered node/address appears in 3D.
- Persistent vertex selection is available only while **Show vertices** is ON.
- Clicking a vertex row, 3D vertex marker, or 3D vertex label selects/toggles that vertex when vertices are ON.
- Clicking empty 3D space clears persistent vertex selection.
- Hovering a 3D vertex label now synchronizes the vertex table just like hovering the dot.
- Table auto-scroll is constrained to the correct vertex/face table instead of scrolling the entire sidebar.

## Dense vertex visualization

V1.0.1 used 3D spheres and full XYZ labels for every visible vertex, which could become crowded.

V1.0.2 uses small screen-sized point markers. When **Show vertices** is ON:

- all vertices remain visible as small gray points with compact `V#` tags;
- hover/selection expands only relevant labels to `V# (x, y, z)`;
- a selected vertex is emphasized while other vertices are muted;
- face hover/selection expands the contributing face vertices to full addresses.

This keeps the model readable while retaining full coordinate access on demand.
