#pragma once

#include "mobile3d/core/command_stack.hpp"
#include "mobile3d/core/commands/mesh_commands.hpp"
#include "mobile3d/core/commands/object_commands.hpp"
#include "mobile3d/core/commands/organization_commands.hpp"
#include "mobile3d/core/project_repository.hpp"
#include "mobile3d/core/selection_model.hpp"
#include "mobile3d/editor/mesh_selection.hpp"
#include "mobile3d/editor/mesh_edit_snapshot.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    [[nodiscard]] bool isDirty() const noexcept;

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
    [[nodiscard]] bool duplicateSelection();
    [[nodiscard]] bool setObjectVisible(ObjectId object, bool visible);
    [[nodiscard]] bool setObjectLocked(ObjectId object, bool locked);
    [[nodiscard]] bool renameObject(ObjectId object, std::string name);
    [[nodiscard]] bool transformObject(ObjectId object, const Transform& transform);
    [[nodiscard]] bool beginTransformTransaction(const std::vector<ObjectId>& objects,
                                                 std::string commandName = "Transform Objects");
    [[nodiscard]] bool previewTransform(ObjectId object, const Transform& transform);
    [[nodiscard]] bool commitTransformTransaction();
    [[nodiscard]] bool cancelTransformTransaction();
    [[nodiscard]] bool hasTransformTransaction() const noexcept { return transformTransaction_.has_value(); }
    [[nodiscard]] bool reparentObject(ObjectId object, std::optional<ObjectId> parent);

    [[nodiscard]] bool beginMeshEdit(ObjectId object, std::string* error = nullptr);
    [[nodiscard]] bool commitMeshEdit(std::string commandName = "Edit Mesh",
                                      std::string* error = nullptr);
    [[nodiscard]] bool cancelMeshEdit();
    [[nodiscard]] bool hasMeshEditTransaction() const noexcept { return meshEditTransaction_.has_value(); }
    [[nodiscard]] const EditableMesh* editableMesh() const noexcept;
    [[nodiscard]] const MeshSelectionModel* meshSelection() const noexcept;
    [[nodiscard]] MeshEditPresentationSnapshot meshEditPresentationSnapshot() const;
    [[nodiscard]] bool setMeshSelectionMode(MeshSelectionMode mode) noexcept;
    [[nodiscard]] bool selectMeshVertex(EditableVertexId vertex,
                                        MeshSelectionAction action = MeshSelectionAction::Replace);
    [[nodiscard]] bool selectMeshEdge(EditableEdgeId edge,
                                      MeshSelectionAction action = MeshSelectionAction::Replace);
    [[nodiscard]] bool selectMeshFace(EditableFaceId face,
                                      MeshSelectionAction action = MeshSelectionAction::Replace);
    [[nodiscard]] bool moveSelectedMeshVertices(Vec3 delta, std::string* error = nullptr);
    [[nodiscard]] bool extrudeSelectedMeshFace(float distance, std::string* error = nullptr);
    [[nodiscard]] bool insetSelectedMeshFace(float ratio, std::string* error = nullptr);
    [[nodiscard]] bool subdivideSelectedMeshFace(std::string* error = nullptr);

    [[nodiscard]] bool select(ObjectId object,
                              SelectionMode mode = SelectionMode::Replace);
    void clearSelection() noexcept;
    [[nodiscard]] const SelectionModel& selection() const noexcept { return selection_; }

    [[nodiscard]] std::optional<LayerId> activeLayer() const noexcept { return activeLayer_; }
    [[nodiscard]] bool setActiveLayer(std::optional<LayerId> layer);

    [[nodiscard]] std::optional<CollectionId> createCollection(std::string name);
    [[nodiscard]] bool deleteCollection(CollectionId collection);
    [[nodiscard]] bool renameCollection(CollectionId collection, std::string name);
    [[nodiscard]] bool setCollectionVisible(CollectionId collection, bool visible);
    [[nodiscard]] bool setCollectionLocked(CollectionId collection, bool locked);
    [[nodiscard]] bool addSelectionToCollection(CollectionId collection);
    [[nodiscard]] bool removeObjectFromCollection(CollectionId collection, ObjectId object);

    [[nodiscard]] std::optional<LayerId> createLayer(std::string name);
    [[nodiscard]] bool deleteLayer(LayerId layer);
    [[nodiscard]] bool renameLayer(LayerId layer, std::string name);
    [[nodiscard]] bool setLayerEnabled(LayerId layer, bool enabled);
    [[nodiscard]] bool addCollectionToLayer(LayerId layer, CollectionId collection);
    [[nodiscard]] bool removeCollectionFromLayer(LayerId layer, CollectionId collection);

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
    struct TransformTransactionState final {
        std::vector<TransformChange> changes;
        std::string commandName;
    };

    struct MeshEditTransactionState final {
        ObjectId object{};
        ResourceId resource{};
        MeshResource before{};
        EditableMesh working{};
        MeshSelectionModel selection{};
        bool dirty{false};
    };

    [[nodiscard]] bool requireProject(std::string* error) const;
    [[nodiscard]] bool transformTransactionHasChanges() const noexcept;
    [[nodiscard]] bool objectLockedByActiveLayer(ObjectId object) const noexcept;
    [[nodiscard]] bool hasActiveMutationTransaction() const noexcept {
        return transformTransaction_.has_value() || meshEditTransaction_.has_value();
    }
    [[nodiscard]] bool applyMeshEditPreview(const EditableMesh& candidate, std::string* error);
    void pruneSelectionForActiveLayer();
    void resetForDocument(bool recoveredDirty) noexcept;
    void sceneMutated(bool pruneSelection = true);

    std::optional<ProjectDocument> document_;
    CommandStack commands_;
    SelectionModel selection_;
    std::optional<LayerId> activeLayer_;
    std::optional<TransformTransactionState> transformTransaction_;
    std::optional<MeshEditTransactionState> meshEditTransaction_;
    Workspace workspace_{Workspace::Layout};
    bool recoveredDirty_{false};
    std::uint64_t sceneRevision_{0};
    std::uint64_t selectionRevision_{0};
    std::uint64_t documentRevision_{0};
    std::uint64_t uiRevision_{0};
};

} // namespace m3d
