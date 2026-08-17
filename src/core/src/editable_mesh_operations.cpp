#include "mobile3d/core/editable_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <set>
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

struct BoundaryArc final {
    EditableEdgeId edge{};
    EditableVertexId destination{};
};

[[nodiscard]] std::optional<std::vector<std::vector<EditableVertexId>>> selectedBoundaryLoops(
    const EditableMesh& mesh, std::span<const EditableEdgeId> selectedEdges,
    std::string* error) {
    std::set<EditableEdgeId> unique(selectedEdges.begin(), selectedEdges.end());
    if (unique.size() < 3U) {
        if (error) *error = "Boundary operation requires at least three unique edges";
        return std::nullopt;
    }

    std::map<EditableVertexId, BoundaryArc> outgoing;
    std::set<EditableVertexId> incoming;
    for (const auto edgeId : unique) {
        const auto* edge = mesh.findEdge(edgeId);
        const auto* halfEdge = edge ? mesh.findHalfEdge(edge->halfEdge) : nullptr;
        const auto* next = halfEdge ? mesh.findHalfEdge(halfEdge->next) : nullptr;
        if (!edge || !halfEdge || !next) {
            if (error) *error = "Selected boundary edge is invalid";
            return std::nullopt;
        }
        if (!halfEdge->twin.isNull()) {
            if (error) *error = "Selected edge is not on an open boundary";
            return std::nullopt;
        }
        const auto origin = halfEdge->origin;
        const auto destination = next->origin;
        if (origin.isNull() || destination.isNull() || origin == destination ||
            outgoing.contains(origin) || incoming.contains(destination)) {
            if (error) *error = "Selected boundary edges branch or repeat a vertex";
            return std::nullopt;
        }
        outgoing.emplace(origin, BoundaryArc{edgeId, destination});
        incoming.insert(destination);
    }

    std::set<EditableEdgeId> visited;
    std::vector<std::vector<EditableVertexId>> loops;
    for (const auto& [origin, arc] : outgoing) {
        if (visited.contains(arc.edge)) continue;
        std::vector<EditableVertexId> loop;
        const EditableVertexId start = origin;
        EditableVertexId current = start;
        bool closed = false;
        for (std::size_t step = 0; step <= unique.size(); ++step) {
            const auto found = outgoing.find(current);
            if (found == outgoing.end()) break;
            if (visited.contains(found->second.edge)) {
                closed = current == start;
                break;
            }
            loop.push_back(current);
            visited.insert(found->second.edge);
            current = found->second.destination;
            if (current == start) {
                closed = true;
                break;
            }
        }
        if (!closed || loop.size() < 3U) {
            if (error) *error = "Selected boundary edges do not form closed loops";
            return std::nullopt;
        }
        loops.push_back(std::move(loop));
    }

    if (visited.size() != unique.size()) {
        if (error) *error = "Selected boundary traversal did not consume every edge";
        return std::nullopt;
    }
    if (error) error->clear();
    return loops;
}

