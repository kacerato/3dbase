#include "mobile3d/editor/editor_session.hpp"

#include "mobile3d/core/commands/object_commands.hpp"
#include "mobile3d/core/composite_command.hpp"

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

const ProjectDocument* EditorSession::document() const noexcept { return document_ ? &*document_ : nullptr; }
ProjectDocument* EditorSession::document() noexcept { return document_ ? &*document_ : nullptr; }
const Scene* EditorSession::scene() const noexcept { return document_ ? &document_->scene : nullptr; }
Scene* EditorSession::scene() noexcept { return document_ ? &document_->scene : nullptr; }

std::optional<ObjectId> EditorSession::createObject(ObjectType type, std::string name,
                                                     std::optional<ObjectId> parent) {
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
    if (!document_ || !document_->scene.contains(object)) return false;
    if (!commands_.execute(std::make_unique<DeleteObjectCommand>(document_->scene, object))) return false;
    sceneMutated(true);
    return true;
}

bool EditorSession::deleteSelection() {
    if (!document_ || selection_.empty()) return false;
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

bool EditorSession::renameObject(ObjectId object, std::string name) {
    if (!document_ || !document_->scene.contains(object)) return false;
    if (!commands_.execute(std::make_unique<RenameObjectCommand>(document_->scene, object, std::move(name)))) return false;
    sceneMutated(false);
    return true;
}

bool EditorSession::transformObject(ObjectId object, const Transform& transform) {
    if (!document_ || !document_->scene.contains(object)) return false;
    if (!commands_.execute(std::make_unique<TransformObjectCommand>(document_->scene, object, transform))) return false;
    sceneMutated(false);
    return true;
}

bool EditorSession::reparentObject(ObjectId object, std::optional<ObjectId> parent) {
    if (!document_ || !document_->scene.contains(object)) return false;
    if (!commands_.execute(std::make_unique<ReparentObjectCommand>(document_->scene, object, parent))) return false;
    sceneMutated(false);
    return true;
}

bool EditorSession::select(ObjectId object, SelectionMode mode) {
    if (!document_ || !selection_.select(document_->scene, object, mode)) return false;
    ++selectionRevision_;
    return true;
}

void EditorSession::clearSelection() noexcept {
    if (selection_.empty()) return;
    selection_.clear();
    ++selectionRevision_;
}

bool EditorSession::undo() {
    if (!document_ || !commands_.undo()) return false;
    sceneMutated(true);
    return true;
}

bool EditorSession::redo() {
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

void EditorSession::resetForDocument(bool recoveredDirty) noexcept {
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
