#include "test_harness.hpp"

#include "mobile3d/core/editable_mesh.hpp"
#include "mobile3d/core/mesh_resource.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <string>
#include <vector>

TEST_CASE("face extrude creates a closed quad-sided prism extension") {
    auto mesh = m3d::EditableMesh::makeCube(1.0F);
    const auto originalFace = mesh.faces().front().id;
    const auto untouchedFace = mesh.faces()[1].id;
    const auto untouchedVertices = mesh.faceVertices(untouchedFace);
    std::string error;

    const auto top = mesh.extrudeFace(originalFace, 1.0F, &error);
    REQUIRE(top.has_value());
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.findFace(originalFace) == nullptr);
    REQUIRE(mesh.findFace(untouchedFace) != nullptr);
    REQUIRE(mesh.faceVertices(untouchedFace) == untouchedVertices);
    REQUIRE(mesh.vertexCount() == 12U);
    REQUIRE(mesh.edgeCount() == 20U);
    REQUIRE(mesh.halfEdgeCount() == 40U);
    REQUIRE(mesh.faceCount() == 10U);
    REQUIRE(mesh.faceVertices(*top).size() == 4U);

    m3d::MeshResource rendered;
    rendered.id = m3d::ResourceId::generate();
    rendered.name = "Extruded";
    rendered.authoring = mesh;
    REQUIRE(rendered.rebuildFromAuthoring(&error));
    const auto bounds = rendered.bounds();
    REQUIRE(bounds.has_value());
    REQUIRE(std::abs(bounds->max.z - 1.5F) < 1.0e-5F);
}

TEST_CASE("face inset creates an inner face and surrounding quad ring") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto face = mesh.faces().front().id;
    std::string error;
    const auto inner = mesh.insetFace(face, 0.25F, &error);
    REQUIRE(inner.has_value());
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.findFace(face) == nullptr);
    REQUIRE(mesh.vertexCount() == 12U);
    REQUIRE(mesh.edgeCount() == 20U);
    REQUIRE(mesh.halfEdgeCount() == 40U);
    REQUIRE(mesh.faceCount() == 10U);

    const auto innerVertices = mesh.faceVertices(*inner);
    REQUIRE(innerVertices.size() == 4U);
    for (const auto id : innerVertices) {
        const auto* vertex = mesh.findVertex(id);
        REQUIRE(vertex != nullptr);
        REQUIRE(std::abs(vertex->position.x) <= 0.75F + 1.0e-5F);
        REQUIRE(std::abs(vertex->position.y) <= 0.75F + 1.0e-5F);
        REQUIRE(std::abs(vertex->position.z - 1.0F) < 1.0e-5F);
    }
}

TEST_CASE("face subdivision replaces a quad with a center fan without changing boundary ids") {
    auto mesh = m3d::EditableMesh::makeCube(1.0F);
    const auto face = mesh.faces().front().id;
    const auto boundary = mesh.faceVertices(face);
    std::string error;
    const auto created = mesh.subdivideFace(face, &error);
    REQUIRE(created.has_value());
    REQUIRE(created->size() == 4U);
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.findFace(face) == nullptr);
    REQUIRE(mesh.vertexCount() == 9U);
    REQUIRE(mesh.edgeCount() == 16U);
    REQUIRE(mesh.halfEdgeCount() == 32U);
    REQUIRE(mesh.faceCount() == 9U);
    for (const auto vertex : boundary) REQUIRE(mesh.findVertex(vertex) != nullptr);
    for (const auto createdFace : *created) REQUIRE(mesh.faceVertices(createdFace).size() == 3U);
}

TEST_CASE("invalid extrude leaves topology unchanged") {
    auto mesh = m3d::EditableMesh::makeCube();
    const auto face = mesh.faces().front().id;
    const auto before = mesh.snapshot();
    std::string error;
    REQUIRE(!mesh.extrudeFace(face, 0.0F, &error).has_value());
    REQUIRE(!error.empty());
    const auto after = mesh.snapshot();
    REQUIRE(after.vertices == before.vertices);
    REQUIRE(after.halfEdges == before.halfEdges);
    REQUIRE(after.edges == before.edges);
    REQUIRE(after.faces == before.faces);
}

