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

std::optional<std::vector<EditableFaceId>> EditableMesh::gridFillBoundaryLoop(
    std::span<const EditableEdgeId> edges, std::uint32_t span, std::uint32_t offset,
    std::string* error) {
    const auto loops = selectedBoundaryLoops(*this, edges, error);
    if (!loops) return std::nullopt;
    if (loops->size() != 1U) {
        if (error) *error = "Grid Fill requires exactly one closed boundary loop";
        return std::nullopt;
    }
    const auto& sourceLoop = loops->front();
    const std::size_t perimeter = sourceLoop.size();
    if (perimeter < 4U || perimeter % 2U != 0U) {
        if (error) *error = "Grid Fill requires an even boundary with at least four edges";
        return std::nullopt;
    }

    const std::size_t halfPerimeter = perimeter / 2U;
    const std::size_t columns = static_cast<std::size_t>(span);
    if (columns == 0U || columns >= halfPerimeter) {
        if (error) *error = "Grid Fill span must be between 1 and half the boundary minus 1";
        return std::nullopt;
    }
    const std::size_t rows = halfPerimeter - columns;
    const std::size_t normalizedOffset = static_cast<std::size_t>(offset) % perimeter;

    std::vector<EditableVertexId> loop;
    loop.reserve(perimeter);
    for (std::size_t index = 0; index < perimeter; ++index) {
        loop.push_back(sourceLoop[(normalizedOffset + index) % perimeter]);
    }

    const std::size_t gridWidth = columns + 1U;
    const std::size_t gridHeight = rows + 1U;
    std::vector<EditableVertexId> grid(gridWidth * gridHeight);
    const auto gridIndex = [gridWidth](std::size_t row, std::size_t column) {
        return row * gridWidth + column;
    };

    for (std::size_t column = 0; column <= columns; ++column) {
        grid[gridIndex(0U, column)] = loop[column % perimeter];
    }
    for (std::size_t row = 0; row <= rows; ++row) {
        grid[gridIndex(row, columns)] = loop[(columns + row) % perimeter];
    }
    for (std::size_t column = 0; column <= columns; ++column) {
        grid[gridIndex(rows, columns - column)] = loop[(columns + rows + column) % perimeter];
    }
    for (std::size_t row = 0; row <= rows; ++row) {
        grid[gridIndex(rows - row, 0U)] = loop[(2U * columns + rows + row) % perimeter];
    }

    const auto positionOf = [this, error](EditableVertexId vertexId) -> std::optional<Vec3> {
        const auto* vertex = findVertex(vertexId);
        if (!vertex) {
            if (error) *error = "Grid Fill boundary contains a missing vertex";
            return std::nullopt;
        }
        return vertex->position;
    };
    const auto p00 = positionOf(grid[gridIndex(0U, 0U)]);
    const auto p10 = positionOf(grid[gridIndex(0U, columns)]);
    const auto p01 = positionOf(grid[gridIndex(rows, 0U)]);
    const auto p11 = positionOf(grid[gridIndex(rows, columns)]);
    if (!p00 || !p10 || !p01 || !p11) return std::nullopt;

    EditableMesh working = *this;
    const auto add = [](Vec3 left, Vec3 right) noexcept {
        return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
    };
    const auto scale = [](Vec3 value, float factor) noexcept {
        return Vec3{value.x * factor, value.y * factor, value.z * factor};
    };
    const auto subtract = [](Vec3 left, Vec3 right) noexcept {
        return Vec3{left.x - right.x, left.y - right.y, left.z - right.z};
    };

    for (std::size_t row = 1U; row < rows; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(rows);
        const auto left = positionOf(grid[gridIndex(row, 0U)]);
        const auto right = positionOf(grid[gridIndex(row, columns)]);
        if (!left || !right) return std::nullopt;
        for (std::size_t column = 1U; column < columns; ++column) {
            const float u = static_cast<float>(column) / static_cast<float>(columns);
            const auto top = positionOf(grid[gridIndex(0U, column)]);
            const auto bottom = positionOf(grid[gridIndex(rows, column)]);
            if (!top || !bottom) return std::nullopt;

            Vec3 blended = add(add(scale(*top, 1.0F - v), scale(*bottom, v)),
                               add(scale(*left, 1.0F - u), scale(*right, u)));
            Vec3 corners{};
            corners = add(corners, scale(*p00, (1.0F - u) * (1.0F - v)));
            corners = add(corners, scale(*p10, u * (1.0F - v)));
            corners = add(corners, scale(*p01, (1.0F - u) * v));
            corners = add(corners, scale(*p11, u * v));
            const EditableVertexId vertex = working.addVertex(subtract(blended, corners));
            grid[gridIndex(row, column)] = vertex;
        }
    }

    std::vector<EditableFaceId> created;
    created.reserve(rows * columns);
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            const std::array<EditableVertexId, 4> quad{
                grid[gridIndex(row, column)],
                grid[gridIndex(row + 1U, column)],
                grid[gridIndex(row + 1U, column + 1U)],
                grid[gridIndex(row, column + 1U)],
            };
            const auto face = working.addFace(quad, error);
            if (!face) return std::nullopt;
            if (!working.faceNormal(*face)) {
                if (error) *error = "Grid Fill generated a degenerate quad";
                return std::nullopt;
            }
            created.push_back(*face);
        }
    }
    if (created.empty() || !working.validate(error)) return std::nullopt;

    *this = std::move(working);
    if (error) error->clear();
    return created;
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


