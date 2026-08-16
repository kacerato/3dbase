#include "mobile3d/core/commands/object_commands.hpp"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace m3d {

CreateObjectCommand::CreateObjectCommand(Scene& scene, ObjectType type, std::string name,
                                         std::optional<ObjectId> parent)
    : scene_(scene) {
    object_.type = type;
    object_.name = std::move(name);
    object_.parent = parent;
}

bool CreateObjectCommand::execute() {
    if (!initialized_) {
        if (object_.parent && !scene_.contains(*object_.parent)) return false;
        do {
            object_.id = ObjectId::generate();
        } while (scene_.contains(object_.id));
        initialized_ = true;
    }
    return scene_.insertObject(object_);
}

bool CreateObjectCommand::undo() {
    const auto removed = scene_.removeSubtree(object_.id);
    return removed.size() == 1;
}

CreateMeshObjectCommand::CreateMeshObjectCommand(Scene& scene, MeshResource resource,
                                                 std::string name,
                                                 std::optional<ObjectId> parent)
    : scene_(scene), resource_(std::move(resource)) {
    object_.type = ObjectType::Mesh;
    object_.name = std::move(name);
    object_.parent = parent;
}

bool CreateMeshObjectCommand::execute() {
    if (!initialized_) {
        if (object_.parent && !scene_.contains(*object_.parent)) return false;
        if (resource_.id.isNull()) {
            do {
                resource_.id = ResourceId::generate();
            } while (scene_.containsResource(resource_.id));
        }
        if (resource_.name.empty()) resource_.name = object_.name + " Geometry";
        std::string error;
        if (!resource_.validate(&error)) return false;
        do {
            object_.id = ObjectId::generate();
        } while (scene_.contains(object_.id));
        object_.meshResource = resource_.id;
        initialized_ = true;
    }

    if (!scene_.insertMeshResource(resource_)) return false;
    if (!scene_.insertObject(object_)) {
        (void)scene_.removeMeshResource(resource_.id);
        return false;
    }
    return true;
}

bool CreateMeshObjectCommand::undo() {
    const auto removed = scene_.removeSubtree(object_.id);
    if (removed.size() != 1) {
        if (!removed.empty()) (void)scene_.restoreObjects(removed);
        return false;
    }
    if (!scene_.removeMeshResource(resource_.id)) {
        (void)scene_.restoreObjects(removed);
        return false;
    }
    return true;
}

DeleteObjectCommand::DeleteObjectCommand(Scene& scene, ObjectId object)
    : scene_(scene), object_(object) {}

bool DeleteObjectCommand::execute() {
    snapshot_ = scene_.removeSubtree(object_);
    if (snapshot_.empty()) return false;

    removedResources_.clear();
    std::vector<ResourceId> candidates;
    for (const auto& object : snapshot_) {
        if (!object.meshResource ||
            std::find(candidates.cbegin(), candidates.cend(), *object.meshResource) != candidates.cend()) {
            continue;
        }
        candidates.push_back(*object.meshResource);
    }

    for (const auto resourceId : candidates) {
        const auto* resource = scene_.findMeshResource(resourceId);
        if (!resource) continue;
        const MeshResource copy = *resource;
        if (scene_.removeMeshResource(resourceId)) removedResources_.push_back(copy);
    }
    return true;
}

bool DeleteObjectCommand::undo() {
    std::vector<ResourceId> inserted;
    inserted.reserve(removedResources_.size());
    for (const auto& resource : removedResources_) {
        if (!scene_.insertMeshResource(resource)) {
            for (const auto id : inserted) (void)scene_.removeMeshResource(id);
            return false;
        }
        inserted.push_back(resource.id);
    }
    if (scene_.restoreObjects(snapshot_)) return true;
    for (const auto id : inserted) (void)scene_.removeMeshResource(id);
    return false;
}

DuplicateObjectsCommand::DuplicateObjectsCommand(Scene& scene, std::vector<ObjectId> objects)
    : scene_(scene), sources_(std::move(objects)) {}

