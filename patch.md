# SMD Viewer — BI301 Subject-Selection Patch

**Date:** 2026-10-01  
**Status:** Exploratory / specimen-driven  
**Affected specimen:** `public/samples/26026-BI301.01.smd`

## Purpose

The original Viewer V1 implementation was intentionally restricted to `#10125` objects.  
That assumption worked for the initial plain-column and pocket-column specimens, but it is
too narrow for `26026-BI301.01.smd`.

The BI301 SMD contains several STRAKON object blocks. The intended subject of the drawing
is the parallel girder:

```text
#10071
...
"BI1 Parallelbinder (8)"
"BI301"
...
```

This object contains a valid explicit BREP with:

```text
52 vertices
30 faces
30 planes
30 face-name records
```

The previous parser skipped this object and selected a later `#10125` object instead.
The renderer therefore displayed a geometrically complete object, but not the intended
subject of the SMD/KON drawing.

After the patch, the viewer selects the `#10071` BI301 object and renders the complete
parallel girder.

## Behaviour before the patch

The parser searched directly for the first `#10125` line:

```ts
const blockStart = lines.findIndex((line) => line.trim() === "#10125");
```

Everything before that position was effectively treated as header material.

The selected geometry was therefore constrained by the rule:

```text
SMD
  -> first #10125
  -> first structurally valid explicit BREP in that block
  -> render
```

For the initial laboratory samples this happened to be correct.

For `26026-BI301.01.smd`, however, the actual drawing subject occurs earlier in a
`#10071` block. The old parser never considered it.

### Consequence

The old viewer rendered:

```text
Block:    #10125
Name:     EWS Erddruckwand ...
Vertices: 16
Faces:    10
```

This body was not incomplete. It was simply the wrong object for the purpose of showing
the intended subject of drawing `BI301`.

## Behaviour after the patch

The parser now performs an experimental document-subject selection.

### 1. Determine the SMD header

The first top-level STRAKON object marker is detected generically:

```text
#<number>
```

rather than assuming that the first relevant record must be `#10125`.

Everything before the first object record remains the SMD header.

### 2. Derive a candidate drawing designation

The header is tokenized and the source `.kon` path is located.

For the BI301 specimen, the nearest preceding non-empty string is:

```text
BI301
```

This value is treated as the **candidate document/drawing designation**.

This is intentionally a specimen-derived heuristic; it is not yet claimed to be a
formally documented STRAKON field.

### 3. Split the SMD into top-level object blocks

Instead of keeping one tail beginning at `#10125`, the parser enumerates all blocks of
the form:

```text
#10071
...
#10125
...
#10159
...
```

Each block can therefore be examined independently.

### 4. Select an object whose designation matches the drawing

For every object block the patch inspects the early metadata layout observed in BI301:

```text
#tag
<numeric record/version>
"<object-id>"
"<name>"
"<designation>"
...
```

A block is selected when:

```text
object designation == document designation
```

**and** the block contains a structurally valid explicit BREP.

For BI301 this selects:

```text
#10071
Object ID:   1KtR1mlXr6AwkVI4_A4xTi
Name:        BI1 Parallelbinder (8)
Designation: BI301
Vertices:    52
Faces:       30
```

This is the object rendered in the successful post-patch screenshot.

### 5. Preserve the original `#10125` behaviour as fallback

If no matching document-subject object can be established, the parser falls back to the
first `#10125` block.

This preserves compatibility with the initial plain-column and pocket-column samples.

## Files changed

### `src/smd/parser10125.ts`

Main functional change.

Added:

- `extractDocumentDesignation()`
- `splitIntoBlocks()`
- `blockDesignation()`
- `containsSupportedBrep()`

Changed parsing from:

```text
find first #10125
```

to:

```text
find first object block
 -> inspect all object blocks
 -> prefer designation match with valid BREP
 -> otherwise fall back to #10125
```

Metadata parsing was also generalized so that the selected block tag is no longer
hard-coded to `#10125`.

The deeper metadata offsets for `partType`, placement origin and local axes are retained
only for `#10125`, because those offsets are not yet verified for `#10071`.

This is why the BI301 display deliberately shows:

```text
Teilart: -
```

rather than interpreting an unverified field.

### `src/smd/types.ts`

Added:

```ts
designation?: string;
geometryBlockTag: string;
documentDesignation?: string;
```

These fields allow the parser and UI to report which STRAKON block was actually selected
and why.

### `src/smd/brepDetector.ts`

Only the diagnostic message was generalized.

Before:

```text
No supported explicit BREP was found in #10125 ...
```

After:

```text
No supported explicit BREP was found in the selected object block ...
```

No BREP detection logic was changed.

### `src/main.ts`

The UI no longer claims that Viewer V1 is exclusively a `#10125` viewer.

It now displays the actual selected block and designation, for example:

```text
Block        #10071
Name         BI1 Parallelbinder (8)
Designation  BI301
```

The V1-scope note was also changed to describe the experimental subject-selection rule
and the retained `#10125` fallback.

## What did *not* change

The following parts were deliberately left untouched:

- explicit BREP decoding;
- vertex parsing;
- face-loop parsing;
- plane validation;
- triangulation;
- Three.js rendering;
- camera fitting.

The successful BI301 result therefore does **not** stem from altered geometry or rendering
logic. The previous renderer was already capable of displaying the girder correctly once
the correct BREP object was supplied.

## Why this patch is useful

The patch separates two questions that had previously been conflated:

1. **How is an explicit STRAKON solid represented?**
2. **Which object in the SMD represents the intended subject of the drawing?**

The BREP representation appears to be usable in more than one STRAKON record class.
The BI301 specimen proves that the desired geometry can occur in `#10071`, not only
`#10125`.

Therefore:

```text
#10125 != necessarily the drawing subject
```

and:

```text
record class != geometry representation
```

The current patch begins to select the drawing subject semantically instead of treating
one record number as globally authoritative.

## Current limitations

This remains exploratory code.

The following points are **not yet established as general STRAKON rules**:

- that the nearest non-empty header string before the `.kon` path is always the drawing
  designation;
- that token offset `+4` is always the designation field for every relevant object class;
- that a designation match alone identifies the root object in every production SMD;
- that `#10071` has one fixed semantic meaning across all files.

The patch therefore retains the old `#10125` fallback and avoids decoding unverified
`#10071` metadata offsets.

Further production specimens should be used to test and refine the subject-selection
rule before it is promoted from heuristic to established parser behaviour.

## Verified result for BI301

Post-patch viewer output:

```text
File:        26026-BI301.01.smd
Block:       #10071
Name:        BI1 Parallelbinder (8)
Designation: BI301
Vertices:    52
Faces:       30
Planes:      30
Face names:  30
```

The complete intended parallel girder is rendered.

## Engineering conclusion

The BI301 issue was not a rendering defect and not an incomplete BREP.

It was an **object-selection error caused by an overly restrictive `#10125` assumption**.

The patch corrects that assumption for the current specimen while preserving the
previous behaviour as a fallback and keeping unverified format semantics explicit.
