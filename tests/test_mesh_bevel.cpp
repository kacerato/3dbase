#include "test_harness.hpp"

#include "mobile3d/core/editable_mesh.hpp"
#include "mobile3d/core/mesh_resource.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

namespace {

[[nodiscard]] bool approx(float left, float right, float tolerance = 1.0e-4F) {
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] bool samePosition(m3d::Vec3 left, m3d::Vec3 right) {
    return approx(left.x, right.x, 1.0e-5F) && approx(left.y, right.y, 1.0e-5F) &&
           approx(left.z, right.z, 1.0e-5F);
}

[[nodiscard]] std::optional<m3d::EditableEdgeId> edgeBetween(const m3d::EditableMesh& mesh,
                                                             m3d::Vec3 first, m3d::Vec3 second) {
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        const auto* next = halfEdge ? mesh.findHalfEdge(halfEdge->next) : nullptr;
        if (!halfEdge || !next) continue;
        const auto a = mesh.findVertex(halfEdge->origin)->position;
        const auto b = mesh.findVertex(next->origin)->position;
        if ((samePosition(a, first) && samePosition(b, second)) ||
            (samePosition(a, second) && samePosition(b, first))) {
            return edge.id;
        }
    }
    return std::nullopt;
}

// Divergence-theorem volume: positive only when every face is oriented outward.
[[nodiscard]] float signedVolume(const m3d::EditableMesh& mesh) {
    double volume = 0.0;
    for (const auto& face : mesh.faces()) {
        const auto loop = mesh.faceVertices(face.id);
        const auto p0 = mesh.findVertex(loop[0])->position;
        for (std::size_t i = 1U; i + 1U < loop.size(); ++i) {
            const auto p1 = mesh.findVertex(loop[i])->position;
            const auto p2 = mesh.findVertex(loop[i + 1U])->position;
            volume += static_cast<double>(p0.x) * (static_cast<double>(p1.y) * p2.z - static_cast<double>(p1.z) * p2.y) -
                      static_cast<double>(p0.y) * (static_cast<double>(p1.x) * p2.z - static_cast<double>(p1.z) * p2.x) +
                      static_cast<double>(p0.z) * (static_cast<double>(p1.x) * p2.y - static_cast<double>(p1.y) * p2.x);
        }
    }
    return static_cast<float>(volume / 6.0);
}

// A closed genus-0 surface that validates, has no boundary, no degenerate faces and renders.
void requireClosedSphereTopology(const m3d::EditableMesh& mesh) {
    std::string error;
    REQUIRE(mesh.validate(&error));
    for (const auto& halfEdge : mesh.halfEdges()) REQUIRE(!halfEdge.twin.isNull());
    for (const auto& face : mesh.faces()) REQUIRE(mesh.faceNormal(face.id).has_value());
    const auto euler = static_cast<long long>(mesh.vertexCount()) -
                       static_cast<long long>(mesh.edgeCount()) +
                       static_cast<long long>(mesh.faceCount());
    REQUIRE(euler == 2);
    REQUIRE(mesh.halfEdgeCount() == 2U * mesh.edgeCount());

    m3d::MeshResource render;
    render.id = m3d::ResourceId::generate();
    render.name = "Beveled";
    render.authoring = mesh;
    REQUIRE(render.rebuildFromAuthoring(&error));
    REQUIRE(render.validate(&error));
}

void requireUnchanged(const m3d::EditableMesh& mesh, const m3d::EditableMeshSnapshot& before) {
    const auto after = mesh.snapshot();
    REQUIRE(after.vertices == before.vertices);
    REQUIRE(after.halfEdges == before.halfEdges);
    REQUIRE(after.edges == before.edges);
    REQUIRE(after.faces == before.faces);
}

[[nodiscard]] std::vector<m3d::EditableEdgeId> allEdges(const m3d::EditableMesh& mesh) {
    std::vector<m3d::EditableEdgeId> result;
    for (const auto& edge : mesh.edges()) result.push_back(edge.id);
    return result;
}