std::optional<std::vector<EditableFaceId>> EditableMesh::bridgeBoundaryLoopsAdaptive(
    std::span<const EditableEdgeId> edges, std::string* error) {
    const auto loops = selectedBoundaryLoops(*this, edges, error);
    if (!loops) return std::nullopt;
    if (loops->size() != 2U) {
        if (error) *error = "Bridge requires exactly two closed boundary loops";
        return std::nullopt;
    }
    const auto& first = (*loops)[0];
    const auto& second = (*loops)[1];
    if (first.size() < 3U || second.size() < 3U) {
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

    const std::size_t firstCount = first.size();
    const std::size_t secondCount = second.size();
    std::size_t bestOffset = 0U;
    double bestCost = std::numeric_limits<double>::infinity();
    for (std::size_t offset = 0; offset < secondCount; ++offset) {
        double cost = 0.0;
        for (std::size_t index = 0; index < firstCount; ++index) {
            const std::size_t normalizedSecond =
                ((index * secondCount + firstCount / 2U) / firstCount) % secondCount;
            const std::size_t secondIndex =
                (offset + secondCount - normalizedSecond) % secondCount;
            cost += static_cast<double>(squaredDistance(firstPositions[index], secondPositions[secondIndex]));
        }
        for (std::size_t index = 0; index < secondCount; ++index) {
            const std::size_t normalizedFirst =
                ((index * firstCount + secondCount / 2U) / secondCount) % firstCount;
            const std::size_t secondIndex = (offset + secondCount - index) % secondCount;
            cost += static_cast<double>(squaredDistance(firstPositions[normalizedFirst], secondPositions[secondIndex]));
        }
        if (cost < bestCost) {
            bestCost = cost;
            bestOffset = offset;
        }
    }

    std::vector<EditableVertexId> alignedSecond;
    alignedSecond.reserve(secondCount);
    for (std::size_t index = 0; index < secondCount; ++index) {
        alignedSecond.push_back(second[(bestOffset + secondCount - index) % secondCount]);
    }

    EditableMesh working = *this;
    std::vector<EditableFaceId> created;
    created.reserve(firstCount + secondCount);
    std::size_t firstIndex = 0U;
    std::size_t secondIndex = 0U;
    constexpr double kProgressEpsilon = 1.0e-12;
    while (firstIndex < firstCount || secondIndex < secondCount) {
        const double nextFirst = firstIndex < firstCount
            ? static_cast<double>(firstIndex + 1U) / static_cast<double>(firstCount)
            : std::numeric_limits<double>::infinity();
        const double nextSecond = secondIndex < secondCount
            ? static_cast<double>(secondIndex + 1U) / static_cast<double>(secondCount)
            : std::numeric_limits<double>::infinity();

        const EditableVertexId a = first[firstIndex % firstCount];
        const EditableVertexId b = alignedSecond[secondIndex % secondCount];
        std::optional<EditableFaceId> face;
        if (std::abs(nextFirst - nextSecond) <= kProgressEpsilon) {
            const std::array<EditableVertexId, 4> quad{
                a,
                b,
                alignedSecond[(secondIndex + 1U) % secondCount],
                first[(firstIndex + 1U) % firstCount],
            };
            face = working.addFace(quad, error);
            ++firstIndex;
            ++secondIndex;
        } else if (nextFirst < nextSecond) {
            const std::array<EditableVertexId, 3> triangle{
                a,
                b,
                first[(firstIndex + 1U) % firstCount],
            };
            face = working.addFace(triangle, error);
            ++firstIndex;
        } else {
            const std::array<EditableVertexId, 3> triangle{
                a,
                b,
                alignedSecond[(secondIndex + 1U) % secondCount],
            };
            face = working.addFace(triangle, error);
            ++secondIndex;
        }
        if (!face) return std::nullopt;
        if (!working.faceNormal(*face)) {
            if (error) *error = "Bridge generated a degenerate side face";
            return std::nullopt;
        }
        created.push_back(*face);
    }

    if (created.empty() || !working.validate(error)) return std::nullopt;
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


std::optional<EditableLoopCutResult> EditableMesh::loopCut(
    EditableEdgeId startEdge, std::uint32_t cuts, std::string* error) {
    constexpr std::uint32_t kMaximumCuts = 32U;
    if (cuts == 0U || cuts > kMaximumCuts) {
        if (error) *error = "Loop Cut count must be between 1 and 32";
        return std::nullopt;
    }
    if (startEdge.isNull() || !findEdge(startEdge)) {
        if (error) *error = "Loop Cut start edge does not exist";
        return std::nullopt;
    }

    struct QuadData final {
        EditableFaceId face{};
        std::array<EditableVertexId, 4> vertices{};
        std::array<EditableEdgeId, 4> edges{};
    };
    struct SplitEdge final {
        EditableVertexId start{};
        EditableVertexId end{};
        std::vector<EditableVertexId> points;
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
        const std::array<EditableHalfEdgeId, 2> incident{representative->id, representative->twin};
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
                if (quad->edges[index] == currentEdge) { edgeIndex = index; break; }
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
        std::vector<std::size_t> indices;
        for (std::size_t index = 0; index < 4U; ++index) {
            if (ringEdges.contains(quad.edges[index])) indices.push_back(index);
        }
        if (indices.size() != 2U || (indices[0] + 2U) % 4U != indices[1]) {
            if (error) *error = "Loop Cut ring does not cross each quad through opposite edges";
            return std::nullopt;
        }
    }

    EditableMesh working = *this;
    std::map<EditableEdgeId, SplitEdge> splits;
    EditableLoopCutResult result;
    result.vertices.reserve(ringEdges.size() * static_cast<std::size_t>(cuts));
    for (const auto edgeId : ringEdges) {
        const auto* edge = findEdge(edgeId);
        const auto* halfEdge = edge ? findHalfEdge(edge->halfEdge) : nullptr;
        const auto* next = halfEdge ? findHalfEdge(halfEdge->next) : nullptr;
        const auto* first = halfEdge ? findVertex(halfEdge->origin) : nullptr;
        const auto* second = next ? findVertex(next->origin) : nullptr;
        if (!edge || !halfEdge || !next || !first || !second) {
            if (error) *error = "Loop Cut could not resolve a ring edge";
            return std::nullopt;
        }
        SplitEdge split;
        split.start = first->id;
        split.end = second->id;
        split.points.reserve(cuts);
        for (std::uint32_t cut = 1U; cut <= cuts; ++cut) {
            const float t = static_cast<float>(cut) / static_cast<float>(cuts + 1U);
            const Vec3 point{
                first->position.x + (second->position.x - first->position.x) * t,
                first->position.y + (second->position.y - first->position.y) * t,
                first->position.z + (second->position.z - first->position.z) * t,
            };
            const EditableVertexId vertex = working.addVertex(point);
            split.points.push_back(vertex);
            result.vertices.push_back(vertex);
        }
        splits.emplace(edgeId, std::move(split));
    }

    for (const auto& [faceId, _] : ringFaces) {
        if (!working.removeFace(faceId, error)) return std::nullopt;
    }

    const auto orientedPoints = [&splits, error](EditableEdgeId edgeId,
                                                 EditableVertexId start,
                                                 EditableVertexId end)
        -> std::optional<std::vector<EditableVertexId>> {
        const auto found = splits.find(edgeId);
        if (found == splits.end()) {
            if (error) *error = "Loop Cut lost split data for a ring edge";
            return std::nullopt;
        }
        if (found->second.start == start && found->second.end == end) return found->second.points;
        if (found->second.start == end && found->second.end == start) {
            auto reversed = found->second.points;
            std::reverse(reversed.begin(), reversed.end());
            return reversed;
        }
        if (error) *error = "Loop Cut edge orientation is inconsistent with its face";
        return std::nullopt;
    };

    std::set<EditableEdgeId> cutEdges;
    result.faces.reserve(ringFaces.size() * static_cast<std::size_t>(cuts + 1U));
    for (const auto& [_, quad] : ringFaces) {
        std::size_t firstRingIndex = 0U;
        bool foundRing = false;
        for (std::size_t index = 0; index < 4U; ++index) {
            if (ringEdges.contains(quad.edges[index])) {
                firstRingIndex = index;
                foundRing = true;
                break;
            }
        }
        if (!foundRing) {
            if (error) *error = "Loop Cut lost a ring edge during reconstruction";
            return std::nullopt;
        }
        const std::size_t oppositeIndex = (firstRingIndex + 2U) % 4U;
        const EditableVertexId v0 = quad.vertices[firstRingIndex];
        const EditableVertexId v1 = quad.vertices[(firstRingIndex + 1U) % 4U];
        const EditableVertexId v2 = quad.vertices[(firstRingIndex + 2U) % 4U];
        const EditableVertexId v3 = quad.vertices[(firstRingIndex + 3U) % 4U];
        const auto aPoints = orientedPoints(quad.edges[firstRingIndex], v0, v1);
        const auto bPoints = orientedPoints(quad.edges[oppositeIndex], v3, v2);
        if (!aPoints || !bPoints || aPoints->size() != cuts || bPoints->size() != cuts) return std::nullopt;

        std::vector<EditableVertexId> sideA;
        std::vector<EditableVertexId> sideB;
        sideA.reserve(static_cast<std::size_t>(cuts) + 2U);
        sideB.reserve(static_cast<std::size_t>(cuts) + 2U);
        sideA.push_back(v0);
        sideA.insert(sideA.end(), aPoints->begin(), aPoints->end());
        sideA.push_back(v1);
        sideB.push_back(v3);
        sideB.insert(sideB.end(), bPoints->begin(), bPoints->end());
        sideB.push_back(v2);

        for (std::size_t strip = 0; strip + 1U < sideA.size(); ++strip) {
            const std::array<EditableVertexId, 4> newQuad{
                sideA[strip], sideA[strip + 1U], sideB[strip + 1U], sideB[strip]
            };
            const auto face = working.addFace(newQuad, error);
            if (!face) return std::nullopt;
            result.faces.push_back(*face);
        }
        for (std::uint32_t cut = 1U; cut <= cuts; ++cut) {
            const EditableVertexId a = sideA[cut];
            const EditableVertexId b = sideB[cut];
            const auto direct = working.directedEdges_.find(DirectedEdgeKey{a.value, b.value});
            const auto reverse = working.directedEdges_.find(DirectedEdgeKey{b.value, a.value});
            const EditableHalfEdgeId cutHalfEdge = direct != working.directedEdges_.end()
                ? direct->second
                : (reverse != working.directedEdges_.end() ? reverse->second : EditableHalfEdgeId{});
            const auto* halfEdge = working.findHalfEdge(cutHalfEdge);
            if (!halfEdge) {
                if (error) *error = "Loop Cut failed to locate a generated cut edge";
                return std::nullopt;
            }
            cutEdges.insert(halfEdge->edge);
        }
    }

    if (!working.validate(error)) return std::nullopt;
    result.edges.assign(cutEdges.begin(), cutEdges.end());
    *this = std::move(working);
    if (error) error->clear();
    return result;
}


std::optional<EditableBevelResult> EditableMesh::bevelEdge(
    EditableEdgeId edgeId, float width, std::string* error) {
    if (!std::isfinite(width) || width <= 1.0e-6F) {
        if (error) *error = "Bevel width must be finite and positive";
        return std::nullopt;
    }
    const auto* edge = findEdge(edgeId);
    const auto* firstHalfEdge = edge ? findHalfEdge(edge->halfEdge) : nullptr;
    const auto* firstNext = firstHalfEdge ? findHalfEdge(firstHalfEdge->next) : nullptr;
    const auto* secondHalfEdge = firstHalfEdge && !firstHalfEdge->twin.isNull()
        ? findHalfEdge(firstHalfEdge->twin) : nullptr;
    const auto* secondNext = secondHalfEdge ? findHalfEdge(secondHalfEdge->next) : nullptr;
    if (!edge || !firstHalfEdge || !firstNext || !secondHalfEdge || !secondNext) {
        if (error) *error = "Bevel baseline requires a closed manifold edge";
        return std::nullopt;
    }

    const EditableVertexId a = firstHalfEdge->origin;
    const EditableVertexId b = firstNext->origin;
    if (secondHalfEdge->origin != b || secondNext->origin != a) {
        if (error) *error = "Bevel edge twin orientation is inconsistent";
        return std::nullopt;
    }
    const EditableFaceId firstFace = firstHalfEdge->face;
    const EditableFaceId secondFace = secondHalfEdge->face;
    const auto firstLoop = faceVertices(firstFace);
    const auto secondLoop = faceVertices(secondFace);
    if (firstLoop.size() < 3U || secondLoop.size() < 3U) {
        if (error) *error = "Bevel requires non-degenerate incident faces";
        return std::nullopt;
    }

    const auto findDirectedIndex = [](const std::vector<EditableVertexId>& loop,
                                      EditableVertexId from, EditableVertexId to)
        -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < loop.size(); ++index) {
            if (loop[index] == from && loop[(index + 1U) % loop.size()] == to) return index;
        }
        return std::nullopt;
    };
    const auto firstIndex = findDirectedIndex(firstLoop, a, b);
    const auto secondIndex = findDirectedIndex(secondLoop, b, a);
    if (!firstIndex || !secondIndex) {
        if (error) *error = "Bevel could not locate the selected edge in both incident faces";
        return std::nullopt;
    }

    const EditableVertexId aNeighborFirst =
        firstLoop[(*firstIndex + firstLoop.size() - 1U) % firstLoop.size()];
    const EditableVertexId bNeighborFirst = firstLoop[(*firstIndex + 2U) % firstLoop.size()];
    const EditableVertexId bNeighborSecond =
        secondLoop[(*secondIndex + secondLoop.size() - 1U) % secondLoop.size()];
    const EditableVertexId aNeighborSecond = secondLoop[(*secondIndex + 2U) % secondLoop.size()];

    const auto incidentFaces = [this](EditableVertexId vertexId) {
        std::set<EditableFaceId> result;
        for (const auto& face : faces()) {
            const auto loop = faceVertices(face.id);
            if (std::find(loop.cbegin(), loop.cend(), vertexId) != loop.cend()) result.insert(face.id);
        }
        return result;
    };
    auto aFaces = incidentFaces(a);
    auto bFaces = incidentFaces(b);
    if (aFaces.size() != 3U || bFaces.size() != 3U) {
        if (error) *error = "Bevel baseline currently requires valence-3 edge endpoints";
        return std::nullopt;
    }
    aFaces.erase(firstFace); aFaces.erase(secondFace);
    bFaces.erase(firstFace); bFaces.erase(secondFace);
    if (aFaces.size() != 1U || bFaces.size() != 1U || *aFaces.begin() == *bFaces.begin()) {
        if (error) *error = "Bevel could not resolve unique endpoint cap faces";
        return std::nullopt;
    }
    const EditableFaceId aCapFace = *aFaces.begin();
    const EditableFaceId bCapFace = *bFaces.begin();
    const auto aCapLoop = faceVertices(aCapFace);
    const auto bCapLoop = faceVertices(bCapFace);

    const auto* aVertex = findVertex(a);
    const auto* bVertex = findVertex(b);
    const auto* aFirstNeighborVertex = findVertex(aNeighborFirst);
    const auto* aSecondNeighborVertex = findVertex(aNeighborSecond);
    const auto* bFirstNeighborVertex = findVertex(bNeighborFirst);
    const auto* bSecondNeighborVertex = findVertex(bNeighborSecond);
    if (!aVertex || !bVertex || !aFirstNeighborVertex || !aSecondNeighborVertex ||
        !bFirstNeighborVertex || !bSecondNeighborVertex) {
        if (error) *error = "Bevel references a missing adjacent vertex";
        return std::nullopt;
    }

    const auto offsetToward = [width, error](Vec3 origin, Vec3 target) -> std::optional<Vec3> {
        const float dx = target.x - origin.x;
        const float dy = target.y - origin.y;
        const float dz = target.z - origin.z;
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(length) || length <= 1.0e-7F) {
            if (error) *error = "Bevel encountered a zero-length adjacent edge";
            return std::nullopt;
        }
        if (width >= length * 0.49F) {
            if (error) *error = "Bevel width is too large for the local edge neighborhood";
            return std::nullopt;
        }
        const float factor = width / length;
        return Vec3{origin.x + dx * factor, origin.y + dy * factor, origin.z + dz * factor};
    };
    const auto aFirstPosition = offsetToward(aVertex->position, aFirstNeighborVertex->position);
    const auto aSecondPosition = offsetToward(aVertex->position, aSecondNeighborVertex->position);
    const auto bFirstPosition = offsetToward(bVertex->position, bFirstNeighborVertex->position);
    const auto bSecondPosition = offsetToward(bVertex->position, bSecondNeighborVertex->position);
    if (!aFirstPosition || !aSecondPosition || !bFirstPosition || !bSecondPosition) return std::nullopt;

    EditableMesh working = *this;
    const EditableVertexId aFirst = working.addVertex(*aFirstPosition);
    const EditableVertexId aSecond = working.addVertex(*aSecondPosition);
    const EditableVertexId bFirst = working.addVertex(*bFirstPosition);
    const EditableVertexId bSecond = working.addVertex(*bSecondPosition);

    const auto replaceSimple = [](const std::vector<EditableVertexId>& loop,
                                  EditableVertexId firstOld, EditableVertexId firstNew,
                                  EditableVertexId secondOld, EditableVertexId secondNew) {
        auto result = loop;
        for (auto& vertex : result) {
            if (vertex == firstOld) vertex = firstNew;
            else if (vertex == secondOld) vertex = secondNew;
        }
        return result;
    };
    const auto expandEndpoint = [error](const std::vector<EditableVertexId>& loop,
                                        EditableVertexId endpoint,
                                        EditableVertexId neighborOne, EditableVertexId replacementOne,
                                        EditableVertexId neighborTwo, EditableVertexId replacementTwo)
        -> std::optional<std::vector<EditableVertexId>> {
        const auto found = std::find(loop.cbegin(), loop.cend(), endpoint);
        if (found == loop.cend()) {
            if (error) *error = "Bevel endpoint cap face does not contain its endpoint";
            return std::nullopt;
        }
        const std::size_t index = static_cast<std::size_t>(std::distance(loop.cbegin(), found));
        const EditableVertexId previous = loop[(index + loop.size() - 1U) % loop.size()];
        const EditableVertexId next = loop[(index + 1U) % loop.size()];
        const auto replacementFor = [&](EditableVertexId neighbor) -> EditableVertexId {
            if (neighbor == neighborOne) return replacementOne;
            if (neighbor == neighborTwo) return replacementTwo;
            return {};
        };
        const EditableVertexId firstReplacement = replacementFor(previous);
        const EditableVertexId secondReplacement = replacementFor(next);
        if (firstReplacement.isNull() || secondReplacement.isNull() ||
            firstReplacement == secondReplacement) {
            if (error) *error = "Bevel endpoint cap adjacency is not valence-3 manifold topology";
            return std::nullopt;
        }
        std::vector<EditableVertexId> result;
        result.reserve(loop.size() + 1U);
        for (std::size_t loopIndex = 0; loopIndex < loop.size(); ++loopIndex) {
            if (loopIndex != index) result.push_back(loop[loopIndex]);
            else {
                result.push_back(firstReplacement);
                result.push_back(secondReplacement);
            }
        }
        return result;
    };

    const auto rebuiltFirst = replaceSimple(firstLoop, a, aFirst, b, bFirst);
    const auto rebuiltSecond = replaceSimple(secondLoop, b, bSecond, a, aSecond);
    const auto rebuiltACap = expandEndpoint(aCapLoop, a,
                                             aNeighborFirst, aFirst,
                                             aNeighborSecond, aSecond);
    const auto rebuiltBCap = expandEndpoint(bCapLoop, b,
                                             bNeighborFirst, bFirst,
                                             bNeighborSecond, bSecond);
    if (!rebuiltACap || !rebuiltBCap) return std::nullopt;

    const std::array<EditableFaceId, 4> affectedFaces{
        firstFace, secondFace, aCapFace, bCapFace
    };
    for (const auto face : affectedFaces) {
        if (!working.removeFace(face, error)) return std::nullopt;
    }

    for (const auto originalVertex : std::array<EditableVertexId, 2>{a, b}) {
        for (const auto& halfEdge : working.halfEdges()) {
            if (halfEdge.origin == originalVertex || working.destination(halfEdge.id) == originalVertex) {
                if (error) *error = "Bevel endpoint remained referenced after removing its valence-3 fan";
                return std::nullopt;
            }
        }
        auto& slot = working.vertices_[static_cast<std::size_t>(originalVertex.value - 1U)];
        if (!slot) {
            if (error) *error = "Bevel endpoint vertex slot disappeared unexpectedly";
            return std::nullopt;
        }
        slot.reset();
        --working.vertexCount_;
    }

    const auto firstReplacementFace = working.addFace(rebuiltFirst, error);
    const auto secondReplacementFace = firstReplacementFace ? working.addFace(rebuiltSecond, error) : std::nullopt;
    const auto aCapReplacementFace = secondReplacementFace ? working.addFace(*rebuiltACap, error) : std::nullopt;
    const auto bCapReplacementFace = aCapReplacementFace ? working.addFace(*rebuiltBCap, error) : std::nullopt;
    const std::array<EditableVertexId, 4> bevelLoop{aFirst, aSecond, bSecond, bFirst};
    const auto bevelFace = bCapReplacementFace ? working.addFace(bevelLoop, error) : std::nullopt;
    if (!firstReplacementFace || !secondReplacementFace || !aCapReplacementFace ||
        !bCapReplacementFace || !bevelFace) return std::nullopt;
    if (!working.faceNormal(*bevelFace)) {
        if (error) *error = "Bevel generated a degenerate chamfer face";
        return std::nullopt;
    }
    if (!working.validate(error)) return std::nullopt;

    *this = std::move(working);
    if (error) error->clear();
    return EditableBevelResult{*bevelFace, {aFirst, aSecond, bSecond, bFirst}};
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


