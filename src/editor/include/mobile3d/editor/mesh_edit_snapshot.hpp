#pragma once

#include "mobile3d/core/id.hpp"
#include "mobile3d/core/math.hpp"
#include "mobile3d/editor/mesh_selection.hpp"

#include <cstdint>
#include <vector>

namespace m3d {

struct MeshEditVertexSnapshot final {
    EditableVertexId id{};
    Vec3 position{};
    bool selected{false};
};

struct MeshEditEdgeSnapshot final {
    EditableEdgeId id{};
    EditableVertexId first{};
    EditableVertexId second{};
    bool selected{false};
};

struct MeshEditFaceSnapshot final {
    EditableFaceId id{};
    std::vector<EditableVertexId> vertices;
    bool selected{false};
};

struct MeshEditPresentationSnapshot final {
    ObjectId object{};
    ResourceId resource{};
    MeshSelectionMode mode{MeshSelectionMode::Vertex};
    std::uint64_t revision{0};
    std::vector<MeshEditVertexSnapshot> vertices;
    std::vector<MeshEditEdgeSnapshot> edges;
    std::vector<MeshEditFaceSnapshot> faces;

    [[nodiscard]] bool active() const noexcept { return !object.isNull() && !resource.isNull(); }
};

class MeshEditSnapshotBuilder final {
public:
    [[nodiscard]] static MeshEditPresentationSnapshot build(
        const EditableMesh& mesh,
        const MeshSelectionModel& selection,
        ObjectId object,
        ResourceId resource,
        std::uint64_t revision);
};

} // namespace m3d
