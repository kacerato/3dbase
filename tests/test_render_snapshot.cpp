#include "test_harness.hpp"

#include "mobile3d/render/render_snapshot.hpp"

TEST_CASE("render snapshot is an immutable deterministic copy of scene presentation state") {
    m3d::Scene scene;
    m3d::SelectionModel selection;

    const auto parent = scene.createObject(m3d::ObjectType::Empty, "Parent");
    const auto mesh = scene.createObject(m3d::ObjectType::Mesh, "Mesh", parent);

    m3d::Transform authoredTransform;
    authoredTransform.position = {2.0F, 3.0F, 4.0F};
    authoredTransform.scale = {1.5F, 2.0F, 0.5F};
    REQUIRE(scene.setTransform(mesh, authoredTransform));
    REQUIRE(selection.select(scene, mesh));

    const auto snapshot = m3d::RenderSnapshotBuilder::build(scene, selection, 17, 9);
    REQUIRE(snapshot.sceneRevision() == 17);
    REQUIRE(snapshot.selectionRevision() == 9);
    REQUIRE(snapshot.size() == 2);

    const auto* meshSnapshot = snapshot.find(mesh);
    REQUIRE(meshSnapshot != nullptr);
    REQUIRE(meshSnapshot->type == m3d::ObjectType::Mesh);
    REQUIRE(meshSnapshot->parent == parent);
    REQUIRE(meshSnapshot->localTransform == authoredTransform);
    REQUIRE(meshSnapshot->visible);
    REQUIRE(meshSnapshot->selected);
    REQUIRE(meshSnapshot->active);

    m3d::Transform changedTransform = authoredTransform;
    changedTransform.position.x = 99.0F;
    REQUIRE(scene.setTransform(mesh, changedTransform));
    selection.clear();

    meshSnapshot = snapshot.find(mesh);
    REQUIRE(meshSnapshot != nullptr);
    REQUIRE(meshSnapshot->localTransform == authoredTransform);
    REQUIRE(meshSnapshot->selected);
    REQUIRE(meshSnapshot->active);
}

TEST_CASE("render snapshot retains hidden objects and their explicit visibility state") {
    m3d::Scene scene;
    m3d::SelectionModel selection;

    const auto hidden = scene.createObject(m3d::ObjectType::Light, "Hidden Light");
    auto* object = scene.find(hidden);
    REQUIRE(object != nullptr);
    object->visible = false;

    const auto snapshot = m3d::RenderSnapshotBuilder::build(scene, selection, 1, 1);
    const auto* hiddenSnapshot = snapshot.find(hidden);
    REQUIRE(hiddenSnapshot != nullptr);
    REQUIRE(!hiddenSnapshot->visible);
    REQUIRE(!hiddenSnapshot->selected);
    REQUIRE(!hiddenSnapshot->active);
}

TEST_CASE("render snapshot ordering does not inherit unordered scene storage order") {
    m3d::Scene scene;
    m3d::SelectionModel selection;

    const m3d::ObjectId highId(9, 1);
    const m3d::ObjectId lowId(2, 7);

    m3d::SceneObject high;
    high.id = highId;
    high.name = "High";
    high.type = m3d::ObjectType::Camera;
    REQUIRE(scene.insertObject(high));

    m3d::SceneObject low;
    low.id = lowId;
    low.name = "Low";
    low.type = m3d::ObjectType::Mesh;
    REQUIRE(scene.insertObject(low));

    const auto snapshot = m3d::RenderSnapshotBuilder::build(scene, selection, 3, 4);
    REQUIRE(snapshot.size() == 2);
    REQUIRE(snapshot.objects().at(0).id == lowId);
    REQUIRE(snapshot.objects().at(1).id == highId);
}