std::optional<std::vector<EditableFaceId>> EditableMesh::flipFaceComponents(
    std::span<const EditableFaceId> seedFaces, std::string* error) {
    if (!validate(error)) return std::nullopt;
    std::set<EditableFaceId> seeds(seedFaces.begin(), seedFaces.end());
    if (seeds.empty()) {
        if (error) *error = "Flip Normals requires at least one seed face";
        return std::nullopt;
    }
    for (const auto face : seeds) {
        if (!findFace(face)) {
            if (error) *error = "Flip Normals selection contains a missing face";
            return std::nullopt;
        }
    }

    std::set<EditableFaceId> componentFaces;
    std::deque<EditableFaceId> pending(seeds.begin(), seeds.end());
    while (!pending.empty()) {
        const EditableFaceId faceId = pending.front();
        pending.pop_front();
        if (!componentFaces.insert(faceId).second) continue;
        const auto* face = findFace(faceId);
        if (!face) {
            if (error) *error = "Flip Normals encountered a missing component face";
            return std::nullopt;
        }
        EditableHalfEdgeId current = face->halfEdge;
        for (std::size_t guard = 0; guard <= halfEdgeCount_; ++guard) {
            const auto* halfEdge = findHalfEdge(current);
            if (!halfEdge || halfEdge->face != faceId) {
                if (error) *error = "Flip Normals encountered an invalid face loop";
                return std::nullopt;
            }
            if (!halfEdge->twin.isNull()) {
                const auto* twin = findHalfEdge(halfEdge->twin);
                if (!twin) {
                    if (error) *error = "Flip Normals encountered an invalid twin";
                    return std::nullopt;
                }
                if (!componentFaces.contains(twin->face)) pending.push_back(twin->face);
            }
            current = halfEdge->next;
            if (current == face->halfEdge) break;
            if (guard == halfEdgeCount_) {
                if (error) *error = "Flip Normals face traversal exceeded topology bounds";
                return std::nullopt;
            }
        }
    }

    std::vector<std::vector<EditableVertexId>> loops;
    loops.reserve(componentFaces.size());
    for (const auto face : componentFaces) {
        auto loop = faceVertices(face);
        if (loop.size() < 3U) {
            if (error) *error = "Flip Normals encountered a degenerate component face";
            return std::nullopt;
        }
        std::reverse(loop.begin(), loop.end());
        loops.push_back(std::move(loop));
    }

    EditableMesh working = *this;
    for (const auto face : componentFaces) {
        if (!working.removeFace(face, error)) return std::nullopt;
    }
    std::vector<EditableFaceId> flippedFaces;
    flippedFaces.reserve(loops.size());
    for (const auto& loop : loops) {
        const auto face = working.addFace(loop, error);
        if (!face) return std::nullopt;
        flippedFaces.push_back(*face);
    }
    if (!working.validate(error)) return std::nullopt;

    *this = std::move(working);
    if (error) error->clear();
    return flippedFaces;
}

