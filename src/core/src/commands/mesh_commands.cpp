#include "mobile3d/core/commands/mesh_commands.hpp"

#include <utility>

namespace m3d {

ReplaceMeshResourceCommand::ReplaceMeshResourceCommand(Scene& scene, MeshResource before,
                                                       MeshResource after, std::string commandName)
    : scene_(scene), resource_(after.id), before_(std::move(before)), after_(std::move(after)),
      commandName_(commandName.empty() ? "Edit Mesh" : std::move(commandName)) {}

bool ReplaceMeshResourceCommand::execute() {
    std::string beforeError;
    std::string afterError;
    if (resource_.isNull() || before_.id != resource_ || after_.id != resource_ ||
        !before_.validate(&beforeError) || !after_.validate(&afterError) ||
        !scene_.findMeshResource(resource_)) {
        return false;
    }
    *scene_.findMeshResource(resource_) = after_;
    return true;
}

bool ReplaceMeshResourceCommand::undo() {
    if (resource_.isNull() || !scene_.findMeshResource(resource_)) return false;
    *scene_.findMeshResource(resource_) = before_;
    return true;
}

} // namespace m3d
