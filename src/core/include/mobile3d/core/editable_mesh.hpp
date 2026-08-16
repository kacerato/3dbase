#pragma once

#include "mobile3d/core/mesh_resource.hpp"

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
};

struct EditableHalfEdge final {
    EditableHalfEdgeId id{};
    EditableVertexId origin{};
    EditableHalfEdgeId next{};
    EditableHalfEdgeId twin{};
    EditableEdgeId edge{};
    EditableFaceId face{};
};

struct EditableEdge final {
    EditableEdgeId id{};
    EditableHalfEdgeId halfEdge{};
};

struct EditableFace final {
    EditableFaceId id{};
    EditableHalfEdgeId halfEdge{};
};

class EditableMesh final {
public:
    EditableMesh() = default;

    [[nodiscard]] EditableVertexId addVertex(Vec3 position);
    [[nodiscard]] std::optional<EditableFaceId> addFace(std::span<const EditableVertexId> vertices,
                                                         std::string* error = nullptr);

    [[nodiscard]] const EditableVertex* findVertex(EditableVertexId id) const noexcept;
    [[nodiscard]] EditableVertex* findVertex(EditableVertexId id) noexcept;
    [[nodiscard]] const EditableHalfEdge* findHalfEdge(EditableHalfEdgeId id) const noexcept;
    [[nodiscard]] const EditableEdge* findEdge(EditableEdgeId id) const noexcept;
    [[nodiscard]] const EditableFace* findFace(EditableFaceId id) const noexcept;

    [[nodiscard]] std::vector<EditableVertex> vertices() const;
    [[nodiscard]] std::vector<EditableEdge> edges() const;
    [[nodiscard]] std::vector<EditableFace> faces() const;
    [[nodiscard]] std::vector<EditableVertexId> faceVertices(EditableFaceId face) const;

    [[nodiscard]] std::size_t vertexCount() const noexcept { return vertexCount_; }
    [[nodiscard]] std::size_t halfEdgeCount() const noexcept { return halfEdgeCount_; }
    [[nodiscard]] std::size_t edgeCount() const noexcept { return edgeCount_; }
    [[nodiscard]] std::size_t faceCount() const noexcept { return faceCount_; }

    [[nodiscard]] bool validate(std::string* error = nullptr) const;

    [[nodiscard]] std::optional<MeshResource> toMeshResource(ResourceId resourceId,
                                                              std::string name,
                                                              std::string* error = nullptr) const;

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
