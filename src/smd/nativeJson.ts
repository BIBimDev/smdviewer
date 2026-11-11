import type { ParsedSmd } from "./types";

function requireValue(condition: boolean, path: string, expected: string): asserts condition {
  if (!condition) {
    throw new Error(`Native JSON: ${path} must be ${expected}.`);
  }
}

function record(value: unknown, path: string): asserts value is Record<string, unknown> {
  requireValue(typeof value === "object" && value !== null && !Array.isArray(value), path, "an object");
}

function array(value: unknown, path: string): asserts value is unknown[] {
  requireValue(Array.isArray(value), path, "an array");
}

function string(value: unknown, path: string): void {
  requireValue(typeof value === "string", path, "a string");
}

function finite(value: unknown, path: string): asserts value is number {
  requireValue(typeof value === "number" && Number.isFinite(value), path, "a finite number");
}

function stringArray(value: unknown, path: string): void {
  array(value, path);
  value.forEach((entry, i) => string(entry, `${path}[${i}]`));
}

function vector(value: unknown, path: string, keys: readonly string[]): void {
  record(value, path);
  keys.forEach((key) => finite(value[key], `${path}.${key}`));
}

function validateParsed(value: unknown): asserts value is ParsedSmd {
  const path = "parsed";
  record(value, path);
  requireValue(typeof value.isDicadAsc === "boolean", `${path}.isDicadAsc`, "a boolean");
  stringArray(value.headerLines, `${path}.headerLines`);
  stringArray(value.geometryBlockLines, `${path}.geometryBlockLines`);

  const g = value.geometry;
  const gp = `${path}.geometry`;
  record(g, gp);
  for (const key of ["objectId", "name"]) {
    if (key in g) string(g[key], `${gp}.${key}`);
  }
  if ("partType" in g) finite(g.partType, `${gp}.partType`);
  for (const key of ["placementOrigin", "xAxis", "yAxis"]) {
    if (key in g) vector(g[key], `${gp}.${key}`, ["x", "y", "z"]);
  }
  array(g.profileEdges, `${gp}.profileEdges`);
  g.profileEdges.forEach((edge, i) => {
    const ep = `${gp}.profileEdges[${i}]`;
    record(edge, ep);
    string(edge.id, `${ep}.id`);
    finite(edge.index, `${ep}.index`);
    finite(edge.type, `${ep}.type`);
    vector(edge.start, `${ep}.start`, ["x", "y"]);
    vector(edge.end, `${ep}.end`, ["x", "y"]);
  });

  const b = g.brep;
  const bp = `${gp}.brep`;
  record(b, bp);
  for (const key of ["verticesLocal", "verticesWorld"]) {
    const vertices = b[key];
    array(vertices, `${bp}.${key}`);
    requireValue(vertices.length > 0, `${bp}.${key}`, "a non-empty vertex array");
    vertices.forEach((vertex, i) => vector(vertex, `${bp}.${key}[${i}]`, ["x", "y", "z"]));
  }
  // Keep local and already-transformed coordinates exactly as supplied. Never
  // apply either the B-rep transform or the unverified element placement here.
  const local = b.verticesLocal;
  const world = b.verticesWorld;
  array(local, `${bp}.verticesLocal`);
  array(world, `${bp}.verticesWorld`);
  requireValue(local.length === world.length, `${bp}.verticesWorld`, "the same length as verticesLocal");

  array(b.faces, `${bp}.faces`);
  requireValue(b.faces.length > 0, `${bp}.faces`, "a non-empty face array");
  b.faces.forEach((face, i) => {
    const fp = `${bp}.faces[${i}]`;
    record(face, fp);
    finite(face.flag, `${fp}.flag`);
    array(face.indices, `${fp}.indices`);
    requireValue(face.indices.length >= 3 && face.indices.length <= world.length,
      `${fp}.indices`, "a loop with 3..vertexCount indices");
    face.indices.forEach((index, j) => {
      finite(index, `${fp}.indices[${j}]`);
      requireValue(Number.isInteger(index) && index >= 0 && index < world.length,
        `${fp}.indices[${j}]`, `an integer in 0..${world.length - 1}`);
    });
    // Face.plane is optional in the TS interface, but required by Viewer V1's
    // existing projection/triangulation path. Do not infer it from other fields.
    vector(face.plane, `${fp}.plane`, ["a", "b", "c", "d"]);
    if ("name" in face) string(face.name, `${fp}.name`);
  });
  array(b.planes, `${bp}.planes`);
  requireValue(b.planes.length === b.faces.length, `${bp}.planes`, "the same length as faces");
  b.planes.forEach((plane, i) => vector(plane, `${bp}.planes[${i}]`, ["a", "b", "c", "d"]));
  stringArray(b.faceNames, `${bp}.faceNames`);
  array(b.faceNames, `${bp}.faceNames`);
  requireValue(b.faceNames.length === b.faces.length, `${bp}.faceNames`, "the same length as faces");
  finite(b.vertexCountTokenIndex, `${bp}.vertexCountTokenIndex`);
  requireValue(Number.isInteger(b.vertexCountTokenIndex) && b.vertexCountTokenIndex >= 0,
    `${bp}.vertexCountTokenIndex`, "a non-negative integer");
  if ("transform" in b) {
    record(b.transform, `${bp}.transform`);
    array(b.transform.values, `${bp}.transform.values`);
    requireValue(b.transform.values.length === 12, `${bp}.transform.values`, "12 numbers");
    b.transform.values.forEach((entry, i) => finite(entry, `${bp}.transform.values[${i}]`));
  }
}

/** Diagnostic JSON v1 bridge, not a persistence schema or a B-rep acceptance test.
 * Returns the supplied parsed object unchanged, including unknown extra fields.
 */
export function validateNativeJson(value: unknown): ParsedSmd {
  record(value, "root");
  requireValue(value.schemaVersion === 1, "schemaVersion", "1 (supported JSON version)");
  validateParsed(value.parsed);
  return value.parsed;
}

export function parseNativeJson(text: string): ParsedSmd {
  let value: unknown;
  try {
    value = JSON.parse(text);
  } catch {
    throw new Error("Native JSON: malformed JSON text.");
  }
  return validateNativeJson(value);
}
