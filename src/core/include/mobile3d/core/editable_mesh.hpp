#pragma once

#include "mobile3d/core/math.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace m3d {

struct MeshResource;

template <typename Tag>
struct MeshElementId final {
    std::uint32_t value{0};

    [[nodiscard]] constexpr bool isNull() const noexcept { return value == 0U; }
    friend constexpr bool operator==(const MeshElementId&, const MeshElementId&) noexcept = default;
    friend constexpr auto operator<=>(const MeshElementId&, const MeshElementId&) noexcept = default;
};

using EditableVertexId = MeshElementId<struct EditableVertexIdTag>;
using EditableHalfEdgeId = MeshElementId<struct EditableHalfEdgeIdTag>;
using EditableEdgeId = MeshElementId<struct EditableEdgeIdTag>;
using EditableFaceId = MeshElementId<struct EditableFaceIdTag>;

struct EditableVertex final {
    EditableVertexId id{};
    Vec3 position{};
    EditableHalfEdgeId outgoing{};
    friend constexpr bool operator==(const EditableVertex&, const EditableVertex&) noexcept = default;
};

struct EditableHalfEdge final {
    EditableHalfEdgeId id{};
    EditableVertexId origin{};
    EditableHalfEdgeId next{};
    EditableHalfEdgeId twin{};
    EditableEdgeId edge{};
    EditableFaceId face{};
    friend constexpr bool operator==(const EditableHalfEdge&, const EditableHalfEdge&) noexcept = default;
};

struct EditableEdge final {
    EditableEdgeId id{};
    EditableHalfEdgeId halfEdge{};
    friend constexpr bool operator==(const EditableEdge&, const EditableEdge&) noexcept = default;
};

struct EditableFace final {
    EditableFaceId id{};
    EditableHalfEdgeId halfEdge{};
    friend constexpr bool operator==(const EditableFace&, const EditableFace&) noexcept = default;
};

struct EditableMeshSnapshot final {
    std::vector<EditableVertex> vertices;
    std::vector<EditableHalfEdge> halfEdges;
    std::vector<EditableEdge> edges;
    std::vector<EditableFace> faces;
};

struct EditableVertexWeldResult final {
    std::vector<EditableVertexId> survivors;
    std::size_t mergedCount{0};
};

class EditableMesh final {
public:
    EditableMesh() = default;

    [[nodiscard]] EditableVertexId addVertex(Vec3 position);
    [[nodiscard]] std::optional<EditableFaceId> addFace(std::span<const EditableVertexId> vertices,
                                                         std::string* error = nullptr);
    [[nodiscard]] bool removeFace(EditableFaceId face, std::string* error = nullptr);

    [[nodiscard]] const EditableVertex* findVertex(EditableVertexId id) const noexcept;
    [[nodiscard]] EditableVertex* findVertex(EditableVertexId id) noexcept;
    [[nodiscard]] const EditableHalfEdge* findHalfEdge(EditableHalfEdgeId id) const noexcept;
    [[nodiscard]] const EditableEdge* findEdge(EditableEdgeId id) const noexcept;
    [[nodiscard]] const EditableFace* findFace(EditableFaceId id) const noexcept;

    [[nodiscard]] std::vector<EditableVertex> vertices() const;
    [[nodiscard]] std::vector<EditableHalfEdge> halfEdges() const;
    [[nodiscard]] std::vector<EditableEdge> edges() const;
    [[nodiscard]] std::vector<EditableFace> faces() const;
    [[nodiscard]] std::vector<EditableVertexId> faceVertices(EditableFaceId face) const;
    [[nodiscard]] std::optional<Vec3> faceNormal(EditableFaceId face) const noexcept;

    [[nodiscard]] std::optional<EditableFaceId> extrudeFace(EditableFaceId face, float distance,
                                                             std::string* error = nullptr);
    [[nodiscard]] std::optional<EditableFaceId> insetFace(EditableFaceId face, float ratio,
                                                           std::string* error = nullptr);
    [[nodiscard]] std::optional<std::vector<EditableFaceId>> subdivideFace(
        EditableFaceId face, std::string* error = nullptr);
    [[nodiscard]] std::optional<EditableVertexId> mergeVertices(
        std::span<const EditableVertexId> vertices, EditableVertexId target,
        std::string* error = nullptr);
    [[nodiscard]] std::optional<EditableVertexWeldResult> weldVertices(
        std::span<const EditableVertexId> vertices, float distance,
        std::optional<EditableVertexId> preferredTarget = std::nullopt,
        std::string* error = nullptr);

    [[nodiscard]] std::size_t vertexCount() const noexcept { return vertexCount_; }
    [[nodiscard]] std::size_t halfEdgeCount() const noexcept { return halfEdgeCount_; }
    [[nodiscard]] std::size_t edgeCount() const noexcept { return edgeCount_; }
    [[nodiscard]] std::size_t faceCount() const noexcept { return faceCount_; }

    [[nodiscard]] bool validate(std::string* error = nullptr) const;
    [[nodiscard]] EditableMeshSnapshot snapshot() const;
    [[nodiscard]] static std::optional<EditableMesh> fromSnapshot(const EditableMeshSnapshot& snapshot,
                                                                   std::string* error = nullptr);

    [[nodiscard]] bool writeRenderMesh(MeshResource& output, std::string* error = nullptr) const;
    [[nodiscard]] static EditableMesh makeCube(float size = 1.0F);
    [[nodiscard]] static std::optional<EditableMesh> fromMeshResource(const MeshResource& mesh,
                                                                       float weldEpsilon = 1.0e-5F,
                                                                       std::string* error = nullptr);

private:
    using DirectedEdgeKey = std::pair<std::uint32_t, std::uint32_t>;

    [[nodiscard]] EditableHalfEdge* findHalfEdgeMutable(EditableHalfEdgeId id) noexcept;
    [[nodiscard]] EditableEdge* findEdgeMutable(EditableEdgeId id) noexcept;
    [[nodiscard]] EditableVertexId destination(EditableHalfEdgeId id) const noexcept;

    [[nodiscard]] EditableHalfEdgeId allocateHalfEdge();
    [[nodiscard]] EditableEdgeId allocateEdge();
    [[nodiscard]] EditableFaceId allocateFace();

    std::vector<std::optional<EditableVertex>> vertices_;
    std::vector<std::optional<EditableHalfEdge>> halfEdges_;
    std::vector<std::optional<EditableEdge>> edges_;
    std::vector<std::optional<EditableFace>> faces_;
    std::map<DirectedEdgeKey, EditableHalfEdgeId> directedEdges_;

    std::size_t vertexCount_{0};
    std::size_t halfEdgeCount_{0};
    std::size_t edgeCount_{0};
    std::size_t faceCount_{0};
};

} // namespace m3d
