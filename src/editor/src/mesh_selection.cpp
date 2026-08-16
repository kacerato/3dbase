#include "mobile3d/editor/mesh_selection.hpp"

#include <algorithm>

namespace m3d {

template <typename Id>
bool MeshSelectionModel::applySelection(std::set<Id>& values, std::optional<Id>& active,
                                        Id id, MeshSelectionAction action) {
    switch (action) {
    case MeshSelectionAction::Replace: {
        const bool unchanged = vertices_.size() + edges_.size() + faces_.size() == 1U &&
                               values.contains(id) && active == id;
        clear();
        values.insert(id);
        active = id;
        return !unchanged;
    }
    case MeshSelectionAction::Add: {
        const bool inserted = values.insert(id).second;
        const bool activeChanged = active != id;
        active = id;
        return inserted || activeChanged;
    }
    case MeshSelectionAction::Toggle: {
        const auto found = values.find(id);
        if (found != values.end()) {
            values.erase(found);
            if (active == id) active.reset();
            return true;
        }
        values.insert(id);
        active = id;
        return true;
    }
    case MeshSelectionAction::Remove: {
        const auto found = values.find(id);
        if (found == values.end()) return false;
        values.erase(found);
        if (active == id) active.reset();
        return true;
    }
    }
    return false;
}

bool MeshSelectionModel::select(const EditableMesh& mesh, EditableVertexId vertex,
                                MeshSelectionAction action) {
    if (!mesh.findVertex(vertex)) return false;
    mode_ = MeshSelectionMode::Vertex;
    return applySelection(vertices_, activeVertex_, vertex, action);
}

bool MeshSelectionModel::select(const EditableMesh& mesh, EditableEdgeId edge,
                                MeshSelectionAction action) {
    if (!mesh.findEdge(edge)) return false;
    mode_ = MeshSelectionMode::Edge;
    return applySelection(edges_, activeEdge_, edge, action);
}

bool MeshSelectionModel::select(const EditableMesh& mesh, EditableFaceId face,
                                MeshSelectionAction action) {
    if (!mesh.findFace(face)) return false;
    mode_ = MeshSelectionMode::Face;
    return applySelection(faces_, activeFace_, face, action);
}

bool MeshSelectionModel::contains(EditableVertexId vertex) const noexcept { return vertices_.contains(vertex); }
bool MeshSelectionModel::contains(EditableEdgeId edge) const noexcept { return edges_.contains(edge); }
bool MeshSelectionModel::contains(EditableFaceId face) const noexcept { return faces_.contains(face); }

std::vector<EditableVertexId> MeshSelectionModel::selectedVertices() const {
    return {vertices_.cbegin(), vertices_.cend()};
}

std::vector<EditableEdgeId> MeshSelectionModel::selectedEdges() const {
    return {edges_.cbegin(), edges_.cend()};
}

std::vector<EditableFaceId> MeshSelectionModel::selectedFaces() const {
    return {faces_.cbegin(), faces_.cend()};
}

bool MeshSelectionModel::empty() const noexcept {
    return vertices_.empty() && edges_.empty() && faces_.empty();
}

bool MeshSelectionModel::emptyCurrentMode() const noexcept {
    switch (mode_) {
    case MeshSelectionMode::Vertex: return vertices_.empty();
    case MeshSelectionMode::Edge: return edges_.empty();
    case MeshSelectionMode::Face: return faces_.empty();
    }
    return true;
}

void MeshSelectionModel::clear() noexcept {
    vertices_.clear();
    edges_.clear();
    faces_.clear();
    activeVertex_.reset();
    activeEdge_.reset();
    activeFace_.reset();
}

void MeshSelectionModel::prune(const EditableMesh& mesh) noexcept {
    for (auto it = vertices_.begin(); it != vertices_.end();) {
        if (mesh.findVertex(*it)) ++it;
        else it = vertices_.erase(it);
    }
    for (auto it = edges_.begin(); it != edges_.end();) {
        if (mesh.findEdge(*it)) ++it;
        else it = edges_.erase(it);
    }
    for (auto it = faces_.begin(); it != faces_.end();) {
        if (mesh.findFace(*it)) ++it;
        else it = faces_.erase(it);
    }
    if (activeVertex_ && !vertices_.contains(*activeVertex_)) activeVertex_.reset();
    if (activeEdge_ && !edges_.contains(*activeEdge_)) activeEdge_.reset();
    if (activeFace_ && !faces_.contains(*activeFace_)) activeFace_.reset();
}

} // namespace m3d
