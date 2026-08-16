#pragma once

#include "mobile3d/core/editable_mesh.hpp"

#include <optional>
#include <set>
#include <vector>

namespace m3d {

enum class MeshSelectionMode {
    Vertex,
    Edge,
    Face,
};

enum class MeshSelectionAction {
    Replace,
    Add,
    Toggle,
    Remove,
};

class MeshSelectionModel final {
public:
    void setMode(MeshSelectionMode mode) noexcept { mode_ = mode; }
    [[nodiscard]] MeshSelectionMode mode() const noexcept { return mode_; }

    [[nodiscard]] bool select(const EditableMesh& mesh, EditableVertexId vertex,
                              MeshSelectionAction action = MeshSelectionAction::Replace);
    [[nodiscard]] bool select(const EditableMesh& mesh, EditableEdgeId edge,
                              MeshSelectionAction action = MeshSelectionAction::Replace);
    [[nodiscard]] bool select(const EditableMesh& mesh, EditableFaceId face,
                              MeshSelectionAction action = MeshSelectionAction::Replace);

    [[nodiscard]] bool contains(EditableVertexId vertex) const noexcept;
    [[nodiscard]] bool contains(EditableEdgeId edge) const noexcept;
    [[nodiscard]] bool contains(EditableFaceId face) const noexcept;

    [[nodiscard]] std::vector<EditableVertexId> selectedVertices() const;
    [[nodiscard]] std::vector<EditableEdgeId> selectedEdges() const;
    [[nodiscard]] std::vector<EditableFaceId> selectedFaces() const;

    [[nodiscard]] std::optional<EditableVertexId> activeVertex() const noexcept { return activeVertex_; }
    [[nodiscard]] std::optional<EditableEdgeId> activeEdge() const noexcept { return activeEdge_; }
    [[nodiscard]] std::optional<EditableFaceId> activeFace() const noexcept { return activeFace_; }

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool emptyCurrentMode() const noexcept;
    void clear() noexcept;
    void prune(const EditableMesh& mesh) noexcept;

private:
    template <typename Id>
    [[nodiscard]] bool applySelection(std::set<Id>& values, std::optional<Id>& active,
                                      Id id, MeshSelectionAction action);

    MeshSelectionMode mode_{MeshSelectionMode::Vertex};
    std::set<EditableVertexId> vertices_;
    std::set<EditableEdgeId> edges_;
    std::set<EditableFaceId> faces_;
    std::optional<EditableVertexId> activeVertex_{};
    std::optional<EditableEdgeId> activeEdge_{};
    std::optional<EditableFaceId> activeFace_{};
};

} // namespace m3d
