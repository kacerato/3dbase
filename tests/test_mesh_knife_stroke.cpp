#include "test_harness.hpp"

#include "mobile3d/core/editable_mesh.hpp"
#include "mobile3d/editor/mesh_screen_picker.hpp"
#include "mobile3d/render/viewport_camera.hpp"

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr float kWidth = 800.0F;
constexpr float kHeight = 600.0F;

// Mirrors VulkanViewport's projection: clip -> NDC -> pixels, keeping 1 / clip.w.
[[nodiscard]] std::optional<m3d::MeshScreenPoint> project(const m3d::Mat4& viewProjection, m3d::Vec3 p) {
    const auto row = [&](std::size_t r) {
        return viewProjection.at(r, 0) * p.x + viewProjection.at(r, 1) * p.y +
               viewProjection.at(r, 2) * p.z + viewProjection.at(r, 3);
    };
    const float w = row(3);
    if (w <= 1.0e-5F) return std::nullopt;
    return m3d::MeshScreenPoint{(row(0) / w * 0.5F + 0.5F) * kWidth, (row(1) / w * 0.5F + 0.5F) * kHeight,
                                row(2) / w, 1.0F / w};
}

struct ScreenMesh final {
    std::vector<m3d::MeshScreenEdge> edges;
    std::vector<m3d::MeshScreenFace> faces;
};

[[nodiscard]] ScreenMesh projectMesh(const m3d::EditableMesh& mesh, const m3d::Mat4& viewProjection) {
    ScreenMesh result;
    std::unordered_map<std::uint32_t, m3d::MeshScreenPoint> projected;
    for (const auto& vertex : mesh.vertices()) {
        if (const auto point = project(viewProjection, vertex.position)) projected.emplace(vertex.id.value, *point);
    }
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        const auto first = halfEdge->origin;
        const auto second = mesh.findHalfEdge(halfEdge->next)->origin;
        result.edges.push_back({edge.id, projected.at(first.value), projected.at(second.value), first, second});
    }
    for (const auto& face : mesh.faces()) {
        m3d::MeshScreenFace screenFace{face.id, {}, mesh.faceVertices(face.id)};
        for (const auto id : screenFace.vertexIds) screenFace.vertices.push_back(projected.at(id.value));
        result.faces.push_back(std::move(screenFace));
    }
    return result;
}

[[nodiscard]] m3d::Vec3 midpoint(m3d::Vec3 a, m3d::Vec3 b) {
    return {(a.x + b.x) * 0.5F, (a.y + b.y) * 0.5F, (a.z + b.z) * 0.5F};
}

[[nodiscard]] float distance(m3d::Vec3 a, m3d::Vec3 b) {
    const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}

// The face of the cube that most directly faces the camera.
[[nodiscard]] m3d::EditableFaceId frontFace(const m3d::EditableMesh& mesh, m3d::Vec3 camera) {
    m3d::EditableFaceId best{};
    float bestScore = -1.0F;
    for (const auto& face : mesh.faces()) {
        const auto normal = *mesh.faceNormal(face.id);
        const auto loop = mesh.faceVertices(face.id);
        const auto p = mesh.findVertex(loop.front())->position;
        const m3d::Vec3 toCamera{camera.x - p.x, camera.y - p.y, camera.z - p.z};
        const float score = (normal.x * toCamera.x + normal.y * toCamera.y + normal.z * toCamera.z) /
                            distance(camera, p);
        if (score > bestScore) { bestScore = score; best = face.id; }
    }
    return best;
}

// A stroke through the screen images of two 3D points, overshooting each end by `overshoot` px.
[[nodiscard]] std::vector<m3d::MeshScreenPoint> strokeThrough(m3d::MeshScreenPoint a, m3d::MeshScreenPoint b,
                                                              float overshoot) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    const float ux = dx / length * overshoot, uy = dy / length * overshoot;
    return {{a.x - ux, a.y - uy, 0.0F, 1.0F}, {b.x + ux, b.y + uy, 0.0F, 1.0F}};
}

} // namespace

TEST_CASE("knife stroke recovers perspective-correct edge parameters") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    m3d::ViewportCamera camera;
    camera.setDistance(4.5F);
    const auto viewProjection = camera.viewProjectionMatrix(kWidth / kHeight);
    const auto face = frontFace(mesh, camera.position());
    const auto loop = mesh.faceVertices(face);
    REQUIRE(loop.size() == 4U);
    const auto position = [&](std::size_t index) { return mesh.findVertex(loop[index % 4U])->position; };

    // Cut between the midpoints of two opposite edges of the front face.
    const auto first = midpoint(position(0), position(1));
    const auto second = midpoint(position(2), position(3));
    const auto screen = projectMesh(mesh, viewProjection);
    const auto a = *project(viewProjection, first);
    const auto b = *project(viewProjection, second);

    // The screen-space split of a perspective edge is not its 3D midpoint: a naive
    // interpolation of the screen parameter would place the cut elsewhere.
    const auto p0 = *project(viewProjection, position(0));
    const auto p1 = *project(viewProjection, position(1));
    const float screenU = std::sqrt((a.x - p0.x) * (a.x - p0.x) + (a.y - p0.y) * (a.y - p0.y)) /
                          std::sqrt((p1.x - p0.x) * (p1.x - p0.x) + (p1.y - p0.y) * (p1.y - p0.y));
    REQUIRE(std::abs(screenU - 0.5F) > 0.01F);

    m3d::MeshKnifeStrokeRequest request;
    request.stroke = strokeThrough(a, b, 3.0F);
    request.vertexSnapRadius = 4.0F;
    const auto path = m3d::MeshScreenPicker::planKnifeStroke(screen.edges, screen.faces, request);
    REQUIRE(path.size() == 2U);
    std::string error;
    const auto result = mesh.knifeCut(path, &error);
    REQUIRE(result.has_value());
    REQUIRE(result->vertices.size() == 2U);
    REQUIRE(distance(mesh.findVertex(result->vertices[0])->position, first) < 2.0e-3F);
    REQUIRE(distance(mesh.findVertex(result->vertices[1])->position, second) < 2.0e-3F);
}