TEST_CASE("invalid inset leaves topology unchanged") {
    auto mesh = m3d::EditableMesh::makeCube();
    const auto face = mesh.faces().front().id;
    const auto before = mesh.snapshot();
    std::string error;
    REQUIRE(!mesh.insetFace(face, 1.0F, &error).has_value());
    REQUIRE(!error.empty());
    const auto after = mesh.snapshot();
    REQUIRE(after.vertices == before.vertices);
    REQUIRE(after.halfEdges == before.halfEdges);
    REQUIRE(after.edges == before.edges);
    REQUIRE(after.faces == before.faces);
}

TEST_CASE("vertex merge to active preserves untouched topology identities") {
    auto mesh = m3d::EditableMesh::makeCube(1.0F);
    const auto edge = mesh.edges().front();
    const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
    REQUIRE(halfEdge != nullptr);
    const auto* next = mesh.findHalfEdge(halfEdge->next);
    REQUIRE(next != nullptr);
    const auto target = halfEdge->origin;
    const auto source = next->origin;

    std::optional<m3d::EditableFaceId> untouchedFace;
    std::vector<m3d::EditableVertexId> untouchedLoop;
    for (const auto& face : mesh.faces()) {
        const auto loop = mesh.faceVertices(face.id);
        if (std::find(loop.cbegin(), loop.cend(), source) == loop.cend() &&
            std::find(loop.cbegin(), loop.cend(), target) == loop.cend()) {
            untouchedFace = face.id;
            untouchedLoop = loop;
            break;
        }
    }
    REQUIRE(untouchedFace.has_value());

    const std::array<m3d::EditableVertexId, 2> selected{target, source};
    std::string error;
    const auto merged = mesh.mergeVertices(selected, target, &error);
    REQUIRE(merged == target);
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.findVertex(target) != nullptr);
    REQUIRE(mesh.findVertex(source) == nullptr);
    REQUIRE(mesh.vertexCount() == 7U);
    REQUIRE(mesh.edgeCount() == 11U);
    REQUIRE(mesh.halfEdgeCount() == 22U);
    REQUIRE(mesh.faceCount() == 6U);
    REQUIRE(mesh.findFace(*untouchedFace) != nullptr);
    REQUIRE(mesh.faceVertices(*untouchedFace) == untouchedLoop);
}

TEST_CASE("weld by distance prefers active target and reports merged count") {
    auto mesh = m3d::EditableMesh::makeCube(1.0F);
    const auto edge = mesh.edges().front();
    const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
    const auto* next = halfEdge ? mesh.findHalfEdge(halfEdge->next) : nullptr;
    REQUIRE(halfEdge != nullptr);
    REQUIRE(next != nullptr);
    const auto active = halfEdge->origin;
    const auto other = next->origin;
    const std::array<m3d::EditableVertexId, 2> selected{active, other};
    std::string error;

    const auto result = mesh.weldVertices(selected, 1.01F, active, &error);
    REQUIRE(result.has_value());
    REQUIRE(error.empty());
    REQUIRE(result->mergedCount == 1U);
    REQUIRE(result->survivors.size() == 1U);
    REQUIRE(result->survivors.front() == active);
    REQUIRE(mesh.findVertex(other) == nullptr);
    REQUIRE(mesh.vertexCount() == 7U);
    REQUIRE(mesh.validate(&error));
}

TEST_CASE("weld with no vertices inside threshold is an exact no-op") {
    auto mesh = m3d::EditableMesh::makeCube(1.0F);
    const auto vertices = mesh.vertices();
    REQUIRE(vertices.size() >= 2U);
    const std::array<m3d::EditableVertexId, 2> selected{vertices[0].id, vertices[6].id};
    const auto before = mesh.snapshot();
    std::string error;
    const auto result = mesh.weldVertices(selected, 0.01F, vertices[0].id, &error);
    REQUIRE(result.has_value());
    REQUIRE(result->mergedCount == 0U);
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}

TEST_CASE("invalid merge target leaves topology unchanged") {
    auto mesh = m3d::EditableMesh::makeCube();
    const auto vertices = mesh.vertices();
    const std::array<m3d::EditableVertexId, 2> selected{vertices[0].id, vertices[1].id};
    const auto before = mesh.snapshot();
    std::string error;
    REQUIRE(!mesh.mergeVertices(selected, vertices[2].id, &error).has_value());
    REQUIRE(!error.empty());
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}

