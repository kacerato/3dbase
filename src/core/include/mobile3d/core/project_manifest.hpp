#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace m3d {

struct ProjectManifest final {
    static constexpr int currentFormatVersion = 1;

    int formatVersion{currentFormatVersion};
    std::string name{"Untitled"};
    std::filesystem::path mainScene{"scenes/main.m3scene"};
};

class ProjectManifestCodec final {
public:
    [[nodiscard]] static bool write(const std::filesystem::path& path,
                                    const ProjectManifest& manifest,
                                    std::string* error = nullptr);
    [[nodiscard]] static std::optional<ProjectManifest> read(const std::filesystem::path& path,
                                                             std::string* error = nullptr);
};

} // namespace m3d
