#include "mobile3d/core/scene.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace m3d {
namespace {

template <typename T>
[[nodiscard]] bool containsValue(const std::vector<T>& values, const T& value) {
    return std::find(values.cbegin(), values.cend(), value) != values.cend();
}

template <typename T>
[[nodiscard]] bool hasDuplicates(const std::vector<T>& values) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (std::find(values.cbegin() + static_cast<std::ptrdiff_t>(i + 1), values.cend(), values[i]) != values.cend()) {
            return true;
        }
    }
    return false;
}

template <typename T>
bool eraseValue(std::vector<T>& values, const T& value) {
    const auto before = values.size();
    std::erase(values, value);
    return values.size() != before;
}

} // namespace

ObjectId Scene::createObject(ObjectType type, std::string name, std::optional<ObjectId> parent) {
    if (parent && !contains(*parent)) return ObjectId::null();
    SceneObject object;
    do { object.id = ObjectId::generate(); } while (contains(object.id));
    object.type = type;
    object.name = std::move(name);
    object.parent = parent;
    const auto id = object.id;
    objects_.emplace(id, std::move(object));
    return id;
}

bool Scene::insertObject(SceneObject object) {
    if (object.id.isNull() || contains(object.id)) return false;
    if (object.parent && !contains(*object.parent)) return false;
    if (object.meshResource && (object.type != ObjectType::Mesh || !containsResource(*object.meshResource))) return false;
    return objects_.emplace(object.id, std::move(object)).second;
}

bool Scene::contains(ObjectId id) const { return objects_.contains(id); }
SceneObject* Scene::find(ObjectId id) { const auto it = objects_.find(id); return it == objects_.end() ? nullptr : &it->second; }
const SceneObject* Scene::find(ObjectId id) const { const auto it = objects_.find(id); return it == objects_.end() ? nullptr : &it->second; }

std::vector<ObjectId> Scene::roots() const {
    std::vector<ObjectId> result;
    result.reserve(objects_.size());
    for (const auto& [id, object] : objects_) if (!object.parent) result.push_back(id);
    return result;
}

std::vector<ObjectId> Scene::childrenOf(ObjectId parent) const {
    std::vector<ObjectId> result;
    for (const auto& [id, object] : objects_) if (object.parent && *object.parent == parent) result.push_back(id);
    return result;
}

std::vector<SceneObject> Scene::objects() const {
    std::vector<SceneObject> result;
    result.reserve(objects_.size());
    for (const auto& [id, object] : objects_) { (void)id; result.push_back(object); }
    std::sort(result.begin(), result.end(), [](const SceneObject& lhs, const SceneObject& rhs) { return lhs.id.toString() < rhs.id.toString(); });
    return result;
}

ResourceId Scene::createMeshResource(MeshResource resource) {
    if (resource.id.isNull()) do { resource.id = ResourceId::generate(); } while (containsResource(resource.id));
    const auto id = resource.id;
    return insertMeshResource(std::move(resource)) ? id : ResourceId::null();
}

bool Scene::insertMeshResource(MeshResource resource) {
    std::string error;
    if (!resource.validate(&error) || containsResource(resource.id)) return false;
    return meshResources_.emplace(resource.id, std::move(resource)).second;
}

bool Scene::containsResource(ResourceId id) const { return meshResources_.contains(id); }
MeshResource* Scene::findMeshResource(ResourceId id) { const auto it = meshResources_.find(id); return it == meshResources_.end() ? nullptr : &it->second; }
const MeshResource* Scene::findMeshResource(ResourceId id) const { const auto it = meshResources_.find(id); return it == meshResources_.end() ? nullptr : &it->second; }

std::vector<MeshResource> Scene::meshResources() const {
    std::vector<MeshResource> result;
    result.reserve(meshResources_.size());
    for (const auto& [id, resource] : meshResources_) { (void)id; result.push_back(resource); }
    std::sort(result.begin(), result.end(), [](const MeshResource& lhs, const MeshResource& rhs) { return lhs.id.toString() < rhs.id.toString(); });
    return result;
}

bool Scene::assignMesh(ObjectId objectId, std::optional<ResourceId> resource) {
    auto* object = find(objectId);
    if (!object || object->type != ObjectType::Mesh) return false;
    if (resource && !containsResource(*resource)) return false;
    object->meshResource = resource;
    return true;
}

bool Scene::removeMeshResource(ResourceId resource) {
    if (!containsResource(resource)) return false;
    const auto referenced = std::any_of(objects_.cbegin(), objects_.cend(), [resource](const auto& entry) {
        return entry.second.meshResource && *entry.second.meshResource == resource;
    });
    if (referenced) return false;
    return meshResources_.erase(resource) == 1U;
}

