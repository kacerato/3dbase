#include "test_harness.hpp"

#include "mobile3d/core/project_repository.hpp"

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

    const auto cube = project->scene.createObject(m3d::ObjectType::Mesh, "Cube");
    m3d::Transform transform;
    transform.position = {1.0F, 2.0F, 3.0F};
    REQUIRE(project->scene.setTransform(cube, transform));
    REQUIRE(m3d::ProjectRepository::save(*project, &error));

    const auto reopened = m3d::ProjectRepository::open(path, &error);
    REQUIRE(reopened.has_value());
    REQUIRE(reopened->manifest.name == "Test Project");
    REQUIRE(reopened->scene.size() == 1);
    REQUIRE(reopened->scene.find(cube) != nullptr);
    REQUIRE(reopened->scene.find(cube)->localTransform.position == transform.position);

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
