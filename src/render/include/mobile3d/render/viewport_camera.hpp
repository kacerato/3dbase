#pragma once

#include "mobile3d/core/math.hpp"

#include <array>
#include <cstddef>

namespace m3d {

struct Mat4 final {
    std::array<float, 16> values{};

    [[nodiscard]] constexpr float& at(std::size_t row, std::size_t column) noexcept {
        return values[column * 4U + row];
    }

    [[nodiscard]] constexpr float at(std::size_t row, std::size_t column) const noexcept {
        return values[column * 4U + row];
    }

    [[nodiscard]] static constexpr Mat4 identity() noexcept {
        Mat4 result;
        result.at(0, 0) = 1.0F;
        result.at(1, 1) = 1.0F;
        result.at(2, 2) = 1.0F;
        result.at(3, 3) = 1.0F;
        return result;
    }

    friend constexpr bool operator==(const Mat4&, const Mat4&) noexcept = default;
};

enum class CameraProjection {
    Perspective,
    Orthographic,
};

class ViewportCamera final {
public:
    static constexpr float kMinPitchRadians = -1.55334306F; // -89 degrees
    static constexpr float kMaxPitchRadians = 1.55334306F;  //  89 degrees
    static constexpr float kMinDistance = 0.05F;
    static constexpr float kMaxDistance = 1'000'000.0F;
    static constexpr float kMinOrthoHeight = 0.01F;
    static constexpr float kMaxOrthoHeight = 1'000'000.0F;

    [[nodiscard]] CameraProjection projection() const noexcept { return projection_; }
    void setProjection(CameraProjection projection) noexcept { projection_ = projection; }

    [[nodiscard]] Vec3 target() const noexcept { return target_; }
    void setTarget(Vec3 target) noexcept;

    [[nodiscard]] float yawRadians() const noexcept { return yawRadians_; }
    [[nodiscard]] float pitchRadians() const noexcept { return pitchRadians_; }
    void setOrbit(float yawRadians, float pitchRadians) noexcept;
    void orbit(float deltaYawRadians, float deltaPitchRadians) noexcept;

    [[nodiscard]] float distance() const noexcept { return distance_; }
    void setDistance(float distance) noexcept;

    [[nodiscard]] float perspectiveFovDegrees() const noexcept { return perspectiveFovDegrees_; }
    void setPerspectiveFovDegrees(float degrees) noexcept;

    [[nodiscard]] float orthographicHeight() const noexcept { return orthographicHeight_; }
    void setOrthographicHeight(float height) noexcept;

    [[nodiscard]] float nearClip() const noexcept { return nearClip_; }
    [[nodiscard]] float farClip() const noexcept { return farClip_; }
    [[nodiscard]] bool setClipping(float nearClip, float farClip) noexcept;

    [[nodiscard]] Vec3 position() const noexcept;
    [[nodiscard]] Vec3 forward() const noexcept;
    [[nodiscard]] Vec3 right() const noexcept;
    [[nodiscard]] Vec3 up() const noexcept;

    // Pan amounts are expressed in world units along camera right/up axes.
    void pan(float horizontal, float vertical) noexcept;

    // scaleFactor > 1 zooms in, < 1 zooms out. Perspective changes orbit
    // distance; orthographic mode changes visible world height.
    void zoom(float scaleFactor) noexcept;

    [[nodiscard]] Mat4 viewMatrix() const noexcept;
    [[nodiscard]] Mat4 projectionMatrix(float aspectRatio) const noexcept;
    [[nodiscard]] Mat4 viewProjectionMatrix(float aspectRatio) const noexcept;

private:
    CameraProjection projection_{CameraProjection::Perspective};
    Vec3 target_{};
    float yawRadians_{0.785398163F};
    float pitchRadians_{-0.523598776F};
    float distance_{8.0F};
    float perspectiveFovDegrees_{60.0F};
    float orthographicHeight_{10.0F};
    float nearClip_{0.05F};
    float farClip_{10'000.0F};
};

[[nodiscard]] Mat4 multiply(const Mat4& left, const Mat4& right) noexcept;

} // namespace m3d
