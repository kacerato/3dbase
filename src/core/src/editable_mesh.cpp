#include "mobile3d/core/editable_mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include <utility>

namespace m3d {
namespace {

[[nodiscard]] bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] std::optional<Vec3> newellNormal(const std::vector<Vec3>& positions) noexcept {
    if (positions.size() < 3U) return std::nullopt;

    Vec3 normal{};
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const Vec3& current = positions[i];
        const Vec3& next = positions[(i + 1U) % positions.size()];
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

} // namespace

EditableVertexId EditableMesh::addVertex(Vec3 position) {
    const auto value = static_cast<std::uint32_t>(vertices_.size() + 1U);
    const EditableVertexId id{value};
    vertices_.push_back(EditableVertex{id, position, {}});
    ++vertexCount_;
    return id;
}

std::optional<EditableFaceId> EditableMesh::addFace(std::span<const EditableVertexId> vertices,
                                                     std::string* error) {
    if (vertices.size() < 3U) {
        if (error) *error = "Editable face requires at least three vertices";
        return std::nullopt;
    }

    std::set<std::uint32_t> uniqueVertices;
    for (const auto vertex : vertices) {
        if (!findVertex(vertex)) {
            if (error) *error = "Editable face references a missing vertex";
            return std::nullopt;
        }
        if (!uniqueVertices.insert(vertex.value).second) {
            if (error) *error = "Editable face contains a repeated vertex";
            return std::nullopt;
        }
    }

    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto origin = vertices[i];
        const auto destinationId = vertices[(i + 1U) % vertices.size()];
        const DirectedEdgeKey key{origin.value, destinationId.value};
        if (directedEdges_.contains(key)) {
            if (error) *error = "Editable face duplicates an oriented edge";
            return std::nullopt;
        }

        const DirectedEdgeKey reverse{destinationId.value, origin.value};
        const auto reverseIt = directedEdges_.find(reverse);
        if (reverseIt == directedEdges_.end()) continue;
        const auto* reverseHalfEdge = findHalfEdge(reverseIt->second);
        if (!reverseHalfEdge || !reverseHalfEdge->twin.isNull()) {
            if (error) *error = "Editable edge would become non-manifold";
            return std::nullopt;
        }
    }

    const EditableFaceId faceId = allocateFace();
    auto& face = *faces_[static_cast<std::size_t>(faceId.value - 1U)];

    std::vector<EditableHalfEdgeId> newHalfEdges;
    newHalfEdges.reserve(vertices.size());
    for (std::size_t i = 0; i < vertices.size(); ++i) newHalfEdges.push_back(allocateHalfEdge());
    face.halfEdge = newHalfEdges.front();

    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const EditableVertexId origin = vertices[i];
        const EditableVertexId destinationId = vertices[(i + 1U) % vertices.size()];
        const EditableHalfEdgeId halfEdgeId = newHalfEdges[i];
        auto* halfEdge = findHalfEdgeMutable(halfEdgeId);
        if (!halfEdge) {
            if (error) *error = "Editable mesh internal half-edge allocation failed";
            return std::nullopt;
        }

        halfEdge->origin = origin;
        halfEdge->next = newHalfEdges[(i + 1U) % newHalfEdges.size()];
        halfEdge->face = faceId;

        const DirectedEdgeKey reverse{destinationId.value, origin.value};
        const auto reverseIt = directedEdges_.find(reverse);
        if (reverseIt != directedEdges_.end()) {
            auto* twin = findHalfEdgeMutable(reverseIt->second);
            if (!twin) {
                if (error) *error = "Editable mesh internal twin reference is invalid";
                return std::nullopt;
            }
            halfEdge->twin = twin->id;
            twin->twin = halfEdgeId;
            halfEdge->edge = twin->edge;
        } else {
            const EditableEdgeId edgeId = allocateEdge();
            auto* edge = findEdgeMutable(edgeId);
            if (!edge) {
                if (error) *error = "Editable mesh internal edge allocation failed";
                return std::nullopt;
            }
            edge->halfEdge = halfEdgeId;
            halfEdge->edge = edgeId;
        }

        directedEdges_.emplace(DirectedEdgeKey{origin.value, destinationId.value}, halfEdgeId);
        auto* vertex = findVertex(origin);
        if (vertex && vertex->outgoing.isNull()) vertex->outgoing = halfEdgeId;
    }

    if (error) error->clear();
    return faceId;
}