TEST_CASE("knife stroke snaps to vertices and ignores hidden edges") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    m3d::ViewportCamera camera;
    camera.setDistance(4.5F);
    const auto viewProjection = camera.viewProjectionMatrix(kWidth / kHeight);
    const auto face = frontFace(mesh, camera.position());
    const auto loop = mesh.faceVertices(face);
    const auto screen = projectMesh(mesh, viewProjection);
    const auto a = *project(viewProjection, mesh.findVertex(loop[0])->position);
    const auto c = *project(viewProjection, mesh.findVertex(loop[2])->position);

    m3d::MeshKnifeStrokeRequest request;
    request.stroke = strokeThrough(a, c, 2.0F);
    request.vertexSnapRadius = 8.0F;
    const auto path = m3d::MeshScreenPicker::planKnifeStroke(screen.edges, screen.faces, request);
    // Only the two visible corners: the hidden edges meeting behind them are occluded.
    REQUIRE(path.size() == 2U);
    REQUIRE(path[0].vertex == loop[0]);
    REQUIRE(path[1].vertex == loop[2]);
    std::string error;
    const auto result = mesh.knifeCut(path, &error);
    REQUIRE(result.has_value());
    REQUIRE(result->vertices.empty());
    REQUIRE(result->edges.size() == 1U);
    REQUIRE(mesh.faceCount() == 7U);
}

TEST_CASE("knife stroke across the whole object cuts only the visible side") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    m3d::ViewportCamera camera;
    camera.setDistance(5.0F);
    const auto viewProjection = camera.viewProjectionMatrix(kWidth / kHeight);
    const auto screen = projectMesh(mesh, viewProjection);
    const auto centre = *project(viewProjection, {0.0F, 0.0F, 0.0F});

    m3d::MeshKnifeStrokeRequest request;
    request.stroke = {{0.0F, centre.y, 0.0F, 1.0F}, {kWidth, centre.y + 7.0F, 0.0F, 1.0F}};
    request.vertexSnapRadius = 2.0F;
    const auto path = m3d::MeshScreenPicker::planKnifeStroke(screen.edges, screen.faces, request);
    REQUIRE(path.size() >= 3U);
    for (const auto& point : path) REQUIRE(!point.edge.isNull() || !point.vertex.isNull());

    const auto cameraPosition = camera.position();
    std::string error;
    const auto result = mesh.knifeCut(path, &error);
    REQUIRE(result.has_value());
    REQUIRE(mesh.validate(&error));
    for (const auto edgeId : result->edges) {
        const auto* halfEdge = mesh.findHalfEdge(mesh.findEdge(edgeId)->halfEdge);
        for (const auto faceId : {halfEdge->face, mesh.findHalfEdge(halfEdge->twin)->face}) {
            const auto normal = *mesh.faceNormal(faceId);
            const auto p = mesh.findVertex(mesh.faceVertices(faceId).front())->position;
            const float facing = normal.x * (cameraPosition.x - p.x) + normal.y * (cameraPosition.y - p.y) +
                                 normal.z * (cameraPosition.z - p.z);
            REQUIRE(facing > 0.0F);
        }
    }
}

TEST_CASE("knife stroke breaks the path where it leaves the surface") {
    // Two disjoint cubes side by side along X.
    auto mesh = m3d::EditableMesh::makeCube(1.0F);
    const auto source = m3d::EditableMesh::makeCube(1.0F);
    std::unordered_map<std::uint32_t, m3d::EditableVertexId> copied;
    for (const auto& vertex : source.vertices()) {
        copied.emplace(vertex.id.value,
                       mesh.addVertex({vertex.position.x + 3.0F, vertex.position.y, vertex.position.z}));
    }
    for (const auto& face : source.faces()) {
        std::vector<m3d::EditableVertexId> loop;
        for (const auto id : source.faceVertices(face.id)) loop.push_back(copied.at(id.value));
        REQUIRE(mesh.addFace(loop).has_value());
    }
    m3d::ViewportCamera camera;
    camera.setTarget({1.5F, 0.0F, 0.0F});
    camera.setDistance(9.0F);
    const auto viewProjection = camera.viewProjectionMatrix(kWidth / kHeight);
    const auto screen = projectMesh(mesh, viewProjection);
    const auto left = *project(viewProjection, {0.0F, 0.0F, 0.0F});
    const auto right = *project(viewProjection, {3.0F, 0.0F, 0.0F});

    m3d::MeshKnifeStrokeRequest request;
    request.stroke = strokeThrough(left, right, 120.0F);
    request.vertexSnapRadius = 2.0F;
    const auto path = m3d::MeshScreenPicker::planKnifeStroke(screen.edges, screen.faces, request);
    std::size_t breaks = 0U;
    for (const auto& point : path) breaks += point.edge.isNull() && point.vertex.isNull() ? 1U : 0U;
    REQUIRE(breaks >= 1U);
    std::string error;
    const auto result = mesh.knifeCut(path, &error);
    REQUIRE(result.has_value());
    REQUIRE(mesh.validate(&error));
    bool cutLeft = false;
    bool cutRight = false;
    for (const auto vertex : result->vertices) {
        const float x = mesh.findVertex(vertex)->position.x;
        cutLeft = cutLeft || x < 1.0F;
        cutRight = cutRight || x > 2.0F;
    }
    REQUIRE(cutLeft);
    REQUIRE(cutRight);
}
