from pathlib import Path

def replace_once(path, old, new):
    p=Path(path); s=p.read_text(); c=s.count(old)
    if c!=1: raise SystemExit(f'{path}: expected 1 match, got {c}')
    p.write_text(s.replace(old,new,1))

p='src/core/src/scene_serializer.cpp'
replace_once(p,
'''    output << "count " << scene.size() << '\\n';
''',
'''    output << "collection_count " << scene.collectionCount() << '\\n';
    for (const auto& collection : scene.collections()) {
        output << "collection " << std::quoted(collection.id.toString()) << ' '
               << std::quoted(collection.name) << ' '
               << (collection.visible ? 1 : 0) << ' '
               << (collection.locked ? 1 : 0) << ' '
               << collection.objects.size();
        for (const auto object : collection.objects) output << ' ' << std::quoted(object.toString());
        output << '\\n';
    }

    output << "layer_count " << scene.layerCount() << '\\n';
    for (const auto& layer : scene.layers()) {
        output << "layer " << std::quoted(layer.id.toString()) << ' '
               << std::quoted(layer.name) << ' '
               << (layer.enabled ? 1 : 0) << ' '
               << layer.collections.size();
        for (const auto collection : layer.collections) output << ' ' << std::quoted(collection.toString());
        output << '\\n';
    }

    output << "count " << scene.size() << '\\n';
''')

replace_once(p,
'''    std::string countKey;
    std::size_t count = 0;
''',
'''    std::vector<SceneCollection> decodedCollections;
    std::vector<SceneLayer> decodedLayers;
    if (version >= 3) {
        std::string collectionCountKey;
        std::size_t collectionCount = 0;
        if (!(input >> collectionCountKey >> collectionCount) || collectionCountKey != "collection_count") {
            if (error) *error = "Invalid scene collection count";
            return std::nullopt;
        }
        decodedCollections.reserve(collectionCount);
        for (std::size_t collectionIndex = 0; collectionIndex < collectionCount; ++collectionIndex) {
            std::string record;
            std::string idText;
            int visible = 1;
            int locked = 0;
            std::size_t objectCount = 0;
            SceneCollection collection;
            if (!(input >> record >> std::quoted(idText) >> std::quoted(collection.name)
                  >> visible >> locked >> objectCount) || record != "collection") {
                if (error) *error = "Malformed collection record";
                return std::nullopt;
            }
            const auto id = CollectionId::fromString(idText);
            if (!id || id->isNull()) { if (error) *error = "Invalid collection id"; return std::nullopt; }
            collection.id = *id;
            collection.visible = visible != 0;
            collection.locked = locked != 0;
            collection.objects.reserve(objectCount);
            for (std::size_t member = 0; member < objectCount; ++member) {
                std::string objectText;
                if (!(input >> std::quoted(objectText))) { if (error) *error = "Malformed collection membership"; return std::nullopt; }
                const auto objectId = ObjectId::fromString(objectText);
                if (!objectId || objectId->isNull()) { if (error) *error = "Invalid collection object id"; return std::nullopt; }
                collection.objects.push_back(*objectId);
            }
            decodedCollections.push_back(std::move(collection));
        }

        std::string layerCountKey;
        std::size_t layerCount = 0;
        if (!(input >> layerCountKey >> layerCount) || layerCountKey != "layer_count") {
            if (error) *error = "Invalid scene layer count";
            return std::nullopt;
        }
        decodedLayers.reserve(layerCount);
        for (std::size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
            std::string record;
            std::string idText;
            int enabled = 1;
            std::size_t collectionCount = 0;
            SceneLayer layer;
            if (!(input >> record >> std::quoted(idText) >> std::quoted(layer.name)
                  >> enabled >> collectionCount) || record != "layer") {
                if (error) *error = "Malformed layer record";
                return std::nullopt;
            }
            const auto id = LayerId::fromString(idText);
            if (!id || id->isNull()) { if (error) *error = "Invalid layer id"; return std::nullopt; }
            layer.id = *id;
            layer.enabled = enabled != 0;
            layer.collections.reserve(collectionCount);
            for (std::size_t member = 0; member < collectionCount; ++member) {
                std::string collectionText;
                if (!(input >> std::quoted(collectionText))) { if (error) *error = "Malformed layer membership"; return std::nullopt; }
                const auto collectionId = CollectionId::fromString(collectionText);
                if (!collectionId || collectionId->isNull()) { if (error) *error = "Invalid layer collection id"; return std::nullopt; }
                layer.collections.push_back(*collectionId);
            }
            decodedLayers.push_back(std::move(layer));
        }
    }

    std::string countKey;
    std::size_t count = 0;
''')

replace_once(p,
'''    if (!scene.restoreObjects(decoded)) {
        if (error) *error = "Scene hierarchy or resource references are invalid";
        return std::nullopt;
    }
    return scene;
''',
'''    if (!scene.restoreObjects(decoded)) {
        if (error) *error = "Scene hierarchy or resource references are invalid";
        return std::nullopt;
    }
    for (auto& collection : decodedCollections) {
        if (!scene.insertCollection(std::move(collection))) {
            if (error) *error = "Invalid collection or object membership";
            return std::nullopt;
        }
    }
    for (auto& layer : decodedLayers) {
        if (!scene.insertLayer(std::move(layer))) {
            if (error) *error = "Invalid layer or collection membership";
            return std::nullopt;
        }
    }
    return scene;
''')

# Add tests to test_scene.cpp.
t=Path('tests/test_scene.cpp'); s=t.read_text()
s += r'''

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
'''
t.write_text(s)

# Add serializer roundtrip test to test_project.cpp.
t=Path('tests/test_project.cpp'); s=t.read_text()
s += r'''

TEST_CASE("scene format v3 round trips collections layers and remains organization aware") {
    const auto root = uniqueProjectPath();
    ProjectCleanup cleanup(root);
    std::filesystem::create_directories(root);
    const auto file = root / "organization.m3scene";
    m3d::Scene scene;
    const auto object = scene.createObject(m3d::ObjectType::Empty, "Tree");
    const auto collection = scene.createCollection("Nature");
    const auto layer = scene.createLayer("Outdoor");
    REQUIRE(scene.addObjectToCollection(collection, object));
    REQUIRE(scene.addCollectionToLayer(layer, collection));
    REQUIRE(scene.setCollectionLocked(collection, true));
    REQUIRE(scene.setLayerEnabled(layer, true));
    std::string error;
    REQUIRE(m3d::SceneSerializer::write(file, scene, &error));
    const auto loaded = m3d::SceneSerializer::read(file, &error);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->collectionCount() == 1);
    REQUIRE(loaded->layerCount() == 1);
    REQUIRE(loaded->findCollection(collection) != nullptr);
    REQUIRE(loaded->findLayer(layer) != nullptr);
    REQUIRE(loaded->findCollection(collection)->objects == std::vector<m3d::ObjectId>{object});
    REQUIRE(loaded->findLayer(layer)->collections == std::vector<m3d::CollectionId>{collection});
    REQUIRE(loaded->isObjectVisibleInLayer(object, layer));
    REQUIRE(loaded->isObjectLockedByOrganization(object, layer));
}
'''
t.write_text(s)
print('organization persistence v3 applied')
