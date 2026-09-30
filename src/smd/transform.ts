import type { Transform3x4, Vec3 } from "./types";

export function applyTransform(v: Vec3, t: Transform3x4): Vec3 {
  const m = t.values;

  return {
    x: m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3],
    y: m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7],
    z: m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11],
  };
}