[[nodiscard]] float squaredDistance(Vec3 left, Vec3 right) noexcept {
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return x * x + y * y + z * z;
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


std::optional<EditableVertexId> EditableMesh::mergeVertices(
    std::span<const EditableVertexId> vertices, EditableVertexId target,
    std::string* error) {
    std::set<EditableVertexId> unique(vertices.begin(), vertices.end());
    if (unique.size() < 2U) {
        if (error) *error = "Merge requires at least two unique vertices";
        return std::nullopt;
    }
    if (target.isNull() || !unique.contains(target)) {
        if (error) *error = "Merge target must be one of the selected vertices";
        return std::nullopt;
    }
    for (const auto vertex : unique) {
        if (!findVertex(vertex)) {
            if (error) *error = "Merge selection contains a missing vertex";
            return std::nullopt;
        }
    }

    std::set<EditableVertexId> sources = unique;
    sources.erase(target);
    EditableMesh working = *this;

    struct RebuiltFace final {
        EditableFaceId original{};
        std::vector<EditableVertexId> vertices;
    };
    std::vector<RebuiltFace> affected;
    for (const auto& face : working.faces()) {
        const auto loop = working.faceVertices(face.id);
        const bool touchesSource = std::any_of(loop.cbegin(), loop.cend(),
                                               [&sources](EditableVertexId vertex) {
                                                   return sources.contains(vertex);
                                               });
        if (!touchesSource) continue;

        std::vector<EditableVertexId> replaced;
        replaced.reserve(loop.size());
        for (const auto vertex : loop) replaced.push_back(sources.contains(vertex) ? target : vertex);

        std::vector<EditableVertexId> compact;
        compact.reserve(replaced.size());
        for (const auto vertex : replaced) {
            if (compact.empty() || compact.back() != vertex) compact.push_back(vertex);
        }
        if (compact.size() > 1U && compact.front() == compact.back()) compact.pop_back();

        std::set<EditableVertexId> distinct(compact.begin(), compact.end());
        if (compact.size() >= 3U && distinct.size() != compact.size()) {
            if (error) *error = "Merge would create a self-touching face";
            return std::nullopt;
        }
        if (distinct.size() < 3U) compact.clear();
        affected.push_back(RebuiltFace{face.id, std::move(compact)});
    }
    if (affected.empty()) {
        if (error) *error = "Merge vertices are not referenced by any editable face";
        return std::nullopt;
    }

    for (const auto& face : affected) {
        if (!working.removeFace(face.original, error)) return std::nullopt;
    }

    for (const auto source : sources) {
        for (const auto& halfEdge : working.halfEdges()) {
            if (halfEdge.origin == source || working.destination(halfEdge.id) == source) {
                if (error) *error = "Merge source is still referenced after removing incident faces";
                return std::nullopt;
            }
        }
        if (source.isNull() || static_cast<std::size_t>(source.value) > working.vertices_.size()) {
            if (error) *error = "Merge source vertex slot is invalid";
            return std::nullopt;
        }
        auto& slot = working.vertices_[static_cast<std::size_t>(source.value - 1U)];
        if (!slot) {
            if (error) *error = "Merge source vertex disappeared unexpectedly";
            return std::nullopt;
        }
        slot.reset();
        --working.vertexCount_;
    }

    for (const auto& face : affected) {
        if (face.vertices.empty()) continue;
        if (!working.addFace(face.vertices, error)) return std::nullopt;
    }

    if (!working.validate(error)) return std::nullopt;
    *this = std::move(working);
    if (error) error->clear();
    return target;
}

std::optional<EditableVertexWeldResult> EditableMesh::weldVertices(
    std::span<const EditableVertexId> vertices, float distance,
    std::optional<EditableVertexId> preferredTarget, std::string* error) {
    if (!std::isfinite(distance) || distance <= 0.0F) {
        if (error) *error = "Weld distance must be finite and positive";
        return std::nullopt;
    }

    std::set<EditableVertexId> uniqueSet(vertices.begin(), vertices.end());
    if (uniqueSet.size() < 2U) {
        if (error) *error = "Weld requires at least two unique vertices";
        return std::nullopt;
    }
    if (preferredTarget && !uniqueSet.contains(*preferredTarget)) {
        if (error) *error = "Preferred weld target must be selected";
        return std::nullopt;
    }

    std::vector<EditableVertexId> unique(uniqueSet.begin(), uniqueSet.end());
    std::vector<Vec3> positions;
    positions.reserve(unique.size());
    for (const auto id : unique) {
        const auto* vertex = findVertex(id);
        if (!vertex) {
            if (error) *error = "Weld selection contains a missing vertex";
            return std::nullopt;
        }
        positions.push_back(vertex->position);
    }

    std::vector<std::size_t> parent(unique.size());
    for (std::size_t index = 0; index < parent.size(); ++index) parent[index] = index;
    const auto findRoot = [&parent](std::size_t index) {
        std::size_t root = index;
        while (parent[root] != root) root = parent[root];
        while (parent[index] != index) {
            const std::size_t next = parent[index];
            parent[index] = root;
            index = next;
        }
        return root;
    };
    const float distanceSquared = distance * distance;
    for (std::size_t left = 0; left < unique.size(); ++left) {
        for (std::size_t right = left + 1U; right < unique.size(); ++right) {
            const float dx = positions[left].x - positions[right].x;
            const float dy = positions[left].y - positions[right].y;
            const float dz = positions[left].z - positions[right].z;
            if (dx * dx + dy * dy + dz * dz > distanceSquared) continue;
            const std::size_t leftRoot = findRoot(left);
            const std::size_t rightRoot = findRoot(right);
            if (leftRoot != rightRoot) parent[rightRoot] = leftRoot;
        }
    }

    std::map<std::size_t, std::vector<EditableVertexId>> groups;
    for (std::size_t index = 0; index < unique.size(); ++index) groups[findRoot(index)].push_back(unique[index]);

    EditableMesh working = *this;
    EditableVertexWeldResult result;
    result.survivors.reserve(groups.size());
    for (auto& [_, group] : groups) {
        std::sort(group.begin(), group.end());
        EditableVertexId representative = group.front();
        if (preferredTarget && std::find(group.cbegin(), group.cend(), *preferredTarget) != group.cend()) {
            representative = *preferredTarget;
        }
        if (group.size() > 1U) {
            if (!working.mergeVertices(group, representative, error)) return std::nullopt;
            result.mergedCount += group.size() - 1U;
        }
        result.survivors.push_back(representative);
    }

    std::sort(result.survivors.begin(), result.survivors.end());
    if (result.mergedCount > 0U) {
        if (!working.validate(error)) return std::nullopt;
        *this = std::move(working);
    }
    if (error) error->clear();
    return result;
}


std::optional<EditableFaceId> EditableMesh::fillBoundaryLoop(
    std::span<const EditableEdgeId> edges, std::string* error) {
    const auto loops = selectedBoundaryLoops(*this, edges, error);
    if (!loops) return std::nullopt;
    if (loops->size() != 1U) {
        if (error) *error = "Fill requires exactly one closed boundary loop";
        return std::nullopt;
    }

    EditableMesh working = *this;
    std::vector<EditableVertexId> faceVertices = loops->front();
    std::reverse(faceVertices.begin(), faceVertices.end());
    const auto face = working.addFace(faceVertices, error);
    if (!face || !working.validate(error)) return std::nullopt;

    *this = std::move(working);
    if (error) error->clear();
    return face;
}

std::optional<std::vector<EditableFaceId>> EditableMesh::bridgeBoundaryLoops(
    std::span<const EditableEdgeId> edges, std::string* error) {
    const auto loops = selectedBoundaryLoops(*this, edges, error);
    if (!loops) return std::nullopt;
    if (loops->size() != 2U) {
        if (error) *error = "Bridge currently requires exactly two closed boundary loops";
        return std::nullopt;
    }
    const auto& first = (*loops)[0];
    const auto& second = (*loops)[1];
    if (first.size() != second.size()) {
        if (error) *error = "Bridge currently requires boundary loops with the same vertex count";
        return std::nullopt;
    }
    if (first.size() < 3U) {
        if (error) *error = "Bridge loops must contain at least three vertices";
        return std::nullopt;
    }

    std::set<EditableVertexId> firstVertices(first.begin(), first.end());
    for (const auto vertex : second) {
        if (firstVertices.contains(vertex)) {
            if (error) *error = "Bridge boundary loops must be vertex-disjoint";
            return std::nullopt;
        }
    }

    std::vector<Vec3> firstPositions;
    std::vector<Vec3> secondPositions;
    firstPositions.reserve(first.size());
    secondPositions.reserve(second.size());
    for (const auto vertex : first) {
        const auto* value = findVertex(vertex);
        if (!value) {
            if (error) *error = "Bridge loop contains a missing vertex";
            return std::nullopt;
        }
        firstPositions.push_back(value->position);
    }
    for (const auto vertex : second) {
        const auto* value = findVertex(vertex);
        if (!value) {
            if (error) *error = "Bridge loop contains a missing vertex";
            return std::nullopt;
        }
        secondPositions.push_back(value->position);
    }

    const std::size_t count = first.size();
    std::size_t bestOffset = 0U;
    float bestCost = std::numeric_limits<float>::infinity();
    for (std::size_t offset = 0; offset < count; ++offset) {
        float cost = 0.0F;
        for (std::size_t index = 0; index < count; ++index) {
            const std::size_t secondIndex = (offset + count - index) % count;
            cost += squaredDistance(firstPositions[index], secondPositions[secondIndex]);
        }
        if (cost < bestCost) {
            bestCost = cost;
            bestOffset = offset;
        }
    }

    EditableMesh working = *this;
    std::vector<EditableFaceId> created;
    created.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t nextIndex = (index + 1U) % count;
        const std::size_t secondIndex = (bestOffset + count - index) % count;
        const std::size_t secondNext = (bestOffset + count - nextIndex) % count;
        const std::array<EditableVertexId, 4> quad{
            first[index], second[secondIndex], second[secondNext], first[nextIndex]
        };
        const auto face = working.addFace(quad, error);
        if (!face) return std::nullopt;
        created.push_back(*face);
    }
    if (!working.validate(error)) return std::nullopt;

    *this = std::move(working);
    if (error) error->clear();
    return created;
}


