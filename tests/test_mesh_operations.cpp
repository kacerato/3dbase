#include "test_harness.hpp"

#include "mobile3d/core/editable_mesh.hpp"
#include "mobile3d/core/mesh_resource.hpp"

#include <cmath>
#include <string>

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
