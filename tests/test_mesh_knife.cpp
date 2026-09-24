#include "test_harness.hpp"

#include "mobile3d/core/editable_mesh.hpp"
#include "mobile3d/core/mesh_resource.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace {

using Point = m3d::EditableKnifePoint;

[[nodiscard]] bool samePosition(m3d::Vec3 left, m3d::Vec3 right) {
    return std::abs(left.x - right.x) <= 1.0e-5F && std::abs(left.y - right.y) <= 1.0e-5F &&
           std::abs(left.z - right.z) <= 1.0e-5F;
}

[[nodiscard]] m3d::EditableVertexId vertexAt(const m3d::EditableMesh& mesh, m3d::Vec3 position) {
    for (const auto& vertex : mesh.vertices()) {
        if (samePosition(vertex.position, position)) return vertex.id;
    }
    return {};
}

// Edge point at `t` measured from the vertex at `from` toward the vertex at `to`.
[[nodiscard]] Point edgePoint(const m3d::EditableMesh& mesh, m3d::Vec3 from, m3d::Vec3 to, float t) {
    const auto a = vertexAt(mesh, from);
    const auto b = vertexAt(mesh, to);
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        const auto origin = halfEdge->origin;
        const auto end = mesh.findHalfEdge(halfEdge->next)->origin;
        if ((origin == a && end == b) || (origin == b && end == a)) return Point::onEdge(edge.id, a, t);
    }
    return Point{};
}

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

void requireClosedSurface(const m3d::EditableMesh& mesh) {
    std::string error;
    REQUIRE(mesh.validate(&error));
    for (const auto& halfEdge : mesh.halfEdges()) REQUIRE(!halfEdge.twin.isNull());
    REQUIRE(static_cast<long long>(mesh.vertexCount()) - static_cast<long long>(mesh.edgeCount()) +
                static_cast<long long>(mesh.faceCount()) == 2);
    REQUIRE(std::abs(signedVolume(mesh) - 8.0F) < 1.0e-4F);
    m3d::MeshResource render;
    render.id = m3d::ResourceId::generate();
    render.name = "Knifed";
    render.authoring = mesh;
    REQUIRE(render.rebuildFromAuthoring(&error));
}

void requireUnchanged(const m3d::EditableMesh& mesh, const m3d::EditableMeshSnapshot& before) {
    const auto after = mesh.snapshot();
    REQUIRE(after.vertices == before.vertices);
    REQUIRE(after.halfEdges == before.halfEdges);
    REQUIRE(after.edges == before.edges);
    REQUIRE(after.faces == before.faces);
}

// makeCube(2.0F) corners; the +Z face is (-1,-1,1) (1,-1,1) (1,1,1) (-1,1,1).
constexpr m3d::Vec3 kA{-1.0F, -1.0F, 1.0F};
constexpr m3d::Vec3 kB{1.0F, -1.0F, 1.0F};
constexpr m3d::Vec3 kC{1.0F, 1.0F, 1.0F};
constexpr m3d::Vec3 kD{-1.0F, 1.0F, 1.0F};

} // namespace

TEST_CASE("knife splits a face between two edge points and the neighbouring faces") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const std::array path{edgePoint(mesh, kA, kB, 0.25F), edgePoint(mesh, kD, kC, 0.25F)};
    std::string error;
    const auto result = mesh.knifeCut(path, &error);
    REQUIRE(result.has_value());
    REQUIRE(error.empty());
    requireClosedSurface(mesh);
    REQUIRE(mesh.vertexCount() == 10U);
    REQUIRE(mesh.edgeCount() == 15U);
    REQUIRE(mesh.faceCount() == 7U);
    REQUIRE(result->vertices.size() == 2U);
    REQUIRE(result->edges.size() == 1U);
    REQUIRE(samePosition(mesh.findVertex(result->vertices[0])->position, {-0.5F, -1.0F, 1.0F}));
    REQUIRE(samePosition(mesh.findVertex(result->vertices[1])->position, {-0.5F, 1.0F, 1.0F}));
}

