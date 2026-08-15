#pragma once

#include "mobile3d/core/project_manifest.hpp"
#include "mobile3d/core/scene.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace m3d {

struct ProjectDocument final {
    std::filesystem::path root;
    ProjectManifest manifest;
    Scene scene;
};

class ProjectRepository final {
public:
    [[nodiscard]] static std::optional<ProjectDocument> create(const std::filesystem::path& root,
                                                               std::string name,
                                                               std::string* error = nullptr);
    [[nodiscard]] static std::optional<ProjectDocument> open(const std::filesystem::path& root,
                                                             std::string* error = nullptr);
    [[nodiscard]] static bool save(const ProjectDocument& document, std::string* error = nullptr);

    [[nodiscard]] static bool writeAutosave(const ProjectDocument& document,
                                            std::string* error = nullptr);
    [[nodiscard]] static bool hasAutosave(const std::filesystem::path& root);
    [[nodiscard]] static std::optional<Scene> loadAutosave(const std::filesystem::path& root,
                                                           std::string* error = nullptr);
    [[nodiscard]] static bool clearAutosave(const std::filesystem::path& root,
                                            std::string* error = nullptr);

    [[nodiscard]] static std::filesystem::path manifestPath(const std::filesystem::path& root);
    [[nodiscard]] static std::filesystem::path autosavePath(const std::filesystem::path& root);

private:
    [[nodiscard]] static bool createProjectDirectories(const std::filesystem::path& root,
                                                       std::string* error);
};

} // namespace m3d