std::optional<EditableLoopCutResult> EditableMesh::loopCut(
    EditableEdgeId startEdge, std::string* error) {
    if (startEdge.isNull() || !findEdge(startEdge)) {
        if (error) *error = "Loop Cut start edge does not exist";
        return std::nullopt;
    }

    struct QuadData final {
        EditableFaceId face{};
        std::array<EditableVertexId, 4> vertices{};
        std::array<EditableEdgeId, 4> edges{};
    };

    const auto readQuad = [this, error](EditableFaceId faceId) -> std::optional<QuadData> {
        const auto* face = findFace(faceId);
        if (!face) {
            if (error) *error = "Loop Cut encountered a missing face";
            return std::nullopt;
        }
        QuadData data;
        data.face = faceId;
        EditableHalfEdgeId current = face->halfEdge;
        for (std::size_t index = 0; index < 4U; ++index) {
            const auto* halfEdge = findHalfEdge(current);
            if (!halfEdge || halfEdge->face != faceId) {
                if (error) *error = "Loop Cut encountered an invalid quad loop";
                return std::nullopt;
            }
            data.vertices[index] = halfEdge->origin;
            data.edges[index] = halfEdge->edge;
            current = halfEdge->next;
        }
        if (current != face->halfEdge) {
            if (error) *error = "Loop Cut currently supports quad faces only";
            return std::nullopt;
        }
        return data;
    };

    std::set<EditableEdgeId> ringEdges{startEdge};
    std::map<EditableFaceId, QuadData> ringFaces;
    std::deque<EditableEdgeId> pending{startEdge};
    while (!pending.empty()) {
        const EditableEdgeId currentEdge = pending.front();
        pending.pop_front();
        const auto* edge = findEdge(currentEdge);
        const auto* representative = edge ? findHalfEdge(edge->halfEdge) : nullptr;
        if (!edge || !representative) {
            if (error) *error = "Loop Cut ring contains an invalid edge";
            return std::nullopt;
        }

        std::array<EditableHalfEdgeId, 2> incident{representative->id, representative->twin};
        for (const auto halfEdgeId : incident) {
            if (halfEdgeId.isNull()) continue;
            const auto* halfEdge = findHalfEdge(halfEdgeId);
            if (!halfEdge) {
                if (error) *error = "Loop Cut ring contains an invalid half-edge";
                return std::nullopt;
            }
            if (ringFaces.contains(halfEdge->face)) continue;
            const auto quad = readQuad(halfEdge->face);
            if (!quad) return std::nullopt;

            std::optional<std::size_t> edgeIndex;
            for (std::size_t index = 0; index < 4U; ++index) {
                if (quad->edges[index] == currentEdge) {
                    edgeIndex = index;
                    break;
                }
            }
            if (!edgeIndex) {
                if (error) *error = "Loop Cut could not locate the traversed edge in its face";
                return std::nullopt;
            }
            const EditableEdgeId opposite = quad->edges[(*edgeIndex + 2U) % 4U];
            if (opposite.isNull() || opposite == currentEdge) {
                if (error) *error = "Loop Cut found an invalid opposite quad edge";
                return std::nullopt;
            }
            ringFaces.emplace(quad->face, *quad);
            if (ringEdges.insert(opposite).second) pending.push_back(opposite);
        }
    }
    if (ringFaces.empty() || ringEdges.size() < 2U) {
        if (error) *error = "Loop Cut did not discover a valid quad ring";
        return std::nullopt;
    }

    for (const auto& [_, quad] : ringFaces) {
        std::size_t ringCount = 0U;
        std::array<std::size_t, 2> indices{};
        for (std::size_t index = 0; index < 4U; ++index) {
            if (!ringEdges.contains(quad.edges[index])) continue;
            if (ringCount < indices.size()) indices[ringCount] = index;
            ++ringCount;
        }
        if (ringCount != 2U || (indices[0] + 2U) % 4U != indices[1]) {
            if (error) *error = "Loop Cut ring does not cross each quad through opposite edges";
            return std::nullopt;
        }
    }

    EditableMesh working = *this;
    std::map<EditableEdgeId, EditableVertexId> midpointByEdge;
    EditableLoopCutResult result;
    result.vertices.reserve(ringEdges.size());
    for (const auto edgeId : ringEdges) {
        const auto* edge = findEdge(edgeId);
        const auto* halfEdge = edge ? findHalfEdge(edge->halfEdge) : nullptr;
        const auto* next = halfEdge ? findHalfEdge(halfEdge->next) : nullptr;
        const auto* first = halfEdge ? findVertex(halfEdge->origin) : nullptr;
        const auto* second = next ? findVertex(next->origin) : nullptr;
        if (!edge || !halfEdge || !next || !first || !second) {
            if (error) *error = "Loop Cut could not resolve an edge midpoint";
            return std::nullopt;
        }
        const Vec3 midpoint{
            (first->position.x + second->position.x) * 0.5F,
            (first->position.y + second->position.y) * 0.5F,
            (first->position.z + second->position.z) * 0.5F,
        };
        const EditableVertexId vertex = working.addVertex(midpoint);
        midpointByEdge.emplace(edgeId, vertex);
        result.vertices.push_back(vertex);
    }

    for (const auto& [faceId, _] : ringFaces) {
        if (!working.removeFace(faceId, error)) return std::nullopt;
    }

    std::set<EditableEdgeId> cutEdges;
    result.faces.reserve(ringFaces.size() * 2U);
    for (const auto& [_, quad] : ringFaces) {
        std::size_t firstRingIndex = 0U;
        bool found = false;
        for (std::size_t index = 0; index < 4U; ++index) {
            if (!ringEdges.contains(quad.edges[index])) continue;
            firstRingIndex = index;
            found = true;
            break;
        }
        if (!found) {
            if (error) *error = "Loop Cut lost a quad ring edge during reconstruction";
            return std::nullopt;
        }
        const std::size_t oppositeIndex = (firstRingIndex + 2U) % 4U;
        const EditableVertexId v0 = quad.vertices[firstRingIndex];
        const EditableVertexId v1 = quad.vertices[(firstRingIndex + 1U) % 4U];
        const EditableVertexId v2 = quad.vertices[(firstRingIndex + 2U) % 4U];
        const EditableVertexId v3 = quad.vertices[(firstRingIndex + 3U) % 4U];
        const EditableVertexId m0 = midpointByEdge.at(quad.edges[firstRingIndex]);
        const EditableVertexId m1 = midpointByEdge.at(quad.edges[oppositeIndex]);
        const std::array<EditableVertexId, 4> firstQuad{v0, m0, m1, v3};
        const std::array<EditableVertexId, 4> secondQuad{m0, v1, v2, m1};
        const auto firstFace = working.addFace(firstQuad, error);
        const auto secondFace = firstFace ? working.addFace(secondQuad, error) : std::nullopt;
        if (!firstFace || !secondFace) return std::nullopt;
        result.faces.push_back(*firstFace);
        result.faces.push_back(*secondFace);

        const auto direct = working.directedEdges_.find(DirectedEdgeKey{m0.value, m1.value});
        const auto reverse = working.directedEdges_.find(DirectedEdgeKey{m1.value, m0.value});
        const EditableHalfEdgeId cutHalfEdge = direct != working.directedEdges_.end()
            ? direct->second
            : (reverse != working.directedEdges_.end() ? reverse->second : EditableHalfEdgeId{});
        const auto* halfEdge = working.findHalfEdge(cutHalfEdge);
        if (!halfEdge) {
            if (error) *error = "Loop Cut failed to locate the new cut edge";
            return std::nullopt;
        }
        cutEdges.insert(halfEdge->edge);
    }

    if (!working.validate(error)) return std::nullopt;
    result.edges.assign(cutEdges.begin(), cutEdges.end());
    *this = std::move(working);
    if (error) error->clear();
    return result;
}


