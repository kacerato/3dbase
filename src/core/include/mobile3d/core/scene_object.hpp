#pragma once

#include "mobile3d/core/id.hpp"
#include "mobile3d/core/math.hpp"

#include <optional>
#include <string>

namespace m3d {

enum class ObjectType {
    Mesh,
    Camera,
    Light,
    Empty,
    Curve,
    Text,
    Armature,
    Volume,
    Image,
    Reference,
    Collection,
};

struct SceneObject final {
    ObjectId id{};
    std::string name{"Object"};
    ObjectType type{ObjectType::Empty};
    Transform localTransform{};
    std::optional<ObjectId> parent{};
    std::optional<ResourceId> meshResource{};
    bool visible{true};
    bool locked{false};
};

} // namespace m3d
