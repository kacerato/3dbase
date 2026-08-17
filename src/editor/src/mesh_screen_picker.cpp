#include "mobile3d/editor/mesh_screen_picker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace m3d {
namespace {

[[nodiscard]] bool finitePoint(const MeshScreenPoint& point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.depth);
}
[[nodiscard]] float squaredDistance(float ax, float ay, float bx, float by) noexcept {
    const float dx = ax - bx; const float dy = ay - by; return dx * dx + dy * dy;
}
[[nodiscard]] std::optional<float> triangleDepthAt(float x, float y,
                                                   const MeshScreenPoint& a,
                                                   const MeshScreenPoint& b,
                                                   const MeshScreenPoint& c) noexcept {
    if (!finitePoint(a) || !finitePoint(b) || !finitePoint(c)) return std::nullopt;
    const float denominator = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::abs(denominator) <= 1.0e-8F) return std::nullopt;
    const float wa = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / denominator;
    const float wb = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / denominator;
    const float wc = 1.0F - wa - wb;
    constexpr float epsilon = -1.0e-5F;
    if (wa < epsilon || wb < epsilon || wc < epsilon) return std::nullopt;
    return wa * a.depth + wb * b.depth + wc * c.depth;
}
struct FaceDepthHit final { EditableFaceId face{}; float depth{1.0F}; };
[[nodiscard]] std::optional<FaceDepthHit> frontFaceAt(std::span<const MeshScreenFace> faces,
                                                       float x, float y) {
    std::optional<FaceDepthHit> best;
    for (const auto& face : faces) {
        if (face.id.isNull() || face.vertices.size() < 3U) continue;
        const auto& origin = face.vertices.front();
        for (std::size_t index = 1U; index + 1U < face.vertices.size(); ++index) {
            const auto depth = triangleDepthAt(x, y, origin, face.vertices[index], face.vertices[index + 1U]);
            if (!depth || *depth < 0.0F || *depth > 1.0F) continue;
            if (!best || *depth < best->depth) best = FaceDepthHit{face.id, *depth};
        }
    }
    return best;
}
[[nodiscard]] bool occluded(float depth, const std::optional<FaceDepthHit>& front, float epsilon) noexcept {
    return front && depth > front->depth + std::max(epsilon, 0.0F);
}
struct EdgeDistance final { float distance{std::numeric_limits<float>::infinity()}; float depth{1.0F}; };
[[nodiscard]] EdgeDistance distanceToEdge(float x, float y,
                                          const MeshScreenPoint& first,
                                          const MeshScreenPoint& second) noexcept {
    if (!finitePoint(first) || !finitePoint(second)) return {};
    const float dx = second.x - first.x; const float dy = second.y - first.y;
    const float lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1.0e-8F) return {std::sqrt(squaredDistance(x, y, first.x, first.y)), first.depth};
    const float t = std::clamp(((x - first.x) * dx + (y - first.y) * dy) / lengthSquared, 0.0F, 1.0F);
    const float cx = first.x + dx * t; const float cy = first.y + dy * t;
    return {std::sqrt(squaredDistance(x, y, cx, cy)), first.depth + (second.depth - first.depth) * t};
}
[[nodiscard]] bool better(float distance, float depth,
                          const std::optional<MeshScreenPickResult>& best) noexcept {
    if (!best) return true;
    constexpr float tie = 0.25F;
    return distance + tie < best->screenDistance ||
           (std::abs(distance - best->screenDistance) <= tie && depth < best->depth);
}

} // namespace

std::optional<MeshScreenPickResult> MeshScreenPicker::pick(
    std::span<const MeshScreenVertex> vertices,
    std::span<const MeshScreenEdge> edges,
    std::span<const MeshScreenFace> faces,
    const MeshScreenPickRequest& request) {
    if (!std::isfinite(request.x) || !std::isfinite(request.y) ||
        !std::isfinite(request.vertexRadius) || !std::isfinite(request.edgeTolerance) ||
        request.vertexRadius < 0.0F || request.edgeTolerance < 0.0F) return std::nullopt;
    const auto front = frontFaceAt(faces, request.x, request.y);
    if (request.mode == MeshSelectionMode::Face) {
        if (!front) return std::nullopt;
        return MeshScreenPickResult{MeshSelectionMode::Face, front->face.value, 0.0F, front->depth};
    }
    std::optional<MeshScreenPickResult> best;
    if (request.mode == MeshSelectionMode::Vertex) {
        for (const auto& vertex : vertices) {
            if (vertex.id.isNull() || !finitePoint(vertex.point) || vertex.point.depth < 0.0F || vertex.point.depth > 1.0F) continue;
            const float distance = std::sqrt(squaredDistance(request.x, request.y, vertex.point.x, vertex.point.y));
            if (distance > request.vertexRadius || occluded(vertex.point.depth, front, request.occlusionDepthEpsilon)) continue;
            if (better(distance, vertex.point.depth, best))
                best = MeshScreenPickResult{MeshSelectionMode::Vertex, vertex.id.value, distance, vertex.point.depth};
        }
        return best;
    }
    for (const auto& edge : edges) {
        if (edge.id.isNull()) continue;
        const auto candidate = distanceToEdge(request.x, request.y, edge.first, edge.second);
        if (!std::isfinite(candidate.distance) || candidate.distance > request.edgeTolerance ||
            candidate.depth < 0.0F || candidate.depth > 1.0F ||
            occluded(candidate.depth, front, request.occlusionDepthEpsilon)) continue;
        if (better(candidate.distance, candidate.depth, best))
            best = MeshScreenPickResult{MeshSelectionMode::Edge, edge.id.value, candidate.distance, candidate.depth};
    }
    return best;
}

} // namespace m3d
