#pragma once

namespace m3d {

struct Vec3 final {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};

    friend constexpr bool operator==(const Vec3&, const Vec3&) noexcept = default;
};

struct Quat final {
    float x{0.0F};
    float y{0.0F};
    float z{0.0F};
    float w{1.0F};

    friend constexpr bool operator==(const Quat&, const Quat&) noexcept = default;
};

struct Transform final {
    Vec3 position{};
    Quat rotation{};
    Vec3 scale{1.0F, 1.0F, 1.0F};

    friend constexpr bool operator==(const Transform&, const Transform&) noexcept = default;
};

} // namespace m3d
