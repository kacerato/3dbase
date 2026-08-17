#include "mobile3d/editor/editor_session.hpp"

#include <cmath>
#include <utility>

namespace m3d {

bool EditorSession::beginMeshEdit(ObjectId object, std::string* error) {
    if (!document_) {
        if (error) *error = "No project is open";
        return false;
    }
    if (hasActiveMutationTransaction()) {
        if (error) *error = "Another editor transaction is already active";
        return false;
    }
    const auto* sceneObject = document_->scene.find(object);
    if (!sceneObject || sceneObject->type != ObjectType::Mesh || !sceneObject->meshResource) {
        if (error) *error = "Edit Mode requires a mesh object with a mesh resource";
        return false;
    }
    if (objectLockedByActiveLayer(object)) {
        if (error) *error = "Locked objects cannot enter Edit Mode";
        return false;
    }
    const auto* resource = document_->scene.findMeshResource(*sceneObject->meshResource);
    if (!resource) {
        if (error) *error = "Mesh resource is missing";
        return false;
    }
    const auto editable = EditableMesh::fromMeshResource(*resource, 1.0e-5F, error);
    if (!editable) return false;

    MeshEditTransactionState state;
    state.object = object;
    state.resource = resource->id;
    state.before = *resource;
    state.working = *editable;
    meshEditTransaction_ = std::move(state);
    ++uiRevision_;
    if (error) error->clear();
    return true;
}

const EditableMesh* EditorSession::editableMesh() const noexcept {
    return meshEditTransaction_ ? &meshEditTransaction_->working : nullptr;
}

const MeshSelectionModel* EditorSession::meshSelection() const noexcept {
    return meshEditTransaction_ ? &meshEditTransaction_->selection : nullptr;
}

MeshEditPresentationSnapshot EditorSession::meshEditPresentationSnapshot() const {
    if (!meshEditTransaction_) return {};
    return MeshEditSnapshotBuilder::build(meshEditTransaction_->working,
                                          meshEditTransaction_->selection,
                                          meshEditTransaction_->object,
                                          meshEditTransaction_->resource,
                                          selectionRevision_);
}

bool EditorSession::setMeshSelectionMode(MeshSelectionMode mode) noexcept {
    if (!meshEditTransaction_) return false;
    if (meshEditTransaction_->selection.mode() == mode) return true;
    meshEditTransaction_->selection.setMode(mode);
    ++selectionRevision_;
    ++uiRevision_;
    return true;
}

bool EditorSession::selectMeshVertex(EditableVertexId vertex, MeshSelectionAction action) {
    if (!meshEditTransaction_) return false;
    if (!meshEditTransaction_->selection.select(meshEditTransaction_->working, vertex, action)) return false;
    ++selectionRevision_;
    ++uiRevision_;
    return true;
}

bool EditorSession::selectMeshEdge(EditableEdgeId edge, MeshSelectionAction action) {
    if (!meshEditTransaction_) return false;
    if (!meshEditTransaction_->selection.select(meshEditTransaction_->working, edge, action)) return false;
    ++selectionRevision_;
    ++uiRevision_;
    return true;
}

bool EditorSession::selectMeshFace(EditableFaceId face, MeshSelectionAction action) {
    if (!meshEditTransaction_) return false;
    if (!meshEditTransaction_->selection.select(meshEditTransaction_->working, face, action)) return false;
    ++selectionRevision_;
    ++uiRevision_;
    return true;
}

bool EditorSession::applyMeshEditPreview(const EditableMesh& candidate, std::string* error) {
    if (!document_ || !meshEditTransaction_) return false;
    std::string validationError;
    if (!candidate.validate(&validationError)) {
        if (error) *error = validationError;
        return false;
    }

    MeshResource preview = meshEditTransaction_->before;
    preview.authoring = candidate;
    if (!preview.rebuildFromAuthoring(error)) return false;

    auto* resource = document_->scene.findMeshResource(meshEditTransaction_->resource);
    if (!resource) {
        if (error) *error = "Mesh resource disappeared during Edit Mode";
        return false;
    }
    *resource = std::move(preview);
    meshEditTransaction_->working = candidate;
    meshEditTransaction_->dirty = true;
    meshEditTransaction_->selection.prune(meshEditTransaction_->working);
    ++sceneRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}

bool EditorSession::moveSelectedMeshVertices(Vec3 delta, std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z)) {
        if (error) *error = "Vertex translation must be finite";
        return false;
    }
    const auto selected = meshEditTransaction_->selection.selectedVertices();
    if (selected.empty()) {
        if (error) *error = "No vertices are selected";
        return false;
    }
    if (delta == Vec3{}) {
        if (error) error->clear();
        return true;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    for (const auto vertexId : selected) {
        auto* vertex = candidate.findVertex(vertexId);
        if (!vertex) {
            if (error) *error = "Selected vertex no longer exists";
            return false;
        }
        vertex->position.x += delta.x;
        vertex->position.y += delta.y;
        vertex->position.z += delta.z;
    }
    return applyMeshEditPreview(candidate, error);
}

bool EditorSession::commitMeshEdit(std::string commandName, std::string* error) {
    if (!document_ || !meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    if (!meshEditTransaction_->dirty) {
        meshEditTransaction_.reset();
        ++uiRevision_;
        if (error) error->clear();
        return true;
    }

    auto* current = document_->scene.findMeshResource(meshEditTransaction_->resource);
    if (!current) {
        if (error) *error = "Mesh resource disappeared before Edit Mode commit";
        return false;
    }
    const MeshResource before = meshEditTransaction_->before;
    const MeshResource after = *current;
    auto command = std::make_unique<ReplaceMeshResourceCommand>(document_->scene, before, after,
                                                                 std::move(commandName));
    if (!commands_.execute(std::move(command))) {
        *current = before;
        meshEditTransaction_.reset();
        ++sceneRevision_;
        if (error) *error = "Could not record mesh edit in Undo history";
        return false;
    }
    meshEditTransaction_.reset();
    sceneMutated(false);
    if (error) error->clear();
    return true;
}

bool EditorSession::cancelMeshEdit() {
    if (!document_ || !meshEditTransaction_) return false;
    auto* current = document_->scene.findMeshResource(meshEditTransaction_->resource);
    if (!current) {
        meshEditTransaction_.reset();
        ++uiRevision_;
        return false;
    }
    *current = meshEditTransaction_->before;
    meshEditTransaction_.reset();
    ++sceneRevision_;
    ++uiRevision_;
    return true;
}

} // namespace m3d
