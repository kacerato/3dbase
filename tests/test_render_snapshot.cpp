#include "test_harness.hpp"

#include "mobile3d/render/render_snapshot.hpp"

TEST_CASE("render snapshot is an immutable deterministic copy of scene presentation state") {
    m3d::Scene scene;
    m3d::SelectionModel selection;

    const auto parent = scene.createObject(m3d::ObjectType::Empty, "Parent");
    m3d::Transform parentTransform;
    parentTransform.position = {5.0F, 0.0F, 0.0F};
    REQUIRE(scene.setTransform(parent, parentTransform));
    const auto mesh = scene.createObject(m3d::ObjectType::Mesh, "Mesh", parent);
    const auto resource = scene.createMeshResource(m3d::MeshResource::makeCube("Geometry", 2.0F));
    REQUIRE(scene.assignMesh(mesh, resource));

    m3d::Transform authoredTransform;
    authoredTransform.position = {2.0F, 3.0F, 4.0F};
    authoredTransform.scale = {1.5F, 2.0F, 0.5F};
    REQUIRE(scene.setTransform(mesh, authoredTransform));
    REQUIRE(selection.select(scene, mesh));

    const auto snapshot = m3d::RenderSnapshotBuilder::build(scene, selection, 17, 9);
    REQUIRE(snapshot.sceneRevision() == 17);
    REQUIRE(snapshot.selectionRevision() == 9);
    REQUIRE(snapshot.size() == 2);
    REQUIRE(snapshot.meshes().size() == 1);

    const auto* meshSnapshot = snapshot.find(mesh);
    REQUIRE(meshSnapshot != nullptr);
    REQUIRE(meshSnapshot->pickId != m3d::backgroundPickId);
    REQUIRE(snapshot.findPickId(meshSnapshot->pickId) == meshSnapshot);
    REQUIRE(meshSnapshot->type == m3d::ObjectType::Mesh);
    REQUIRE(meshSnapshot->parent == parent);
    REQUIRE(meshSnapshot->meshResource == resource);
    REQUIRE(meshSnapshot->localTransform == authoredTransform);
    REQUIRE(meshSnapshot->worldTransform.at(0, 3) == 7.0F);
    REQUIRE(meshSnapshot->worldTransform.at(1, 3) == 3.0F);
    REQUIRE(meshSnapshot->worldTransform.at(2, 3) == 4.0F);
    REQUIRE(meshSnapshot->visible);
    REQUIRE(meshSnapshot->selected);
    REQUIRE(meshSnapshot->active);
    REQUIRE(snapshot.findPickId(m3d::backgroundPickId) == nullptr);

    const auto* geometry = snapshot.findMesh(resource);
    REQUIRE(geometry != nullptr);
    REQUIRE(geometry->vertices.size() == 24);
    REQUIRE(geometry->indices.size() == 36);
    REQUIRE(geometry->bounds.has_value());
    REQUIRE(geometry->contentHash != 0);
    const auto originalHash = geometry->contentHash;
    REQUIRE((geometry->bounds->min == m3d::Vec3{-1.0F, -1.0F, -1.0F}));
    REQUIRE((geometry->bounds->max == m3d::Vec3{1.0F, 1.0F, 1.0F}));

    m3d::Transform changedTransform = authoredTransform;
    changedTransform.position.x = 99.0F;
    REQUIRE(scene.setTransform(mesh, changedTransform));
    scene.findMeshResource(resource)->vertices.front().position.x = 77.0F;
    selection.clear();

    meshSnapshot = snapshot.find(mesh);
    geometry = snapshot.findMesh(resource);
    REQUIRE(meshSnapshot != nullptr);
    REQUIRE(meshSnapshot->localTransform == authoredTransform);
    REQUIRE(meshSnapshot->selected);
    REQUIRE(meshSnapshot->active);
    REQUIRE(geometry != nullptr);
    REQUIRE(geometry->vertices.front().position.x == -1.0F);
    REQUIRE(geometry->contentHash == originalHash);

    const auto changedSnapshot = m3d::RenderSnapshotBuilder::build(scene, selection, 18, 10);
    REQUIRE(changedSnapshot.findMesh(resource)->contentHash != originalHash);
    REQUIRE(changedSnapshot.find(mesh)->pickId == meshSnapshot->pickId);
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
    REQUIRE(hiddenSnapshot->pickId != m3d::backgroundPickId);
    REQUIRE(!hiddenSnapshot->visible);
    REQUIRE(!hiddenSnapshot->selected);
    REQUIRE(!hiddenSnapshot->active);
}

TEST_CASE("render snapshot ordering and pick ids do not inherit unordered scene storage order") {
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
    REQUIRE(snapshot.objects().at(0).pickId == 1U);
    REQUIRE(snapshot.objects().at(1).pickId == 2U);
    REQUIRE(snapshot.findPickId(1U)->id == lowId);
    REQUIRE(snapshot.findPickId(2U)->id == highId);
}

TEST_CASE("render snapshot applies active layer organization state") {
    m3d::Scene scene;
    m3d::SelectionModel selection;
    const auto visible = scene.createObject(m3d::ObjectType::Empty, "Visible");
    const auto hidden = scene.createObject(m3d::ObjectType::Empty, "Hidden");
    const auto visibleCollection = scene.createCollection("Visible Collection");
    const auto hiddenCollection = scene.createCollection("Hidden Collection");
    const auto layer = scene.createLayer("Layer");
    REQUIRE(scene.addObjectToCollection(visibleCollection, visible));
    REQUIRE(scene.addObjectToCollection(hiddenCollection, hidden));
    REQUIRE(scene.addCollectionToLayer(layer, visibleCollection));
    REQUIRE(scene.setCollectionLocked(visibleCollection, true));

    const auto snapshot = m3d::RenderSnapshotBuilder::build(scene, selection, 1, 1, layer);
    REQUIRE(snapshot.find(visible) != nullptr);
    REQUIRE(snapshot.find(hidden) != nullptr);
    REQUIRE(snapshot.find(visible)->visible);
    REQUIRE(snapshot.find(visible)->locked);
    REQUIRE(!snapshot.find(hidden)->visible);
}