CollectionId Scene::createCollection(std::string name) {
    if (name.empty()) return CollectionId::null();
    SceneCollection collection;
    do { collection.id = CollectionId::generate(); } while (containsCollection(collection.id));
    collection.name = std::move(name);
    const auto id = collection.id;
    return insertCollection(std::move(collection)) ? id : CollectionId::null();
}

bool Scene::insertCollection(SceneCollection collection) {
    if (collection.id.isNull() || collection.name.empty() || containsCollection(collection.id) || hasDuplicates(collection.objects)) return false;
    for (const auto object : collection.objects) if (!contains(object)) return false;
    return collections_.emplace(collection.id, std::move(collection)).second;
}

bool Scene::containsCollection(CollectionId id) const { return collections_.contains(id); }
SceneCollection* Scene::findCollection(CollectionId id) { const auto it = collections_.find(id); return it == collections_.end() ? nullptr : &it->second; }
const SceneCollection* Scene::findCollection(CollectionId id) const { const auto it = collections_.find(id); return it == collections_.end() ? nullptr : &it->second; }

std::vector<SceneCollection> Scene::collections() const {
    std::vector<SceneCollection> result;
    result.reserve(collections_.size());
    for (const auto& [id, collection] : collections_) { (void)id; result.push_back(collection); }
    std::sort(result.begin(), result.end(), [](const SceneCollection& lhs, const SceneCollection& rhs) { return lhs.id.toString() < rhs.id.toString(); });
    return result;
}

bool Scene::renameCollection(CollectionId id, std::string name) { auto* value = findCollection(id); if (!value || name.empty()) return false; value->name = std::move(name); return true; }
bool Scene::setCollectionVisible(CollectionId id, bool visible) { auto* value = findCollection(id); if (!value) return false; value->visible = visible; return true; }
bool Scene::setCollectionLocked(CollectionId id, bool locked) { auto* value = findCollection(id); if (!value) return false; value->locked = locked; return true; }

bool Scene::addObjectToCollection(CollectionId collection, ObjectId object) {
    auto* target = findCollection(collection);
    if (!target || !contains(object) || containsValue(target->objects, object)) return false;
    target->objects.push_back(object);
    return true;
}

bool Scene::removeObjectFromCollection(CollectionId collection, ObjectId object) {
    auto* target = findCollection(collection);
    return target && eraseValue(target->objects, object);
}

bool Scene::removeCollection(CollectionId collection) {
    if (!containsCollection(collection)) return false;
    for (auto& [id, layer] : layers_) { (void)id; eraseValue(layer.collections, collection); }
    return collections_.erase(collection) == 1U;
}

LayerId Scene::createLayer(std::string name) {
    if (name.empty()) return LayerId::null();
    SceneLayer layer;
    do { layer.id = LayerId::generate(); } while (containsLayer(layer.id));
    layer.name = std::move(name);
    const auto id = layer.id;
    return insertLayer(std::move(layer)) ? id : LayerId::null();
}

bool Scene::insertLayer(SceneLayer layer) {
    if (layer.id.isNull() || layer.name.empty() || containsLayer(layer.id) || hasDuplicates(layer.collections)) return false;
    for (const auto collection : layer.collections) if (!containsCollection(collection)) return false;
    return layers_.emplace(layer.id, std::move(layer)).second;
}

bool Scene::containsLayer(LayerId id) const { return layers_.contains(id); }
SceneLayer* Scene::findLayer(LayerId id) { const auto it = layers_.find(id); return it == layers_.end() ? nullptr : &it->second; }
const SceneLayer* Scene::findLayer(LayerId id) const { const auto it = layers_.find(id); return it == layers_.end() ? nullptr : &it->second; }

std::vector<SceneLayer> Scene::layers() const {
    std::vector<SceneLayer> result;
    result.reserve(layers_.size());
    for (const auto& [id, layer] : layers_) { (void)id; result.push_back(layer); }
    std::sort(result.begin(), result.end(), [](const SceneLayer& lhs, const SceneLayer& rhs) { return lhs.id.toString() < rhs.id.toString(); });
    return result;
}

bool Scene::renameLayer(LayerId id, std::string name) { auto* value = findLayer(id); if (!value || name.empty()) return false; value->name = std::move(name); return true; }
bool Scene::setLayerEnabled(LayerId id, bool enabled) { auto* value = findLayer(id); if (!value) return false; value->enabled = enabled; return true; }

bool Scene::addCollectionToLayer(LayerId layer, CollectionId collection) {
    auto* target = findLayer(layer);
    if (!target || !containsCollection(collection) || containsValue(target->collections, collection)) return false;
    target->collections.push_back(collection);
    return true;
}

bool Scene::removeCollectionFromLayer(LayerId layer, CollectionId collection) {
    auto* target = findLayer(layer);
    return target && eraseValue(target->collections, collection);
}

