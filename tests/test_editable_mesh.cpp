#include "test_harness.hpp"

#include "mobile3d/core/editable_mesh.hpp"
#include "mobile3d/core/mesh_resource.hpp"

#include <array>
#include <string>

TEST_CASE("editable cube preserves quad topology and manifold twins") {
    const auto mesh = m3d::EditableMesh::makeCube(2.0F);
    std::string error;
    REQUIRE(mesh.validate(&error));
    REQUIRE(error.empty());
    REQUIRE(mesh.vertexCount() == 8U);
    REQUIRE(mesh.edgeCount() == 12U);
    REQUIRE(mesh.halfEdgeCount() == 24U);
    REQUIRE(mesh.faceCount() == 6U);

    for (const auto& face : mesh.faces()) REQUIRE(mesh.faceVertices(face.id).size() == 4U);
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        REQUIRE(halfEdge != nullptr);
        REQUIRE(!halfEdge->twin.isNull());
        REQUIRE(mesh.findHalfEdge(halfEdge->twin) != nullptr);
    }
}

TEST_CASE("editable cube rebuilds validated render geometry") {
    m3d::MeshResource rendered;
    rendered.id = m3d::ResourceId::generate();
    rendered.name = "Editable Cube";
    rendered.authoring = m3d::EditableMesh::makeCube(2.0F);
    std::string error;
    REQUIRE(rendered.rebuildFromAuthoring(&error));
    REQUIRE(error.empty());
    REQUIRE(rendered.vertices.size() == 24U);
    REQUIRE(rendered.indices.size() == 36U);
    REQUIRE(rendered.validate(&error));
    const auto bounds = rendered.bounds();
    REQUIRE(bounds.has_value());
    REQUIRE((bounds->min == m3d::Vec3{-1.0F, -1.0F, -1.0F}));
    REQUIRE((bounds->max == m3d::Vec3{1.0F, 1.0F, 1.0F}));
}

TEST_CASE("mesh cube keeps authored quads alongside derived render triangles") {
    const auto resource = m3d::MeshResource::makeCube("Cube", 2.0F);
    REQUIRE(resource.authoring.has_value());
    REQUIRE(resource.authoring->vertexCount() == 8U);
    REQUIRE(resource.authoring->edgeCount() == 12U);
    REQUIRE(resource.authoring->faceCount() == 6U);
    REQUIRE(resource.vertices.size() == 24U);
    REQUIRE(resource.indices.size() == 36U);
}

TEST_CASE("legacy triangle render cube reconstructs welded editable topology") {
    auto rendered = m3d::MeshResource::makeCube("Render Cube", 2.0F);
    rendered.authoring.reset();
    std::string error;
    const auto editable = m3d::EditableMesh::fromMeshResource(rendered, 1.0e-5F, &error);
    REQUIRE(editable.has_value());
    REQUIRE(error.empty());
    REQUIRE(editable->validate(&error));
    REQUIRE(editable->vertexCount() == 8U);
    REQUIRE(editable->faceCount() == 12U);
    REQUIRE(editable->halfEdgeCount() == 36U);
    REQUIRE(editable->edgeCount() == 18U);
}

TEST_CASE("editable mesh snapshot round trip preserves every topology id") {
    const auto original = m3d::EditableMesh::makeCube();
    const auto snapshot = original.snapshot();
    std::string error;
    const auto restored = m3d::EditableMesh::fromSnapshot(snapshot, &error);
    REQUIRE(restored.has_value());
    REQUIRE(error.empty());
    REQUIRE(restored->validate(&error));
    REQUIRE(restored->snapshot().vertices == snapshot.vertices);
    REQUIRE(restored->snapshot().halfEdges == snapshot.halfEdges);
    REQUIRE(restored->snapshot().edges == snapshot.edges);
    REQUIRE(restored->snapshot().faces == snapshot.faces);
}

TEST_CASE("editable mesh rejects a third face on an existing manifold edge") {
    m3d::EditableMesh mesh;
    const auto a = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const auto b = mesh.addVertex({1.0F, 0.0F, 0.0F});
    const auto c = mesh.addVertex({0.0F, 1.0F, 0.0F});
    const auto d = mesh.addVertex({0.0F, -1.0F, 0.0F});
    const auto e = mesh.addVertex({0.0F, 0.0F, 1.0F});

    const std::array first{a, b, c};
    const std::array second{b, a, d};
    const std::array third{a, b, e};
    REQUIRE(mesh.addFace(first).has_value());
    REQUIRE(mesh.addFace(second).has_value());
    std::string error;
    REQUIRE(!mesh.addFace(third, &error).has_value());
    REQUIRE(!error.empty());
}

TEST_CASE("editable topology element ids are stable monotonic handles") {
    m3d::EditableMesh mesh;
    const auto a = mesh.addVertex({0.0F, 0.0F, 0.0F});
    const auto b = mesh.addVertex({1.0F, 0.0F, 0.0F});
    const auto c = mesh.addVertex({0.0F, 1.0F, 0.0F});
    REQUIRE(a.value == 1U);
    REQUIRE(b.value == 2U);
    REQUIRE(c.value == 3U);
    REQUIRE(mesh.findVertex(a) != nullptr);
    REQUIRE(mesh.findVertex(b) != nullptr);
    REQUIRE(mesh.findVertex(c) != nullptr);
}
