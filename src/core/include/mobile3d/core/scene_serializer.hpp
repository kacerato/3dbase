#pragma once

#include "mobile3d/core/scene.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace m3d {

class SceneSerializer final {
public:
    static constexpr int currentFormatVersion = 2;

    [[nodiscard]] static bool write(const std::filesystem::path& path,
                                    const Scene& scene,
                                    std::string* error = nullptr);
    [[nodiscard]] static std::optional<Scene> read(const std::filesystem::path& path,
                                                   std::string* error = nullptr);
};

} // namespace m3d
