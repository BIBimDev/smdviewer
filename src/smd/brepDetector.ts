import { applyTransform } from "./transform";
import type { Face, ParsedBrep, Plane, Token, Transform3x4, Vec3 } from "./types";

function asNumber(token: Token | undefined): number | undefined {
  return token?.type === "number" ? token.value : undefined;
}

function isInteger(value: number): boolean {
  return Number.isFinite(value) && Number.isInteger(value);
}

function detectTransformBeforeVertexCount(
  tokens: Token[],
  vertexCountTokenIndex: number,
): Transform3x4 | undefined {
  // Confirmed in the plain-column sample:
  // 5
  // [12 numeric values forming a 3x4 transform]
  // 1
  // 0
  // <vertex-count>
  const markerIndex = vertexCountTokenIndex - 15;
  if (markerIndex < 0) {
    return undefined;
  }

  const marker = asNumber(tokens[markerIndex]);
  if (marker !== 5) {
    return undefined;
  }

  const values: number[] = [];
  for (let i = markerIndex + 1; i < markerIndex + 13; i += 1) {
    const value = asNumber(tokens[i]);
    if (value === undefined) {
      return undefined;
    }
    values.push(value);
  }

  const trailingOne = asNumber(tokens[markerIndex + 13]);
  const trailingZero = asNumber(tokens[markerIndex + 14]);
  if (trailingOne !== 1 || trailingZero !== 0) {
    return undefined;
  }

  return {
    values: values as Transform3x4["values"],
  };
}

function parseCandidate(tokens: Token[], start: number): ParsedBrep | undefined {
  const n = asNumber(tokens[start]);
  if (n === undefined || !isInteger(n) || n < 4 || n > 100000) {
    return undefined;
  }

  let cursor = start + 1;
  const vertices: Vec3[] = [];

  for (let i = 0; i < n; i += 1) {
    const x = asNumber(tokens[cursor]);
    const y = asNumber(tokens[cursor + 1]);
    const z = asNumber(tokens[cursor + 2]);
    if (x === undefined || y === undefined || z === undefined) {
      return undefined;
    }
    vertices.push({ x, y, z });
    cursor += 3;
  }

  const faceCount = asNumber(tokens[cursor]);
  if (
    faceCount === undefined ||
    !isInteger(faceCount) ||
    faceCount < 4 ||
    faceCount > 100000
  ) {
    return undefined;
  }
  cursor += 1;

  const faces: Face[] = [];
  for (let faceIndex = 0; faceIndex < faceCount; faceIndex += 1) {
    const flag = asNumber(tokens[cursor]);
    const k = asNumber(tokens[cursor + 1]);
    if (
      flag === undefined ||
      k === undefined ||
      !isInteger(k) ||
      k < 3 ||
      k > n
    ) {
      return undefined;
    }
    cursor += 2;

    const indices: number[] = [];
    for (let j = 0; j < k; j += 1) {
      const indexValue = asNumber(tokens[cursor]);
      if (
        indexValue === undefined ||
        !isInteger(indexValue) ||
        indexValue < 0 ||
        indexValue >= n
      ) {
        return undefined;
      }
      indices.push(indexValue);
      cursor += 1;
    }

    faces.push({ flag, indices });
  }

  const planeCount = asNumber(tokens[cursor]);
  if (planeCount === undefined || !isInteger(planeCount) || planeCount !== faceCount) {
    return undefined;
  }
  cursor += 1;

  const planes: Plane[] = [];
  for (let i = 0; i < planeCount; i += 1) {
    const a = asNumber(tokens[cursor]);
    const b = asNumber(tokens[cursor + 1]);
    const c = asNumber(tokens[cursor + 2]);
    const d = asNumber(tokens[cursor + 3]);
    if (a === undefined || b === undefined || c === undefined || d === undefined) {
      return undefined;
    }
    planes.push({ a, b, c, d });
    cursor += 4;
  }

  // In both confirmed samples there is one numeric separator flag (0)
  // between the plane list and the face-name count. Accept it when the
  // following token equals the face count.
  const possibleSeparator = asNumber(tokens[cursor]);
  const possibleNameCount = asNumber(tokens[cursor + 1]);
  if (possibleSeparator === 0 && possibleNameCount === faceCount) {
    cursor += 1;
  }

  const nameCount = asNumber(tokens[cursor]);
  if (nameCount === undefined || !isInteger(nameCount) || nameCount !== faceCount) {
    return undefined;
  }
  cursor += 1;

  const faceNames: string[] = [];
  for (let i = 0; i < nameCount; i += 1) {
    const prefix = asNumber(tokens[cursor]);
    const nameToken = tokens[cursor + 1];
    const suffixA = asNumber(tokens[cursor + 2]);
    const suffixB = asNumber(tokens[cursor + 3]);

    if (
      prefix === undefined ||
      nameToken?.type !== "string" ||
      suffixA === undefined ||
      suffixB === undefined
    ) {
      return undefined;
    }

    faceNames.push(nameToken.value);
    cursor += 4;
  }

  faces.forEach((face, i) => {
    face.plane = planes[i];
    face.name = faceNames[i];
  });

  // Validate that each local face lies on its corresponding local plane.
  // The current confirmed SMD samples use exact or near-exact values.
  const tolerance = 1e-5;
  for (let i = 0; i < faces.length; i += 1) {
    const face = faces[i];
    const plane = planes[i];
    for (const vertexIndex of face.indices) {
      const v = vertices[vertexIndex];
      const error = plane.a * v.x + plane.b * v.y + plane.c * v.z + plane.d;
      if (Math.abs(error) > tolerance) {
        return undefined;
      }
    }
  }

  const transform = detectTransformBeforeVertexCount(tokens, start);
  const verticesWorld = transform
    ? vertices.map((vertex) => applyTransform(vertex, transform))
    : vertices.map((vertex) => ({ ...vertex }));

  return {
    verticesLocal: vertices,
    verticesWorld,
    faces,
    planes,
    faceNames,
    transform,
    vertexCountTokenIndex: start,
  };
}

export function detectBrep(tokens: Token[]): ParsedBrep {
  for (let i = 0; i < tokens.length; i += 1) {
    const candidate = parseCandidate(tokens, i);
    if (candidate) {
      return candidate;
    }
  }

  throw new Error(
    "No supported explicit BREP was found in #10125. Viewer V1 currently expects vertices + faces + planes + face names.",
  );
}
