#pragma once

#include "mobile3d/core/id.hpp"
#include "mobile3d/core/math.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace m3d {

struct Bounds3 final {
    Vec3 min{};
    Vec3 max{};

    friend constexpr bool operator==(const Bounds3&, const Bounds3&) noexcept = default;
};

struct MeshVertex final {
    Vec3 position{};
    Vec3 normal{};

    friend constexpr bool operator==(const MeshVertex&, const MeshVertex&) noexcept = default;
};

struct MeshResource final {
    ResourceId id{};
    std::string name{"Mesh"};
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] bool validate(std::string* error = nullptr) const;
    [[nodiscard]] std::optional<Bounds3> bounds() const noexcept;
    [[nodiscard]] static MeshResource makeCube(std::string name = "Cube", float size = 1.0F);
};

} // namespace m3d
