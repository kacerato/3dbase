from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text()


def write(path: str, content: str) -> None:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content)


def replace_once(path: str, old: str, new: str) -> None:
    content = read(path)
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected 1 match, found {count}: {old[:120]!r}")
    write(path, content.replace(old, new, 1))


write('src/editor/include/mobile3d/editor/mesh_edit_snapshot.hpp', r'''#pragma once

#include "mobile3d/core/id.hpp"
#include "mobile3d/core/math.hpp"
#include "mobile3d/editor/mesh_selection.hpp"

#include <cstdint>
#include <vector>

namespace m3d {

struct MeshEditVertexSnapshot final {
    EditableVertexId id{};
    Vec3 position{};
    bool selected{false};
};

struct MeshEditEdgeSnapshot final {
    EditableEdgeId id{};
    EditableVertexId first{};
    EditableVertexId second{};
    bool selected{false};
};

struct MeshEditFaceSnapshot final {
    EditableFaceId id{};
    std::vector<EditableVertexId> vertices;
    bool selected{false};
};

struct MeshEditPresentationSnapshot final {
    ObjectId object{};
    ResourceId resource{};
    MeshSelectionMode mode{MeshSelectionMode::Vertex};
    std::uint64_t revision{0};
    std::vector<MeshEditVertexSnapshot> vertices;
    std::vector<MeshEditEdgeSnapshot> edges;
    std::vector<MeshEditFaceSnapshot> faces;

    [[nodiscard]] bool active() const noexcept { return !object.isNull() && !resource.isNull(); }
};

class MeshEditSnapshotBuilder final {
public:
    [[nodiscard]] static MeshEditPresentationSnapshot build(
        const EditableMesh& mesh,
        const MeshSelectionModel& selection,
        ObjectId object,
        ResourceId resource,
        std::uint64_t revision);
};

} // namespace m3d
''')

write('src/editor/src/mesh_edit_snapshot.cpp', r'''#include "mobile3d/editor/mesh_edit_snapshot.hpp"

namespace m3d {

MeshEditPresentationSnapshot MeshEditSnapshotBuilder::build(
    const EditableMesh& mesh,
    const MeshSelectionModel& selection,
    ObjectId object,
    ResourceId resource,
    std::uint64_t revision) {
    MeshEditPresentationSnapshot snapshot;
    snapshot.object = object;
    snapshot.resource = resource;
    snapshot.mode = selection.mode();
    snapshot.revision = revision;

    const auto vertices = mesh.vertices();
    snapshot.vertices.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        snapshot.vertices.push_back(MeshEditVertexSnapshot{
            .id = vertex.id,
            .position = vertex.position,
            .selected = selection.contains(vertex.id),
        });
    }

    const auto edges = mesh.edges();
    snapshot.edges.reserve(edges.size());
    for (const auto& edge : edges) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        const auto* next = halfEdge ? mesh.findHalfEdge(halfEdge->next) : nullptr;
        if (!halfEdge || !next) continue;
        snapshot.edges.push_back(MeshEditEdgeSnapshot{
            .id = edge.id,
            .first = halfEdge->origin,
            .second = next->origin,
            .selected = selection.contains(edge.id),
        });
    }

    const auto faces = mesh.faces();
    snapshot.faces.reserve(faces.size());
    for (const auto& face : faces) {
        snapshot.faces.push_back(MeshEditFaceSnapshot{
            .id = face.id,
            .vertices = mesh.faceVertices(face.id),
            .selected = selection.contains(face.id),
        });
    }
    return snapshot;
}

} // namespace m3d
''')

replace_once('src/editor/CMakeLists.txt',
'''    src/mesh_selection.cpp\n''',
'''    src/mesh_selection.cpp\n    src/mesh_edit_snapshot.cpp\n''')

replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''#include "mobile3d/editor/mesh_selection.hpp"\n''',
'''#include "mobile3d/editor/mesh_selection.hpp"\n#include "mobile3d/editor/mesh_edit_snapshot.hpp"\n''')

replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] const MeshSelectionModel* meshSelection() const noexcept;\n''',
'''    [[nodiscard]] const MeshSelectionModel* meshSelection() const noexcept;\n    [[nodiscard]] MeshEditPresentationSnapshot meshEditPresentationSnapshot() const;\n''')

replace_once('src/editor/src/editor_mesh_edit.cpp',
'''const MeshSelectionModel* EditorSession::meshSelection() const noexcept {\n    return meshEditTransaction_ ? &meshEditTransaction_->selection : nullptr;\n}\n''',
'''const MeshSelectionModel* EditorSession::meshSelection() const noexcept {\n    return meshEditTransaction_ ? &meshEditTransaction_->selection : nullptr;\n}\n\nMeshEditPresentationSnapshot EditorSession::meshEditPresentationSnapshot() const {\n    if (!meshEditTransaction_) return {};\n    return MeshEditSnapshotBuilder::build(meshEditTransaction_->working,\n                                          meshEditTransaction_->selection,\n                                          meshEditTransaction_->object,\n                                          meshEditTransaction_->resource,\n                                          selectionRevision_);\n}\n''')

# Qt controller state + commands.
path = 'src/app/qt/editor_controller.hpp'
replace_once(path,
'''    Q_PROPERTY(bool transformInProgress READ transformInProgress NOTIFY transformActivityChanged)\n''',
'''    Q_PROPERTY(bool transformInProgress READ transformInProgress NOTIFY transformActivityChanged)\n    Q_PROPERTY(bool editMode READ editMode NOTIFY editModeChanged)\n    Q_PROPERTY(QString meshSelectionMode READ meshSelectionMode NOTIFY editModeChanged)\n    Q_PROPERTY(QStringList meshSelectionModes READ meshSelectionModes CONSTANT)\n    Q_PROPERTY(int selectedMeshElementCount READ selectedMeshElementCount NOTIFY editModeChanged)\n''')
replace_once(path,
'''    [[nodiscard]] bool transformInProgress() const noexcept { return manipulator_.active(); }\n''',
'''    [[nodiscard]] bool transformInProgress() const noexcept { return manipulator_.active(); }\n    [[nodiscard]] bool editMode() const noexcept { return session_.hasMeshEditTransaction(); }\n    [[nodiscard]] QString meshSelectionMode() const;\n    [[nodiscard]] QStringList meshSelectionModes() const;\n    [[nodiscard]] int selectedMeshElementCount() const;\n    [[nodiscard]] m3d::MeshEditPresentationSnapshot meshEditSnapshot() const {\n        return session_.meshEditPresentationSnapshot();\n    }\n''')
replace_once(path,
'''    Q_INVOKABLE void setTransformSnapEnabled(bool enabled);\n''',
'''    Q_INVOKABLE void setTransformSnapEnabled(bool enabled);\n    Q_INVOKABLE bool toggleEditMode();\n    Q_INVOKABLE bool commitEditMode();\n    Q_INVOKABLE bool cancelEditMode();\n    Q_INVOKABLE bool setMeshSelectionMode(const QString& name);\n    Q_INVOKABLE bool selectMeshElement(const QString& type, int id, bool toggle = false);\n    Q_INVOKABLE bool extrudeSelectedFace(double distance = 0.25);\n    Q_INVOKABLE bool insetSelectedFace(double ratio = 0.25);\n    Q_INVOKABLE bool subdivideSelectedFace();\n''')
replace_once(path,
'''    void transformActivityChanged();\n''',
'''    void transformActivityChanged();\n    void editModeChanged();\n''')

path = 'src/app/qt/editor_controller.cpp'
replace_once(path,
'''m3d::TransformSnapSettings EditorController::transformSnapSettings() const noexcept {\n''',
'''QString EditorController::meshSelectionMode() const {\n    const auto* selection = session_.meshSelection();\n    if (!selection) return QStringLiteral("Object");\n    switch (selection->mode()) {\n    case m3d::MeshSelectionMode::Vertex: return QStringLiteral("Vertex");\n    case m3d::MeshSelectionMode::Edge: return QStringLiteral("Edge");\n    case m3d::MeshSelectionMode::Face: return QStringLiteral("Face");\n    }\n    return QStringLiteral("Vertex");\n}\n\nQStringList EditorController::meshSelectionModes() const {\n    return {QStringLiteral("Vertex"), QStringLiteral("Edge"), QStringLiteral("Face")};\n}\n\nint EditorController::selectedMeshElementCount() const {\n    const auto* selection = session_.meshSelection();\n    if (!selection) return 0;\n    switch (selection->mode()) {\n    case m3d::MeshSelectionMode::Vertex:\n        return static_cast<int>(selection->selectedVertices().size());\n    case m3d::MeshSelectionMode::Edge:\n        return static_cast<int>(selection->selectedEdges().size());\n    case m3d::MeshSelectionMode::Face:\n        return static_cast<int>(selection->selectedFaces().size());\n    }\n    return 0;\n}\n\nm3d::TransformSnapSettings EditorController::transformSnapSettings() const noexcept {\n''')

replace_once(path,
'''void EditorController::setTransformSnapEnabled(bool enabled) {\n    if (manipulator_.active()) (void)cancelViewportTransform();\n    if (transformSnapEnabled_ == enabled) return;\n    transformSnapEnabled_ = enabled;\n    emit transformSettingsChanged();\n}\n''',
'''void EditorController::setTransformSnapEnabled(bool enabled) {\n    if (manipulator_.active()) (void)cancelViewportTransform();\n    if (transformSnapEnabled_ == enabled) return;\n    transformSnapEnabled_ = enabled;\n    emit transformSettingsChanged();\n}\n\nbool EditorController::toggleEditMode() {\n    if (manipulator_.active()) (void)cancelViewportTransform();\n    if (session_.hasMeshEditTransaction()) return commitEditMode();\n    const auto active = session_.selection().active();\n    if (!active) {\n        setStatus(QStringLiteral("Select a mesh object before entering Edit Mode."));\n        return false;\n    }\n    std::string error;\n    if (!session_.beginMeshEdit(*active, &error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    session_.setWorkspace(m3d::Workspace::Modeling);\n    setStatus(QStringLiteral("Edit Mode • Vertex selection"));\n    refreshUi();\n    emit workspaceChanged();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::commitEditMode() {\n    std::string error;\n    if (!session_.commitMeshEdit("Edit Mesh", &error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Mesh edit committed."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::cancelEditMode() {\n    if (!session_.cancelMeshEdit()) return false;\n    setStatus(QStringLiteral("Mesh edit cancelled."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::setMeshSelectionMode(const QString& name) {\n    if (!session_.hasMeshEditTransaction()) return false;\n    const QString value = name.trimmed().toLower();\n    std::optional<m3d::MeshSelectionMode> mode;\n    if (value == QStringLiteral("vertex")) mode = m3d::MeshSelectionMode::Vertex;\n    else if (value == QStringLiteral("edge")) mode = m3d::MeshSelectionMode::Edge;\n    else if (value == QStringLiteral("face")) mode = m3d::MeshSelectionMode::Face;\n    if (!mode || !session_.setMeshSelectionMode(*mode)) return false;\n    setStatus(QStringLiteral("Edit Mode • %1 selection").arg(name));\n    emit editModeChanged();\n    emit selectionChanged();\n    return true;\n}\n\nbool EditorController::selectMeshElement(const QString& type, int id, bool toggle) {\n    if (!session_.hasMeshEditTransaction() || id <= 0) return false;\n    const auto action = toggle ? m3d::MeshSelectionAction::Toggle : m3d::MeshSelectionAction::Replace;\n    const QString value = type.trimmed().toLower();\n    bool selected = false;\n    if (value == QStringLiteral("vertex")) {\n        selected = session_.selectMeshVertex(m3d::EditableVertexId{static_cast<std::uint32_t>(id)}, action);\n    } else if (value == QStringLiteral("edge")) {\n        selected = session_.selectMeshEdge(m3d::EditableEdgeId{static_cast<std::uint32_t>(id)}, action);\n    } else if (value == QStringLiteral("face")) {\n        selected = session_.selectMeshFace(m3d::EditableFaceId{static_cast<std::uint32_t>(id)}, action);\n    }\n    if (!selected) return false;\n    emit editModeChanged();\n    emit selectionChanged();\n    return true;\n}\n\nbool EditorController::extrudeSelectedFace(double distance) {\n    std::string error;\n    if (!session_.extrudeSelectedMeshFace(static_cast<float>(distance), &error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Face extruded."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::insetSelectedFace(double ratio) {\n    std::string error;\n    if (!session_.insetSelectedMeshFace(static_cast<float>(ratio), &error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Face inset."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::subdivideSelectedFace() {\n    std::string error;\n    if (!session_.subdivideSelectedMeshFace(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Face subdivided."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''')

replace_once(path,
'''void EditorController::refreshUi() {\n    outliner_->refresh();\n    emit projectStateChanged();\n    emit historyChanged();\n    emit selectionChanged();\n}\n''',
'''void EditorController::refreshUi() {\n    outliner_->refresh();\n    emit projectStateChanged();\n    emit historyChanged();\n    emit selectionChanged();\n    emit editModeChanged();\n}\n''')

# Touch-first modeling toolbar. Existing transform controls disappear while editing.
path = 'src/app/qml/ViewportPlaceholder.qml'
replace_once(path,
'''    Flow {\n        id: transformToolbar\n''',
'''    Flow {\n        id: transformToolbar\n        visible: !root.controller.editMode\n''')
replace_once(path,
'''    Row {\n        anchors.right: parent.right\n        anchors.bottom: parent.bottom\n''',
'''    Flow {\n        id: modelingToolbar\n        anchors.left: parent.left\n        anchors.top: parent.top\n        anchors.margins: 10\n        width: Math.min(760, parent.width - 20)\n        spacing: 5\n        visible: root.controller.editMode || root.controller.workspace === "Modeling"\n\n        Button {\n            height: 38\n            text: root.controller.editMode ? "Object Mode" : "Edit Mode"\n            highlighted: root.controller.editMode\n            enabled: !root.controller.transformInProgress && root.controller.hasActiveObject\n            onClicked: root.controller.toggleEditMode()\n        }\n        Repeater {\n            model: root.controller.meshSelectionModes\n            delegate: Button {\n                required property string modelData\n                height: 38\n                visible: root.controller.editMode\n                text: modelData\n                highlighted: root.controller.meshSelectionMode === modelData\n                onClicked: root.controller.setMeshSelectionMode(modelData)\n            }\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Extrude"\n            enabled: root.controller.meshSelectionMode === "Face"\n                     && root.controller.selectedMeshElementCount === 1\n            onClicked: root.controller.extrudeSelectedFace(0.25)\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Inset"\n            enabled: root.controller.meshSelectionMode === "Face"\n                     && root.controller.selectedMeshElementCount === 1\n            onClicked: root.controller.insetSelectedFace(0.25)\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Subdivide"\n            enabled: root.controller.meshSelectionMode === "Face"\n                     && root.controller.selectedMeshElementCount === 1\n            onClicked: root.controller.subdivideSelectedFace()\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Cancel"\n            onClicked: root.controller.cancelEditMode()\n        }\n    }\n\n    Row {\n        anchors.right: parent.right\n        anchors.bottom: parent.bottom\n''')
replace_once(path,
'''                  + (root.controller.transformInProgress ? " • transforming" : "")\n''',
'''                  + (root.controller.transformInProgress ? " • transforming" : "")\n                  + (root.controller.editMode ? " • Edit " + root.controller.meshSelectionMode : "")\n''')

# Dedicated snapshot tests.
write('tests/test_mesh_edit_snapshot.cpp', r'''#include "test_harness.hpp"

#include "mobile3d/editor/editor_session.hpp"

#include <filesystem>
#include <string>

namespace {
std::filesystem::path snapshotProjectPath() {
    return std::filesystem::temp_directory_path() /
           ("mobile3d-edit-snapshot-" + m3d::ObjectId::generate().toString());
}
struct Cleanup final {
    explicit Cleanup(std::filesystem::path value) : path(std::move(value)) {}
    ~Cleanup() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};
} // namespace

TEST_CASE("mesh edit presentation snapshot is immutable editor render data") {
    const auto path = snapshotProjectPath();
    Cleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Edit Snapshot", &error));
    const auto object = session.createObject(m3d::ObjectType::Mesh, "Cube");
    REQUIRE(object.has_value());
    REQUIRE(session.beginMeshEdit(*object, &error));

    const auto initial = session.meshEditPresentationSnapshot();
    REQUIRE(initial.active());
    REQUIRE(initial.object == *object);
    REQUIRE(initial.vertices.size() == 8U);
    REQUIRE(initial.edges.size() == 12U);
    REQUIRE(initial.faces.size() == 6U);
    REQUIRE(initial.mode == m3d::MeshSelectionMode::Vertex);

    const auto face = initial.faces.front().id;
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Face));
    REQUIRE(session.selectMeshFace(face));
    const auto selected = session.meshEditPresentationSnapshot();
    REQUIRE(selected.mode == m3d::MeshSelectionMode::Face);
    REQUIRE(selected.revision > initial.revision);
    bool foundSelected = false;
    for (const auto& item : selected.faces) {
        if (item.id == face) foundSelected = item.selected;
    }
    REQUIRE(foundSelected);

    REQUIRE(session.extrudeSelectedMeshFace(0.25F, &error));
    const auto extruded = session.meshEditPresentationSnapshot();
    REQUIRE(extruded.vertices.size() > initial.vertices.size());
    REQUIRE(extruded.faces.size() > initial.faces.size());
    REQUIRE(extruded.revision > selected.revision);
    REQUIRE(session.cancelMeshEdit());
    REQUIRE(!session.meshEditPresentationSnapshot().active());
}
''')

replace_once('tests/CMakeLists.txt',
'''    test_mesh_edit_transaction.cpp\n''',
'''    test_mesh_edit_transaction.cpp\n    test_mesh_edit_snapshot.cpp\n''')

print('Stage 5 Edit Mode UI/snapshot patch prepared')