TEST_CASE("fill boundary loop closes a removed cube face and reuses boundary edge ids") {
    auto mesh = m3d::EditableMesh::makeCube(1.0F);
    const auto removed = mesh.faces().front().id;
    const auto removedVertices = mesh.faceVertices(removed);
    std::string error;
    REQUIRE(mesh.removeFace(removed, &error));
    REQUIRE(mesh.faceCount() == 5U);

    std::vector<m3d::EditableEdgeId> boundary;
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        if (halfEdge && halfEdge->twin.isNull()) boundary.push_back(edge.id);
    }
    REQUIRE(boundary.size() == 4U);
    const auto boundaryIds = boundary;
    const auto filled = mesh.fillBoundaryLoop(boundary, &error);
    REQUIRE(filled.has_value());
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 8U);
    REQUIRE(mesh.edgeCount() == 12U);
    REQUIRE(mesh.halfEdgeCount() == 24U);
    REQUIRE(mesh.faceCount() == 6U);
    REQUIRE(mesh.faceVertices(*filled).size() == removedVertices.size());
    for (const auto edgeId : boundaryIds) {
        const auto* edge = mesh.findEdge(edgeId);
        const auto* halfEdge = edge ? mesh.findHalfEdge(edge->halfEdge) : nullptr;
        REQUIRE(edge != nullptr);
        REQUIRE(halfEdge != nullptr);
        REQUIRE(!halfEdge->twin.isNull());
    }
}

TEST_CASE("bridge equal boundary loops creates a closed quad band") {
    m3d::EditableMesh mesh;
    const std::array<m3d::EditableVertexId, 4> bottom{
        mesh.addVertex({-1.0F, -1.0F, 0.0F}), mesh.addVertex({1.0F, -1.0F, 0.0F}),
        mesh.addVertex({1.0F, 1.0F, 0.0F}), mesh.addVertex({-1.0F, 1.0F, 0.0F})
    };
    const std::array<m3d::EditableVertexId, 4> top{
        mesh.addVertex({-1.0F, -1.0F, 2.0F}), mesh.addVertex({1.0F, -1.0F, 2.0F}),
        mesh.addVertex({1.0F, 1.0F, 2.0F}), mesh.addVertex({-1.0F, 1.0F, 2.0F})
    };
    const std::array<m3d::EditableVertexId, 4> bottomWinding{bottom[0], bottom[3], bottom[2], bottom[1]};
    const std::array<m3d::EditableVertexId, 4> topWinding{top[0], top[1], top[2], top[3]};
    std::string error;
    REQUIRE(mesh.addFace(bottomWinding, &error).has_value());
    REQUIRE(mesh.addFace(topWinding, &error).has_value());
    REQUIRE(mesh.validate(&error));

    std::vector<m3d::EditableEdgeId> boundaries;
    for (const auto& edge : mesh.edges()) boundaries.push_back(edge.id);
    REQUIRE(boundaries.size() == 8U);
    const auto bridge = mesh.bridgeBoundaryLoops(boundaries, &error);
    REQUIRE(bridge.has_value());
    REQUIRE(bridge->size() == 4U);
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 8U);
    REQUIRE(mesh.edgeCount() == 12U);
    REQUIRE(mesh.halfEdgeCount() == 24U);
    REQUIRE(mesh.faceCount() == 6U);
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        REQUIRE(halfEdge != nullptr);
        REQUIRE(!halfEdge->twin.isNull());
    }
}

TEST_CASE("fill rejects an incomplete boundary selection atomically") {
    auto mesh = m3d::EditableMesh::makeCube();
    std::string error;
    REQUIRE(mesh.removeFace(mesh.faces().front().id, &error));
    std::vector<m3d::EditableEdgeId> boundary;
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        if (halfEdge && halfEdge->twin.isNull()) boundary.push_back(edge.id);
    }
    REQUIRE(boundary.size() == 4U);
    boundary.pop_back();
    const auto before = mesh.snapshot();
    REQUIRE(!mesh.fillBoundaryLoop(boundary, &error).has_value());
    REQUIRE(!error.empty());
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}

