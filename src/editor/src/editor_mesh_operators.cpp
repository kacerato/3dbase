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


bool EditorSession::mergeSelectedMeshVertices(std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Vertex) {
        if (error) *error = "Merge requires Vertex selection mode";
        return false;
    }
    const auto selected = selection.selectedVertices();
    const auto active = selection.activeVertex();
    if (selected.size() < 2U || !active) {
        if (error) *error = "Merge to Active requires at least two selected vertices and an active vertex";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto merged = candidate.mergeVertices(selected, *active, error);
    if (!merged || !applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    selection.setMode(MeshSelectionMode::Vertex);
    (void)selection.select(meshEditTransaction_->working, *merged, MeshSelectionAction::Replace);
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}

bool EditorSession::weldSelectedMeshVertices(float distance, std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Vertex) {
        if (error) *error = "Weld requires Vertex selection mode";
        return false;
    }
    const auto selected = selection.selectedVertices();
    if (selected.size() < 2U) {
        if (error) *error = "Weld requires at least two selected vertices";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto result = candidate.weldVertices(selected, distance, selection.activeVertex(), error);
    if (!result) return false;
    if (result->mergedCount == 0U) {
        if (error) *error = "No selected vertices are within the weld distance";
        return false;
    }
    if (!applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    selection.setMode(MeshSelectionMode::Vertex);
    bool first = true;
    for (const auto survivor : result->survivors) {
        (void)selection.select(meshEditTransaction_->working, survivor,
                               first ? MeshSelectionAction::Replace : MeshSelectionAction::Add);
        first = false;
    }
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}


bool EditorSession::fillSelectedMeshBoundary(std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Edge) {
        if (error) *error = "Fill requires Edge selection mode";
        return false;
    }
    const auto selected = selection.selectedEdges();
    if (selected.size() < 3U) {
        if (error) *error = "Fill requires a closed boundary loop with at least three selected edges";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto face = candidate.fillBoundaryLoop(selected, error);
    if (!face || !applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    selection.setMode(MeshSelectionMode::Face);
    (void)selection.select(meshEditTransaction_->working, *face, MeshSelectionAction::Replace);
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}

bool EditorSession::bridgeSelectedMeshBoundaries(std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Edge) {
        if (error) *error = "Bridge requires Edge selection mode";
        return false;
    }
    const auto selected = selection.selectedEdges();
    if (selected.size() < 6U) {
        if (error) *error = "Bridge requires two closed boundary loops";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto faces = candidate.bridgeBoundaryLoops(selected, error);
    if (!faces || faces->empty() || !applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    selection.setMode(MeshSelectionMode::Face);
    bool first = true;
    for (const auto face : *faces) {
        (void)selection.select(meshEditTransaction_->working, face,
                               first ? MeshSelectionAction::Replace : MeshSelectionAction::Add);
        first = false;
    }
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}


bool EditorSession::loopCutSelectedMeshEdge(std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Edge) {
        if (error) *error = "Loop Cut requires Edge selection mode";
        return false;
    }
    const auto selected = selection.selectedEdges();
    if (selected.size() != 1U) {
        if (error) *error = "Loop Cut requires exactly one selected start edge";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto result = candidate.loopCut(selected.front(), error);
    if (!result || result->edges.empty() || !applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    selection.setMode(MeshSelectionMode::Edge);
    bool first = true;
    for (const auto edge : result->edges) {
        (void)selection.select(meshEditTransaction_->working, edge,
                               first ? MeshSelectionAction::Replace : MeshSelectionAction::Add);
        first = false;
    }
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}

} // namespace m3d