bool EditableMesh::deleteFaces(std::span<const EditableFaceId> facesToDelete,
                               std::string* error) {
    std::set<EditableFaceId> unique(facesToDelete.begin(), facesToDelete.end());
    if (unique.empty()) {
        if (error) *error = "Delete Faces requires at least one selected face";
        return false;
    }
    for (const auto face : unique) {
        if (!findFace(face)) {
            if (error) *error = "Delete Faces selection contains a missing face";
            return false;
        }
    }
    if (unique.size() >= faceCount_) {
        if (error) *error = "Delete Faces cannot remove the last editable face";
        return false;
    }

    EditableMesh working = *this;
    for (const auto face : unique) {
        if (!working.removeFace(face, error)) return false;
    }
    if (!working.validate(error)) return false;
    *this = std::move(working);
    if (error) error->clear();
    return true;
}

bool EditableMesh::deleteEdges(std::span<const EditableEdgeId> edgesToDelete,
                               std::string* error) {
    std::set<EditableEdgeId> unique(edgesToDelete.begin(), edgesToDelete.end());
    if (unique.empty()) {
        if (error) *error = "Delete Edges requires at least one selected edge";
        return false;
    }

    std::set<EditableFaceId> incidentFaces;
    for (const auto edgeId : unique) {
        const auto* edge = findEdge(edgeId);
        const auto* halfEdge = edge ? findHalfEdge(edge->halfEdge) : nullptr;
        if (!edge || !halfEdge) {
            if (error) *error = "Delete Edges selection contains a missing edge";
            return false;
        }
        incidentFaces.insert(halfEdge->face);
        if (!halfEdge->twin.isNull()) {
            const auto* twin = findHalfEdge(halfEdge->twin);
            if (!twin) {
                if (error) *error = "Delete Edges encountered an invalid twin";
                return false;
            }
            incidentFaces.insert(twin->face);
        }
    }
    if (incidentFaces.empty()) {
        if (error) *error = "Delete Edges found no incident editable faces";
        return false;
    }
    if (incidentFaces.size() >= faceCount_) {
        if (error) *error = "Delete Edges cannot remove the last editable face";
        return false;
    }

    EditableMesh working = *this;
    for (const auto face : incidentFaces) {
        if (working.findFace(face) && !working.removeFace(face, error)) return false;
    }
    if (!working.validate(error)) return false;
    *this = std::move(working);
    if (error) error->clear();
    return true;
}

