#pragma once

#include "mobile3d/editor/mesh_selection.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace m3d {

// inverseW is 1 / clip.w of the projected point (1 for orthographic projection). Screen-space
// interpolation of it recovers perspective-correct parameters along projected edges.
struct MeshScreenPoint final { float x{0.0F}; float y{0.0F}; float depth{1.0F}; float inverseW{1.0F}; };
struct MeshScreenVertex final { EditableVertexId id{}; MeshScreenPoint point{}; };
struct MeshScreenEdge final {
    EditableEdgeId id{};
    MeshScreenPoint first{};
    MeshScreenPoint second{};
    EditableVertexId firstVertex{};
    EditableVertexId secondVertex{};
};
struct MeshScreenFace final {
    EditableFaceId id{};
    std::vector<MeshScreenPoint> vertices;
    std::vector<EditableVertexId> vertexIds{};
};

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

struct MeshKnifeStrokeRequest final {
    std::vector<MeshScreenPoint> stroke; // screen-space polyline, only x/y are used
    float vertexSnapRadius{12.0F};
    float occlusionDepthEpsilon{0.003F};
};

class MeshScreenPicker final {
public:
    [[nodiscard]] static std::optional<MeshScreenPickResult> pick(
        std::span<const MeshScreenVertex> vertices,
        std::span<const MeshScreenEdge> edges,
        std::span<const MeshScreenFace> faces,
        const MeshScreenPickRequest& request);

    // Turns a drawn stroke into a knife path over the visible surface: every crossing with a
    // visible edge becomes an edge point (perspective-correct parameter), crossings within the
    // snap radius of an endpoint become that vertex, and the path is broken with an empty
    // point wherever the stroke leaves the face shared by two consecutive crossings.
    [[nodiscard]] static std::vector<EditableKnifePoint> planKnifeStroke(
        std::span<const MeshScreenEdge> edges,
        std::span<const MeshScreenFace> faces,
        const MeshKnifeStrokeRequest& request);
};

} // namespace m3d
