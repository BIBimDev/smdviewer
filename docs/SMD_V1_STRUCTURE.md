# SMD structures currently supported by Viewer V1

This is sample-verified reverse-engineering knowledge, not official STRAKON/DICAD documentation.

## File scope

Viewer V1 only requires:

```text
"DICAD ASC"
<header fields currently ignored for rendering>

#10125
<main geometry/object>
```

The header is verified only by checking the `DICAD ASC` marker. The remaining header fields are retained by the source file but are not needed for V1 rendering.

## `#10125` metadata used in V1

For the current samples:

```text
#10125
1
"<object-id>"
"<object-name>"
...
19
```

Known sample meaning:

```text
19 = Teilart `st` / `Stütze`
```

## Profile edge marker

Known profile pattern:

```text
4
"edge[...][0]"
0
x1
y1
x2
y2
...
```

Each currently known straight edge is interpreted as:

```text
"edge[id][index]"
edge-type-or-flag
x-start
y-start
x-end
y-end
```

For the current columns the profile is:

```text
(0,0) -> (750,0)
(750,0) -> (750,650)
(750,650) -> (0,650)
(0,650) -> (0,0)
```

## Placement/origin pattern

Current centered column sample:

```text
-375
-325
-1000
1
0
0
0
1
0
```

Working interpretation:

```text
origin/offset = (-375, -325, -1000)
local X       = (1, 0, 0)
local Y       = (0, 1, 0)
```

## Explicit BREP signature

Viewer V1 searches structurally rather than by fixed line number.

Expected sequence:

```text
N
<3*N numeric vertex coordinates>

M
<M variable-length face records>

M
<4*M plane-equation values>

0                 <- confirmed separator flag in current samples
M
<M face-name records>
```

### Vertex section

Plain column:

```text
8
...
```

Pocket column:

```text
16
...
```

The viewer does not hard-code 8 or 16; it treats the first value as a candidate and validates the complete BREP structure.

## Face record

Current confirmed face format:

```text
1
K
index_0
index_1
...
index_(K-1)
```

Working interpretation:

```text
1 = face flag/type currently preserved
K = polygon vertex count
following K values = indices into the BREP vertex list
```

Examples:

```text
1
4
0
1
2
3
```

is a quadrilateral.

The pocket top uses an eight-vertex polygon:

```text
1
8
0
1
2
3
5
6
7
4
```

## Plane equation section

One plane per face:

```text
A
B
C
D
```

interpreted as:

```text
A*x + B*y + C*z + D = 0
```

The viewer validates that every vertex referenced by a face lies on its corresponding plane.

Examples:

```text
0 0 1 -5000  -> Z = 5000
1 0 0 -75    -> X = 75
0 1 0 125    -> Y = -125
```

## Separator between planes and face names

Both current explicit-BREP samples contain:

```text
0
```

between the last plane record and the face-name count.

Viewer V1 treats this as a confirmed numeric separator/flag but does not assign an undocumented semantic meaning to it.

## Face-name records

Current repeated pattern:

```text
-1
"face-name"
-1
0
```

Examples:

```text
"top"
"bottom"
"sweepface_1_0_1"
"top-1"
```

Names are associated with faces by order.

## Plain-column BREP transform

The plain-column sample contains this immediately before the vertex count:

```text
5
1 0 0 375
0 1 0 325
0 0 1 -1000
1
0
8
```

Viewer V1 recognizes the 12 values as the confirmed 3x4 transform:

```text
[1 0 0  375 ]
[0 1 0  325 ]
[0 0 1 -1000]
```

The local BREP bounds:

```text
X = -750..0
Y = -650..0
Z = 0..6000
```

become the final rendered bounds:

```text
X = -375..375
Y = -325..325
Z = -1000..5000
```

## Pocket-column BREP

The pocket sample has no corresponding confirmed 3x4 BREP transform immediately before its 16-vertex list. Its explicit vertices are already in final element coordinates.

Known result:

```text
vertices = 16
faces    = 10
bounds   = X -375..375, Y -325..325, Z -1000..5000
```

Pocket boundary evidence:

```text
X = 75..375
Y = -125..75
Z = 4500..5000
```

## Rendering rule

Viewer V1 uses the explicit BREP as the authoritative render geometry:

```text
vertices -> polygon faces -> Earcut triangulation -> Three.js mesh
```

Profile and placement information are parsed for diagnostics but are not used to rebuild the BREP when an explicit BREP is present.
