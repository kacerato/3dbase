#include "mobile3d/editor/editor_session.hpp"

#include "mobile3d/core/commands/object_commands.hpp"
#include "mobile3d/core/composite_command.hpp"

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

namespace m3d {

std::string_view workspaceName(Workspace workspace) noexcept {
    switch (workspace) {
    case Workspace::Layout: return "Layout";
    case Workspace::Modeling: return "Modeling";
    case Workspace::Sculpt: return "Sculpt";
    case Workspace::UV: return "UV";
    case Workspace::Paint: return "Paint";
    case Workspace::Shading: return "Shading";
    case Workspace::Animation: return "Animation";
    case Workspace::Rigging: return "Rigging";
    case Workspace::Nodes: return "Nodes";
    case Workspace::Render: return "Render";
    }
    return "Layout";
}

bool EditorSession::createProject(const std::filesystem::path& root, std::string name,
                                  std::string* error) {
    auto created = ProjectRepository::create(root, std::move(name), error);
    if (!created) return false;
    document_ = std::move(*created);
    resetForDocument(false);
    return true;
}

bool EditorSession::openProject(const std::filesystem::path& root, std::string* error) {
    auto opened = ProjectRepository::open(root, error);
    if (!opened) return false;
    document_ = std::move(*opened);
    resetForDocument(false);
    return true;
}

void EditorSession::closeProject() noexcept {
    transformTransaction_.reset();
    document_.reset();
    commands_.clear();
    selection_.clear();
    recoveredDirty_ = false;
    ++sceneRevision_;
    ++selectionRevision_;
    ++documentRevision_;
}

bool EditorSession::saveProject(std::string* error) {
    if (!requireProject(error)) return false;
    if (transformTransaction_) {
        if (error) *error = "Cannot save during an active transform transaction";
        return false;
    }
    if (!ProjectRepository::save(*document_, error)) return false;
    commands_.markSaved();
    recoveredDirty_ = false;
    ++documentRevision_;
    if (ProjectRepository::hasAutosave(document_->root)) {
        std::string ignored;
        (void)ProjectRepository::clearAutosave(document_->root, &ignored);
    }
    return true;
}

bool EditorSession::writeAutosave(std::string* error) const {
    if (!requireProject(error)) return false;
    if (transformTransaction_) {
        if (error) *error = "Cannot autosave during an active transform transaction";
        return false;
    }
    return ProjectRepository::writeAutosave(*document_, error);
}

bool EditorSession::hasAutosave() const {
    return document_ && ProjectRepository::hasAutosave(document_->root);
}

bool EditorSession::recoverAutosave(std::string* error) {
    if (!requireProject(error)) return false;
    auto recovered = ProjectRepository::loadAutosave(document_->root, error);
    if (!recovered) return false;
    document_->scene = std::move(*recovered);
    commands_.clear();
    commands_.markSaved();
    selection_.clear();
    recoveredDirty_ = true;
    ++sceneRevision_;
    ++selectionRevision_;
    ++documentRevision_;
    return true;
}

bool EditorSession::discardAutosave(std::string* error) {
    if (!requireProject(error)) return false;
    return ProjectRepository::clearAutosave(document_->root, error);
}

bool EditorSession::isDirty() const noexcept {
    return recoveredDirty_ || commands_.isDirty() || transformTransactionHasChanges();
}

const ProjectDocument* EditorSession::document() const noexcept { return document_ ? &*document_ : nullptr; }
ProjectDocument* EditorSession::document() noexcept { return document_ ? &*document_ : nullptr; }
const Scene* EditorSession::scene() const noexcept { return document_ ? &document_->scene : nullptr; }
Scene* EditorSession::scene() noexcept { return document_ ? &document_->scene : nullptr; }

std::optional<ObjectId> EditorSession::createObject(ObjectType type, std::string name,
                                                     std::optional<ObjectId> parent) {
    if (transformTransaction_) return std::nullopt;
    if (!document_) return std::nullopt;
    if (type == ObjectType::Mesh) {
        const std::string geometryName = name.empty() ? "Mesh Geometry" : name + " Geometry";
        return createMeshObject(MeshResource::makeCube(geometryName, 1.0F), std::move(name), parent);
    }
    auto command = std::make_unique<CreateObjectCommand>(document_->scene, type, std::move(name), parent);
    auto* createdCommand = command.get();
    if (!commands_.execute(std::move(command))) return std::nullopt;
    const auto created = createdCommand->createdId();
    (void)selection_.select(document_->scene, created, SelectionMode::Replace);
    sceneMutated(false);
    ++selectionRevision_;
    return created;
}

std::optional<ObjectId> EditorSession::createMeshObject(MeshResource resource, std::string name,
                                                         std::optional<ObjectId> parent) {
    if (transformTransaction_) return std::nullopt;
    if (!document_) return std::nullopt;
    auto command = std::make_unique<CreateMeshObjectCommand>(document_->scene, std::move(resource),
                                                              std::move(name), parent);
    auto* createdCommand = command.get();
    if (!commands_.execute(std::move(command))) return std::nullopt;
    const auto created = createdCommand->createdId();
    (void)selection_.select(document_->scene, created, SelectionMode::Replace);
    sceneMutated(false);
    ++selectionRevision_;
    return created;
}

bool EditorSession::deleteObject(ObjectId object) {
    if (transformTransaction_) return false;
    if (!document_ || !document_->scene.contains(object)) return false;
    const auto* target = document_->scene.find(object);
    if (!target || target->locked) return false;
    if (!commands_.execute(std::make_unique<DeleteObjectCommand>(document_->scene, object))) return false;
    sceneMutated(true);
    return true;
}

bool EditorSession::deleteSelection() {
    if (transformTransaction_) return false;
    if (!document_ || selection_.empty()) return false;
    for (const auto id : selection_.selected()) {
        const auto* object = document_->scene.find(id);
        if (!object || object->locked) return false;
    }
    std::vector<ObjectId> roots;
    roots.reserve(selection_.size());
    for (const auto candidate : selection_.selected()) {
        const auto* object = document_->scene.find(candidate);
        if (!object) continue;
        bool selectedAncestor = false;
        auto parent = object->parent;
        while (parent) {
            if (selection_.contains(*parent)) {
                selectedAncestor = true;
                break;
            }
            const auto* parentObject = document_->scene.find(*parent);
            if (!parentObject) break;
            parent = parentObject->parent;
        }
        if (!selectedAncestor) roots.push_back(candidate);
    }
    if (roots.empty()) return false;
    if (roots.size() == 1) {
        if (!commands_.execute(std::make_unique<DeleteObjectCommand>(document_->scene, roots.front()))) return false;
    } else {
        auto transaction = std::make_unique<CompositeCommand>("Delete Selection");
        for (const auto root : roots) transaction->add(std::make_unique<DeleteObjectCommand>(document_->scene, root));
        if (!commands_.execute(std::move(transaction))) return false;
    }
    sceneMutated(true);
    return true;
}

bool EditorSession::duplicateSelection() {
    if (transformTransaction_ || !document_ || selection_.empty()) return false;
    for (const auto id : selection_.selected()) {
        const auto* object = document_->scene.find(id);
        if (!object || object->locked) return false;
    }
    const auto sources = selection_.selected();
    const auto previousActive = selection_.active();
    auto command = std::make_unique<DuplicateObjectsCommand>(document_->scene, sources);
    auto* duplicateCommand = command.get();
    if (!commands_.execute(std::move(command))) return false;

    const auto mappings = duplicateCommand->mappings();
    selection_.clear();
    bool selectedAny = false;
    std::optional<ObjectId> activeDuplicate;
    for (const auto& mapping : mappings) {
        if (previousActive && mapping.source == *previousActive) {
            activeDuplicate = mapping.duplicate;
            continue;
        }
        const auto mode = selectedAny ? SelectionMode::Add : SelectionMode::Replace;
        selectedAny = selection_.select(document_->scene, mapping.duplicate, mode) || selectedAny;
    }
    if (activeDuplicate) {
        const auto mode = selectedAny ? SelectionMode::Add : SelectionMode::Replace;
        selectedAny = selection_.select(document_->scene, *activeDuplicate, mode) || selectedAny;
    }
    if (!selectedAny && !mappings.empty()) {
        (void)selection_.select(document_->scene, mappings.front().duplicate, SelectionMode::Replace);
    }
    sceneMutated(false);
    ++selectionRevision_;
    return true;
}

bool EditorSession::setObjectVisible(ObjectId object, bool visible) {
    if (transformTransaction_ || !document_) return false;
    const auto* current = document_->scene.find(object);
    if (!current || current->visible == visible) return false;
    auto command = std::make_unique<SetObjectVisibilityCommand>(document_->scene, object, visible);
    if (!commands_.execute(std::move(command))) return false;
    if (!visible && selection_.contains(object)) {
        selection_.remove(object);
        ++selectionRevision_;
    }
    sceneMutated(false);
    return true;
}

bool EditorSession::setObjectLocked(ObjectId object, bool locked) {
    if (transformTransaction_ || !document_) return false;
    const auto* current = document_->scene.find(object);
    if (!current || current->locked == locked) return false;
    auto command = std::make_unique<SetObjectLockedCommand>(document_->scene, object, locked);
    if (!commands_.execute(std::move(command))) return false;
    sceneMutated(false);
    return true;
}

bool EditorSession::renameObject(ObjectId object, std::string name) {
    if (transformTransaction_) return false;
    if (!document_ || !document_->scene.contains(object)) return false;
    const auto* current = document_->scene.find(object);
    if (!current || current->locked) return false;
    if (!commands_.execute(std::make_unique<RenameObjectCommand>(document_->scene, object, std::move(name)))) return false;
    sceneMutated(false);
    return true;
}

bool EditorSession::transformObject(ObjectId object, const Transform& transform) {
    if (transformTransaction_) return false;
    if (!document_ || !document_->scene.contains(object)) return false;
    const auto* current = document_->scene.find(object);
    if (!current || current->locked) return false;
    if (!commands_.execute(std::make_unique<TransformObjectCommand>(document_->scene, object, transform))) return false;
    sceneMutated(false);
    return true;
}

bool EditorSession::beginTransformTransaction(const std::vector<ObjectId>& objects,
                                              std::string commandName) {
    if (!document_ || transformTransaction_ || objects.empty()) return false;
    TransformTransactionState transaction;
    transaction.commandName = commandName.empty() ? "Transform Objects" : std::move(commandName);
    transaction.changes.reserve(objects.size());
    for (const auto objectId : objects) {
        const auto* object = document_->scene.find(objectId);
        if (!object || object->locked) return false;
        const auto duplicate = std::find_if(transaction.changes.cbegin(), transaction.changes.cend(),
                                            [objectId](const TransformChange& change) {
                                                return change.object == objectId;
                                            });
        if (duplicate != transaction.changes.cend()) return false;
        transaction.changes.push_back(TransformChange{
            .object = objectId,
            .before = object->localTransform,
            .after = object->localTransform,
        });
    }
    transformTransaction_ = std::move(transaction);
    return true;
}

bool EditorSession::previewTransform(ObjectId object, const Transform& transform) {
    if (!document_ || !transformTransaction_) return false;
    auto found = std::find_if(transformTransaction_->changes.begin(), transformTransaction_->changes.end(),
                              [object](const TransformChange& change) {
                                  return change.object == object;
                              });
    if (found == transformTransaction_->changes.end()) return false;
    if (!document_->scene.setTransform(object, transform)) return false;
    found->after = transform;
    ++sceneRevision_;
    return true;
}

bool EditorSession::commitTransformTransaction() {
    if (!document_ || !transformTransaction_) return false;
    std::vector<TransformChange> changes;
    changes.reserve(transformTransaction_->changes.size());
    for (const auto& change : transformTransaction_->changes) {
        if (change.before != change.after) changes.push_back(change);
    }
    const std::string commandName = transformTransaction_->commandName;
    if (changes.empty()) {
        transformTransaction_.reset();
        return true;
    }
    auto command = std::make_unique<TransformObjectsCommand>(document_->scene, changes, commandName);
    if (!commands_.execute(std::move(command))) {
        for (const auto& change : changes) {
            (void)document_->scene.setTransform(change.object, change.before);
        }
        transformTransaction_.reset();
        ++sceneRevision_;
        return false;
    }
    transformTransaction_.reset();
    sceneMutated(false);
    return true;
}

bool EditorSession::cancelTransformTransaction() {
    if (!document_ || !transformTransaction_) return false;
    bool success = true;
    for (const auto& change : transformTransaction_->changes) {
        success = document_->scene.setTransform(change.object, change.before) && success;
    }
    transformTransaction_.reset();
    ++sceneRevision_;
    return success;
}

bool EditorSession::reparentObject(ObjectId object, std::optional<ObjectId> parent) {
    if (transformTransaction_) return false;
    if (!document_ || !document_->scene.contains(object)) return false;
    const auto* current = document_->scene.find(object);
    if (!current || current->locked) return false;
    if (!commands_.execute(std::make_unique<ReparentObjectCommand>(document_->scene, object, parent))) return false;
    sceneMutated(false);
    return true;
}

bool EditorSession::select(ObjectId object, SelectionMode mode) {
    if (transformTransaction_) return false;
    if (!document_ || !selection_.select(document_->scene, object, mode)) return false;
    ++selectionRevision_;
    return true;
}

void EditorSession::clearSelection() noexcept {
    if (transformTransaction_ || selection_.empty()) return;
    selection_.clear();
    ++selectionRevision_;
}

bool EditorSession::undo() {
    if (transformTransaction_) return false;
    if (!document_ || !commands_.undo()) return false;
    sceneMutated(true);
    return true;
}

bool EditorSession::redo() {
    if (transformTransaction_) return false;
    if (!document_ || !commands_.redo()) return false;
    sceneMutated(true);
    return true;
}

void EditorSession::setWorkspace(Workspace workspace) noexcept {
    if (workspace_ == workspace) return;
    workspace_ = workspace;
    ++uiRevision_;
}

bool EditorSession::requireProject(std::string* error) const {
    if (document_) return true;
    if (error) *error = "No project is open";
    return false;
}

bool EditorSession::transformTransactionHasChanges() const noexcept {
    if (!transformTransaction_) return false;
    return std::any_of(transformTransaction_->changes.cbegin(), transformTransaction_->changes.cend(),
                       [](const TransformChange& change) { return change.before != change.after; });
}

void EditorSession::resetForDocument(bool recoveredDirty) noexcept {
    transformTransaction_.reset();
    commands_.clear();
    commands_.markSaved();
    selection_.clear();
    recoveredDirty_ = recoveredDirty;
    ++sceneRevision_;
    ++selectionRevision_;
    ++documentRevision_;
}

void EditorSession::sceneMutated(bool pruneSelection) {
    ++sceneRevision_;
    recoveredDirty_ = false;
    if (pruneSelection && document_) {
        const auto previous = selection_.selected();
        selection_.prune(document_->scene);
        if (selection_.selected() != previous) ++selectionRevision_;
    }
}

} // namespace m3d