TEST_CASE("centered loop cut propagates through an entire quad ring") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto originalFaces = mesh.faces();
    std::map<m3d::EditableFaceId, std::vector<m3d::EditableVertexId>> originalLoops;
    for (const auto& face : originalFaces) originalLoops.emplace(face.id, mesh.faceVertices(face.id));
    const auto startEdge = mesh.edges().front().id;
    std::string error;

    const auto result = mesh.loopCut(startEdge, &error);
    REQUIRE(result.has_value());
    REQUIRE(error.empty());
    REQUIRE(result->vertices.size() == 4U);
    REQUIRE(result->edges.size() == 4U);
    REQUIRE(result->faces.size() == 8U);
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 12U);
    REQUIRE(mesh.edgeCount() == 20U);
    REQUIRE(mesh.halfEdgeCount() == 40U);
    REQUIRE(mesh.faceCount() == 10U);

    std::size_t untouched = 0U;
    for (const auto& [faceId, loop] : originalLoops) {
        if (!mesh.findFace(faceId)) continue;
        ++untouched;
        REQUIRE(mesh.faceVertices(faceId) == loop);
    }
    REQUIRE(untouched == 2U);

    m3d::MeshResource render;
    render.id = m3d::ResourceId::generate();
    render.authoring = mesh;
    REQUIRE(render.rebuildFromAuthoring(&error));
    REQUIRE(render.validate(&error));
}

TEST_CASE("loop cut rejects a non quad ring without changing topology") {
    m3d::EditableMesh mesh;
    const auto a = mesh.addVertex({0.0F,0.0F,0.0F});
    const auto b = mesh.addVertex({1.0F,0.0F,0.0F});
    const auto c = mesh.addVertex({0.0F,1.0F,0.0F});
    const std::array<m3d::EditableVertexId,3> triangle{a,b,c};
    std::string error;
    REQUIRE(mesh.addFace(triangle,&error).has_value());
    const auto before = mesh.snapshot();
    REQUIRE(!mesh.loopCut(mesh.edges().front().id,&error).has_value());
    REQUIRE(!error.empty());
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}

TEST_CASE("delete face opens topology without deleting its vertices") {
    auto mesh = m3d::EditableMesh::makeCube();
    const auto face = mesh.faces().front().id;
    const std::array<m3d::EditableFaceId, 1> selected{face};
    std::string error;
    REQUIRE(mesh.deleteFaces(selected, &error));
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 8U);
    REQUIRE(mesh.edgeCount() == 12U);
    REQUIRE(mesh.halfEdgeCount() == 20U);
    REQUIRE(mesh.faceCount() == 5U);
    REQUIRE(mesh.findFace(face) == nullptr);
    std::size_t boundaryEdges = 0U;
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        if (halfEdge && halfEdge->twin.isNull()) ++boundaryEdges;
    }
    REQUIRE(boundaryEdges == 4U);
}

TEST_CASE("delete edge removes both incident cube faces") {
    auto mesh = m3d::EditableMesh::makeCube();
    const auto edge = mesh.edges().front().id;
    const std::array<m3d::EditableEdgeId, 1> selected{edge};
    std::string error;
    REQUIRE(mesh.deleteEdges(selected, &error));
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 8U);
    REQUIRE(mesh.edgeCount() == 11U);
    REQUIRE(mesh.halfEdgeCount() == 16U);
    REQUIRE(mesh.faceCount() == 4U);
    REQUIRE(mesh.findEdge(edge) == nullptr);
}

TEST_CASE("delete vertex removes every incident face then the isolated vertex") {
    auto mesh = m3d::EditableMesh::makeCube();
    const auto vertex = mesh.vertices().front().id;
    const std::array<m3d::EditableVertexId, 1> selected{vertex};
    std::string error;
    REQUIRE(mesh.deleteVertices(selected, &error));
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 7U);
    REQUIRE(mesh.edgeCount() == 9U);
    REQUIRE(mesh.halfEdgeCount() == 12U);
    REQUIRE(mesh.faceCount() == 3U);
    REQUIRE(mesh.findVertex(vertex) == nullptr);
}

TEST_CASE("delete all faces is rejected atomically") {
    auto mesh = m3d::EditableMesh::makeCube();
    std::vector<m3d::EditableFaceId> selected;
    for (const auto& face : mesh.faces()) selected.push_back(face.id);
    const auto before = mesh.snapshot();
    std::string error;
    REQUIRE(!mesh.deleteFaces(selected, &error));
    REQUIRE(!error.empty());
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}