std::optional<std::size_t> EditableMesh::recalculateOutside(std::string* error) {
    if (!validate(error)) return std::nullopt;

    struct ComponentInfo final {
        std::vector<EditableFaceId> faces;
        bool closed{true};
        double signedVolume{0.0};
    };

    std::set<EditableFaceId> visited;
    std::vector<ComponentInfo> components;
    for (const auto& rootFace : faces()) {
        if (visited.contains(rootFace.id)) continue;
        ComponentInfo component;
        std::deque<EditableFaceId> pending{rootFace.id};
        while (!pending.empty()) {
            const EditableFaceId faceId = pending.front();
            pending.pop_front();
            if (!visited.insert(faceId).second) continue;
            component.faces.push_back(faceId);
            const auto* face = findFace(faceId);
            if (!face) {
                if (error) *error = "Recalculate Outside encountered a missing face";
                return std::nullopt;
            }

            const auto loop = faceVertices(faceId);
            if (loop.size() < 3U) {
                if (error) *error = "Recalculate Outside encountered a degenerate face";
                return std::nullopt;
            }
            std::vector<Vec3> positions;
            positions.reserve(loop.size());
            for (const auto vertexId : loop) {
                const auto* vertex = findVertex(vertexId);
                if (!vertex) {
                    if (error) *error = "Recalculate Outside encountered a missing vertex";
                    return std::nullopt;
                }
                positions.push_back(vertex->position);
            }
            const auto cross = [](Vec3 left, Vec3 right) noexcept {
                return Vec3{
                    left.y * right.z - left.z * right.y,
                    left.z * right.x - left.x * right.z,
                    left.x * right.y - left.y * right.x,
                };
            };
            const auto dot = [](Vec3 left, Vec3 right) noexcept {
                return static_cast<double>(left.x) * right.x +
                       static_cast<double>(left.y) * right.y +
                       static_cast<double>(left.z) * right.z;
            };
            for (std::size_t index = 1U; index + 1U < positions.size(); ++index) {
                component.signedVolume += dot(positions[0], cross(positions[index], positions[index + 1U])) / 6.0;
            }

            EditableHalfEdgeId current = face->halfEdge;
            for (std::size_t guard = 0; guard <= halfEdgeCount_; ++guard) {
                const auto* halfEdge = findHalfEdge(current);
                if (!halfEdge) {
                    if (error) *error = "Recalculate Outside encountered an invalid half-edge";
                    return std::nullopt;
                }
                if (halfEdge->twin.isNull()) {
                    component.closed = false;
                } else {
                    const auto* twin = findHalfEdge(halfEdge->twin);
                    if (!twin) {
                        if (error) *error = "Recalculate Outside encountered an invalid twin";
                        return std::nullopt;
                    }
                    if (!visited.contains(twin->face)) pending.push_back(twin->face);
                }
                current = halfEdge->next;
                if (current == face->halfEdge) break;
                if (guard == halfEdgeCount_) {
                    if (error) *error = "Recalculate Outside traversal exceeded topology bounds";
                    return std::nullopt;
                }
            }
        }
        components.push_back(std::move(component));
    }

    std::vector<EditableFaceId> inwardSeeds;
    constexpr double kVolumeEpsilon = 1.0e-10;
    for (const auto& component : components) {
        if (!component.closed) continue;
        if (std::abs(component.signedVolume) <= kVolumeEpsilon) {
            if (error) *error = "Recalculate Outside found a closed component with ambiguous zero volume";
            return std::nullopt;
        }
        if (component.signedVolume < 0.0) inwardSeeds.push_back(component.faces.front());
    }

    EditableMesh working = *this;
    std::size_t flippedComponents = 0U;
    for (const auto seed : inwardSeeds) {
        const std::array<EditableFaceId, 1> seedArray{seed};
        if (!working.flipFaceComponents(seedArray, error)) return std::nullopt;
        ++flippedComponents;
    }
    if (flippedComponents > 0U) {
        if (!working.validate(error)) return std::nullopt;
        *this = std::move(working);
    }
    if (error) error->clear();
    return flippedComponents;
}

} // namespace m3d
