#pragma once

#include "mobile3d/core/mesh_resource.hpp"
#include "mobile3d/core/scene_object.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace m3d {

class Scene final {
public:
    [[nodiscard]] ObjectId createObject(ObjectType type, std::string name,
                                        std::optional<ObjectId> parent = std::nullopt);
    [[nodiscard]] bool insertObject(SceneObject object);
    [[nodiscard]] bool contains(ObjectId id) const;

    [[nodiscard]] SceneObject* find(ObjectId id);
    [[nodiscard]] const SceneObject* find(ObjectId id) const;

    [[nodiscard]] std::vector<ObjectId> roots() const;
    [[nodiscard]] std::vector<ObjectId> childrenOf(ObjectId parent) const;
    [[nodiscard]] std::vector<SceneObject> objects() const;

    [[nodiscard]] ResourceId createMeshResource(MeshResource resource);
    [[nodiscard]] bool insertMeshResource(MeshResource resource);
    [[nodiscard]] bool containsResource(ResourceId id) const;
    [[nodiscard]] MeshResource* findMeshResource(ResourceId id);
    [[nodiscard]] const MeshResource* findMeshResource(ResourceId id) const;
    [[nodiscard]] std::vector<MeshResource> meshResources() const;
    [[nodiscard]] bool assignMesh(ObjectId object, std::optional<ResourceId> resource);
    [[nodiscard]] bool removeMeshResource(ResourceId resource);
    [[nodiscard]] std::size_t meshResourceCount() const noexcept { return meshResources_.size(); }

    [[nodiscard]] bool rename(ObjectId id, std::string name);
    [[nodiscard]] bool setTransform(ObjectId id, const Transform& transform);
    [[nodiscard]] bool reparent(ObjectId id, std::optional<ObjectId> newParent);
    [[nodiscard]] bool canReparent(ObjectId id, std::optional<ObjectId> newParent) const;

    [[nodiscard]] std::vector<SceneObject> removeSubtree(ObjectId root);
    [[nodiscard]] bool restoreObjects(const std::vector<SceneObject>& objects);

    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return objects_.size(); }

private:
    [[nodiscard]] bool isDescendantOf(ObjectId candidate, ObjectId ancestor) const;
    void collectSubtree(ObjectId root, std::vector<ObjectId>& out) const;

    std::unordered_map<ObjectId, SceneObject, ObjectIdHash> objects_;
    std::unordered_map<ResourceId, MeshResource, ResourceIdHash> meshResources_;
};

} // namespace m3d
