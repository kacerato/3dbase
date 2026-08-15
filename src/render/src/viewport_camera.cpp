#include "mobile3d/render/viewport_camera.hpp"

#include <algorithm>
#include <cmath>

namespace m3d {
namespace {

constexpr float kDegreesToRadians = 0.01745329251994329577F;
constexpr float kEpsilon = 1.0e-6F;

[[nodiscard]] bool finite(float value) noexcept {
    return std::isfinite(value);
}

[[nodiscard]] bool finite(Vec3 value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

[[nodiscard]] Vec3 add(Vec3 left, Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] Vec3 subtract(Vec3 left, Vec3 right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] Vec3 scale(Vec3 value, float amount) noexcept {
    return {value.x * amount, value.y * amount, value.z * amount};
}

[[nodiscard]] float dot(Vec3 left, Vec3 right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] Vec3 cross(Vec3 left, Vec3 right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] Vec3 normalized(Vec3 value) noexcept {
    const float lengthSquared = dot(value, value);
    if (!finite(lengthSquared) || lengthSquared <= kEpsilon) {
        return {};
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return scale(value, inverseLength);
}

} // namespace

void ViewportCamera::setTarget(Vec3 target) noexcept {
    if (finite(target)) {
        target_ = target;
    }
}

void ViewportCamera::setOrbit(float yawRadians, float pitchRadians) noexcept {
    if (finite(yawRadians)) {
        yawRadians_ = std::remainder(yawRadians, 6.2831853071795864769F);
    }
    if (finite(pitchRadians)) {
        pitchRadians_ = std::clamp(pitchRadians, kMinPitchRadians, kMaxPitchRadians);
    }
}

void ViewportCamera::orbit(float deltaYawRadians, float deltaPitchRadians) noexcept {
    if (!finite(deltaYawRadians) || !finite(deltaPitchRadians)) {
        return;
    }
    setOrbit(yawRadians_ + deltaYawRadians, pitchRadians_ + deltaPitchRadians);
}

void ViewportCamera::setDistance(float distance) noexcept {
    if (finite(distance)) {
        distance_ = std::clamp(distance, kMinDistance, kMaxDistance);
    }
}

void ViewportCamera::setPerspectiveFovDegrees(float degrees) noexcept {
    if (finite(degrees)) {
        perspectiveFovDegrees_ = std::clamp(degrees, 5.0F, 170.0F);
    }
}

void ViewportCamera::setOrthographicHeight(float height) noexcept {
    if (finite(height)) {
        orthographicHeight_ = std::clamp(height, kMinOrthoHeight, kMaxOrthoHeight);
    }
}

bool ViewportCamera::setClipping(float nearClip, float farClip) noexcept {
    if (!finite(nearClip) || !finite(farClip) || nearClip <= 0.0F || farClip <= nearClip) {
        return false;
    }
    nearClip_ = nearClip;
    farClip_ = farClip;
    return true;
}

Vec3 ViewportCamera::position() const noexcept {
    const float cosPitch = std::cos(pitchRadians_);
    const Vec3 offset{
        distance_ * cosPitch * std::sin(yawRadians_),
        distance_ * std::sin(pitchRadians_),
        distance_ * cosPitch * std::cos(yawRadians_),
    };
    return add(target_, offset);
}

Vec3 ViewportCamera::forward() const noexcept {
    return normalized(subtract(target_, position()));
}

Vec3 ViewportCamera::right() const noexcept {
    const Vec3 cameraForward = forward();
    Vec3 cameraRight = normalized(cross(cameraForward, Vec3{0.0F, 1.0F, 0.0F}));
    if (dot(cameraRight, cameraRight) <= kEpsilon) {
        cameraRight = {1.0F, 0.0F, 0.0F};
    }
    return cameraRight;
}

Vec3 ViewportCamera::up() const noexcept {
    return normalized(cross(right(), forward()));
}

void ViewportCamera::pan(float horizontal, float vertical) noexcept {
    if (!finite(horizontal) || !finite(vertical)) {
        return;
    }
    target_ = add(target_, add(scale(right(), horizontal), scale(up(), vertical)));
}

void ViewportCamera::zoom(float scaleFactor) noexcept {
    if (!finite(scaleFactor) || scaleFactor <= 0.0F) {
        return;
    }
    if (projection_ == CameraProjection::Perspective) {
        setDistance(distance_ / scaleFactor);
    } else {
        setOrthographicHeight(orthographicHeight_ / scaleFactor);
    }
}

Mat4 ViewportCamera::viewMatrix() const noexcept {
    const Vec3 eye = position();
    const Vec3 cameraForward = forward();
    const Vec3 cameraRight = right();
    const Vec3 cameraUp = up();

    Mat4 result = Mat4::identity();
    result.at(0, 0) = cameraRight.x;
    result.at(0, 1) = cameraRight.y;
    result.at(0, 2) = cameraRight.z;
    result.at(0, 3) = -dot(cameraRight, eye);

    result.at(1, 0) = cameraUp.x;
    result.at(1, 1) = cameraUp.y;
    result.at(1, 2) = cameraUp.z;
    result.at(1, 3) = -dot(cameraUp, eye);

    result.at(2, 0) = -cameraForward.x;
    result.at(2, 1) = -cameraForward.y;
    result.at(2, 2) = -cameraForward.z;
    result.at(2, 3) = dot(cameraForward, eye);
    return result;
}

Mat4 ViewportCamera::projectionMatrix(float aspectRatio) const noexcept {
    const float aspect = finite(aspectRatio) && aspectRatio > kEpsilon ? aspectRatio : 1.0F;
    Mat4 result{};

    if (projection_ == CameraProjection::Perspective) {
        const float fovRadians = perspectiveFovDegrees_ * kDegreesToRadians;
        const float focalScale = 1.0F / std::tan(fovRadians * 0.5F);
        result.at(0, 0) = focalScale / aspect;
        result.at(1, 1) = -focalScale;
        result.at(2, 2) = farClip_ / (nearClip_ - farClip_);
        result.at(2, 3) = (farClip_ * nearClip_) / (nearClip_ - farClip_);
        result.at(3, 2) = -1.0F;
        return result;
    }

    const float width = orthographicHeight_ * aspect;
    result.at(0, 0) = 2.0F / width;
    result.at(1, 1) = -2.0F / orthographicHeight_;
    result.at(2, 2) = 1.0F / (nearClip_ - farClip_);
    result.at(2, 3) = nearClip_ / (nearClip_ - farClip_);
    result.at(3, 3) = 1.0F;
    return result;
}

Mat4 ViewportCamera::viewProjectionMatrix(float aspectRatio) const noexcept {
    return multiply(projectionMatrix(aspectRatio), viewMatrix());
}

Mat4 multiply(const Mat4& left, const Mat4& right) noexcept {
    Mat4 result{};
    for (std::size_t row = 0; row < 4U; ++row) {
        for (std::size_t column = 0; column < 4U; ++column) {
            float value = 0.0F;
            for (std::size_t element = 0; element < 4U; ++element) {
                value += left.at(row, element) * right.at(element, column);
            }
            result.at(row, column) = value;
        }
    }
    return result;
}

} // namespace m3d