const EditableVertex* EditableMesh::findVertex(EditableVertexId id) const noexcept {
    if (id.isNull() || static_cast<std::size_t>(id.value) > vertices_.size()) return nullptr;
    const auto& value = vertices_[static_cast<std::size_t>(id.value - 1U)];
    return value ? &*value : nullptr;
}

EditableVertex* EditableMesh::findVertex(EditableVertexId id) noexcept {
    if (id.isNull() || static_cast<std::size_t>(id.value) > vertices_.size()) return nullptr;
    auto& value = vertices_[static_cast<std::size_t>(id.value - 1U)];
    return value ? &*value : nullptr;
}

const EditableHalfEdge* EditableMesh::findHalfEdge(EditableHalfEdgeId id) const noexcept {
    if (id.isNull() || static_cast<std::size_t>(id.value) > halfEdges_.size()) return nullptr;
    const auto& value = halfEdges_[static_cast<std::size_t>(id.value - 1U)];
    return value ? &*value : nullptr;
}

EditableHalfEdge* EditableMesh::findHalfEdgeMutable(EditableHalfEdgeId id) noexcept {
    if (id.isNull() || static_cast<std::size_t>(id.value) > halfEdges_.size()) return nullptr;
    auto& value = halfEdges_[static_cast<std::size_t>(id.value - 1U)];
    return value ? &*value : nullptr;
}

const EditableEdge* EditableMesh::findEdge(EditableEdgeId id) const noexcept {
    if (id.isNull() || static_cast<std::size_t>(id.value) > edges_.size()) return nullptr;
    const auto& value = edges_[static_cast<std::size_t>(id.value - 1U)];
    return value ? &*value : nullptr;
}

EditableEdge* EditableMesh::findEdgeMutable(EditableEdgeId id) noexcept {
    if (id.isNull() || static_cast<std::size_t>(id.value) > edges_.size()) return nullptr;
    auto& value = edges_[static_cast<std::size_t>(id.value - 1U)];
    return value ? &*value : nullptr;
}

const EditableFace* EditableMesh::findFace(EditableFaceId id) const noexcept {
    if (id.isNull() || static_cast<std::size_t>(id.value) > faces_.size()) return nullptr;
    const auto& value = faces_[static_cast<std::size_t>(id.value - 1U)];
    return value ? &*value : nullptr;
}

std::vector<EditableVertex> EditableMesh::vertices() const {
    std::vector<EditableVertex> result;
    result.reserve(vertexCount_);
    for (const auto& value : vertices_) if (value) result.push_back(*value);
    return result;
}

std::vector<EditableEdge> EditableMesh::edges() const {
    std::vector<EditableEdge> result;
    result.reserve(edgeCount_);
    for (const auto& value : edges_) if (value) result.push_back(*value);
    return result;
}

std::vector<EditableFace> EditableMesh::faces() const {
    std::vector<EditableFace> result;
    result.reserve(faceCount_);
    for (const auto& value : faces_) if (value) result.push_back(*value);
    return result;
}

std::vector<EditableVertexId> EditableMesh::faceVertices(EditableFaceId faceId) const {
    std::vector<EditableVertexId> result;
    const auto* face = findFace(faceId);
    if (!face || face->halfEdge.isNull()) return result;

    EditableHalfEdgeId current = face->halfEdge;
    for (std::size_t step = 0; step <= halfEdges_.size(); ++step) {
        const auto* halfEdge = findHalfEdge(current);
        if (!halfEdge || halfEdge->face != faceId) {
            result.clear();
            return result;
        }
        result.push_back(halfEdge->origin);
        current = halfEdge->next;
        if (current == face->halfEdge) return result;
    }
    result.clear();
    return result;
}

EditableVertexId EditableMesh::destination(EditableHalfEdgeId id) const noexcept {
    const auto* halfEdge = findHalfEdge(id);
    if (!halfEdge) return {};
    const auto* next = findHalfEdge(halfEdge->next);
    return next ? next->origin : EditableVertexId{};
}

EditableHalfEdgeId EditableMesh::allocateHalfEdge() {
    const auto value = static_cast<std::uint32_t>(halfEdges_.size() + 1U);
    const EditableHalfEdgeId id{value};
    halfEdges_.push_back(EditableHalfEdge{id, {}, {}, {}, {}, {}});
    ++halfEdgeCount_;
    return id;
}

EditableEdgeId EditableMesh::allocateEdge() {
    const auto value = static_cast<std::uint32_t>(edges_.size() + 1U);
    const EditableEdgeId id{value};
    edges_.push_back(EditableEdge{id, {}});
    ++edgeCount_;
    return id;
}

