#pragma once

#include "mobile3d/core/scene.hpp"
#include "mobile3d/core/selection_model.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace m3d {

struct RenderObjectSnapshot final {
    ObjectId id{};
    ObjectType type{ObjectType::Empty};
    Transform localTransform{};
    std::optional<ObjectId> parent{};
    bool visible{true};
    bool selected{false};
    bool active{false};
};

class RenderSceneSnapshot final {
public:
    RenderSceneSnapshot() = default;
    RenderSceneSnapshot(std::uint64_t sceneRevision,
                        std::uint64_t selectionRevision,
                        std::vector<RenderObjectSnapshot> objects);

    [[nodiscard]] std::uint64_t sceneRevision() const noexcept { return sceneRevision_; }
    [[nodiscard]] std::uint64_t selectionRevision() const noexcept { return selectionRevision_; }
    [[nodiscard]] const std::vector<RenderObjectSnapshot>& objects() const noexcept { return objects_; }
    [[nodiscard]] const RenderObjectSnapshot* find(ObjectId id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return objects_.size(); }
    [[nodiscard]] bool empty() const noexcept { return objects_.empty(); }

private:
    std::uint64_t sceneRevision_{0};
    std::uint64_t selectionRevision_{0};
    std::vector<RenderObjectSnapshot> objects_;
};

class RenderSnapshotBuilder final {
public:
    [[nodiscard]] static RenderSceneSnapshot build(const Scene& scene,
                                                   const SelectionModel& selection,
                                                   std::uint64_t sceneRevision,
                                                   std::uint64_t selectionRevision);
};

} // namespace m3d
