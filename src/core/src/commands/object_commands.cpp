#include "mobile3d/core/commands/object_commands.hpp"

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
    return !snapshot_.empty();
}

bool DeleteObjectCommand::undo() {
    return scene_.restoreObjects(snapshot_);
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