bool DuplicateObjectsCommand::initialize() {
    if (sources_.empty()) return false;

    std::unordered_set<ObjectId, ObjectIdHash> uniqueSources;
    std::unordered_map<ObjectId, ObjectId, ObjectIdHash> objectMap;
    for (const auto sourceId : sources_) {
        if (!scene_.contains(sourceId) || !uniqueSources.insert(sourceId).second) return false;
        ObjectId duplicateId;
        do {
            duplicateId = ObjectId::generate();
        } while (scene_.contains(duplicateId) ||
                 std::any_of(mappings_.cbegin(), mappings_.cend(),
                             [duplicateId](const DuplicateObjectMapping& mapping) {
                                 return mapping.duplicate == duplicateId;
                             }));
        mappings_.push_back({sourceId, duplicateId});
        objectMap.emplace(sourceId, duplicateId);
    }

    std::unordered_map<ResourceId, ResourceId, ResourceIdHash> resourceMap;
    for (const auto sourceId : sources_) {
        const auto* sourceObject = scene_.find(sourceId);
        if (!sourceObject) return false;
        SceneObject duplicate = *sourceObject;
        duplicate.id = objectMap.at(sourceId);
        duplicate.name += " Copy";
        if (duplicate.parent) {
            const auto parentDuplicate = objectMap.find(*duplicate.parent);
            if (parentDuplicate != objectMap.end()) duplicate.parent = parentDuplicate->second;
        }

        if (duplicate.meshResource) {
            const ResourceId sourceResourceId = *duplicate.meshResource;
            auto mappedResource = resourceMap.find(sourceResourceId);
            if (mappedResource == resourceMap.end()) {
                const auto* sourceResource = scene_.findMeshResource(sourceResourceId);
                if (!sourceResource) return false;
                MeshResource copiedResource = *sourceResource;
                ResourceId duplicateResourceId;
                do {
                    duplicateResourceId = ResourceId::generate();
                } while (scene_.containsResource(duplicateResourceId) ||
                         std::any_of(resources_.cbegin(), resources_.cend(),
                                     [duplicateResourceId](const MeshResource& resource) {
                                         return resource.id == duplicateResourceId;
                                     }));
                copiedResource.id = duplicateResourceId;
                copiedResource.name += " Copy";
                std::string validationError;
                if (!copiedResource.validate(&validationError)) return false;
                resources_.push_back(std::move(copiedResource));
                mappedResource = resourceMap.emplace(sourceResourceId, duplicateResourceId).first;
            }
            duplicate.meshResource = mappedResource->second;
        }
        objects_.push_back(std::move(duplicate));
    }

    initialized_ = true;
    return true;
}

bool DuplicateObjectsCommand::insertPrepared() {
    std::vector<ResourceId> insertedResources;
    insertedResources.reserve(resources_.size());
    for (const auto& resource : resources_) {
        if (!scene_.insertMeshResource(resource)) {
            rollbackInserted();
            return false;
        }
        insertedResources.push_back(resource.id);
    }

    std::vector<bool> inserted(objects_.size(), false);
    std::size_t insertedCount = 0;
    while (insertedCount < objects_.size()) {
        bool progressed = false;
        for (std::size_t index = 0; index < objects_.size(); ++index) {
            if (inserted[index]) continue;
            const auto& object = objects_[index];
            if (object.parent && !scene_.contains(*object.parent)) continue;
            if (!scene_.insertObject(object)) {
                rollbackInserted();
                return false;
            }
            inserted[index] = true;
            ++insertedCount;
            progressed = true;
        }
        if (!progressed) {
            rollbackInserted();
            return false;
        }
    }
    return true;
}

void DuplicateObjectsCommand::rollbackInserted() noexcept {
    for (const auto& object : objects_) {
        if (scene_.contains(object.id)) (void)scene_.removeSubtree(object.id);
    }
    for (const auto& resource : resources_) {
        if (scene_.containsResource(resource.id)) (void)scene_.removeMeshResource(resource.id);
    }
}

bool DuplicateObjectsCommand::execute() {
    if (!initialized_ && !initialize()) return false;
    return insertPrepared();
}

bool DuplicateObjectsCommand::undo() {
    bool success = true;
    for (const auto& object : objects_) {
        if (!scene_.contains(object.id)) continue;
        const auto removed = scene_.removeSubtree(object.id);
        success = !removed.empty() && success;
    }
    for (const auto& resource : resources_) {
        if (!scene_.containsResource(resource.id)) continue;
        success = scene_.removeMeshResource(resource.id) && success;
    }
    return success;
}

