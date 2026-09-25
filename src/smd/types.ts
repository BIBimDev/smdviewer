export interface Vec2 {
  x: number;
  y: number;
}

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export interface ProfileEdge {
  id: string;
  index: number;
  type: number;
  start: Vec2;
  end: Vec2;
}

export interface Plane {
  a: number;
  b: number;
  c: number;
  d: number;
}

export interface Face {
  flag: number;
  indices: number[];
  plane?: Plane;
  name?: string;
}

export interface Transform3x4 {
  values: [
    number, number, number, number,
    number, number, number, number,
    number, number, number, number
  ];
}

export interface ParsedBrep {
  verticesLocal: Vec3[];
  verticesWorld: Vec3[];
  faces: Face[];
  planes: Plane[];
  faceNames: string[];
  transform?: Transform3x4;
  vertexCountTokenIndex: number;
}

export interface Smd10125Geometry {
  objectId?: string;
  name?: string;
  partType?: number;
  profileEdges: ProfileEdge[];
  placementOrigin?: Vec3;
  xAxis?: Vec3;
  yAxis?: Vec3;
  brep: ParsedBrep;
}

export interface ParsedSmd {
  isDicadAsc: boolean;
  headerLines: string[];
  geometryBlockLines: string[];
  geometry: Smd10125Geometry;
}

export type Token =
  | { type: "number"; value: number; raw: string; line: number }
  | { type: "string"; value: string; raw: string; line: number }
  | { type: "block"; value: string; raw: string; line: number };