bool Scene::removeLayer(LayerId layer) { return layers_.erase(layer) == 1U; }

bool Scene::isObjectVisibleInLayer(ObjectId objectId, std::optional<LayerId> layerId) const {
    const auto* object = find(objectId);
    if (!object || !object->visible) return false;
    const SceneLayer* layer = nullptr;
    if (layerId) {
        layer = findLayer(*layerId);
        if (!layer || !layer->enabled) return false;
    }
    bool belongsToCollection = false;
    for (const auto& [collectionId, collection] : collections_) {
        if (!containsValue(collection.objects, objectId)) continue;
        belongsToCollection = true;
        if (!collection.visible) continue;
        if (!layer || containsValue(layer->collections, collectionId)) return true;
    }
    return !belongsToCollection;
}

bool Scene::isObjectLockedByOrganization(ObjectId objectId, std::optional<LayerId> layerId) const {
    const auto* object = find(objectId);
    if (!object) return true;
    if (object->locked) return true;
    const SceneLayer* layer = layerId ? findLayer(*layerId) : nullptr;
    if (layerId && (!layer || !layer->enabled)) return true;
    for (const auto& [collectionId, collection] : collections_) {
        if (!collection.locked || !containsValue(collection.objects, objectId)) continue;
        if (!layer || containsValue(layer->collections, collectionId)) return true;
    }
    return false;
}

bool Scene::rename(ObjectId id, std::string name) { auto* object = find(id); if (!object || name.empty()) return false; object->name = std::move(name); return true; }
bool Scene::setVisible(ObjectId id, bool visible) { auto* object = find(id); if (!object) return false; object->visible = visible; return true; }
bool Scene::setLocked(ObjectId id, bool locked) { auto* object = find(id); if (!object) return false; object->locked = locked; return true; }
bool Scene::setTransform(ObjectId id, const Transform& transform) { auto* object = find(id); if (!object) return false; object->localTransform = transform; return true; }
bool Scene::reparent(ObjectId id, std::optional<ObjectId> newParent) { if (!canReparent(id, newParent)) return false; find(id)->parent = newParent; return true; }

bool Scene::canReparent(ObjectId id, std::optional<ObjectId> newParent) const {
    if (!contains(id)) return false;
    if (!newParent) return true;
    if (!contains(*newParent) || *newParent == id) return false;
    return !isDescendantOf(*newParent, id);
}

bool Scene::isDescendantOf(ObjectId candidate, ObjectId ancestor) const {
    auto current = find(candidate);
    std::unordered_set<ObjectId, ObjectIdHash> visited;
    while (current && current->parent) {
        if (!visited.insert(current->id).second) return true;
        if (*current->parent == ancestor) return true;
        current = find(*current->parent);
    }
    return false;
}

void Scene::collectSubtree(ObjectId root, std::vector<ObjectId>& out) const {
    out.push_back(root);
    for (const auto child : childrenOf(root)) collectSubtree(child, out);
}

void Scene::removeObjectMembership(ObjectId object) noexcept {
    for (auto& [id, collection] : collections_) { (void)id; eraseValue(collection.objects, object); }
}

std::vector<SceneObject> Scene::removeSubtree(ObjectId root) {
    if (!contains(root)) return {};
    std::vector<ObjectId> ids;
    collectSubtree(root, ids);
    std::vector<SceneObject> snapshot;
    snapshot.reserve(ids.size());
    for (const auto id : ids) snapshot.push_back(*find(id));
    for (auto it = ids.rbegin(); it != ids.rend(); ++it) {
        removeObjectMembership(*it);
        objects_.erase(*it);
    }
    return snapshot;
}

bool Scene::restoreObjects(const std::vector<SceneObject>& values) {
    if (values.empty()) return true;
    std::unordered_set<ObjectId, ObjectIdHash> incoming;
    incoming.reserve(values.size());
    for (const auto& object : values) {
        if (object.id.isNull() || contains(object.id) || !incoming.insert(object.id).second) return false;
        if (object.meshResource && (object.type != ObjectType::Mesh || !containsResource(*object.meshResource))) return false;
    }
    for (const auto& object : values) if (object.parent && !contains(*object.parent) && !incoming.contains(*object.parent)) return false;
    std::vector<SceneObject> pending = values;
    while (!pending.empty()) {
        const auto before = pending.size();
        for (auto it = pending.begin(); it != pending.end();) {
            if (!it->parent || contains(*it->parent)) { objects_.emplace(it->id, *it); it = pending.erase(it); }
            else ++it;
        }
        if (pending.size() == before) {
            for (const auto& object : values) objects_.erase(object.id);
            return false;
        }
    }
    return true;
}

void Scene::clear() noexcept {
    objects_.clear();
    meshResources_.clear();
    collections_.clear();
    layers_.clear();
}

} // namespace m3d
