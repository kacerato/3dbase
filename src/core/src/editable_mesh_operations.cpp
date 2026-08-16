#include "mobile3d/core/editable_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace m3d {
namespace {

[[nodiscard]] std::optional<Vec3> normalizedFaceNormal(const EditableMesh& mesh,
                                                        EditableFaceId face) noexcept {
    const auto ids = mesh.faceVertices(face);
    if (ids.size() < 3U) return std::nullopt;

    Vec3 normal{};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const auto* currentVertex = mesh.findVertex(ids[i]);
        const auto* nextVertex = mesh.findVertex(ids[(i + 1U) % ids.size()]);
        if (!currentVertex || !nextVertex) return std::nullopt;
        const Vec3& current = currentVertex->position;
        const Vec3& next = nextVertex->position;
        normal.x += (current.y - next.y) * (current.z + next.z);
        normal.y += (current.z - next.z) * (current.x + next.x);
        normal.z += (current.x - next.x) * (current.y + next.y);
    }

    const float lengthSquared = normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-16F) return std::nullopt;
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    normal.x *= inverseLength;
    normal.y *= inverseLength;
    normal.z *= inverseLength;
    return normal;
}

[[nodiscard]] Vec3 addScaled(Vec3 value, Vec3 direction, float scale) noexcept {
    value.x += direction.x * scale;
    value.y += direction.y * scale;
    value.z += direction.z * scale;
    return value;
}

} // namespace

bool EditableMesh::removeFace(EditableFaceId faceId, std::string* error) {
    const auto* face = findFace(faceId);
    if (!face) {
        if (error) *error = "Editable face does not exist";
        return false;
    }

    std::vector<EditableHalfEdgeId> loop;
    EditableHalfEdgeId current = face->halfEdge;
    for (std::size_t step = 0; step <= halfEdges_.size(); ++step) {
        const auto* halfEdge = findHalfEdge(current);
        if (!halfEdge || halfEdge->face != faceId) {
            if (error) *error = "Editable face loop is invalid";
            return false;
        }
        loop.push_back(current);
        current = halfEdge->next;
        if (current == face->halfEdge) break;
    }
    if (loop.size() < 3U || current != face->halfEdge) {
        if (error) *error = "Editable face loop did not close";
        return false;
    }

    std::vector<EditableVertexId> affectedVertices;
    affectedVertices.reserve(loop.size());
    for (const auto halfEdgeId : loop) {
        const auto* halfEdge = findHalfEdge(halfEdgeId);
        if (!halfEdge) return false;
        const auto destinationId = destination(halfEdgeId);
        if (destinationId.isNull()) {
            if (error) *error = "Editable face contains an invalid half-edge destination";
            return false;
        }
        affectedVertices.push_back(halfEdge->origin);
        directedEdges_.erase(DirectedEdgeKey{halfEdge->origin.value, destinationId.value});
    }

    for (const auto halfEdgeId : loop) {
        const auto* halfEdgeValue = findHalfEdge(halfEdgeId);
        if (!halfEdgeValue) continue;
        const EditableHalfEdge snapshot = *halfEdgeValue;

        if (!snapshot.twin.isNull()) {
            auto* twin = findHalfEdgeMutable(snapshot.twin);
            auto* edge = findEdgeMutable(snapshot.edge);
            if (!twin || !edge) {
                if (error) *error = "Editable face removal encountered an invalid twin edge";
                return false;
            }
            twin->twin = {};
            edge->halfEdge = twin->id;
        } else {
            if (!snapshot.edge.isNull() && static_cast<std::size_t>(snapshot.edge.value) <= edges_.size()) {
                auto& edgeSlot = edges_[static_cast<std::size_t>(snapshot.edge.value - 1U)];
                if (edgeSlot) {
                    edgeSlot.reset();
                    --edgeCount_;
                }
            }
        }

        auto& halfEdgeSlot = halfEdges_[static_cast<std::size_t>(halfEdgeId.value - 1U)];
        if (halfEdgeSlot) {
            halfEdgeSlot.reset();
            --halfEdgeCount_;
        }
    }

    auto& faceSlot = faces_[static_cast<std::size_t>(faceId.value - 1U)];
    faceSlot.reset();
    --faceCount_;

    for (const auto vertexId : affectedVertices) {
        auto* vertex = findVertex(vertexId);
        if (!vertex) continue;
        if (!vertex->outgoing.isNull() && findHalfEdge(vertex->outgoing)) continue;
        vertex->outgoing = {};
        for (const auto& candidate : halfEdges_) {
            if (!candidate || candidate->origin != vertexId) continue;
            vertex->outgoing = candidate->id;
            break;
        }
    }

    if (error) error->clear();
    return true;
}

std::optional<Vec3> EditableMesh::faceNormal(EditableFaceId face) const noexcept {
    return normalizedFaceNormal(*this, face);
}

