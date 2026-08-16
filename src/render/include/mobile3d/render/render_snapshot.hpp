#pragma once

#include "mobile3d/core/scene.hpp"
#include "mobile3d/core/selection_model.hpp"
#include "mobile3d/render/viewport_camera.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace m3d {

using PickId = std::uint32_t;
inline constexpr PickId backgroundPickId = 0;

struct RenderMeshSnapshot final {
    ResourceId id{};
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::optional<Bounds3> bounds{};
    std::uint64_t contentHash{0};
};

struct RenderObjectSnapshot final {
    ObjectId id{};
    PickId pickId{backgroundPickId};
    ObjectType type{ObjectType::Empty};
    Transform localTransform{};
    Mat4 worldTransform{Mat4::identity()};
    Quat worldRotation{};
    std::optional<ObjectId> parent{};
    std::optional<ResourceId> meshResource{};
    bool visible{true};
    bool locked{false};
    bool selected{false};
    bool active{false};
};

class RenderSceneSnapshot final {
public:
    RenderSceneSnapshot() = default;
    RenderSceneSnapshot(std::uint64_t sceneRevision,
                        std::uint64_t selectionRevision,
                        std::vector<RenderObjectSnapshot> objects,
                        std::vector<RenderMeshSnapshot> meshes);

    [[nodiscard]] std::uint64_t sceneRevision() const noexcept { return sceneRevision_; }
    [[nodiscard]] std::uint64_t selectionRevision() const noexcept { return selectionRevision_; }
    [[nodiscard]] const std::vector<RenderObjectSnapshot>& objects() const noexcept { return objects_; }
    [[nodiscard]] const std::vector<RenderMeshSnapshot>& meshes() const noexcept { return meshes_; }
    [[nodiscard]] const RenderObjectSnapshot* find(ObjectId id) const noexcept;
    [[nodiscard]] const RenderObjectSnapshot* findPickId(PickId pickId) const noexcept;
    [[nodiscard]] const RenderMeshSnapshot* findMesh(ResourceId id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return objects_.size(); }
    [[nodiscard]] bool empty() const noexcept { return objects_.empty(); }

private:
    std::uint64_t sceneRevision_{0};
    std::uint64_t selectionRevision_{0};
    std::vector<RenderObjectSnapshot> objects_;
    std::vector<RenderMeshSnapshot> meshes_;
};

class RenderSnapshotBuilder final {
public:
    [[nodiscard]] static RenderSceneSnapshot build(const Scene& scene,
                                                   const SelectionModel& selection,
                                                   std::uint64_t sceneRevision,
                                                   std::uint64_t selectionRevision,
                                                   std::optional<LayerId> activeLayer = std::nullopt);
};

} // namespace m3d