EditableFaceId EditableMesh::allocateFace() {
    const auto value = static_cast<std::uint32_t>(faces_.size() + 1U);
    const EditableFaceId id{value};
    faces_.push_back(EditableFace{id, {}});
    ++faceCount_;
    return id;
}

bool EditableMesh::validate(std::string* error) const {
    if (vertexCount_ == 0U || faceCount_ == 0U) {
        if (error) *error = "Editable mesh must contain vertices and faces";
        return false;
    }

    std::size_t actualVertices = 0;
    for (const auto& value : vertices_) {
        if (!value) continue;
        ++actualVertices;
        if (!finite(value->position)) {
            if (error) *error = "Editable mesh contains a non-finite vertex";
            return false;
        }
        if (!value->outgoing.isNull()) {
            const auto* outgoing = findHalfEdge(value->outgoing);
            if (!outgoing || outgoing->origin != value->id) {
                if (error) *error = "Editable vertex has an invalid outgoing half-edge";
                return false;
            }
        }
    }
    if (actualVertices != vertexCount_) {
        if (error) *error = "Editable vertex count is inconsistent";
        return false;
    }

    std::size_t actualHalfEdges = 0;
    for (const auto& value : halfEdges_) {
        if (!value) continue;
        ++actualHalfEdges;
        const auto& halfEdge = *value;
        if (!findVertex(halfEdge.origin) || !findHalfEdge(halfEdge.next) ||
            !findEdge(halfEdge.edge) || !findFace(halfEdge.face)) {
            if (error) *error = "Editable half-edge references a missing element";
            return false;
        }
        if (!halfEdge.twin.isNull()) {
            const auto* twin = findHalfEdge(halfEdge.twin);
            if (!twin || twin->twin != halfEdge.id || twin->edge != halfEdge.edge ||
                twin->origin != destination(halfEdge.id) || destination(twin->id) != halfEdge.origin) {
                if (error) *error = "Editable half-edge twin relation is inconsistent";
                return false;
            }
        }
    }
    if (actualHalfEdges != halfEdgeCount_) {
        if (error) *error = "Editable half-edge count is inconsistent";
        return false;
    }

    std::size_t actualEdges = 0;
    for (const auto& value : edges_) {
        if (!value) continue;
        ++actualEdges;
        const auto* halfEdge = findHalfEdge(value->halfEdge);
        if (!halfEdge || halfEdge->edge != value->id) {
            if (error) *error = "Editable edge has an invalid representative half-edge";
            return false;
        }
    }
    if (actualEdges != edgeCount_) {
        if (error) *error = "Editable edge count is inconsistent";
        return false;
    }

    std::size_t actualFaces = 0;
    for (const auto& value : faces_) {
        if (!value) continue;
        ++actualFaces;
        const auto faceVerticesValue = faceVertices(value->id);
        if (faceVerticesValue.size() < 3U) {
            if (error) *error = "Editable face loop is invalid";
            return false;
        }
        std::set<std::uint32_t> unique;
        for (const auto vertex : faceVerticesValue) {
            if (!unique.insert(vertex.value).second) {
                if (error) *error = "Editable face loop repeats a vertex";
                return false;
            }
        }
    }
    if (actualFaces != faceCount_) {
        if (error) *error = "Editable face count is inconsistent";
        return false;
    }

    for (const auto& [key, halfEdgeId] : directedEdges_) {
        const auto* halfEdge = findHalfEdge(halfEdgeId);
        if (!halfEdge || halfEdge->origin.value != key.first || destination(halfEdgeId).value != key.second) {
            if (error) *error = "Editable directed-edge lookup is inconsistent";
            return false;
        }
    }

    if (error) error->clear();
    return true;
}

