#include "test_harness.hpp"

#include "mobile3d/core/scene.hpp"

TEST_CASE("scene creates hierarchy and rejects cycles") {
    m3d::Scene scene;
    const auto root = scene.createObject(m3d::ObjectType::Empty, "Root");
    const auto child = scene.createObject(m3d::ObjectType::Mesh, "Child", root);
    const auto grandchild = scene.createObject(m3d::ObjectType::Mesh, "Grandchild", child);

    REQUIRE(!root.isNull());
    REQUIRE(scene.size() == 3);
    REQUIRE(scene.childrenOf(root).size() == 1);
    REQUIRE(!scene.reparent(root, grandchild));
    REQUIRE(!scene.reparent(root, root));
    REQUIRE(scene.reparent(grandchild, root));
}

TEST_CASE("scene removes and restores an entire subtree") {
    m3d::Scene scene;
    const auto root = scene.createObject(m3d::ObjectType::Empty, "Root");
    const auto child = scene.createObject(m3d::ObjectType::Mesh, "Child", root);
    const auto leaf = scene.createObject(m3d::ObjectType::Mesh, "Leaf", child);

    const auto snapshot = scene.removeSubtree(child);
    REQUIRE(snapshot.size() == 2);
    REQUIRE(scene.size() == 1);
    REQUIRE(!scene.contains(child));
    REQUIRE(!scene.contains(leaf));

    REQUIRE(scene.restoreObjects(snapshot));
    REQUIRE(scene.size() == 3);
    REQUIRE(scene.find(child)->parent == root);
    REQUIRE(scene.find(leaf)->parent == child);
}

TEST_CASE("scene owns validated shareable mesh resources") {
    m3d::Scene scene;
    auto cube = m3d::MeshResource::makeCube("Shared Cube", 2.0F);
    const auto resource = scene.createMeshResource(cube);
    REQUIRE(!resource.isNull());
    REQUIRE(scene.meshResourceCount() == 1);
    REQUIRE(scene.findMeshResource(resource) != nullptr);
    REQUIRE(scene.findMeshResource(resource)->vertices.size() == 24);
    REQUIRE(scene.findMeshResource(resource)->indices.size() == 36);

    const auto first = scene.createObject(m3d::ObjectType::Mesh, "First");
    const auto second = scene.createObject(m3d::ObjectType::Mesh, "Second");
    const auto light = scene.createObject(m3d::ObjectType::Light, "Light");
    REQUIRE(scene.assignMesh(first, resource));
    REQUIRE(scene.assignMesh(second, resource));
    REQUIRE(!scene.assignMesh(light, resource));
    REQUIRE(!scene.removeMeshResource(resource));

    REQUIRE(scene.assignMesh(first, std::nullopt));
    REQUIRE(scene.assignMesh(second, std::nullopt));
    REQUIRE(scene.removeMeshResource(resource));
    REQUIRE(scene.meshResourceCount() == 0);
}
