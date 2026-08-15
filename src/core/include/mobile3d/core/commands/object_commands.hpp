#pragma once

#include "mobile3d/core/command.hpp"
#include "mobile3d/core/scene.hpp"

#include <optional>
#include <string>
#include <vector>

namespace m3d {

class CreateObjectCommand final : public EditorCommand {
public:
    CreateObjectCommand(Scene& scene, ObjectType type, std::string name,
                        std::optional<ObjectId> parent = std::nullopt);

    [[nodiscard]] std::string_view name() const noexcept override { return "Create Object"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
    [[nodiscard]] ObjectId createdId() const noexcept { return object_.id; }

private:
    Scene& scene_;
    SceneObject object_;
    bool initialized_{false};
};

class CreateMeshObjectCommand final : public EditorCommand {
public:
    CreateMeshObjectCommand(Scene& scene, MeshResource resource, std::string name,
                            std::optional<ObjectId> parent = std::nullopt);

    [[nodiscard]] std::string_view name() const noexcept override { return "Create Mesh Object"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
    [[nodiscard]] ObjectId createdId() const noexcept { return object_.id; }
    [[nodiscard]] ResourceId resourceId() const noexcept { return resource_.id; }

private:
    Scene& scene_;
    MeshResource resource_;
    SceneObject object_;
    bool initialized_{false};
};

class DeleteObjectCommand final : public EditorCommand {
public:
    DeleteObjectCommand(Scene& scene, ObjectId object);

    [[nodiscard]] std::string_view name() const noexcept override { return "Delete Object"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;

private:
    Scene& scene_;
    ObjectId object_{};
    std::vector<SceneObject> snapshot_;
};

class RenameObjectCommand final : public EditorCommand {
public:
    RenameObjectCommand(Scene& scene, ObjectId object, std::string newName);

    [[nodiscard]] std::string_view name() const noexcept override { return "Rename Object"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;

private:
    Scene& scene_;
    ObjectId object_{};
    std::string oldName_;
    std::string newName_;
    bool captured_{false};
};

class TransformObjectCommand final : public EditorCommand {
public:
    TransformObjectCommand(Scene& scene, ObjectId object, Transform newTransform);

    [[nodiscard]] std::string_view name() const noexcept override { return "Transform Object"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;

private:
    Scene& scene_;
    ObjectId object_{};
    Transform oldTransform_{};
    Transform newTransform_{};
    bool captured_{false};
};

class ReparentObjectCommand final : public EditorCommand {
public:
    ReparentObjectCommand(Scene& scene, ObjectId object, std::optional<ObjectId> newParent);

    [[nodiscard]] std::string_view name() const noexcept override { return "Reparent Object"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;

private:
    Scene& scene_;
    ObjectId object_{};
    std::optional<ObjectId> oldParent_{};
    std::optional<ObjectId> newParent_{};
    bool captured_{false};
};

} // namespace m3d
