#pragma once

#include "mobile3d/core/mesh_resource.hpp"
#include "mobile3d/core/scene_object.hpp"
#include "mobile3d/core/scene_organization.hpp"

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

    [[nodiscard]] CollectionId createCollection(std::string name);
    [[nodiscard]] bool insertCollection(SceneCollection collection);
    [[nodiscard]] bool containsCollection(CollectionId id) const;
    [[nodiscard]] SceneCollection* findCollection(CollectionId id);
    [[nodiscard]] const SceneCollection* findCollection(CollectionId id) const;
    [[nodiscard]] std::vector<SceneCollection> collections() const;
    [[nodiscard]] bool renameCollection(CollectionId id, std::string name);
    [[nodiscard]] bool setCollectionVisible(CollectionId id, bool visible);
    [[nodiscard]] bool setCollectionLocked(CollectionId id, bool locked);
    [[nodiscard]] bool addObjectToCollection(CollectionId collection, ObjectId object);
    [[nodiscard]] bool removeObjectFromCollection(CollectionId collection, ObjectId object);
    [[nodiscard]] bool removeCollection(CollectionId collection);
    [[nodiscard]] std::size_t collectionCount() const noexcept { return collections_.size(); }

    [[nodiscard]] LayerId createLayer(std::string name);
    [[nodiscard]] bool insertLayer(SceneLayer layer);
    [[nodiscard]] bool containsLayer(LayerId id) const;
    [[nodiscard]] SceneLayer* findLayer(LayerId id);
    [[nodiscard]] const SceneLayer* findLayer(LayerId id) const;
    [[nodiscard]] std::vector<SceneLayer> layers() const;
    [[nodiscard]] bool renameLayer(LayerId id, std::string name);
    [[nodiscard]] bool setLayerEnabled(LayerId id, bool enabled);
    [[nodiscard]] bool addCollectionToLayer(LayerId layer, CollectionId collection);
    [[nodiscard]] bool removeCollectionFromLayer(LayerId layer, CollectionId collection);
    [[nodiscard]] bool removeLayer(LayerId layer);
    [[nodiscard]] std::size_t layerCount() const noexcept { return layers_.size(); }

    // View-layer policy: objects outside all collections remain visible in every
    // layer. Collected objects are visible when at least one visible collection
    // containing them belongs to the active enabled layer.
    [[nodiscard]] bool isObjectVisibleInLayer(ObjectId object, std::optional<LayerId> layer) const;
    [[nodiscard]] bool isObjectLockedByOrganization(ObjectId object,
                                                    std::optional<LayerId> layer) const;

    [[nodiscard]] bool rename(ObjectId id, std::string name);
    [[nodiscard]] bool setVisible(ObjectId id, bool visible);
    [[nodiscard]] bool setLocked(ObjectId id, bool locked);
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
    void removeObjectMembership(ObjectId object) noexcept;

    std::unordered_map<ObjectId, SceneObject, ObjectIdHash> objects_;
    std::unordered_map<ResourceId, MeshResource, ResourceIdHash> meshResources_;
    std::unordered_map<CollectionId, SceneCollection, CollectionIdHash> collections_;
    std::unordered_map<LayerId, SceneLayer, LayerIdHash> layers_;
};

} // namespace m3d
