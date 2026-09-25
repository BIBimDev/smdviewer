import earcut from "earcut";
import type { Face, Vec3 } from "../smd/types";

function projectFace(vertices: Vec3[], face: Face): number[] {
  const plane = face.plane;

  if (!plane) {
    throw new Error("Cannot triangulate a face without a plane in Viewer V1.");
  }

  const ax = Math.abs(plane.a);
  const ay = Math.abs(plane.b);
  const az = Math.abs(plane.c);

  const projected: number[] = [];

  for (const index of face.indices) {
    const v = vertices[index];

    if (az >= ax && az >= ay) {
      projected.push(v.x, v.y);
    } else if (ax >= ay && ax >= az) {
      projected.push(v.y, v.z);
    } else {
      projected.push(v.x, v.z);
    }
  }

  return projected;
}

export function triangulateFace(vertices: Vec3[], face: Face): number[][] {
  const projected = projectFace(vertices, face);
  const localTriangleIndices = earcut(projected, undefined, 2);

  const triangles: number[][] = [];
  for (let i = 0; i < localTriangleIndices.length; i += 3) {
    triangles.push([
      face.indices[localTriangleIndices[i]],
      face.indices[localTriangleIndices[i + 1]],
      face.indices[localTriangleIndices[i + 2]],
    ]);
  }

  return triangles;
}