TEST_CASE("multi loop cut creates evenly spaced shared rings") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto startEdge = mesh.edges().front().id;
    std::string error;
    const auto result = mesh.loopCut(startEdge, 3U, &error);
    REQUIRE(result.has_value());
    REQUIRE(error.empty());
    REQUIRE(result->vertices.size() == 12U);
    REQUIRE(result->edges.size() == 12U);
    REQUIRE(result->faces.size() == 16U);
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 20U);
    REQUIRE(mesh.edgeCount() == 36U);
    REQUIRE(mesh.halfEdgeCount() == 72U);
    REQUIRE(mesh.faceCount() == 18U);

    std::set<float> xCoordinates;
    std::set<float> yCoordinates;
    std::set<float> zCoordinates;
    for (const auto vertexId : result->vertices) {
        const auto* vertex = mesh.findVertex(vertexId);
        REQUIRE(vertex != nullptr);
        if (std::abs(vertex->position.x) < 0.999F) xCoordinates.insert(vertex->position.x);
        if (std::abs(vertex->position.y) < 0.999F) yCoordinates.insert(vertex->position.y);
        if (std::abs(vertex->position.z) < 0.999F) zCoordinates.insert(vertex->position.z);
    }
    REQUIRE(xCoordinates.size() == 3U || yCoordinates.size() == 3U || zCoordinates.size() == 3U);
}

TEST_CASE("multi loop cut rejects zero and excessive cut counts atomically") {
    auto mesh = m3d::EditableMesh::makeCube();
    const auto edge = mesh.edges().front().id;
    const auto before = mesh.snapshot();
    std::string error;
    REQUIRE(!mesh.loopCut(edge, 0U, &error).has_value());
    REQUIRE(!error.empty());
    REQUIRE(!mesh.loopCut(edge, 33U, &error).has_value());
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}

TEST_CASE("flip normals reverses an entire connected component consistently") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto seed = mesh.faces().front().id;
    const std::array<m3d::EditableFaceId,1> seeds{seed};
    std::string error;
    const auto flipped = mesh.flipFaceComponents(seeds, &error);
    REQUIRE(flipped.has_value());
    REQUIRE(flipped->size() == 6U);
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));

    float orientationScore = 0.0F;
    for (const auto& face : mesh.faces()) {
        const auto normal = mesh.faceNormal(face.id);
        const auto loop = mesh.faceVertices(face.id);
        REQUIRE(normal.has_value());
        REQUIRE(!loop.empty());
        m3d::Vec3 center{};
        for (const auto vertexId : loop) {
            const auto* vertex = mesh.findVertex(vertexId);
            REQUIRE(vertex != nullptr);
            center.x += vertex->position.x;
            center.y += vertex->position.y;
            center.z += vertex->position.z;
        }
        const float inverse = 1.0F / static_cast<float>(loop.size());
        center.x *= inverse; center.y *= inverse; center.z *= inverse;
        orientationScore += normal->x * center.x + normal->y * center.y + normal->z * center.z;
    }
    REQUIRE(orientationScore < 0.0F);
}

TEST_CASE("recalculate outside restores outward orientation for a closed component") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const std::array<m3d::EditableFaceId,1> seeds{mesh.faces().front().id};
    std::string error;
    REQUIRE(mesh.flipFaceComponents(seeds, &error).has_value());
    const auto flippedComponents = mesh.recalculateOutside(&error);
    REQUIRE(flippedComponents.has_value());
    REQUIRE(*flippedComponents == 1U);
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));

    m3d::MeshResource render;
    render.id = m3d::ResourceId::generate();
    render.name = "Normals Cube";
    render.authoring = mesh;
    REQUIRE(render.rebuildFromAuthoring(&error));
    for (const auto& vertex : render.vertices) {
        const float outward = vertex.normal.x * vertex.position.x +
                              vertex.normal.y * vertex.position.y +
                              vertex.normal.z * vertex.position.z;
        REQUIRE(outward > 0.0F);
    }
}

TEST_CASE("recalculate outside leaves open components unchanged") {
    auto mesh = m3d::EditableMesh::makeCube();
    std::string error;
    REQUIRE(mesh.deleteFaces(std::array<m3d::EditableFaceId,1>{mesh.faces().front().id}, &error));
    const auto before = mesh.snapshot();
    const auto result = mesh.recalculateOutside(&error);
    REQUIRE(result.has_value());
    REQUIRE(*result == 0U);
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}
