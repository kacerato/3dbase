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

std::vector<EditableKnifePoint> MeshScreenPicker::planKnifeStroke(
    std::span<const MeshScreenEdge> edges,
    std::span<const MeshScreenFace> faces,
    const MeshKnifeStrokeRequest& request) {
    struct Crossing final {
        float order{0.0F};
        EditableKnifePoint point{};
        EditableVertexId first{};
        EditableVertexId second{};
        float x{0.0F};
        float y{0.0F};
    };
    const auto& stroke = request.stroke;
    if (stroke.size() < 2U || !std::isfinite(request.vertexSnapRadius)) return {};
    const float snap = std::max(request.vertexSnapRadius, 0.0F);

    std::vector<Crossing> crossings;
    for (std::size_t segment = 0; segment + 1U < stroke.size(); ++segment) {
        const auto& start = stroke[segment];
        const auto& end = stroke[segment + 1U];
        if (!finitePoint(start) || !finitePoint(end)) continue;
        const float rx = end.x - start.x;
        const float ry = end.y - start.y;
        if (rx * rx + ry * ry <= 1.0e-8F) continue;
        // Stroke joints belong to the following segment, except at the very end of the stroke.
        const bool lastSegment = segment + 2U == stroke.size();
        for (const auto& edge : edges) {
            if (edge.id.isNull() || edge.firstVertex.isNull() || edge.secondVertex.isNull() ||
                !finitePoint(edge.first) || !finitePoint(edge.second)) continue;
            const float qx = edge.second.x - edge.first.x;
            const float qy = edge.second.y - edge.first.y;
            const float denominator = rx * qy - ry * qx;
            if (std::abs(denominator) <= 1.0e-9F) continue;
            const float wx = edge.first.x - start.x;
            const float wy = edge.first.y - start.y;
            const float s = (wx * qy - wy * qx) / denominator;
            const float u = (wx * ry - wy * rx) / denominator;
            if (s < 0.0F || (lastSegment ? s > 1.0F : s >= 1.0F) || u < 0.0F || u > 1.0F) continue;
            const float x = start.x + rx * s;
            const float y = start.y + ry * s;
            const float depth = edge.first.depth + (edge.second.depth - edge.first.depth) * u;
            if (depth < 0.0F || depth > 1.0F ||
                occluded(depth, frontFaceAt(faces, x, y), request.occlusionDepthEpsilon)) continue;

            Crossing crossing{static_cast<float>(segment) + s, {}, edge.firstVertex, edge.secondVertex, x, y};
            const float toFirst = std::sqrt(squaredDistance(x, y, edge.first.x, edge.first.y));
            const float toSecond = std::sqrt(squaredDistance(x, y, edge.second.x, edge.second.y));
            if (std::min(toFirst, toSecond) <= snap) {
                crossing.point = EditableKnifePoint::atVertex(toFirst <= toSecond ? edge.firstVertex
                                                                                   : edge.secondVertex);
            } else {
                // 1/w is affine in screen space, so the 3D parameter follows from its interpolation.
                const float firstWeight = (1.0F - u) * edge.first.inverseW;
                const float secondWeight = u * edge.second.inverseW;
                const float total = firstWeight + secondWeight;
                const float t = std::isfinite(total) && total > 1.0e-12F ? secondWeight / total : u;
                crossing.point = EditableKnifePoint::onEdge(edge.id, edge.firstVertex, std::clamp(t, 0.0F, 1.0F));
            }
            crossings.push_back(crossing);
        }
    }
    std::stable_sort(crossings.begin(), crossings.end(),
                     [](const Crossing& left, const Crossing& right) { return left.order < right.order; });

    const auto containsCrossing = [](const MeshScreenFace& face, const Crossing& crossing) {
        const auto has = [&face](EditableVertexId id) {
            return std::find(face.vertexIds.begin(), face.vertexIds.end(), id) != face.vertexIds.end();
        };
        if (!crossing.point.vertex.isNull()) return has(crossing.point.vertex);
        return has(crossing.first) && has(crossing.second);
    };
    std::vector<EditableKnifePoint> path;
    const Crossing* previous = nullptr;
    for (const auto& crossing : crossings) {
        if (previous) {
            if (!crossing.point.vertex.isNull() && crossing.point.vertex == previous->point.vertex) continue;
            // Consecutive crossings are joined only across a visible face that holds both.
            const auto front = frontFaceAt(faces, (previous->x + crossing.x) * 0.5F,
                                           (previous->y + crossing.y) * 0.5F);
            bool joined = front.has_value();
            if (front) {
                for (const auto& face : faces) {
                    if (face.id != front->face || face.vertexIds.empty()) continue;
                    joined = containsCrossing(face, *previous) && containsCrossing(face, crossing);
                    break;
                }
            }
            if (!joined) path.push_back(EditableKnifePoint{});
        }
        path.push_back(crossing.point);
        previous = &crossing;
    }
    return path;
}

} // namespace m3d
