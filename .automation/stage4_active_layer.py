from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text()


def write(path: str, content: str) -> None:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content)


def replace_once(path: str, old: str, new: str) -> None:
    content = read(path)
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {count}: {old[:80]!r}")
    write(path, content.replace(old, new, 1))


def append_once(path: str, marker: str, content: str) -> None:
    current = read(path)
    if marker in current:
        return
    write(path, current.rstrip() + "\n\n" + content.strip() + "\n")


# ---------------------------------------------------------------------------
# Core organization commands: every persistent scene-organization mutation
# participates in the same deterministic Undo/Redo system as object edits.
# ---------------------------------------------------------------------------
write('src/core/include/mobile3d/core/commands/organization_commands.hpp', r'''#pragma once

#include "mobile3d/core/command.hpp"
#include "mobile3d/core/scene.hpp"

#include <string>
#include <vector>

namespace m3d {

class CreateCollectionCommand final : public EditorCommand {
public:
    CreateCollectionCommand(Scene& scene, std::string name);
    [[nodiscard]] std::string_view name() const noexcept override { return "Create Collection"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
    [[nodiscard]] CollectionId createdId() const noexcept { return collection_.id; }
private:
    Scene& scene_;
    SceneCollection collection_;
    bool initialized_{false};
};

class DeleteCollectionCommand final : public EditorCommand {
public:
    DeleteCollectionCommand(Scene& scene, CollectionId collection);
    [[nodiscard]] std::string_view name() const noexcept override { return "Delete Collection"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    CollectionId collection_{};
    SceneCollection snapshot_{};
    std::vector<LayerId> linkedLayers_;
    bool captured_{false};
};

class RenameCollectionCommand final : public EditorCommand {
public:
    RenameCollectionCommand(Scene& scene, CollectionId collection, std::string value);
    [[nodiscard]] std::string_view name() const noexcept override { return "Rename Collection"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    CollectionId collection_{};
    std::string before_;
    std::string after_;
    bool captured_{false};
};

class SetCollectionVisibilityCommand final : public EditorCommand {
public:
    SetCollectionVisibilityCommand(Scene& scene, CollectionId collection, bool visible);
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Collection Visibility"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    CollectionId collection_{};
    bool before_{true};
    bool after_{true};
    bool captured_{false};
};

class SetCollectionLockedCommand final : public EditorCommand {
public:
    SetCollectionLockedCommand(Scene& scene, CollectionId collection, bool locked);
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Collection Lock"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    CollectionId collection_{};
    bool before_{false};
    bool after_{false};
    bool captured_{false};
};

class AddObjectToCollectionCommand final : public EditorCommand {
public:
    AddObjectToCollectionCommand(Scene& scene, CollectionId collection, ObjectId object);
    [[nodiscard]] std::string_view name() const noexcept override { return "Add Object to Collection"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    CollectionId collection_{};
    ObjectId object_{};
};

class RemoveObjectFromCollectionCommand final : public EditorCommand {
public:
    RemoveObjectFromCollectionCommand(Scene& scene, CollectionId collection, ObjectId object);
    [[nodiscard]] std::string_view name() const noexcept override { return "Remove Object from Collection"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    CollectionId collection_{};
    ObjectId object_{};
};

class CreateLayerCommand final : public EditorCommand {
public:
    CreateLayerCommand(Scene& scene, std::string name);
    [[nodiscard]] std::string_view name() const noexcept override { return "Create Layer"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
    [[nodiscard]] LayerId createdId() const noexcept { return layer_.id; }
private:
    Scene& scene_;
    SceneLayer layer_;
    bool initialized_{false};
};

class DeleteLayerCommand final : public EditorCommand {
public:
    DeleteLayerCommand(Scene& scene, LayerId layer);
    [[nodiscard]] std::string_view name() const noexcept override { return "Delete Layer"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    LayerId layer_{};
    SceneLayer snapshot_{};
    bool captured_{false};
};

class RenameLayerCommand final : public EditorCommand {
public:
    RenameLayerCommand(Scene& scene, LayerId layer, std::string value);
    [[nodiscard]] std::string_view name() const noexcept override { return "Rename Layer"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    LayerId layer_{};
    std::string before_;
    std::string after_;
    bool captured_{false};
};

class SetLayerEnabledCommand final : public EditorCommand {
public:
    SetLayerEnabledCommand(Scene& scene, LayerId layer, bool enabled);
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Layer Enabled"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    LayerId layer_{};
    bool before_{true};
    bool after_{true};
    bool captured_{false};
};

class AddCollectionToLayerCommand final : public EditorCommand {
public:
    AddCollectionToLayerCommand(Scene& scene, LayerId layer, CollectionId collection);
    [[nodiscard]] std::string_view name() const noexcept override { return "Add Collection to Layer"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    LayerId layer_{};
    CollectionId collection_{};
};

class RemoveCollectionFromLayerCommand final : public EditorCommand {
public:
    RemoveCollectionFromLayerCommand(Scene& scene, LayerId layer, CollectionId collection);
    [[nodiscard]] std::string_view name() const noexcept override { return "Remove Collection from Layer"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    LayerId layer_{};
    CollectionId collection_{};
};

} // namespace m3d
''')