[[nodiscard]] std::vector<m3d::EditableEdgeId> faceEdges(const m3d::EditableMesh& mesh,
                                                          m3d::EditableFaceId face) {
    std::vector<m3d::EditableEdgeId> result;
    auto current = mesh.findFace(face)->halfEdge;
    do {
        const auto* halfEdge = mesh.findHalfEdge(current);
        result.push_back(halfEdge->edge);
        current = halfEdge->next;
    } while (current != mesh.findFace(face)->halfEdge);
    return result;
}

[[nodiscard]] m3d::EditableMesh makeOctahedron() {
    m3d::EditableMesh mesh;
    const auto px = mesh.addVertex({1.0F, 0.0F, 0.0F});
    const auto nx = mesh.addVertex({-1.0F, 0.0F, 0.0F});
    const auto py = mesh.addVertex({0.0F, 1.0F, 0.0F});
    const auto ny = mesh.addVertex({0.0F, -1.0F, 0.0F});
    const auto pz = mesh.addVertex({0.0F, 0.0F, 1.0F});
    const auto nz = mesh.addVertex({0.0F, 0.0F, -1.0F});
    const std::array<std::array<m3d::EditableVertexId, 3>, 8> faces{{
        {px, py, pz}, {py, nx, pz}, {nx, ny, pz}, {ny, px, pz},
        {py, px, nz}, {nx, py, nz}, {ny, nx, nz}, {px, ny, nz},
    }};
    for (const auto& face : faces) (void)mesh.addFace(face);
    return mesh;
}

constexpr float kHalf = 1.0F; // makeCube(2.0F) spans [-1, 1]

} // namespace

TEST_CASE("bevel single cube edge removes the exact chamfer prism volume") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    REQUIRE(approx(signedVolume(mesh), 8.0F));
    const auto edge = edgeBetween(mesh, {kHalf, kHalf, -kHalf}, {kHalf, kHalf, kHalf});
    REQUIRE(edge.has_value());
    std::string error;
    const auto result = mesh.bevelEdges(std::array{*edge}, 0.2F, 1U, &error);
    REQUIRE(result.has_value());
    REQUIRE(error.empty());
    requireClosedSphereTopology(mesh);
    REQUIRE(mesh.vertexCount() == 10U);
    REQUIRE(mesh.edgeCount() == 15U);
    REQUIRE(mesh.faceCount() == 7U);
    // Right-angle chamfer with legs 0.2 along an edge of length 2: 8 - 0.2^2 / 2 * 2.
    REQUIRE(approx(signedVolume(mesh), 7.96F));
}

TEST_CASE("bevel segments follow an exact circular arc on a right-angle edge") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto edge = edgeBetween(mesh, {kHalf, kHalf, -kHalf}, {kHalf, kHalf, kHalf});
    REQUIRE(edge.has_value());
    std::string error;
    const float width = 0.3F;
    const auto result = mesh.bevelEdges(std::array{*edge}, width, 5U, &error);
    REQUIRE(result.has_value());
    requireClosedSphereTopology(mesh);
    REQUIRE(result->faces.size() == 5U);
    // Two valence-3 caps each gain the four interior profile points.
    REQUIRE(mesh.vertexCount() == 10U + 2U * 4U);
    REQUIRE(mesh.faceCount() == 7U + 4U);
    REQUIRE(result->vertices.size() == 4U + 2U * 4U);
    for (const auto vertex : result->vertices) {
        const auto position = mesh.findVertex(vertex)->position;
        // The edge runs along Z at x = y = 1; the arc is centred on x = y = 1 - width.
        const float dx = position.x - (kHalf - width);
        const float dy = position.y - (kHalf - width);
        REQUIRE(approx(std::sqrt(dx * dx + dy * dy), width, 1.0e-5F));
    }
}

