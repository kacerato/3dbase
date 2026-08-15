#include "mobile3d/render/render_math.hpp"

#include <cmath>

namespace m3d {

Mat4 transformMatrix(const Transform& transform) noexcept {
    float x = transform.rotation.x;
    float y = transform.rotation.y;
    float z = transform.rotation.z;
    float w = transform.rotation.w;
    const float normSquared = x * x + y * y + z * z + w * w;
    if (normSquared > 0.0000001F) {
        const float inverseNorm = 1.0F / std::sqrt(normSquared);
        x *= inverseNorm;
        y *= inverseNorm;
        z *= inverseNorm;
        w *= inverseNorm;
    } else {
        x = 0.0F;
        y = 0.0F;
        z = 0.0F;
        w = 1.0F;
    }

    const float xx = x * x;
    const float yy = y * y;
    const float zz = z * z;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float wx = w * x;
    const float wy = w * y;
    const float wz = w * z;

    Mat4 result = Mat4::identity();
    result.at(0, 0) = (1.0F - 2.0F * (yy + zz)) * transform.scale.x;
    result.at(1, 0) = (2.0F * (xy + wz)) * transform.scale.x;
    result.at(2, 0) = (2.0F * (xz - wy)) * transform.scale.x;

    result.at(0, 1) = (2.0F * (xy - wz)) * transform.scale.y;
    result.at(1, 1) = (1.0F - 2.0F * (xx + zz)) * transform.scale.y;
    result.at(2, 1) = (2.0F * (yz + wx)) * transform.scale.y;

    result.at(0, 2) = (2.0F * (xz + wy)) * transform.scale.z;
    result.at(1, 2) = (2.0F * (yz - wx)) * transform.scale.z;
    result.at(2, 2) = (1.0F - 2.0F * (xx + yy)) * transform.scale.z;

    result.at(0, 3) = transform.position.x;
    result.at(1, 3) = transform.position.y;
    result.at(2, 3) = transform.position.z;
    return result;
}

} // namespace m3d
