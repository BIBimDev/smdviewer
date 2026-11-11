#include "smd/parser.hpp"

namespace smd {
Vec3 applyTransform(Vec3 v, const Transform3x4& t) {
    const auto& m = t.values;
    return {m[0]*v.x + m[1]*v.y + m[2]*v.z + m[3],
            m[4]*v.x + m[5]*v.y + m[6]*v.z + m[7],
            m[8]*v.x + m[9]*v.y + m[10]*v.z + m[11]};
}
} // namespace smd