std::optional<MeshResource> EditableMesh::toMeshResource(ResourceId resourceId,
                                                          std::string name,
                                                          std::string* error) const {
    if (name.empty()) {
        if (error) *error = "Render mesh name cannot be empty";
        return std::nullopt;
    }
    if (!validate(error)) return std::nullopt;

    MeshResource result;
    result.id = resourceId.isNull() ? ResourceId::generate() : resourceId;
    result.name = std::move(name);

    for (const auto& face : faces()) {
        const auto ids = faceVertices(face.id);
        std::vector<Vec3> positions;
        positions.reserve(ids.size());
        for (const auto id : ids) {
            const auto* vertex = findVertex(id);
            if (!vertex) {
                if (error) *error = "Editable face references a missing vertex during triangulation";
                return std::nullopt;
            }
            positions.push_back(vertex->position);
        }

        const auto normal = newellNormal(positions);
        if (!normal) {
            if (error) *error = "Editable face is degenerate and cannot be triangulated";
            return std::nullopt;
        }

        const std::size_t base = result.vertices.size();
        if (base + positions.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            if (error) *error = "Editable mesh exceeds 32-bit render index capacity";
            return std::nullopt;
        }
        for (const auto position : positions) result.vertices.push_back(MeshVertex{position, *normal});

        const auto baseIndex = static_cast<std::uint32_t>(base);
        for (std::size_t i = 1U; i + 1U < positions.size(); ++i) {
            result.indices.push_back(baseIndex);
            result.indices.push_back(baseIndex + static_cast<std::uint32_t>(i));
            result.indices.push_back(baseIndex + static_cast<std::uint32_t>(i + 1U));
        }
    }

    if (!result.validate(error)) return std::nullopt;
    return result;
}

EditableMesh EditableMesh::makeCube(float size) {
    EditableMesh mesh;
    const float half = std::max(size, 0.0001F) * 0.5F;
    const std::array<EditableVertexId, 8> v{
        mesh.addVertex({-half, -half, -half}),
        mesh.addVertex({ half, -half, -half}),
        mesh.addVertex({ half,  half, -half}),
        mesh.addVertex({-half,  half, -half}),
        mesh.addVertex({-half, -half,  half}),
        mesh.addVertex({ half, -half,  half}),
        mesh.addVertex({ half,  half,  half}),
        mesh.addVertex({-half,  half,  half}),
    };

    const std::array<std::array<EditableVertexId, 4>, 6> faces{{
        {v[4], v[5], v[6], v[7]},
        {v[1], v[0], v[3], v[2]},
        {v[0], v[4], v[7], v[3]},
        {v[5], v[1], v[2], v[6]},
        {v[7], v[6], v[2], v[3]},
        {v[0], v[1], v[5], v[4]},
    }};
    for (const auto& face : faces) (void)mesh.addFace(face);
    return mesh;
}

std::optional<EditableMesh> EditableMesh::fromMeshResource(const MeshResource& mesh,
                                                            float weldEpsilon,
                                                            std::string* error) {
    if (!mesh.validate(error)) return std::nullopt;
    if (!std::isfinite(weldEpsilon) || weldEpsilon <= 0.0F) {
        if (error) *error = "Editable mesh weld epsilon must be finite and positive";
        return std::nullopt;
    }

    using WeldKey = std::tuple<long long, long long, long long>;
    std::map<WeldKey, EditableVertexId> welded;
    std::vector<EditableVertexId> renderToEditable(mesh.vertices.size());
    EditableMesh result;

    const double inverseEpsilon = 1.0 / static_cast<double>(weldEpsilon);
    const double maxQuantized = static_cast<double>(std::numeric_limits<long long>::max()) * 0.5;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const auto& position = mesh.vertices[i].position;
        const double qx = static_cast<double>(position.x) * inverseEpsilon;
        const double qy = static_cast<double>(position.y) * inverseEpsilon;
        const double qz = static_cast<double>(position.z) * inverseEpsilon;
        if (std::abs(qx) > maxQuantized || std::abs(qy) > maxQuantized || std::abs(qz) > maxQuantized) {
            if (error) *error = "Editable mesh vertex is outside weld quantization range";
            return std::nullopt;
        }
        const WeldKey key{std::llround(qx), std::llround(qy), std::llround(qz)};
        const auto found = welded.find(key);
        if (found != welded.end()) {
            renderToEditable[i] = found->second;
            continue;
        }
        const auto id = result.addVertex(position);
        welded.emplace(key, id);
        renderToEditable[i] = id;
    }

    for (std::size_t i = 0; i < mesh.indices.size(); i += 3U) {
        const std::array<EditableVertexId, 3> triangle{
            renderToEditable[mesh.indices[i]],
            renderToEditable[mesh.indices[i + 1U]],
            renderToEditable[mesh.indices[i + 2U]],
        };
        std::string faceError;
        if (!result.addFace(triangle, &faceError)) {
            if (error) *error = "Failed to reconstruct editable topology: " + faceError;
            return std::nullopt;
        }
    }

    if (!result.validate(error)) return std::nullopt;
    return result;
}

} // namespace m3d
