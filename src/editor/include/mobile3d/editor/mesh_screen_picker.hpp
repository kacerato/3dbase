#pragma once

#include "mobile3d/editor/mesh_selection.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace m3d {

struct MeshScreenPoint final { float x{0.0F}; float y{0.0F}; float depth{1.0F}; };
struct MeshScreenVertex final { EditableVertexId id{}; MeshScreenPoint point{}; };
struct MeshScreenEdge final { EditableEdgeId id{}; MeshScreenPoint first{}; MeshScreenPoint second{}; };
struct MeshScreenFace final { EditableFaceId id{}; std::vector<MeshScreenPoint> vertices; };

struct MeshScreenPickRequest final {
    MeshSelectionMode mode{MeshSelectionMode::Vertex};
    float x{0.0F};
    float y{0.0F};
    float vertexRadius{14.0F};
    float edgeTolerance{12.0F};
    float occlusionDepthEpsilon{0.003F};
};

struct MeshScreenPickResult final {
    MeshSelectionMode mode{MeshSelectionMode::Vertex};
    std::uint32_t elementId{0};
    float screenDistance{0.0F};
    float depth{1.0F};
};

class MeshScreenPicker final {
public:
    [[nodiscard]] static std::optional<MeshScreenPickResult> pick(
        std::span<const MeshScreenVertex> vertices,
        std::span<const MeshScreenEdge> edges,
        std::span<const MeshScreenFace> faces,
        const MeshScreenPickRequest& request);
};

} // namespace m3d
