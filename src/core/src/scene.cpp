#include "mobile3d/core/scene.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace m3d {

ObjectId Scene::createObject(ObjectType type, std::string name, std::optional<ObjectId> parent) {
    if (parent && !contains(*parent)) {
        return ObjectId::null();
    }

    SceneObject object;
    do {
        object.id = ObjectId::generate();
    } while (contains(object.id));
    object.type = type;
    object.name = std::move(name);
    object.parent = parent;

    const auto id = object.id;
    objects_.emplace(id, std::move(object));
    return id;
}

bool Scene::insertObject(SceneObject object) {
    if (object.id.isNull() || contains(object.id)) {
        return false;
    }
    if (object.parent && !contains(*object.parent)) {
        return false;
    }
    if (object.meshResource && (object.type != ObjectType::Mesh || !containsResource(*object.meshResource))) {
        return false;
    }
    return objects_.emplace(object.id, std::move(object)).second;
}

bool Scene::contains(ObjectId id) const {
    return objects_.contains(id);
}

SceneObject* Scene::find(ObjectId id) {
    const auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : &it->second;
}

const SceneObject* Scene::find(ObjectId id) const {
    const auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : &it->second;
}

std::vector<ObjectId> Scene::roots() const {
    std::vector<ObjectId> result;
    result.reserve(objects_.size());
    for (const auto& [id, object] : objects_) {
        if (!object.parent) result.push_back(id);
    }
    return result;
}

std::vector<ObjectId> Scene::childrenOf(ObjectId parent) const {
    std::vector<ObjectId> result;
    for (const auto& [id, object] : objects_) {
        if (object.parent && *object.parent == parent) result.push_back(id);
    }
    return result;
}

std::vector<SceneObject> Scene::objects() const {
    std::vector<SceneObject> result;
    result.reserve(objects_.size());
    for (const auto& [id, object] : objects_) {
        (void)id;
        result.push_back(object);
    }
    std::sort(result.begin(), result.end(), [](const SceneObject& lhs, const SceneObject& rhs) {
        return lhs.id.toString() < rhs.id.toString();
    });
    return result;
}

ResourceId Scene::createMeshResource(MeshResource resource) {
    if (resource.id.isNull()) {
        do {
            resource.id = ResourceId::generate();
        } while (containsResource(resource.id));
    }
    const auto id = resource.id;
    return insertMeshResource(std::move(resource)) ? id : ResourceId::null();
}

bool Scene::insertMeshResource(MeshResource resource) {
    std::string error;
    if (!resource.validate(&error) || containsResource(resource.id)) {
        return false;
    }
    return meshResources_.emplace(resource.id, std::move(resource)).second;
}

bool Scene::containsResource(ResourceId id) const {
    return meshResources_.contains(id);
}

MeshResource* Scene::findMeshResource(ResourceId id) {
    const auto it = meshResources_.find(id);
    return it == meshResources_.end() ? nullptr : &it->second;
}

const MeshResource* Scene::findMeshResource(ResourceId id) const {
    const auto it = meshResources_.find(id);
    return it == meshResources_.end() ? nullptr : &it->second;
}

std::vector<MeshResource> Scene::meshResources() const {
    std::vector<MeshResource> result;
    result.reserve(meshResources_.size());
    for (const auto& [id, resource] : meshResources_) {
        (void)id;
        result.push_back(resource);
    }
    std::sort(result.begin(), result.end(), [](const MeshResource& lhs, const MeshResource& rhs) {
        return lhs.id.toString() < rhs.id.toString();
    });
    return result;
}

bool Scene::assignMesh(ObjectId objectId, std::optional<ResourceId> resource) {
    auto* object = find(objectId);
    if (!object || object->type != ObjectType::Mesh) {
        return false;
    }
    if (resource && !containsResource(*resource)) {
        return false;
    }
    object->meshResource = resource;
    return true;
}

bool Scene::removeMeshResource(ResourceId resource) {
    if (!containsResource(resource)) {
        return false;
    }
    const auto referenced = std::any_of(objects_.cbegin(), objects_.cend(), [resource](const auto& entry) {
        return entry.second.meshResource && *entry.second.meshResource == resource;
    });
    if (referenced) {
        return false;
    }
    return meshResources_.erase(resource) == 1U;
}

bool Scene::rename(ObjectId id, std::string name) {
    auto* object = find(id);
    if (!object || name.empty()) return false;
    object->name = std::move(name);
    return true;
}

bool Scene::setVisible(ObjectId id, bool visible) {
    auto* object = find(id);
    if (!object) return false;
    object->visible = visible;
    return true;
}

bool Scene::setLocked(ObjectId id, bool locked) {
    auto* object = find(id);
    if (!object) return false;
    object->locked = locked;
    return true;
}

bool Scene::setTransform(ObjectId id, const Transform& transform) {
    auto* object = find(id);
    if (!object) return false;
    object->localTransform = transform;
    return true;
}

bool Scene::reparent(ObjectId id, std::optional<ObjectId> newParent) {
    if (!canReparent(id, newParent)) return false;
    find(id)->parent = newParent;
    return true;
}

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

std::vector<SceneObject> Scene::removeSubtree(ObjectId root) {
    if (!contains(root)) return {};
    std::vector<ObjectId> ids;
    collectSubtree(root, ids);
    std::vector<SceneObject> snapshot;
    snapshot.reserve(ids.size());
    for (const auto id : ids) snapshot.push_back(*find(id));
    for (auto it = ids.rbegin(); it != ids.rend(); ++it) objects_.erase(*it);
    return snapshot;
}

bool Scene::restoreObjects(const std::vector<SceneObject>& objects) {
    if (objects.empty()) return true;

    std::unordered_set<ObjectId, ObjectIdHash> incoming;
    incoming.reserve(objects.size());
    for (const auto& object : objects) {
        if (object.id.isNull() || contains(object.id) || !incoming.insert(object.id).second) return false;
        if (object.meshResource && (object.type != ObjectType::Mesh || !containsResource(*object.meshResource))) {
            return false;
        }
    }
    for (const auto& object : objects) {
        if (object.parent && !contains(*object.parent) && !incoming.contains(*object.parent)) return false;
    }

    std::vector<SceneObject> pending = objects;
    while (!pending.empty()) {
        const auto before = pending.size();
        for (auto it = pending.begin(); it != pending.end();) {
            if (!it->parent || contains(*it->parent)) {
                objects_.emplace(it->id, *it);
                it = pending.erase(it);
            } else {
                ++it;
            }
        }
        if (pending.size() == before) {
            for (const auto& object : objects) objects_.erase(object.id);
            return false;
        }
    }
    return true;
}

void Scene::clear() noexcept {
    objects_.clear();
    meshResources_.clear();
}

} // namespace m3d