SetObjectVisibilityCommand::SetObjectVisibilityCommand(Scene& scene, ObjectId object, bool visible)
    : scene_(scene), object_(object), visible_(visible) {}

bool SetObjectVisibilityCommand::execute() {
    const auto* object = scene_.find(object_);
    if (!object) return false;
    if (!captured_) {
        previous_ = object->visible;
        captured_ = true;
    }
    return scene_.setVisible(object_, visible_);
}

bool SetObjectVisibilityCommand::undo() {
    return captured_ && scene_.setVisible(object_, previous_);
}

SetObjectLockedCommand::SetObjectLockedCommand(Scene& scene, ObjectId object, bool locked)
    : scene_(scene), object_(object), locked_(locked) {}

bool SetObjectLockedCommand::execute() {
    const auto* object = scene_.find(object_);
    if (!object) return false;
    if (!captured_) {
        previous_ = object->locked;
        captured_ = true;
    }
    return scene_.setLocked(object_, locked_);
}

bool SetObjectLockedCommand::undo() {
    return captured_ && scene_.setLocked(object_, previous_);
}

RenameObjectCommand::RenameObjectCommand(Scene& scene, ObjectId object, std::string newName)
    : scene_(scene), object_(object), newName_(std::move(newName)) {}

bool RenameObjectCommand::execute() {
    const auto* object = scene_.find(object_);
    if (!object || newName_.empty()) return false;
    if (!captured_) {
        oldName_ = object->name;
        captured_ = true;
    }
    return scene_.rename(object_, newName_);
}

bool RenameObjectCommand::undo() {
    return captured_ && scene_.rename(object_, oldName_);
}

TransformObjectCommand::TransformObjectCommand(Scene& scene, ObjectId object, Transform newTransform)
    : scene_(scene), object_(object), newTransform_(newTransform) {}

bool TransformObjectCommand::execute() {
    const auto* object = scene_.find(object_);
    if (!object) return false;
    if (!captured_) {
        oldTransform_ = object->localTransform;
        captured_ = true;
    }
    return scene_.setTransform(object_, newTransform_);
}

bool TransformObjectCommand::undo() {
    return captured_ && scene_.setTransform(object_, oldTransform_);
}

TransformObjectsCommand::TransformObjectsCommand(Scene& scene,
                                                     std::vector<TransformChange> changes,
                                                     std::string commandName)
    : scene_(scene), changes_(std::move(changes)), commandName_(std::move(commandName)) {
    if (commandName_.empty()) commandName_ = "Transform Objects";
}

bool TransformObjectsCommand::execute() {
    if (changes_.empty()) return false;
    for (std::size_t index = 0; index < changes_.size(); ++index) {
        if (!scene_.contains(changes_[index].object)) return false;
        for (std::size_t other = index + 1; other < changes_.size(); ++other) {
            if (changes_[index].object == changes_[other].object) return false;
        }
    }
    std::size_t applied = 0;
    for (; applied < changes_.size(); ++applied) {
        if (!scene_.setTransform(changes_[applied].object, changes_[applied].after)) break;
    }
    if (applied == changes_.size()) return true;
    while (applied > 0) {
        --applied;
        (void)scene_.setTransform(changes_[applied].object, changes_[applied].before);
    }
    return false;
}

bool TransformObjectsCommand::undo() {
    for (const auto& change : changes_) {
        if (!scene_.contains(change.object)) return false;
    }
    std::size_t applied = 0;
    for (; applied < changes_.size(); ++applied) {
        if (!scene_.setTransform(changes_[applied].object, changes_[applied].before)) break;
    }
    if (applied == changes_.size()) return true;
    while (applied > 0) {
        --applied;
        (void)scene_.setTransform(changes_[applied].object, changes_[applied].after);
    }
    return false;
}

ReparentObjectCommand::ReparentObjectCommand(Scene& scene, ObjectId object,
                                             std::optional<ObjectId> newParent)
    : scene_(scene), object_(object), newParent_(newParent) {}

bool ReparentObjectCommand::execute() {
    const auto* object = scene_.find(object_);
    if (!object) return false;
    if (!captured_) {
        oldParent_ = object->parent;
        captured_ = true;
    }
    return scene_.reparent(object_, newParent_);
}

bool ReparentObjectCommand::undo() {
    return captured_ && scene_.reparent(object_, oldParent_);
}

} // namespace m3d
