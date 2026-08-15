#pragma once

#include "mobile3d/core/command_stack.hpp"
#include "mobile3d/core/project_repository.hpp"
#include "mobile3d/core/selection_model.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace m3d {

enum class Workspace {
    Layout,
    Modeling,
    Sculpt,
    UV,
    Paint,
    Shading,
    Animation,
    Rigging,
    Nodes,
    Render,
};

[[nodiscard]] std::string_view workspaceName(Workspace workspace) noexcept;

class EditorSession final {
public:
    [[nodiscard]] bool createProject(const std::filesystem::path& root, std::string name,
                                     std::string* error = nullptr);
    [[nodiscard]] bool openProject(const std::filesystem::path& root,
                                   std::string* error = nullptr);
    void closeProject() noexcept;

    [[nodiscard]] bool saveProject(std::string* error = nullptr);
    [[nodiscard]] bool writeAutosave(std::string* error = nullptr) const;
    [[nodiscard]] bool hasAutosave() const;
    [[nodiscard]] bool recoverAutosave(std::string* error = nullptr);
    [[nodiscard]] bool discardAutosave(std::string* error = nullptr);

    [[nodiscard]] bool hasProject() const noexcept { return document_.has_value(); }
    [[nodiscard]] bool isDirty() const noexcept { return recoveredDirty_ || commands_.isDirty(); }

    [[nodiscard]] const ProjectDocument* document() const noexcept;
    [[nodiscard]] ProjectDocument* document() noexcept;
    [[nodiscard]] const Scene* scene() const noexcept;
    [[nodiscard]] Scene* scene() noexcept;

    [[nodiscard]] std::optional<ObjectId> createObject(
        ObjectType type, std::string name, std::optional<ObjectId> parent = std::nullopt);
    [[nodiscard]] std::optional<ObjectId> createMeshObject(
        MeshResource resource, std::string name, std::optional<ObjectId> parent = std::nullopt);
    [[nodiscard]] bool deleteObject(ObjectId object);
    [[nodiscard]] bool deleteSelection();
    [[nodiscard]] bool renameObject(ObjectId object, std::string name);
    [[nodiscard]] bool transformObject(ObjectId object, const Transform& transform);
    [[nodiscard]] bool reparentObject(ObjectId object, std::optional<ObjectId> parent);

    [[nodiscard]] bool select(ObjectId object,
                              SelectionMode mode = SelectionMode::Replace);
    void clearSelection() noexcept;
    [[nodiscard]] const SelectionModel& selection() const noexcept { return selection_; }

    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();
    [[nodiscard]] bool canUndo() const noexcept { return commands_.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return commands_.canRedo(); }
    [[nodiscard]] std::string_view nextUndoName() const noexcept { return commands_.nextUndoName(); }
    [[nodiscard]] std::string_view nextRedoName() const noexcept { return commands_.nextRedoName(); }

    void setWorkspace(Workspace workspace) noexcept;
    [[nodiscard]] Workspace workspace() const noexcept { return workspace_; }

    [[nodiscard]] std::uint64_t sceneRevision() const noexcept { return sceneRevision_; }
    [[nodiscard]] std::uint64_t selectionRevision() const noexcept { return selectionRevision_; }
    [[nodiscard]] std::uint64_t documentRevision() const noexcept { return documentRevision_; }
    [[nodiscard]] std::uint64_t uiRevision() const noexcept { return uiRevision_; }

private:
    [[nodiscard]] bool requireProject(std::string* error) const;
    void resetForDocument(bool recoveredDirty) noexcept;
    void sceneMutated(bool pruneSelection = true);

    std::optional<ProjectDocument> document_;
    CommandStack commands_;
    SelectionModel selection_;
    Workspace workspace_{Workspace::Layout};
    bool recoveredDirty_{false};
    std::uint64_t sceneRevision_{0};
    std::uint64_t selectionRevision_{0};
    std::uint64_t documentRevision_{0};
    std::uint64_t uiRevision_{0};
};

} // namespace m3d
