#include "mobile3d/editor/editor_session.hpp"

namespace m3d {

bool EditorSession::extrudeSelectedMeshFace(float distance, std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    const auto selected = meshEditTransaction_->selection.selectedFaces();
    if (selected.size() != 1U) {
        if (error) *error = "Extrude currently requires exactly one selected face";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto created = candidate.extrudeFace(selected.front(), distance, error);
    if (!created || !applyMeshEditPreview(candidate, error)) return false;

    meshEditTransaction_->selection.clear();
    (void)meshEditTransaction_->selection.select(meshEditTransaction_->working, *created,
                                                  MeshSelectionAction::Replace);
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}

bool EditorSession::insetSelectedMeshFace(float ratio, std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    const auto selected = meshEditTransaction_->selection.selectedFaces();
    if (selected.size() != 1U) {
        if (error) *error = "Inset currently requires exactly one selected face";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto created = candidate.insetFace(selected.front(), ratio, error);
    if (!created || !applyMeshEditPreview(candidate, error)) return false;

    meshEditTransaction_->selection.clear();
    (void)meshEditTransaction_->selection.select(meshEditTransaction_->working, *created,
                                                  MeshSelectionAction::Replace);
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}

bool EditorSession::subdivideSelectedMeshFace(std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    const auto selected = meshEditTransaction_->selection.selectedFaces();
    if (selected.size() != 1U) {
        if (error) *error = "Subdivide currently requires exactly one selected face";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto created = candidate.subdivideFace(selected.front(), error);
    if (!created || created->empty() || !applyMeshEditPreview(candidate, error)) return false;

    meshEditTransaction_->selection.clear();
    bool first = true;
    for (const auto face : *created) {
        (void)meshEditTransaction_->selection.select(
            meshEditTransaction_->working, face,
            first ? MeshSelectionAction::Replace : MeshSelectionAction::Add);
        first = false;
    }
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}

} // namespace m3d
