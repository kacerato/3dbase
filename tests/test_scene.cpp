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