TEST_CASE("bevel with two segments removes the inscribed quarter-circle area") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto edge = edgeBetween(mesh, {kHalf, kHalf, -kHalf}, {kHalf, kHalf, kHalf});
    REQUIRE(edge.has_value());
    std::string error;
    REQUIRE(mesh.bevelEdges(std::array{*edge}, 0.2F, 2U, &error).has_value());
    requireClosedSphereTopology(mesh);
    // Removed cross-section: w^2 (1 - sin(45 deg)), extruded along the full edge length.
    const float removed = 0.04F * (1.0F - std::sqrt(0.5F)) * 2.0F;
    REQUIRE(approx(signedVolume(mesh), 8.0F - removed));
}

TEST_CASE("bevel closed face loop mitres every corner and keeps the surface closed") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto top = mesh.faces().front().id; // +Z face of makeCube
    const auto edges = faceEdges(mesh, top);
    REQUIRE(edges.size() == 4U);
    std::string error;
    const auto result = mesh.bevelEdges(edges, 0.2F, 1U, &error);
    REQUIRE(result.has_value());
    requireClosedSphereTopology(mesh);
    REQUIRE(result->faces.size() == 4U);
    REQUIRE(mesh.vertexCount() == 12U);
    REQUIRE(mesh.edgeCount() == 20U);
    REQUIRE(mesh.faceCount() == 10U);
    // Four edge prisms of w^2/2 * 2 overlap in four corner pyramids of w^3/3.
    REQUIRE(approx(signedVolume(mesh), 8.0F - 4.0F * 0.04F + 4.0F * 0.008F / 3.0F));
}

TEST_CASE("bevel chain shares interior profile vertices through each mitred corner") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto edges = faceEdges(mesh, mesh.faces().front().id);
    std::string error;
    const auto result = mesh.bevelEdges(edges, 0.2F, 3U, &error);
    REQUIRE(result.has_value());
    requireClosedSphereTopology(mesh);
    REQUIRE(result->faces.size() == 12U);
    // Each corner splits into two sector vertices plus two shared interior profile vertices.
    REQUIRE(mesh.vertexCount() == 8U - 4U + 4U * 4U);
    REQUIRE(mesh.faceCount() == 6U + 12U);
}

TEST_CASE("bevel three edges meeting at a corner closes the corner with a triangle patch") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const m3d::Vec3 corner{kHalf, kHalf, kHalf};
    const std::array<m3d::EditableEdgeId, 3> edges{
        *edgeBetween(mesh, corner, {-kHalf, kHalf, kHalf}),
        *edgeBetween(mesh, corner, {kHalf, -kHalf, kHalf}),
        *edgeBetween(mesh, corner, {kHalf, kHalf, -kHalf}),
    };
    std::string error;
    const auto result = mesh.bevelEdges(edges, 0.2F, 1U, &error);
    REQUIRE(result.has_value());
    requireClosedSphereTopology(mesh);
    REQUIRE(result->faces.size() == 4U);
    REQUIRE(mesh.vertexCount() == 13U);
    REQUIRE(mesh.edgeCount() == 21U);
    REQUIRE(mesh.faceCount() == 10U);
    std::size_t triangles = 0U;
    for (const auto face : result->faces) triangles += mesh.faceVertices(face).size() == 3U ? 1U : 0U;
    REQUIRE(triangles == 1U);
}

TEST_CASE("bevel every cube edge produces the chamfered cube") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    std::string error;
    const auto result = mesh.bevelEdges(allEdges(mesh), 0.25F, 1U, &error);
    REQUIRE(result.has_value());
    requireClosedSphereTopology(mesh);
    REQUIRE(mesh.vertexCount() == 24U);
    REQUIRE(mesh.edgeCount() == 48U);
    REQUIRE(mesh.faceCount() == 26U);
    REQUIRE(result->faces.size() == 12U + 8U);
    REQUIRE(signedVolume(mesh) > 7.0F);
    REQUIRE(signedVolume(mesh) < 8.0F);
}

