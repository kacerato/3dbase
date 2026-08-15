#include "test_harness.hpp"

#include "mobile3d/core/scene.hpp"
#include "mobile3d/core/selection_model.hpp"

TEST_CASE("selection model supports replace add and toggle") {
    m3d::Scene scene;
    const auto first = scene.createObject(m3d::ObjectType::Mesh, "First");
    const auto second = scene.createObject(m3d::ObjectType::Mesh, "Second");
    m3d::SelectionModel selection;

    REQUIRE(selection.select(scene, first));
    REQUIRE(selection.size() == 1);
    REQUIRE(selection.active() == first);

    REQUIRE(selection.select(scene, second, m3d::SelectionMode::Add));
    REQUIRE(selection.size() == 2);
    REQUIRE(selection.active() == second);
    REQUIRE(selection.contains(first));

    REQUIRE(selection.select(scene, second, m3d::SelectionMode::Toggle));
    REQUIRE(selection.size() == 1);
    REQUIRE(selection.active() == first);

    REQUIRE(selection.select(scene, first, m3d::SelectionMode::Toggle));
    REQUIRE(selection.empty());
    REQUIRE(!selection.active().has_value());
}

TEST_CASE("selection prunes objects deleted from scene") {
    m3d::Scene scene;
    const auto root = scene.createObject(m3d::ObjectType::Empty, "Root");
    const auto child = scene.createObject(m3d::ObjectType::Mesh, "Child", root);
    m3d::SelectionModel selection;

    REQUIRE(selection.select(scene, root));
    REQUIRE(selection.select(scene, child, m3d::SelectionMode::Add));
    REQUIRE(scene.removeSubtree(root).size() == 2);

    selection.prune(scene);
    REQUIRE(selection.empty());
    REQUIRE(!selection.active().has_value());
}
