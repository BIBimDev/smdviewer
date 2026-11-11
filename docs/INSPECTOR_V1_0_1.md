# Geometry Inspector - V1.0.1

This branch adds interactive inspection without changing the original Viewer V1 SMD parser.

## Interaction model

### Vertices

The viewer uses `brep.verticesWorld`, so displayed labels show the final transformed coordinates rather than raw stored BREP coordinates.

Vertex labels follow the format:

```text
V0 (-375, -325, 5000)
```

The Vertices table and 3D dots share the same vertex index.

### Faces

Viewer V1 creates one Three.js mesh per STRAKON face. Each face mesh stores:

```text
mesh.userData.faceIndex
```

This allows Three.js raycasting to map a 3D hover/click directly to `F0`, `F1`, etc.

Selected faces highlight all vertex indices in `face.indices`.

### Face colors

The multi-color display is deterministic by face index. Appearance priority is:

```text
selected face
> hovered face
> face palette
> default grey
```

## V1/V2 separation

V1.0.1 still uses:

```text
Face -> indices[]
```

It does not contain V2's multi-loop face representation. This was deliberate so inspection features can be tested in V1 first and ported to V2 later.