TEST_CASE("bevel every cube edge with segments fans rounded corner patches") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    std::string error;
    const auto result = mesh.bevelEdges(allEdges(mesh), 0.25F, 2U, &error);
    REQUIRE(result.has_value());
    requireClosedSphereTopology(mesh);
    // Per corner: 3 sector vertices, 3 interior profile vertices and one patch centre.
    REQUIRE(mesh.vertexCount() == 8U * 7U);
    REQUIRE(mesh.faceCount() == 6U + 12U * 2U + 8U * 6U);
    const auto rounded = signedVolume(mesh);

    auto chamfered = m3d::EditableMesh::makeCube(2.0F);
    REQUIRE(chamfered.bevelEdges(allEdges(chamfered), 0.25F, 1U, &error).has_value());
    // A rounded profile keeps more material than a flat chamfer of the same width.
    REQUIRE(rounded > signedVolume(chamfered));
    REQUIRE(rounded < 8.0F);
}

TEST_CASE("bevel valence four vertices with triangle faces uses planar corner patches") {
    auto mesh = makeOctahedron();
    std::string error;
    REQUIRE(mesh.validate(&error));
    const float before = signedVolume(mesh);
    REQUIRE(approx(before, 4.0F / 3.0F));
    const auto result = mesh.bevelEdges(allEdges(mesh), 0.1F, 1U, &error);
    REQUIRE(result.has_value());
    requireClosedSphereTopology(mesh);
    REQUIRE(mesh.vertexCount() == 24U);
    REQUIRE(mesh.edgeCount() == 48U);
    REQUIRE(mesh.faceCount() == 26U);
    std::size_t quads = 0U;
    for (const auto face : result->faces) quads += mesh.faceVertices(face).size() == 4U ? 1U : 0U;
    REQUIRE(quads == 12U + 6U);
    REQUIRE(signedVolume(mesh) < before);

    auto rounded = makeOctahedron();
    REQUIRE(rounded.bevelEdges(allEdges(rounded), 0.1F, 2U, &error).has_value());
    requireClosedSphereTopology(rounded);
    REQUIRE(rounded.vertexCount() == 24U + 6U * 5U);
    REQUIRE(rounded.faceCount() == 8U + 12U * 2U + 6U * 8U);
}

TEST_CASE("bevel terminal edge at valence four keeps the vertex and tapers the strip") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto ringEdge = edgeBetween(mesh, {kHalf, kHalf, -kHalf}, {kHalf, kHalf, kHalf});
    std::string error;
    const auto cut = mesh.loopCut(*ringEdge, &error);
    REQUIRE(cut.has_value());
    REQUIRE(mesh.vertexCount() == 12U);
    REQUIRE(cut->edges.size() == 4U);

    for (const std::uint32_t segments : {1U, 2U, 3U}) {
        auto beveled = mesh;
        const auto* halfEdge = beveled.findHalfEdge(beveled.findEdge(cut->edges.front())->halfEdge);
        const auto start = halfEdge->origin;
        const auto end = beveled.findHalfEdge(halfEdge->next)->origin;
        const auto result = beveled.bevelEdges(std::array{cut->edges.front()}, 0.2F, segments, &error);
        REQUIRE(result.has_value());
        requireClosedSphereTopology(beveled);
        // The loop lies on a flat cube face, so the bevel must not change the solid.
        REQUIRE(approx(signedVolume(beveled), 8.0F));
        REQUIRE(beveled.findVertex(start) != nullptr);
        REQUIRE(beveled.findVertex(end) != nullptr);
        REQUIRE(beveled.vertexCount() == 16U);
        // One segment is a hexagon through both kept vertices; with more segments the middle
        // rails collapse onto the original edge and only the two outer segments remain.
        REQUIRE(result->faces.size() == (segments == 1U ? 1U : 2U));
        REQUIRE(beveled.faceCount() == 10U + result->faces.size());
    }
}

