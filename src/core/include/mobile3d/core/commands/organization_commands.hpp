#pragma once

#include "mobile3d/core/command.hpp"
#include "mobile3d/core/scene.hpp"

#include <string>
#include <vector>

namespace m3d {

class CreateCollectionCommand final : public EditorCommand {
public:
    CreateCollectionCommand(Scene& scene, std::string name);
    [[nodiscard]] std::string_view name() const noexcept override { return "Create Collection"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
    [[nodiscard]] CollectionId createdId() const noexcept { return collection_.id; }
private:
    Scene& scene_;
    SceneCollection collection_;
    bool initialized_{false};
};

class DeleteCollectionCommand final : public EditorCommand {
public:
    DeleteCollectionCommand(Scene& scene, CollectionId collection);
    [[nodiscard]] std::string_view name() const noexcept override { return "Delete Collection"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    CollectionId collection_{};
    SceneCollection snapshot_{};
    std::vector<LayerId> linkedLayers_;
    bool captured_{false};
};

class RenameCollectionCommand final : public EditorCommand {
public:
    RenameCollectionCommand(Scene& scene, CollectionId collection, std::string value);
    [[nodiscard]] std::string_view name() const noexcept override { return "Rename Collection"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    CollectionId collection_{};
    std::string before_;
    std::string after_;
    bool captured_{false};
};

class SetCollectionVisibilityCommand final : public EditorCommand {
public:
    SetCollectionVisibilityCommand(Scene& scene, CollectionId collection, bool visible);
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Collection Visibility"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    CollectionId collection_{};
    bool before_{true};
    bool after_{true};
    bool captured_{false};
};

class SetCollectionLockedCommand final : public EditorCommand {
public:
    SetCollectionLockedCommand(Scene& scene, CollectionId collection, bool locked);
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Collection Lock"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    CollectionId collection_{};
    bool before_{false};
    bool after_{false};
    bool captured_{false};
};

class AddObjectToCollectionCommand final : public EditorCommand {
public:
    AddObjectToCollectionCommand(Scene& scene, CollectionId collection, ObjectId object);
    [[nodiscard]] std::string_view name() const noexcept override { return "Add Object to Collection"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    CollectionId collection_{};
    ObjectId object_{};
};

class RemoveObjectFromCollectionCommand final : public EditorCommand {
public:
    RemoveObjectFromCollectionCommand(Scene& scene, CollectionId collection, ObjectId object);
    [[nodiscard]] std::string_view name() const noexcept override { return "Remove Object from Collection"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    CollectionId collection_{};
    ObjectId object_{};
};

class CreateLayerCommand final : public EditorCommand {
public:
    CreateLayerCommand(Scene& scene, std::string name);
    [[nodiscard]] std::string_view name() const noexcept override { return "Create Layer"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
    [[nodiscard]] LayerId createdId() const noexcept { return layer_.id; }
private:
    Scene& scene_;
    SceneLayer layer_;
    bool initialized_{false};
};

class DeleteLayerCommand final : public EditorCommand {
public:
    DeleteLayerCommand(Scene& scene, LayerId layer);
    [[nodiscard]] std::string_view name() const noexcept override { return "Delete Layer"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    LayerId layer_{};
    SceneLayer snapshot_{};
    bool captured_{false};
};

class RenameLayerCommand final : public EditorCommand {
public:
    RenameLayerCommand(Scene& scene, LayerId layer, std::string value);
    [[nodiscard]] std::string_view name() const noexcept override { return "Rename Layer"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    LayerId layer_{};
    std::string before_;
    std::string after_;
    bool captured_{false};
};

class SetLayerEnabledCommand final : public EditorCommand {
public:
    SetLayerEnabledCommand(Scene& scene, LayerId layer, bool enabled);
    [[nodiscard]] std::string_view name() const noexcept override { return "Set Layer Enabled"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    LayerId layer_{};
    bool before_{true};
    bool after_{true};
    bool captured_{false};
};

class AddCollectionToLayerCommand final : public EditorCommand {
public:
    AddCollectionToLayerCommand(Scene& scene, LayerId layer, CollectionId collection);
    [[nodiscard]] std::string_view name() const noexcept override { return "Add Collection to Layer"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    LayerId layer_{};
    CollectionId collection_{};
};

class RemoveCollectionFromLayerCommand final : public EditorCommand {
public:
    RemoveCollectionFromLayerCommand(Scene& scene, LayerId layer, CollectionId collection);
    [[nodiscard]] std::string_view name() const noexcept override { return "Remove Collection from Layer"; }
    [[nodiscard]] bool execute() override;
    [[nodiscard]] bool undo() override;
private:
    Scene& scene_;
    LayerId layer_{};
    CollectionId collection_{};
};

} // namespace m3d