TEST_CASE("knife around four faces reproduces a loop cut") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    // Around the Z = 0 belt: midpoints of the four vertical edges, closing on the first.
    const std::array<m3d::Vec3, 4> bottom{{{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}}};
    const std::array<m3d::Vec3, 4> top{{{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}};
    std::vector<Point> path;
    for (std::size_t index = 0; index <= 4U; ++index) {
        path.push_back(edgePoint(mesh, bottom[index % 4U], top[index % 4U], 0.5F));
    }
    std::string error;
    const auto result = mesh.knifeCut(path, &error);
    REQUIRE(result.has_value());
    requireClosedSurface(mesh);
    REQUIRE(result->vertices.size() == 4U);
    REQUIRE(result->edges.size() == 4U);

    auto reference = m3d::EditableMesh::makeCube(2.0F);
    const auto ring = edgePoint(reference, bottom[0], top[0], 0.5F).edge;
    REQUIRE(reference.loopCut(ring, &error).has_value());
    REQUIRE(mesh.vertexCount() == reference.vertexCount());
    REQUIRE(mesh.edgeCount() == reference.edgeCount());
    REQUIRE(mesh.faceCount() == reference.faceCount());
    for (const auto vertex : result->vertices) {
        REQUIRE(std::abs(mesh.findVertex(vertex)->position.z) < 1.0e-6F);
    }
}

TEST_CASE("knife connects vertices and snaps edge endpoints to vertices") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto a = vertexAt(mesh, kA);
    const auto c = vertexAt(mesh, kC);
    std::string error;
    const auto diagonal = mesh.knifeCut(std::array{Point::atVertex(a), Point::atVertex(c)}, &error);
    REQUIRE(diagonal.has_value());
    requireClosedSurface(mesh);
    REQUIRE(diagonal->vertices.empty());
    REQUIRE(diagonal->edges.size() == 1U);
    REQUIRE(mesh.faceCount() == 7U);
    REQUIRE(mesh.edgeCount() == 13U);

    auto snapped = m3d::EditableMesh::makeCube(2.0F);
    // t = 1e-5 from B is the vertex B itself; the cut runs from B to the middle of D-A.
    const std::array path{edgePoint(snapped, kB, kA, 1.0e-5F), edgePoint(snapped, kD, kA, 0.5F)};
    const auto result = snapped.knifeCut(path, &error);
    REQUIRE(result.has_value());
    requireClosedSurface(snapped);
    REQUIRE(result->vertices.size() == 1U);
    REQUIRE(snapped.vertexCount() == 9U);
}

TEST_CASE("knife zig-zag through one face splits it into three pieces") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const std::array path{
        edgePoint(mesh, kA, kB, 0.25F),
        edgePoint(mesh, kD, kC, 0.5F),
        edgePoint(mesh, kA, kB, 0.75F),
    };
    std::string error;
    const auto result = mesh.knifeCut(path, &error);
    REQUIRE(result.has_value());
    requireClosedSurface(mesh);
    REQUIRE(result->vertices.size() == 3U);
    REQUIRE(result->edges.size() == 2U);
    REQUIRE(mesh.faceCount() == 8U);
}

TEST_CASE("knife works on open meshes and splits boundary edges") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    std::string error;
    REQUIRE(mesh.deleteFaces(std::array{mesh.faces().front().id}, &error)); // removes +Z
    // Across the -Y side face, from its boundary edge A-B down to the bottom edge.
    const std::array path{edgePoint(mesh, kA, kB, 0.5F),
                          edgePoint(mesh, {-1.0F, -1.0F, -1.0F}, {1.0F, -1.0F, -1.0F}, 0.5F)};
    const auto result = mesh.knifeCut(path, &error);
    REQUIRE(result.has_value());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.faceCount() == 6U);
    REQUIRE(mesh.vertexCount() == 10U);
}

