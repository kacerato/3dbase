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


TEST_CASE("collections and layers are separate from transform hierarchy") {
    m3d::Scene scene;
    const auto a = scene.createObject(m3d::ObjectType::Empty, "A");
    const auto b = scene.createObject(m3d::ObjectType::Empty, "B", a);
    const auto collection = scene.createCollection("Environment");
    const auto layer = scene.createLayer("Gameplay");
    REQUIRE(!collection.isNull());
    REQUIRE(!layer.isNull());
    REQUIRE(scene.addObjectToCollection(collection, a));
    REQUIRE(scene.addObjectToCollection(collection, b));
    REQUIRE(scene.addCollectionToLayer(layer, collection));
    REQUIRE(scene.find(b)->parent == a);
    REQUIRE(scene.findCollection(collection)->objects.size() == 2);
    REQUIRE(scene.findLayer(layer)->collections.size() == 1);
    REQUIRE(scene.isObjectVisibleInLayer(a, layer));
    REQUIRE(scene.setCollectionVisible(collection, false));
    REQUIRE(!scene.isObjectVisibleInLayer(a, layer));
    REQUIRE(scene.setCollectionVisible(collection, true));
    REQUIRE(scene.setCollectionLocked(collection, true));
    REQUIRE(scene.isObjectLockedByOrganization(b, layer));
    REQUIRE(scene.removeCollection(collection));
    REQUIRE(scene.findLayer(layer)->collections.empty());
}

TEST_CASE("removing objects cleans collection membership") {
    m3d::Scene scene;
    const auto parent = scene.createObject(m3d::ObjectType::Empty, "Parent");
    const auto child = scene.createObject(m3d::ObjectType::Empty, "Child", parent);
    const auto collection = scene.createCollection("Things");
    REQUIRE(scene.addObjectToCollection(collection, parent));
    REQUIRE(scene.addObjectToCollection(collection, child));
    const auto removed = scene.removeSubtree(parent);
    REQUIRE(removed.size() == 2);
    REQUIRE(scene.findCollection(collection)->objects.empty());
}