write('src/core/src/commands/organization_commands.cpp', r'''#include "mobile3d/core/commands/organization_commands.hpp"

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
''')

replace_once('src/core/CMakeLists.txt',
'''    src/commands/object_commands.cpp\n''',
'''    src/commands/object_commands.cpp\n    src/commands/organization_commands.cpp\n''')

# ---------------------------------------------------------------------------
# EditorSession: active view layer is editor state; persistent organization
# changes are commands. Effective organization locks gate destructive edits.
# ---------------------------------------------------------------------------
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''#include "mobile3d/core/commands/object_commands.hpp"\n''',
'''#include "mobile3d/core/commands/object_commands.hpp"\n#include "mobile3d/core/commands/organization_commands.hpp"\n''')
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] const SelectionModel& selection() const noexcept { return selection_; }\n\n    [[nodiscard]] bool undo();\n''',
'''    [[nodiscard]] const SelectionModel& selection() const noexcept { return selection_; }\n\n    [[nodiscard]] std::optional<LayerId> activeLayer() const noexcept { return activeLayer_; }\n    [[nodiscard]] bool setActiveLayer(std::optional<LayerId> layer);\n\n    [[nodiscard]] std::optional<CollectionId> createCollection(std::string name);\n    [[nodiscard]] bool deleteCollection(CollectionId collection);\n    [[nodiscard]] bool renameCollection(CollectionId collection, std::string name);\n    [[nodiscard]] bool setCollectionVisible(CollectionId collection, bool visible);\n    [[nodiscard]] bool setCollectionLocked(CollectionId collection, bool locked);\n    [[nodiscard]] bool addSelectionToCollection(CollectionId collection);\n    [[nodiscard]] bool removeObjectFromCollection(CollectionId collection, ObjectId object);\n\n    [[nodiscard]] std::optional<LayerId> createLayer(std::string name);\n    [[nodiscard]] bool deleteLayer(LayerId layer);\n    [[nodiscard]] bool renameLayer(LayerId layer, std::string name);\n    [[nodiscard]] bool setLayerEnabled(LayerId layer, bool enabled);\n    [[nodiscard]] bool addCollectionToLayer(LayerId layer, CollectionId collection);\n    [[nodiscard]] bool removeCollectionFromLayer(LayerId layer, CollectionId collection);\n\n    [[nodiscard]] bool undo();\n''')
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool transformTransactionHasChanges() const noexcept;\n    void resetForDocument(bool recoveredDirty) noexcept;\n    void sceneMutated(bool pruneSelection = true);\n''',
'''    [[nodiscard]] bool transformTransactionHasChanges() const noexcept;\n    [[nodiscard]] bool objectLockedByActiveLayer(ObjectId object) const noexcept;\n    void pruneSelectionForActiveLayer();\n    void resetForDocument(bool recoveredDirty) noexcept;\n    void sceneMutated(bool pruneSelection = true);\n''')
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    SelectionModel selection_;\n    std::optional<TransformTransactionState> transformTransaction_;\n''',
'''    SelectionModel selection_;\n    std::optional<LayerId> activeLayer_;\n    std::optional<TransformTransactionState> transformTransaction_;\n''')

replace_once('src/editor/src/editor_session.cpp',
'''void EditorSession::closeProject() noexcept {\n    transformTransaction_.reset();\n''',
'''void EditorSession::closeProject() noexcept {\n    transformTransaction_.reset();\n    activeLayer_.reset();\n''')
replace_once('src/editor/src/editor_session.cpp',
'''    selection_.clear();\n    recoveredDirty_ = true;\n''',
'''    selection_.clear();\n    activeLayer_.reset();\n    recoveredDirty_ = true;\n''')
replace_once('src/editor/src/editor_session.cpp',
'''bool EditorSession::isDirty() const noexcept {\n    return recoveredDirty_ || commands_.isDirty() || transformTransactionHasChanges();\n}\n''',
'''bool EditorSession::isDirty() const noexcept {\n    return recoveredDirty_ || commands_.isDirty() || transformTransactionHasChanges();\n}\n\nbool EditorSession::setActiveLayer(std::optional<LayerId> layer) {\n    if (!document_ || transformTransaction_) return false;\n    if (layer && !document_->scene.containsLayer(*layer)) return false;\n    if (activeLayer_ == layer) return true;\n    activeLayer_ = layer;\n    pruneSelectionForActiveLayer();\n    ++sceneRevision_;\n    ++uiRevision_;\n    return true;\n}\n''')

# Effective lock checks for destructive/editing operations. Authored visibility
# and lock toggles themselves remain editable so users can recover state.
s = read('src/editor/src/editor_session.cpp')
s = s.replace('if (!target || target->locked) return false;',
              'if (!target || objectLockedByActiveLayer(object)) return false;')
s = s.replace('if (!object || object->locked) return false;',
              'if (!object || objectLockedByActiveLayer(id)) return false;', 2)
s = s.replace('if (!current || current->locked) return false;',
              'if (!current || objectLockedByActiveLayer(object)) return false;', 3)
s = s.replace('if (!object || object->locked) return false;',
              'if (!object || objectLockedByActiveLayer(objectId)) return false;', 1)
write('src/editor/src/editor_session.cpp', s)

# Organization methods live before undo/redo methods.
replace_once('src/editor/src/editor_session.cpp',
'''bool EditorSession::undo() {\n''',
r'''std::optional<CollectionId> EditorSession::createCollection(std::string name) {
    if (!document_ || transformTransaction_ || name.empty()) return std::nullopt;
    auto command = std::make_unique<CreateCollectionCommand>(document_->scene, std::move(name));
    auto* created = command.get();
    if (!commands_.execute(std::move(command))) return std::nullopt;
    const auto id = created->createdId();
    sceneMutated(false);
    return id;
}

bool EditorSession::deleteCollection(CollectionId collection) {
    if (!document_ || transformTransaction_ || !document_->scene.containsCollection(collection)) return false;
    if (!commands_.execute(std::make_unique<DeleteCollectionCommand>(document_->scene, collection))) return false;
    pruneSelectionForActiveLayer();
    sceneMutated(false);
    return true;
}

bool EditorSession::renameCollection(CollectionId collection, std::string name) {
    if (!document_ || transformTransaction_ || name.empty()) return false;
    if (!commands_.execute(std::make_unique<RenameCollectionCommand>(document_->scene, collection, std::move(name)))) return false;
    sceneMutated(false);
    return true;
}

bool EditorSession::setCollectionVisible(CollectionId collection, bool visible) {
    if (!document_ || transformTransaction_) return false;
    const auto* value = document_->scene.findCollection(collection);
    if (!value || value->visible == visible) return false;
    if (!commands_.execute(std::make_unique<SetCollectionVisibilityCommand>(document_->scene, collection, visible))) return false;
    pruneSelectionForActiveLayer();
    sceneMutated(false);
    return true;
}

bool EditorSession::setCollectionLocked(CollectionId collection, bool locked) {
    if (!document_ || transformTransaction_) return false;
    const auto* value = document_->scene.findCollection(collection);
    if (!value || value->locked == locked) return false;
    if (!commands_.execute(std::make_unique<SetCollectionLockedCommand>(document_->scene, collection, locked))) return false;
    sceneMutated(false);
    return true;
}

bool EditorSession::addSelectionToCollection(CollectionId collection) {
    if (!document_ || transformTransaction_ || selection_.empty()) return false;
    const auto* value = document_->scene.findCollection(collection);
    if (!value) return false;
    auto transaction = std::make_unique<CompositeCommand>("Add Selection to Collection");
    std::size_t added = 0;
    for (const auto object : selection_.selected()) {
        if (std::find(value->objects.cbegin(), value->objects.cend(), object) != value->objects.cend()) continue;
        transaction->add(std::make_unique<AddObjectToCollectionCommand>(document_->scene, collection, object));
        ++added;
    }
    if (added == 0) return false;
    if (!commands_.execute(std::move(transaction))) return false;
    sceneMutated(false);
    return true;
}

bool EditorSession::removeObjectFromCollection(CollectionId collection, ObjectId object) {
    if (!document_ || transformTransaction_) return false;
    if (!commands_.execute(std::make_unique<RemoveObjectFromCollectionCommand>(document_->scene, collection, object))) return false;
    sceneMutated(false);
    return true;
}

std::optional<LayerId> EditorSession::createLayer(std::string name) {
    if (!document_ || transformTransaction_ || name.empty()) return std::nullopt;
    auto command = std::make_unique<CreateLayerCommand>(document_->scene, std::move(name));
    auto* created = command.get();
    if (!commands_.execute(std::move(command))) return std::nullopt;
    const auto id = created->createdId();
    sceneMutated(false);
    return id;
}

bool EditorSession::deleteLayer(LayerId layer) {
    if (!document_ || transformTransaction_ || !document_->scene.containsLayer(layer)) return false;
    if (!commands_.execute(std::make_unique<DeleteLayerCommand>(document_->scene, layer))) return false;
    if (activeLayer_ && *activeLayer_ == layer) activeLayer_.reset();
    pruneSelectionForActiveLayer();
    sceneMutated(false);
    return true;
}

bool EditorSession::renameLayer(LayerId layer, std::string name) {
    if (!document_ || transformTransaction_ || name.empty()) return false;
    if (!commands_.execute(std::make_unique<RenameLayerCommand>(document_->scene, layer, std::move(name)))) return false;
    sceneMutated(false);
    return true;
}

bool EditorSession::setLayerEnabled(LayerId layer, bool enabled) {
    if (!document_ || transformTransaction_) return false;
    const auto* value = document_->scene.findLayer(layer);
    if (!value || value->enabled == enabled) return false;
    if (!commands_.execute(std::make_unique<SetLayerEnabledCommand>(document_->scene, layer, enabled))) return false;
    pruneSelectionForActiveLayer();
    sceneMutated(false);
    return true;
}

bool EditorSession::addCollectionToLayer(LayerId layer, CollectionId collection) {
    if (!document_ || transformTransaction_) return false;
    if (!commands_.execute(std::make_unique<AddCollectionToLayerCommand>(document_->scene, layer, collection))) return false;
    pruneSelectionForActiveLayer();
    sceneMutated(false);
    return true;
}

bool EditorSession::removeCollectionFromLayer(LayerId layer, CollectionId collection) {
    if (!document_ || transformTransaction_) return false;
    if (!commands_.execute(std::make_unique<RemoveCollectionFromLayerCommand>(document_->scene, layer, collection))) return false;
    pruneSelectionForActiveLayer();
    sceneMutated(false);
    return true;
}

bool EditorSession::undo() {
''')

replace_once('src/editor/src/editor_session.cpp',
'''bool EditorSession::transformTransactionHasChanges() const noexcept {\n''',
r'''bool EditorSession::objectLockedByActiveLayer(ObjectId object) const noexcept {
    return !document_ || document_->scene.isObjectLockedByOrganization(object, activeLayer_);
}

void EditorSession::pruneSelectionForActiveLayer() {
    if (!document_ || selection_.empty()) return;
    const auto selected = selection_.selected();
    bool changed = false;
    for (const auto object : selected) {
        if (document_->scene.isObjectVisibleInLayer(object, activeLayer_)) continue;
        changed = selection_.remove(object) || changed;
    }
    if (changed) ++selectionRevision_;
}

bool EditorSession::transformTransactionHasChanges() const noexcept {
''')
replace_once('src/editor/src/editor_session.cpp',
'''void EditorSession::resetForDocument(bool recoveredDirty) noexcept {\n    transformTransaction_.reset();\n''',
'''void EditorSession::resetForDocument(bool recoveredDirty) noexcept {\n    transformTransaction_.reset();\n    activeLayer_.reset();\n''')

# ---------------------------------------------------------------------------
# Render snapshot and Outliner consume effective organization state.
# ---------------------------------------------------------------------------
replace_once('src/render/include/mobile3d/render/render_snapshot.hpp',
'''                                                   std::uint64_t sceneRevision,\n                                                   std::uint64_t selectionRevision);\n''',
'''                                                   std::uint64_t sceneRevision,\n                                                   std::uint64_t selectionRevision,\n                                                   std::optional<LayerId> activeLayer = std::nullopt);\n''')
replace_once('src/render/src/render_snapshot.cpp',
'''                                                 std::uint64_t sceneRevision,\n                                                 std::uint64_t selectionRevision) {\n''',
'''                                                 std::uint64_t sceneRevision,\n                                                 std::uint64_t selectionRevision,\n                                                 std::optional<LayerId> activeLayer) {\n''')
replace_once('src/render/src/render_snapshot.cpp',
'''            .visible = object.visible,\n            .locked = object.locked,\n''',
'''            .visible = scene.isObjectVisibleInLayer(object.id, activeLayer),\n            .locked = scene.isObjectLockedByOrganization(object.id, activeLayer),\n''')
replace_once('src/app/qt/outliner_model.cpp',
'''    case VisibleRole:\n        return object->visible;\n    case LockedRole:\n        return object->locked;\n''',
'''    case VisibleRole:\n        return scene->isObjectVisibleInLayer(row.id, session_.activeLayer());\n    case LockedRole:\n        return scene->isObjectLockedByOrganization(row.id, session_.activeLayer());\n''')

# ---------------------------------------------------------------------------
# Qt bridge and touch-friendly organization controls.
# ---------------------------------------------------------------------------
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_PROPERTY(QString pivotMode READ pivotMode NOTIFY transformSettingsChanged)\n''',
'''    Q_PROPERTY(QString pivotMode READ pivotMode NOTIFY transformSettingsChanged)\n    Q_PROPERTY(QString activeLayerName READ activeLayerName NOTIFY layerChanged)\n    Q_PROPERTY(QStringList layerNames READ layerNames NOTIFY projectStateChanged)\n    Q_PROPERTY(QStringList collectionNames READ collectionNames NOTIFY projectStateChanged)\n''')
replace_once('src/app/qt/editor_controller.hpp',
'''    [[nodiscard]] QString pivotMode() const;\n''',
'''    [[nodiscard]] QString pivotMode() const;\n    [[nodiscard]] QString activeLayerName() const;\n    [[nodiscard]] QStringList layerNames() const;\n    [[nodiscard]] QStringList collectionNames() const;\n''')
replace_once('src/app/qt/editor_controller.hpp',
'''        return m3d::RenderSnapshotBuilder::build(*scene, session_.selection(),\n                                                 session_.sceneRevision(),\n                                                 session_.selectionRevision());\n''',
'''        return m3d::RenderSnapshotBuilder::build(*scene, session_.selection(),\n                                                 session_.sceneRevision(),\n                                                 session_.selectionRevision(),\n                                                 session_.activeLayer());\n''')
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_INVOKABLE bool setPivotMode(const QString& name);\n    Q_INVOKABLE void setTransformSnapEnabled(bool enabled);\n''',
'''    Q_INVOKABLE bool setPivotMode(const QString& name);\n    Q_INVOKABLE bool setActiveLayer(const QString& name);\n    Q_INVOKABLE bool createCollection();\n    Q_INVOKABLE bool createLayer();\n    Q_INVOKABLE bool addSelectionToCollection(const QString& collectionName);\n    Q_INVOKABLE bool addCollectionToLayer(const QString& collectionName, const QString& layerName);\n    Q_INVOKABLE bool toggleCollectionVisible(const QString& collectionName);\n    Q_INVOKABLE bool toggleCollectionLocked(const QString& collectionName);\n    Q_INVOKABLE void setTransformSnapEnabled(bool enabled);\n''')
replace_once('src/app/qt/editor_controller.hpp',
'''    void transformActivityChanged();\n''',
'''    void transformActivityChanged();\n    void layerChanged();\n''')

replace_once('src/app/qt/editor_controller.cpp',
'''m3d::TransformSnapSettings EditorController::transformSnapSettings() const noexcept {\n''',
r'''QString EditorController::activeLayerName() const {
    const auto active = session_.activeLayer();
    if (!active) return QStringLiteral("All");
    const auto* scene = session_.scene();
    const auto* layer = scene ? scene->findLayer(*active) : nullptr;
    return layer ? QString::fromStdString(layer->name) : QStringLiteral("All");
}

QStringList EditorController::layerNames() const {
    QStringList values{QStringLiteral("All")};
    const auto* scene = session_.scene();
    if (!scene) return values;
    for (const auto& layer : scene->layers()) values.push_back(QString::fromStdString(layer.name));
    if (values.size() > 2) std::sort(values.begin() + 1, values.end());
    return values;
}

QStringList EditorController::collectionNames() const {
    QStringList values;
    const auto* scene = session_.scene();
    if (!scene) return values;
    for (const auto& collection : scene->collections()) values.push_back(QString::fromStdString(collection.name));
    std::sort(values.begin(), values.end());
    return values;
}

m3d::TransformSnapSettings EditorController::transformSnapSettings() const noexcept {
''')

replace_once('src/app/qt/editor_controller.cpp',
'''void EditorController::setTransformSnapEnabled(bool enabled) {\n''',
r'''bool EditorController::setActiveLayer(const QString& name) {
    if (!session_.hasProject() || manipulator_.active()) return false;
    const QString cleaned = name.trimmed();
    if (cleaned.compare(QStringLiteral("All"), Qt::CaseInsensitive) == 0) {
        if (!session_.setActiveLayer(std::nullopt)) return false;
        setStatus(QStringLiteral("Layer: All"));
        outliner_->refresh();
        emit selectionChanged();
        emit layerChanged();
        return true;
    }
    const auto* scene = session_.scene();
    if (!scene) return false;
    for (const auto& layer : scene->layers()) {
        if (QString::fromStdString(layer.name).compare(cleaned, Qt::CaseInsensitive) != 0) continue;
        if (!session_.setActiveLayer(layer.id)) return false;
        setStatus(QStringLiteral("Layer: %1").arg(QString::fromStdString(layer.name)));
        outliner_->refresh();
        emit selectionChanged();
        emit layerChanged();
        return true;
    }
    return false;
}

bool EditorController::createCollection() {
    auto* scene = session_.scene();
    if (!scene || manipulator_.active()) return false;
    int suffix = static_cast<int>(scene->collectionCount()) + 1;
    std::string name;
    for (;;) {
        name = "Collection " + std::to_string(suffix++);
        const auto duplicate = std::any_of(scene->collections().cbegin(), scene->collections().cend(), [&name](const m3d::SceneCollection& item) {
            return item.name == name;
        });
        if (!duplicate) break;
    }
    if (!session_.createCollection(name)) return false;
    setStatus(QStringLiteral("Collection created."));
    refreshUi();
    return true;
}

bool EditorController::createLayer() {
    auto* scene = session_.scene();
    if (!scene || manipulator_.active()) return false;
    int suffix = static_cast<int>(scene->layerCount()) + 1;
    std::string name;
    for (;;) {
        name = "Layer " + std::to_string(suffix++);
        const auto duplicate = std::any_of(scene->layers().cbegin(), scene->layers().cend(), [&name](const m3d::SceneLayer& item) {
            return item.name == name;
        });
        if (!duplicate) break;
    }
    const auto created = session_.createLayer(name);
    if (!created) return false;
    (void)session_.setActiveLayer(*created);
    setStatus(QStringLiteral("Layer created."));
    refreshUi();
    emit layerChanged();
    return true;
}

bool EditorController::addSelectionToCollection(const QString& collectionName) {
    auto* scene = session_.scene();
    if (!scene) return false;
    for (const auto& collection : scene->collections()) {
        if (QString::fromStdString(collection.name) != collectionName) continue;
        if (!session_.addSelectionToCollection(collection.id)) return false;
        setStatus(QStringLiteral("Selection added to %1.").arg(collectionName));
        refreshUi();
        return true;
    }
    return false;
}

bool EditorController::addCollectionToLayer(const QString& collectionName, const QString& layerName) {
    auto* scene = session_.scene();
    if (!scene || layerName == QStringLiteral("All")) return false;
    std::optional<m3d::CollectionId> collectionId;
    std::optional<m3d::LayerId> layerId;
    for (const auto& collection : scene->collections()) {
        if (QString::fromStdString(collection.name) == collectionName) collectionId = collection.id;
    }
    for (const auto& layer : scene->layers()) {
        if (QString::fromStdString(layer.name) == layerName) layerId = layer.id;
    }
    if (!collectionId || !layerId || !session_.addCollectionToLayer(*layerId, *collectionId)) return false;
    setStatus(QStringLiteral("Collection linked to layer."));
    refreshUi();
    emit layerChanged();
    return true;
}

bool EditorController::toggleCollectionVisible(const QString& collectionName) {
    auto* scene = session_.scene();
    if (!scene) return false;
    for (const auto& collection : scene->collections()) {
        if (QString::fromStdString(collection.name) != collectionName) continue;
        if (!session_.setCollectionVisible(collection.id, !collection.visible)) return false;
        refreshUi();
        return true;
    }
    return false;
}

bool EditorController::toggleCollectionLocked(const QString& collectionName) {
    auto* scene = session_.scene();
    if (!scene) return false;
    for (const auto& collection : scene->collections()) {
        if (QString::fromStdString(collection.name) != collectionName) continue;
        if (!session_.setCollectionLocked(collection.id, !collection.locked)) return false;
        refreshUi();
        return true;
    }
    return false;
}

void EditorController::setTransformSnapEnabled(bool enabled) {
''')

# Add a compact active-layer switch to the transform toolbar.
replace_once('src/app/qml/ViewportPlaceholder.qml',
'''        Button {\n            height: 36\n            text: root.controller.transformSnapEnabled ? "Snap On" : "Snap Off"\n''',
'''        Button {\n            height: 36\n            text: "Layer: " + root.controller.activeLayerName\n            enabled: !root.controller.transformInProgress\n            onClicked: {\n                const names = root.controller.layerNames\n                const current = Math.max(0, names.indexOf(root.controller.activeLayerName))\n                root.controller.setActiveLayer(names[(current + 1) % names.length])\n            }\n        }\n        Button {\n            height: 36\n            text: root.controller.transformSnapEnabled ? "Snap On" : "Snap Off"\n''')

# Organization controls above the object list.
replace_once('src/app/qml/OutlinerPanel.qml',
'''        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#252a33" }\n\n        ListView {\n''',
r'''        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#252a33" }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: collectionBox
                    Layout.fillWidth: true
                    model: root.controller.collectionNames
                    displayText: currentText.length > 0 ? currentText : "Collections"
                }
                ToolButton { text: "+C"; onClicked: root.controller.createCollection() }
                ToolButton {
                    text: "Add"
                    enabled: collectionBox.currentText.length > 0
                    onClicked: root.controller.addSelectionToCollection(collectionBox.currentText)
                }
                ToolButton {
                    text: "◉"
                    enabled: collectionBox.currentText.length > 0
                    onClicked: root.controller.toggleCollectionVisible(collectionBox.currentText)
                }
                ToolButton {
                    text: "🔒"
                    enabled: collectionBox.currentText.length > 0
                    onClicked: root.controller.toggleCollectionLocked(collectionBox.currentText)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: layerBox
                    Layout.fillWidth: true
                    model: root.controller.layerNames
                    onActivated: root.controller.setActiveLayer(currentText)
                }
                ToolButton { text: "+L"; onClicked: root.controller.createLayer() }
                ToolButton {
                    text: "Link"
                    enabled: collectionBox.currentText.length > 0 && layerBox.currentText !== "All"
                    onClicked: root.controller.addCollectionToLayer(collectionBox.currentText, layerBox.currentText)
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#252a33" }

        ListView {
''')

# ---------------------------------------------------------------------------
# Tests.
# ---------------------------------------------------------------------------
write('tests/test_organization.cpp', r'''#include "test_harness.hpp"

#include "mobile3d/core/commands/organization_commands.hpp"

#include <algorithm>

TEST_CASE("collection commands preserve identity and memberships through undo redo") {
    m3d::Scene scene;
    const auto object = scene.createObject(m3d::ObjectType::Empty, "Object");

    m3d::CreateCollectionCommand create(scene, "Gameplay");
    REQUIRE(create.execute());
    const auto collection = create.createdId();
    REQUIRE(scene.containsCollection(collection));
    REQUIRE(create.undo());
    REQUIRE(!scene.containsCollection(collection));
    REQUIRE(create.execute());
    REQUIRE(scene.containsCollection(collection));

    m3d::AddObjectToCollectionCommand membership(scene, collection, object);
    REQUIRE(membership.execute());
    REQUIRE(scene.findCollection(collection)->objects == std::vector<m3d::ObjectId>{object});
    REQUIRE(membership.undo());
    REQUIRE(scene.findCollection(collection)->objects.empty());
    REQUIRE(membership.execute());

    m3d::CreateLayerCommand createLayer(scene, "Main");
    REQUIRE(createLayer.execute());
    const auto layer = createLayer.createdId();
    m3d::AddCollectionToLayerCommand link(scene, layer, collection);
    REQUIRE(link.execute());

    m3d::DeleteCollectionCommand remove(scene, collection);
    REQUIRE(remove.execute());
    REQUIRE(!scene.containsCollection(collection));
    REQUIRE(scene.findLayer(layer)->collections.empty());
    REQUIRE(remove.undo());
    REQUIRE(scene.containsCollection(collection));
    REQUIRE(scene.findCollection(collection)->objects == std::vector<m3d::ObjectId>{object});
    REQUIRE(scene.findLayer(layer)->collections == std::vector<m3d::CollectionId>{collection});
}

TEST_CASE("collection and layer state commands are fully reversible") {
    m3d::Scene scene;
    const auto collection = scene.createCollection("Collection");
    const auto layer = scene.createLayer("Layer");

    m3d::SetCollectionVisibilityCommand visibility(scene, collection, false);
    REQUIRE(visibility.execute());
    REQUIRE(!scene.findCollection(collection)->visible);
    REQUIRE(visibility.undo());
    REQUIRE(scene.findCollection(collection)->visible);

    m3d::SetCollectionLockedCommand lock(scene, collection, true);
    REQUIRE(lock.execute());
    REQUIRE(scene.findCollection(collection)->locked);
    REQUIRE(lock.undo());
    REQUIRE(!scene.findCollection(collection)->locked);

    m3d::SetLayerEnabledCommand enabled(scene, layer, false);
    REQUIRE(enabled.execute());
    REQUIRE(!scene.findLayer(layer)->enabled);
    REQUIRE(enabled.undo());
    REQUIRE(scene.findLayer(layer)->enabled);
}
''')
replace_once('tests/CMakeLists.txt',
'''    test_commands.cpp\n''',
'''    test_commands.cpp\n    test_organization.cpp\n''')

append_once('tests/test_render_snapshot.cpp',
            'render snapshot applies active layer organization state',
r'''TEST_CASE("render snapshot applies active layer organization state") {
    m3d::Scene scene;
    m3d::SelectionModel selection;
    const auto visible = scene.createObject(m3d::ObjectType::Empty, "Visible");
    const auto hidden = scene.createObject(m3d::ObjectType::Empty, "Hidden");
    const auto visibleCollection = scene.createCollection("Visible Collection");
    const auto hiddenCollection = scene.createCollection("Hidden Collection");
    const auto layer = scene.createLayer("Layer");
    REQUIRE(scene.addObjectToCollection(visibleCollection, visible));
    REQUIRE(scene.addObjectToCollection(hiddenCollection, hidden));
    REQUIRE(scene.addCollectionToLayer(layer, visibleCollection));
    REQUIRE(scene.setCollectionLocked(visibleCollection, true));

    const auto snapshot = m3d::RenderSnapshotBuilder::build(scene, selection, 1, 1, layer);
    REQUIRE(snapshot.find(visible) != nullptr);
    REQUIRE(snapshot.find(hidden) != nullptr);
    REQUIRE(snapshot.find(visible)->visible);
    REQUIRE(snapshot.find(visible)->locked);
    REQUIRE(!snapshot.find(hidden)->visible);
}
''')

append_once('tests/test_editor_session.cpp',
            'active layer gates organization locks and collection membership is undoable',
r'''TEST_CASE("active layer gates organization locks and collection membership is undoable") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Organization", &error));
    const auto object = session.createObject(m3d::ObjectType::Empty, "Object");
    REQUIRE(object.has_value());
    REQUIRE(session.select(*object, m3d::SelectionMode::Replace));

    const auto collection = session.createCollection("Locked");
    const auto layer = session.createLayer("Layer");
    REQUIRE(collection.has_value());
    REQUIRE(layer.has_value());
    REQUIRE(session.addSelectionToCollection(*collection));
    REQUIRE(session.addCollectionToLayer(*layer, *collection));
    REQUIRE(session.setCollectionLocked(*collection, true));
    REQUIRE(session.setActiveLayer(*layer));
    REQUIRE(!session.beginTransformTransaction({*object}, "Move"));

    REQUIRE(session.setCollectionLocked(*collection, false));
    REQUIRE(session.beginTransformTransaction({*object}, "Move"));
    REQUIRE(session.cancelTransformTransaction());

    REQUIRE(session.undo());
    REQUIRE(session.scene()->findCollection(*collection)->locked);
    REQUIRE(session.redo());
    REQUIRE(!session.scene()->findCollection(*collection)->locked);
}
''')

print('Stage 4 scene organization integration prepared successfully')