TEST_CASE("knife rejects self-crossing, non-convex and disconnected paths atomically") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto before = mesh.snapshot();
    std::string error;

    // Left-right, then right-top, then top-bottom crosses the first chord inside the face.
    const std::array crossing{
        edgePoint(mesh, kD, kA, 0.5F), edgePoint(mesh, kB, kC, 0.5F),
        edgePoint(mesh, kC, kD, 0.5F), edgePoint(mesh, kA, kB, 0.5F),
    };
    REQUIRE(!mesh.knifeCut(crossing, &error).has_value());
    REQUIRE(error.find("crosses itself") != std::string::npos);
    requireUnchanged(mesh, before);

    // +Z and -Z faces share nothing.
    const std::array disconnected{edgePoint(mesh, kA, kB, 0.5F),
                                  edgePoint(mesh, {-1.0F, 1.0F, -1.0F}, {1.0F, 1.0F, -1.0F}, 0.5F)};
    REQUIRE(!mesh.knifeCut(disconnected, &error).has_value());
    requireUnchanged(mesh, before);

    const auto a = vertexAt(mesh, kA);
    const auto b = vertexAt(mesh, kB);
    REQUIRE(!mesh.knifeCut(std::array{Point::atVertex(a), Point::atVertex(b)}, &error).has_value());
    REQUIRE(error.find("does not cross") != std::string::npos);
    REQUIRE(!mesh.knifeCut(std::array{Point::atVertex(a)}, &error).has_value());
    auto outOfRange = edgePoint(mesh, kA, kB, 0.5F);
    outOfRange.t = 1.5F;
    REQUIRE(!mesh.knifeCut(std::array{outOfRange, edgePoint(mesh, kD, kC, 0.5F)}, &error).has_value());
    auto wrongStart = edgePoint(mesh, kA, kB, 0.5F);
    wrongStart.from = vertexAt(mesh, kC);
    REQUIRE(!mesh.knifeCut(std::array{wrongStart, edgePoint(mesh, kD, kC, 0.5F)}, &error).has_value());
    requireUnchanged(mesh, before);

    // An L-shaped face: the chord between its two reflex-side corners leaves the polygon.
    m3d::EditableMesh shape;
    const std::array<m3d::EditableVertexId, 6> l{
        shape.addVertex({0, 0, 0}), shape.addVertex({2, 0, 0}), shape.addVertex({2, 1, 0}),
        shape.addVertex({1, 1, 0}), shape.addVertex({1, 2, 0}), shape.addVertex({0, 2, 0})};
    REQUIRE(shape.addFace(l, &error).has_value());
    const auto shapeBefore = shape.snapshot();
    REQUIRE(!shape.knifeCut(std::array{Point::atVertex(l[2]), Point::atVertex(l[4])}, &error).has_value());
    REQUIRE(error.find("leaves the face") != std::string::npos);
    requireUnchanged(shape, shapeBefore);
    // The chord from the reflex corner to the opposite corner stays inside and is accepted.
    REQUIRE(shape.knifeCut(std::array{Point::atVertex(l[3]), Point::atVertex(l[0])}, &error).has_value());
    REQUIRE(shape.faceCount() == 2U);
}

TEST_CASE("knife cuts independent runs separated by empty points in one operation") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const m3d::Vec3 a{-1.0F, -1.0F, -1.0F};
    const m3d::Vec3 b{1.0F, -1.0F, -1.0F};
    const m3d::Vec3 c{1.0F, 1.0F, -1.0F};
    const m3d::Vec3 d{-1.0F, 1.0F, -1.0F};
    const std::array path{
        Point{}, edgePoint(mesh, kA, kB, 0.5F), edgePoint(mesh, kD, kC, 0.5F), Point{}, Point{},
        edgePoint(mesh, a, b, 0.5F), edgePoint(mesh, d, c, 0.5F), Point{},
    };
    std::string error;
    const auto result = mesh.knifeCut(path, &error);
    REQUIRE(result.has_value());
    requireClosedSurface(mesh);
    REQUIRE(result->edges.size() == 2U);
    REQUIRE(result->vertices.size() == 4U);
    REQUIRE(mesh.faceCount() == 8U);
}
