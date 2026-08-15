#include "mobile3d/core/project_repository.hpp"

#include "mobile3d/core/scene_serializer.hpp"

#include <array>
#include <system_error>
#include <utility>

namespace m3d {

std::filesystem::path ProjectRepository::manifestPath(const std::filesystem::path& root) {
    return root / "project.m3dproj";
}

std::filesystem::path ProjectRepository::autosavePath(const std::filesystem::path& root) {
    return root / "autosave" / "main.m3scene.autosave";
}

bool ProjectRepository::createProjectDirectories(const std::filesystem::path& root, std::string* error) {
    static constexpr std::array<const char*, 10> directories{
        "scenes", "models", "materials", "textures", "animations",
        "scripts", "cache", "autosave", "thumbnails", "imports"
    };

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) {
        if (error) *error = "Could not create project root: " + ec.message();
        return false;
    }
    for (const auto* directory : directories) {
        std::filesystem::create_directories(root / directory, ec);
        if (ec) {
            if (error) *error = "Could not create project directory '" + std::string(directory) + "': " + ec.message();
            return false;
        }
    }
    return true;
}

std::optional<ProjectDocument> ProjectRepository::create(const std::filesystem::path& root,
                                                         std::string name,
                                                         std::string* error) {
    if (name.empty()) {
        if (error) *error = "Project name cannot be empty";
        return std::nullopt;
    }
    if (!createProjectDirectories(root, error)) {
        return std::nullopt;
    }

    ProjectDocument document;
    document.root = root;
    document.manifest.name = std::move(name);

    if (!save(document, error)) {
        return std::nullopt;
    }
    return document;
}

std::optional<ProjectDocument> ProjectRepository::open(const std::filesystem::path& root,
                                                       std::string* error) {
    const auto manifest = ProjectManifestCodec::read(manifestPath(root), error);
    if (!manifest) {
        return std::nullopt;
    }

    const auto scene = SceneSerializer::read(root / manifest->mainScene, error);
    if (!scene) {
        return std::nullopt;
    }

    ProjectDocument document;
    document.root = root;
    document.manifest = *manifest;
    document.scene = *scene;
    return document;
}

bool ProjectRepository::save(const ProjectDocument& document, std::string* error) {
    if (!createProjectDirectories(document.root, error)) {
        return false;
    }
    if (!SceneSerializer::write(document.root / document.manifest.mainScene, document.scene, error)) {
        return false;
    }
    if (!ProjectManifestCodec::write(manifestPath(document.root), document.manifest, error)) {
        return false;
    }
    return true;
}

bool ProjectRepository::writeAutosave(const ProjectDocument& document, std::string* error) {
    return SceneSerializer::write(autosavePath(document.root), document.scene, error);
}

bool ProjectRepository::hasAutosave(const std::filesystem::path& root) {
    std::error_code ec;
    return std::filesystem::is_regular_file(autosavePath(root), ec) && !ec;
}

std::optional<Scene> ProjectRepository::loadAutosave(const std::filesystem::path& root,
                                                     std::string* error) {
    return SceneSerializer::read(autosavePath(root), error);
}

bool ProjectRepository::clearAutosave(const std::filesystem::path& root, std::string* error) {
    std::error_code ec;
    const bool removed = std::filesystem::remove(autosavePath(root), ec);
    if (ec) {
        if (error) *error = "Could not remove autosave: " + ec.message();
        return false;
    }
    return removed || !hasAutosave(root);
}

} // namespace m3d
