#include "mobile3d/core/commands/organization_commands.hpp"

#include <algorithm>
#include <utility>

namespace m3d {

CreateCollectionCommand::CreateCollectionCommand(Scene& scene, std::string name)
    : scene_(scene) { collection_.name = std::move(name); }

bool CreateCollectionCommand::execute() {
    if (collection_.name.empty()) return false;
    if (!initialized_) {
        do { collection_.id = CollectionId::generate(); } while (scene_.containsCollection(collection_.id));
        initialized_ = true;
    }
    return scene_.insertCollection(collection_);
}

bool CreateCollectionCommand::undo() { return scene_.removeCollection(collection_.id); }

DeleteCollectionCommand::DeleteCollectionCommand(Scene& scene, CollectionId collection)
    : scene_(scene), collection_(collection) {}

bool DeleteCollectionCommand::execute() {
    const auto* value = scene_.findCollection(collection_);
    if (!value) return false;
    if (!captured_) {
        snapshot_ = *value;
        for (const auto& layer : scene_.layers()) {
            if (std::find(layer.collections.cbegin(), layer.collections.cend(), collection_) != layer.collections.cend()) {
                linkedLayers_.push_back(layer.id);
            }
        }
        captured_ = true;
    }
    return scene_.removeCollection(collection_);
}

bool DeleteCollectionCommand::undo() {
    if (!captured_ || !scene_.insertCollection(snapshot_)) return false;
    for (const auto layer : linkedLayers_) {
        if (!scene_.containsLayer(layer)) continue;
        if (!scene_.addCollectionToLayer(layer, collection_)) {
            (void)scene_.removeCollection(collection_);
            return false;
        }
    }
    return true;
}

RenameCollectionCommand::RenameCollectionCommand(Scene& scene, CollectionId collection, std::string value)
    : scene_(scene), collection_(collection), after_(std::move(value)) {}

bool RenameCollectionCommand::execute() {
    const auto* value = scene_.findCollection(collection_);
    if (!value || after_.empty()) return false;
    if (!captured_) { before_ = value->name; captured_ = true; }
    return scene_.renameCollection(collection_, after_);
}

bool RenameCollectionCommand::undo() { return captured_ && scene_.renameCollection(collection_, before_); }

SetCollectionVisibilityCommand::SetCollectionVisibilityCommand(Scene& scene, CollectionId collection, bool visible)
    : scene_(scene), collection_(collection), after_(visible) {}

bool SetCollectionVisibilityCommand::execute() {
    const auto* value = scene_.findCollection(collection_);
    if (!value) return false;
    if (!captured_) { before_ = value->visible; captured_ = true; }
    return scene_.setCollectionVisible(collection_, after_);
}

bool SetCollectionVisibilityCommand::undo() {
    return captured_ && scene_.setCollectionVisible(collection_, before_);
}

SetCollectionLockedCommand::SetCollectionLockedCommand(Scene& scene, CollectionId collection, bool locked)
    : scene_(scene), collection_(collection), after_(locked) {}

bool SetCollectionLockedCommand::execute() {
    const auto* value = scene_.findCollection(collection_);
    if (!value) return false;
    if (!captured_) { before_ = value->locked; captured_ = true; }
    return scene_.setCollectionLocked(collection_, after_);
}

bool SetCollectionLockedCommand::undo() { return captured_ && scene_.setCollectionLocked(collection_, before_); }

AddObjectToCollectionCommand::AddObjectToCollectionCommand(Scene& scene, CollectionId collection, ObjectId object)
    : scene_(scene), collection_(collection), object_(object) {}

bool AddObjectToCollectionCommand::execute() { return scene_.addObjectToCollection(collection_, object_); }
bool AddObjectToCollectionCommand::undo() { return scene_.removeObjectFromCollection(collection_, object_); }

RemoveObjectFromCollectionCommand::RemoveObjectFromCollectionCommand(Scene& scene, CollectionId collection, ObjectId object)
    : scene_(scene), collection_(collection), object_(object) {}

bool RemoveObjectFromCollectionCommand::execute() { return scene_.removeObjectFromCollection(collection_, object_); }
bool RemoveObjectFromCollectionCommand::undo() { return scene_.addObjectToCollection(collection_, object_); }

CreateLayerCommand::CreateLayerCommand(Scene& scene, std::string name)
    : scene_(scene) { layer_.name = std::move(name); }

bool CreateLayerCommand::execute() {
    if (layer_.name.empty()) return false;
    if (!initialized_) {
        do { layer_.id = LayerId::generate(); } while (scene_.containsLayer(layer_.id));
        initialized_ = true;
    }
    return scene_.insertLayer(layer_);
}

bool CreateLayerCommand::undo() { return scene_.removeLayer(layer_.id); }

DeleteLayerCommand::DeleteLayerCommand(Scene& scene, LayerId layer)
    : scene_(scene), layer_(layer) {}

bool DeleteLayerCommand::execute() {
    const auto* value = scene_.findLayer(layer_);
    if (!value) return false;
    if (!captured_) { snapshot_ = *value; captured_ = true; }
    return scene_.removeLayer(layer_);
}

bool DeleteLayerCommand::undo() { return captured_ && scene_.insertLayer(snapshot_); }

RenameLayerCommand::RenameLayerCommand(Scene& scene, LayerId layer, std::string value)
    : scene_(scene), layer_(layer), after_(std::move(value)) {}

bool RenameLayerCommand::execute() {
    const auto* value = scene_.findLayer(layer_);
    if (!value || after_.empty()) return false;
    if (!captured_) { before_ = value->name; captured_ = true; }
    return scene_.renameLayer(layer_, after_);
}

bool RenameLayerCommand::undo() { return captured_ && scene_.renameLayer(layer_, before_); }

SetLayerEnabledCommand::SetLayerEnabledCommand(Scene& scene, LayerId layer, bool enabled)
    : scene_(scene), layer_(layer), after_(enabled) {}

bool SetLayerEnabledCommand::execute() {
    const auto* value = scene_.findLayer(layer_);
    if (!value) return false;
    if (!captured_) { before_ = value->enabled; captured_ = true; }
    return scene_.setLayerEnabled(layer_, after_);
}

bool SetLayerEnabledCommand::undo() { return captured_ && scene_.setLayerEnabled(layer_, before_); }

AddCollectionToLayerCommand::AddCollectionToLayerCommand(Scene& scene, LayerId layer, CollectionId collection)
    : scene_(scene), layer_(layer), collection_(collection) {}

bool AddCollectionToLayerCommand::execute() { return scene_.addCollectionToLayer(layer_, collection_); }
bool AddCollectionToLayerCommand::undo() { return scene_.removeCollectionFromLayer(layer_, collection_); }

RemoveCollectionFromLayerCommand::RemoveCollectionFromLayerCommand(Scene& scene, LayerId layer, CollectionId collection)
    : scene_(scene), layer_(layer), collection_(collection) {}

bool RemoveCollectionFromLayerCommand::execute() { return scene_.removeCollectionFromLayer(layer_, collection_); }
bool RemoveCollectionFromLayerCommand::undo() { return scene_.addCollectionToLayer(layer_, collection_); }

} // namespace m3d
