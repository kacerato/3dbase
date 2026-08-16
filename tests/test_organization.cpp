#include "test_harness.hpp"

#include "mobile3d/core/commands/organization_commands.hpp"

#include <algorithm>

TEST_CASE("collection commands preserve identity and memberships through undo redo") {
    m3d::Scene scene;
    const auto object = scene.createObject(m3d::ObjectType::Empty, "Object");

    m3d::CreateCollectionCommand create(scene, "Gameplay");
    REQUIRE(create.execute());
    const auto collection = create.createdId();
    REQUIRE(scene.containsCollection(collection));
    REQUIRE(create.undo());
    REQUIRE(!scene.containsCollection(collection));
    REQUIRE(create.execute());
    REQUIRE(scene.containsCollection(collection));

    m3d::AddObjectToCollectionCommand membership(scene, collection, object);
    REQUIRE(membership.execute());
    REQUIRE(scene.findCollection(collection)->objects == std::vector<m3d::ObjectId>{object});
    REQUIRE(membership.undo());
    REQUIRE(scene.findCollection(collection)->objects.empty());
    REQUIRE(membership.execute());

    m3d::CreateLayerCommand createLayer(scene, "Main");
    REQUIRE(createLayer.execute());
    const auto layer = createLayer.createdId();
    m3d::AddCollectionToLayerCommand link(scene, layer, collection);
    REQUIRE(link.execute());

    m3d::DeleteCollectionCommand remove(scene, collection);
    REQUIRE(remove.execute());
    REQUIRE(!scene.containsCollection(collection));
    REQUIRE(scene.findLayer(layer)->collections.empty());
    REQUIRE(remove.undo());
    REQUIRE(scene.containsCollection(collection));
    REQUIRE(scene.findCollection(collection)->objects == std::vector<m3d::ObjectId>{object});
    REQUIRE(scene.findLayer(layer)->collections == std::vector<m3d::CollectionId>{collection});
}

TEST_CASE("collection and layer state commands are fully reversible") {
    m3d::Scene scene;
    const auto collection = scene.createCollection("Collection");
    const auto layer = scene.createLayer("Layer");

    m3d::SetCollectionVisibilityCommand visibility(scene, collection, false);
    REQUIRE(visibility.execute());
    REQUIRE(!scene.findCollection(collection)->visible);
    REQUIRE(visibility.undo());
    REQUIRE(scene.findCollection(collection)->visible);

    m3d::SetCollectionLockedCommand lock(scene, collection, true);
    REQUIRE(lock.execute());
    REQUIRE(scene.findCollection(collection)->locked);
    REQUIRE(lock.undo());
    REQUIRE(!scene.findCollection(collection)->locked);

    m3d::SetLayerEnabledCommand enabled(scene, layer, false);
    REQUIRE(enabled.execute());
    REQUIRE(!scene.findLayer(layer)->enabled);
    REQUIRE(enabled.undo());
    REQUIRE(scene.findLayer(layer)->enabled);
}