TEST_CASE("bevel closed edge loop through valence four vertices stays on the surface") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto ringEdge = edgeBetween(mesh, {kHalf, kHalf, -kHalf}, {kHalf, kHalf, kHalf});
    std::string error;
    const auto cut = mesh.loopCut(*ringEdge, &error);
    REQUIRE(cut.has_value());
    const auto result = mesh.bevelEdges(cut->edges, 0.2F, 3U, &error);
    REQUIRE(result.has_value());
    requireClosedSphereTopology(mesh);
    REQUIRE(result->faces.size() == 12U);
    REQUIRE(approx(signedVolume(mesh), 8.0F));
    for (const auto& vertex : mesh.vertices()) {
        const auto p = vertex.position;
        const bool onSurface = approx(std::abs(p.x), kHalf, 1.0e-5F) ||
                               approx(std::abs(p.y), kHalf, 1.0e-5F) ||
                               approx(std::abs(p.z), kHalf, 1.0e-5F);
        REQUIRE(onSurface);
    }
}

TEST_CASE("bevel rejects invalid parameters and unsupported topology atomically") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto before = mesh.snapshot();
    const auto edges = allEdges(mesh);
    std::string error;

    REQUIRE(!mesh.bevelEdges(edges, 0.1F, 0U, &error).has_value());
    REQUIRE(!error.empty());
    requireUnchanged(mesh, before);
    REQUIRE(!mesh.bevelEdges(edges, 0.1F, m3d::EditableMesh::kMaximumBevelSegments + 1U, &error).has_value());
    requireUnchanged(mesh, before);
    REQUIRE(!mesh.bevelEdges(std::span<const m3d::EditableEdgeId>{}, 0.1F, 1U, &error).has_value());
    requireUnchanged(mesh, before);
    REQUIRE(!mesh.bevelEdges(edges, std::nanf(""), 1U, &error).has_value());
    requireUnchanged(mesh, before);
    // Corner sector offsets would pass the middle of the 2-unit edges.
    REQUIRE(!mesh.bevelEdges(edges, 0.99F, 1U, &error).has_value());
    REQUIRE(!error.empty());
    requireUnchanged(mesh, before);
    // Duplicated selections are the same selection.
    const std::array<m3d::EditableEdgeId, 2> duplicated{edges.front(), edges.front()};
    REQUIRE(mesh.bevelEdges(duplicated, 0.1F, 1U, &error).has_value());
    REQUIRE(mesh.vertexCount() == 10U);

    auto open = m3d::EditableMesh::makeCube(2.0F);
    REQUIRE(open.deleteFaces(std::array{open.faces().front().id}, &error));
    // A closed edge whose endpoint lies on the hole: the vertex fan is not closed.
    std::optional<m3d::EditableEdgeId> touchingHole;
    for (const auto& edge : open.edges()) {
        const auto* halfEdge = open.findHalfEdge(edge.halfEdge);
        if (halfEdge->twin.isNull()) continue;
        for (const auto& other : open.halfEdges()) {
            if (other.twin.isNull() && other.origin == halfEdge->origin) touchingHole = edge.id;
        }
        if (touchingHole) break;
    }
    REQUIRE(touchingHole.has_value());
    const auto openBefore = open.snapshot();
    REQUIRE(!open.bevelEdges(std::array{*touchingHole}, 0.1F, 1U, &error).has_value());
    REQUIRE(error.find("interior") != std::string::npos);
    requireUnchanged(open, openBefore);
}

TEST_CASE("editable mesh validation rejects referenced vertices without an outgoing half-edge") {
    auto snapshot = m3d::EditableMesh::makeCube().snapshot();
    snapshot.vertices.front().outgoing = {};
    std::string error;
    REQUIRE(!m3d::EditableMesh::fromSnapshot(snapshot, &error).has_value());
    REQUIRE(error.find("outgoing") != std::string::npos);
}

TEST_CASE("face removal repairs outgoing half-edges from surviving neighbours") {
    auto mesh = m3d::EditableMesh::makeCube();
    std::string error;
    for (const auto& face : mesh.faces()) {
        auto copy = mesh;
        REQUIRE(copy.deleteFaces(std::array{face.id}, &error));
        for (const auto& vertex : copy.vertices()) {
            const auto* outgoing = copy.findHalfEdge(vertex.outgoing);
            REQUIRE(outgoing != nullptr);
            REQUIRE(outgoing->origin == vertex.id);
        }
    }
}
