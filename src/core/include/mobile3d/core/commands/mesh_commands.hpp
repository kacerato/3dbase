#pragma once

#include "mobile3d/core/command.hpp"
#include "mobile3d/core/scene.hpp"

namespace m3d {

class ReplaceMeshResourceCommand final : public EditorCommand {
public:
    ReplaceMeshResourceCommand(Scene& scene, MeshResource before, MeshResource after,
                               std::string commandName = "Edit Mesh");

    [[nodiscard]] std::string_view name() const noexcept override { return commandName_; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;

private:
    Scene& scene_;
    ResourceId resource_{};
    MeshResource before_{};
    MeshResource after_{};
    std::string commandName_;
};

} // namespace m3d
