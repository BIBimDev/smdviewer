# Geometry Inspector - V1.0.3

## View cube

The lower-left view cube mirrors the main camera orientation.

- Click a center tile for an orthogonal face view.
- Click an edge tile for a two-axis oblique view.
- Click a corner tile for an isometric view.
- Drag the Rubik-style cube to orbit the model.
- The normal Three.js orbit controls remain available directly in the viewport.
- The **View cube** option hides/shows the widget.

## Solid and transparent modes

### Solid

Vertex markers and labels are filtered by camera visibility. A vertex is hidden when a model face lies between the camera and that vertex.

This prevents back-side vertices from visually bleeding through an opaque solid.

### Transparent

Faces become translucent and vertex occlusion filtering is disabled. Requested vertices can therefore be inspected through the object.

## Vertex labels

- Normal visible vertex: compact `V#`.
- Direct vertex hover/selection: full `V# (x, y, z)`.
- Face hover/selection: contributing vertices stay compact and use face-related colors.

This keeps selected faces readable even when they reference many vertices.

## Selected-face details

The inspector contains:

- selected face ID and name;
- plane summary;
- compact vertex chips;
- full X/Y/Z coordinate table.

Hovering a vertex chip temporarily links back to the corresponding vertex/table row.

## Toolbar groups

### Commands

- Open `.smd`
- Fit model

### View options

- Show vertices
- Select faces
- Face colors
- Transparent
- View cube

### Status

Displays load status plus Solid/Transparent, Vertices on/off, and Face select on/off.


### Refined cube control

- Z-up camera basis matches the main viewer.
- Real 3 x 3 Rubik-style tiles on every cube side.
- Center / edge / corner tiles map to face / oblique / isometric views.
- No separate dot overlay or surrounding card.
