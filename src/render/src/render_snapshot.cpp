#include "mobile3d/render/render_snapshot.hpp"

#include <algorithm>
#include <utility>

namespace m3d {

RenderSceneSnapshot::RenderSceneSnapshot(std::uint64_t sceneRevision,
                                         std::uint64_t selectionRevision,
                                         std::vector<RenderObjectSnapshot> objects,
                                         std::vector<RenderMeshSnapshot> meshes)
    : sceneRevision_(sceneRevision),
      selectionRevision_(selectionRevision),
      objects_(std::move(objects)),
      meshes_(std::move(meshes)) {}

const RenderObjectSnapshot* RenderSceneSnapshot::find(ObjectId id) const noexcept {
    const auto found = std::find_if(objects_.cbegin(), objects_.cend(),
                                    [id](const RenderObjectSnapshot& object) { return object.id == id; });
    return found == objects_.cend() ? nullptr : &*found;
}

const RenderMeshSnapshot* RenderSceneSnapshot::findMesh(ResourceId id) const noexcept {
    const auto found = std::find_if(meshes_.cbegin(), meshes_.cend(),
                                    [id](const RenderMeshSnapshot& mesh) { return mesh.id == id; });
    return found == meshes_.cend() ? nullptr : &*found;
}

RenderSceneSnapshot RenderSnapshotBuilder::build(const Scene& scene,
                                                 const SelectionModel& selection,
                                                 std::uint64_t sceneRevision,
                                                 std::uint64_t selectionRevision) {
    auto authoredObjects = scene.objects();
    std::sort(authoredObjects.begin(), authoredObjects.end(),
              [](const SceneObject& left, const SceneObject& right) {
                  if (left.id.high() != right.id.high()) return left.id.high() < right.id.high();
                  return left.id.low() < right.id.low();
              });

    auto authoredMeshes = scene.meshResources();
    std::sort(authoredMeshes.begin(), authoredMeshes.end(),
              [](const MeshResource& left, const MeshResource& right) {
                  if (left.id.high() != right.id.high()) return left.id.high() < right.id.high();
                  return left.id.low() < right.id.low();
              });

    std::vector<RenderMeshSnapshot> meshSnapshots;
    meshSnapshots.reserve(authoredMeshes.size());
    for (const auto& mesh : authoredMeshes) {
        meshSnapshots.push_back(RenderMeshSnapshot{
            .id = mesh.id,
            .vertices = mesh.vertices,
            .indices = mesh.indices,
            .bounds = mesh.bounds(),
        });
    }

    const auto active = selection.active();
    std::vector<RenderObjectSnapshot> snapshots;
    snapshots.reserve(authoredObjects.size());
    for (const auto& object : authoredObjects) {
        snapshots.push_back(RenderObjectSnapshot{
            .id = object.id,
            .type = object.type,
            .localTransform = object.localTransform,
            .parent = object.parent,
            .meshResource = object.meshResource,
            .visible = object.visible,
            .selected = selection.contains(object.id),
            .active = active && *active == object.id,
        });
    }

    return RenderSceneSnapshot(sceneRevision, selectionRevision,
                               std::move(snapshots), std::move(meshSnapshots));
}

} // namespace m3d