bool EditableMesh::deleteVertices(std::span<const EditableVertexId> verticesToDelete,
                                  std::string* error) {
    std::set<EditableVertexId> unique(verticesToDelete.begin(), verticesToDelete.end());
    if (unique.empty()) {
        if (error) *error = "Delete Vertices requires at least one selected vertex";
        return false;
    }
    for (const auto vertex : unique) {
        if (!findVertex(vertex)) {
            if (error) *error = "Delete Vertices selection contains a missing vertex";
            return false;
        }
    }
    if (unique.size() >= vertexCount_) {
        if (error) *error = "Delete Vertices cannot remove every editable vertex";
        return false;
    }

    std::set<EditableFaceId> incidentFaces;
    for (const auto& face : faces()) {
        const auto loop = faceVertices(face.id);
        if (std::any_of(loop.cbegin(), loop.cend(), [&unique](EditableVertexId vertex) {
                return unique.contains(vertex);
            })) {
            incidentFaces.insert(face.id);
        }
    }
    if (incidentFaces.size() >= faceCount_) {
        if (error) *error = "Delete Vertices cannot remove the last editable face";
        return false;
    }

    EditableMesh working = *this;
    for (const auto face : incidentFaces) {
        if (working.findFace(face) && !working.removeFace(face, error)) return false;
    }

    for (const auto vertexId : unique) {
        for (const auto& halfEdge : working.halfEdges()) {
            if (halfEdge.origin == vertexId || working.destination(halfEdge.id) == vertexId) {
                if (error) *error = "Delete Vertices left a selected vertex referenced by topology";
                return false;
            }
        }
        if (vertexId.isNull() || static_cast<std::size_t>(vertexId.value) > working.vertices_.size()) {
            if (error) *error = "Delete Vertices encountered an invalid vertex slot";
            return false;
        }
        auto& slot = working.vertices_[static_cast<std::size_t>(vertexId.value - 1U)];
        if (!slot) {
            if (error) *error = "Delete Vertices encountered an empty vertex slot";
            return false;
        }
        slot.reset();
        --working.vertexCount_;
    }

    if (!working.validate(error)) return false;
    *this = std::move(working);
    if (error) error->clear();
    return true;
}

} // namespace m3d
