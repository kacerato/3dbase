#pragma once

#include "mobile3d/core/id.hpp"

#include <string>
#include <vector>

namespace m3d {

struct SceneCollection final {
    CollectionId id{};
    std::string name;
    bool visible{true};
    bool locked{false};
    std::vector<ObjectId> objects;
};

struct SceneLayer final {
    LayerId id{};
    std::string name;
    bool enabled{true};
    std::vector<CollectionId> collections;
};

} // namespace m3d
