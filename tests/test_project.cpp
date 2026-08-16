#include "test_harness.hpp"

#include "mobile3d/core/project_repository.hpp"
#include "mobile3d/core/scene_serializer.hpp"

#include <chrono>
#include <filesystem>

namespace {

std::filesystem::path tempProjectPath() {
    const auto value = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("mobile3d-test-" + std::to_string(value));
}

} // namespace

TEST_CASE("project repository creates saves and reopens a versioned project") {
    const auto path = tempProjectPath();
    std::string error;

    auto project = m3d::ProjectRepository::create(path, "Test Project", &error);
    REQUIRE(project.has_value());
    REQUIRE(error.empty());
    REQUIRE(std::filesystem::exists(path / "project.m3dproj"));
    REQUIRE(std::filesystem::is_directory(path / "textures"));

    const auto resource = project->scene.createMeshResource(m3d::MeshResource::makeCube("Cube Geometry", 1.0F));
    REQUIRE(!resource.isNull());
    const auto cube = project->scene.createObject(m3d::ObjectType::Mesh, "Cube");
    REQUIRE(project->scene.assignMesh(cube, resource));
    m3d::Transform transform;
    transform.position = {1.0F, 2.0F, 3.0F};
    REQUIRE(project->scene.setTransform(cube, transform));
    REQUIRE(m3d::ProjectRepository::save(*project, &error));

    const auto reopened = m3d::ProjectRepository::open(path, &error);
    REQUIRE(reopened.has_value());
    REQUIRE(reopened->manifest.name == "Test Project");
    REQUIRE(reopened->scene.size() == 1);
    REQUIRE(reopened->scene.meshResourceCount() == 1);
    REQUIRE(reopened->scene.find(cube) != nullptr);
    REQUIRE(reopened->scene.find(cube)->localTransform.position == transform.position);
    REQUIRE(reopened->scene.find(cube)->meshResource == resource);
    const auto* reopenedMesh = reopened->scene.findMeshResource(resource);
    REQUIRE(reopenedMesh != nullptr);
    REQUIRE(reopenedMesh->vertices.size() == 24);
    REQUIRE(reopenedMesh->indices.size() == 36);

    std::filesystem::remove_all(path);
}

TEST_CASE("autosave is independent from the primary scene and recoverable") {
    const auto path = tempProjectPath();
    std::string error;
    auto project = m3d::ProjectRepository::create(path, "Autosave", &error);
    REQUIRE(project.has_value());

    const auto id = project->scene.createObject(m3d::ObjectType::Mesh, "Unsaved Cube");
    REQUIRE(m3d::ProjectRepository::writeAutosave(*project, &error));
    REQUIRE(m3d::ProjectRepository::hasAutosave(path));

    const auto primary = m3d::ProjectRepository::open(path, &error);
    REQUIRE(primary.has_value());
    REQUIRE(primary->scene.size() == 0);

    const auto recovered = m3d::ProjectRepository::loadAutosave(path, &error);
    REQUIRE(recovered.has_value());
    REQUIRE(recovered->contains(id));
    REQUIRE(m3d::ProjectRepository::clearAutosave(path, &error));
    REQUIRE(!m3d::ProjectRepository::hasAutosave(path));

    std::filesystem::remove_all(path);
}

TEST_CASE("scene format v3 round trips collections layers and remains organization aware") {
    const auto root = tempProjectPath();
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

    std::filesystem::remove_all(root);
}
