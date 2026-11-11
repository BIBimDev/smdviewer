# V1.0.1 changelog

Viewer V1.0.1 keeps the original V1 SMD parser and adds a geometry-inspection layer.

## Added

- Show/hide vertex nodes and `Vn(x,y,z)` world-coordinate labels.
- Vertices accordion/table.
- Two-way vertex hover synchronization between the table and 3D view.
- Faces accordion/table.
- Two-way face hover synchronization between the table and 3D view.
- 3D face-selection mode using raycasting.
- Face selection from the face table.
- Selected face detail panel with name, plane and vertex coordinates.
- Automatic highlighting of the selected face's vertices in the 3D view and vertex table.
- Deterministic multi-color face mode.
- Selection and hover styles override face palette colors.

## Deliberately unchanged

- V1 face representation remains `Face.indices[]`.
- V2 multi-loop/hole support is not included.
- Reinforcement, BIP and special meshes remain outside V1 scope.