std::optional<EditableFaceId> EditableMesh::extrudeFace(EditableFaceId face, float distance,
                                                         std::string* error) {
    if (!std::isfinite(distance) || std::abs(distance) <= 1.0e-7F) {
        if (error) *error = "Extrude distance must be finite and non-zero";
        return std::nullopt;
    }

    EditableMesh working = *this;
    const auto baseVertices = working.faceVertices(face);
    const auto normal = working.faceNormal(face);
    if (baseVertices.size() < 3U || !normal) {
        if (error) *error = "Extrude requires a valid non-degenerate face";
        return std::nullopt;
    }

    std::vector<Vec3> basePositions;
    basePositions.reserve(baseVertices.size());
    for (const auto id : baseVertices) {
        const auto* vertex = working.findVertex(id);
        if (!vertex) return std::nullopt;
        basePositions.push_back(vertex->position);
    }

    if (!working.removeFace(face, error)) return std::nullopt;

    std::vector<EditableVertexId> topVertices;
    topVertices.reserve(baseVertices.size());
    for (const auto position : basePositions) {
        topVertices.push_back(working.addVertex(addScaled(position, *normal, distance)));
    }

    const auto topFace = working.addFace(topVertices, error);
    if (!topFace) return std::nullopt;

    for (std::size_t i = 0; i < baseVertices.size(); ++i) {
        const std::size_t next = (i + 1U) % baseVertices.size();
        const std::array<EditableVertexId, 4> side{
            baseVertices[i], baseVertices[next], topVertices[next], topVertices[i]};
        if (!working.addFace(side, error)) return std::nullopt;
    }

    if (!working.validate(error)) return std::nullopt;
    *this = std::move(working);
    if (error) error->clear();
    return topFace;
}

std::optional<EditableFaceId> EditableMesh::insetFace(EditableFaceId face, float ratio,
                                                       std::string* error) {
    if (!std::isfinite(ratio) || ratio <= 0.0F || ratio >= 1.0F) {
        if (error) *error = "Inset ratio must be greater than zero and less than one";
        return std::nullopt;
    }

    EditableMesh working = *this;
    const auto baseVertices = working.faceVertices(face);
    if (baseVertices.size() < 3U) {
        if (error) *error = "Inset requires a valid face";
        return std::nullopt;
    }

    Vec3 center{};
    std::vector<Vec3> positions;
    positions.reserve(baseVertices.size());
    for (const auto id : baseVertices) {
        const auto* vertex = working.findVertex(id);
        if (!vertex) return std::nullopt;
        positions.push_back(vertex->position);
        center.x += vertex->position.x;
        center.y += vertex->position.y;
        center.z += vertex->position.z;
    }
    const float inverseCount = 1.0F / static_cast<float>(positions.size());
    center.x *= inverseCount;
    center.y *= inverseCount;
    center.z *= inverseCount;

    if (!working.removeFace(face, error)) return std::nullopt;

    std::vector<EditableVertexId> innerVertices;
    innerVertices.reserve(baseVertices.size());
    const float retained = 1.0F - ratio;
    for (const auto position : positions) {
        Vec3 inner;
        inner.x = center.x + (position.x - center.x) * retained;
        inner.y = center.y + (position.y - center.y) * retained;
        inner.z = center.z + (position.z - center.z) * retained;
        innerVertices.push_back(working.addVertex(inner));
    }

    const auto innerFace = working.addFace(innerVertices, error);
    if (!innerFace) return std::nullopt;
    for (std::size_t i = 0; i < baseVertices.size(); ++i) {
        const std::size_t next = (i + 1U) % baseVertices.size();
        const std::array<EditableVertexId, 4> ring{
            baseVertices[i], baseVertices[next], innerVertices[next], innerVertices[i]};
        if (!working.addFace(ring, error)) return std::nullopt;
    }

    if (!working.validate(error)) return std::nullopt;
    *this = std::move(working);
    if (error) error->clear();
    return innerFace;
}

std::optional<std::vector<EditableFaceId>> EditableMesh::subdivideFace(EditableFaceId face,
                                                                        std::string* error) {
    EditableMesh working = *this;
    const auto baseVertices = working.faceVertices(face);
    if (baseVertices.size() < 3U) {
        if (error) *error = "Subdivide requires a valid face";
        return std::nullopt;
    }

    Vec3 center{};
    for (const auto id : baseVertices) {
        const auto* vertex = working.findVertex(id);
        if (!vertex) return std::nullopt;
        center.x += vertex->position.x;
        center.y += vertex->position.y;
        center.z += vertex->position.z;
    }
    const float inverseCount = 1.0F / static_cast<float>(baseVertices.size());
    center.x *= inverseCount;
    center.y *= inverseCount;
    center.z *= inverseCount;

    if (!working.removeFace(face, error)) return std::nullopt;
    const auto centerVertex = working.addVertex(center);

    std::vector<EditableFaceId> created;
    created.reserve(baseVertices.size());
    for (std::size_t i = 0; i < baseVertices.size(); ++i) {
        const std::size_t next = (i + 1U) % baseVertices.size();
        const std::array<EditableVertexId, 3> triangle{
            baseVertices[i], baseVertices[next], centerVertex};
        const auto faceId = working.addFace(triangle, error);
        if (!faceId) return std::nullopt;
        created.push_back(*faceId);
    }

    if (!working.validate(error)) return std::nullopt;
    *this = std::move(working);
    if (error) error->clear();
    return created;
}

} // namespace m3d
