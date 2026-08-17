from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text()


def write(path: str, content: str) -> None:
    Path(path).write_text(content)


def replace_once(path: str, old: str, new: str) -> None:
    content = read(path)
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected 1 match, found {count}: {old[:120]!r}")
    write(path, content.replace(old, new, 1))

# Test includes required by merge/weld core coverage.
replace_once('tests/test_mesh_operations.cpp',
'''#include <cmath>\n#include <string>\n''',
'''#include <algorithm>\n#include <array>\n#include <cmath>\n#include <string>\n#include <vector>\n''')

# EditorSession operator API.
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool subdivideSelectedMeshFace(std::string* error = nullptr);\n''',
'''    [[nodiscard]] bool subdivideSelectedMeshFace(std::string* error = nullptr);\n    [[nodiscard]] bool mergeSelectedMeshVertices(std::string* error = nullptr);\n    [[nodiscard]] bool weldSelectedMeshVertices(float distance, std::string* error = nullptr);\n''')

path = 'src/editor/src/editor_mesh_operators.cpp'
content = read(path)
closing = '\n} // namespace m3d\n'
append = r'''

bool EditorSession::mergeSelectedMeshVertices(std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Vertex) {
        if (error) *error = "Merge requires Vertex selection mode";
        return false;
    }
    const auto selected = selection.selectedVertices();
    const auto active = selection.activeVertex();
    if (selected.size() < 2U || !active) {
        if (error) *error = "Merge to Active requires at least two selected vertices and an active vertex";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto merged = candidate.mergeVertices(selected, *active, error);
    if (!merged || !applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    selection.setMode(MeshSelectionMode::Vertex);
    (void)selection.select(meshEditTransaction_->working, *merged, MeshSelectionAction::Replace);
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}

bool EditorSession::weldSelectedMeshVertices(float distance, std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Vertex) {
        if (error) *error = "Weld requires Vertex selection mode";
        return false;
    }
    const auto selected = selection.selectedVertices();
    if (selected.size() < 2U) {
        if (error) *error = "Weld requires at least two selected vertices";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto result = candidate.weldVertices(selected, distance, selection.activeVertex(), error);
    if (!result) return false;
    if (result->mergedCount == 0U) {
        if (error) *error = "No selected vertices are within the weld distance";
        return false;
    }
    if (!applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    selection.setMode(MeshSelectionMode::Vertex);
    bool first = true;
    for (const auto survivor : result->survivors) {
        (void)selection.select(meshEditTransaction_->working, survivor,
                               first ? MeshSelectionAction::Replace : MeshSelectionAction::Add);
        first = false;
    }
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}
'''
if 'EditorSession::mergeSelectedMeshVertices' not in content:
    if not content.endswith(closing): raise SystemExit('editor_mesh_operators namespace closing not found')
    write(path, content[:-len(closing)] + append + closing)

# Qt controller API and actions.
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_INVOKABLE bool subdivideSelectedFace();\n''',
'''    Q_INVOKABLE bool subdivideSelectedFace();\n    Q_INVOKABLE bool mergeSelectedVertices();\n    Q_INVOKABLE bool weldSelectedVertices(double distance = 0.01);\n''')

path = 'src/app/qt/editor_controller.cpp'
replace_once(path,
'''bool EditorController::subdivideSelectedFace() {\n    std::string error;\n    if (!session_.subdivideSelectedMeshFace(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Face subdivided."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''',
'''bool EditorController::subdivideSelectedFace() {\n    std::string error;\n    if (!session_.subdivideSelectedMeshFace(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Face subdivided."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::mergeSelectedVertices() {\n    std::string error;\n    if (!session_.mergeSelectedMeshVertices(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Vertices merged to active."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::weldSelectedVertices(double distance) {\n    std::string error;\n    if (!session_.weldSelectedMeshVertices(static_cast<float>(distance), &error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Selected vertices welded."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''')

# Touch controls for vertex modeling.
path = 'src/app/qml/ViewportPlaceholder.qml'
replace_once(path,
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Subdivide"\n            enabled: root.controller.meshSelectionMode === "Face"\n                     && root.controller.selectedMeshElementCount === 1\n            onClicked: root.controller.subdivideSelectedFace()\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Cancel"\n''',
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Subdivide"\n            enabled: root.controller.meshSelectionMode === "Face"\n                     && root.controller.selectedMeshElementCount === 1\n            onClicked: root.controller.subdivideSelectedFace()\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Merge Active"\n            enabled: root.controller.meshSelectionMode === "Vertex"\n                     && root.controller.selectedMeshElementCount >= 2\n            onClicked: root.controller.mergeSelectedVertices()\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Weld"\n            enabled: root.controller.meshSelectionMode === "Vertex"\n                     && root.controller.selectedMeshElementCount >= 2\n            onClicked: root.controller.weldSelectedVertices(0.01)\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Cancel"\n''')

# Editor transaction coverage.
path = 'tests/test_mesh_edit_operators.cpp'
content = read(path)
if 'edit mode merge to active previews and commits as one mesh undo' not in content:
    content = content.rstrip() + r'''

TEST_CASE("edit mode merge to active previews and commits as one mesh undo") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());

    const auto edge = session.editableMesh()->edges().front();
    const auto* halfEdge = session.editableMesh()->findHalfEdge(edge.halfEdge);
    const auto* next = halfEdge ? session.editableMesh()->findHalfEdge(halfEdge->next) : nullptr;
    REQUIRE(halfEdge != nullptr);
    REQUIRE(next != nullptr);
    const auto first = halfEdge->origin;
    const auto active = next->origin;
    REQUIRE(session.selectMeshVertex(first));
    REQUIRE(session.selectMeshVertex(active, m3d::MeshSelectionAction::Add));
    REQUIRE(session.meshSelection()->activeVertex() == active);
    REQUIRE(session.mergeSelectedMeshVertices(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->vertexCount() == 7U);
    REQUIRE(session.editableMesh()->findVertex(first) == nullptr);
    REQUIRE(session.editableMesh()->findVertex(active) != nullptr);
    REQUIRE(session.meshSelection()->selectedVertices().size() == 1U);
    REQUIRE(session.meshSelection()->activeVertex() == active);

    REQUIRE(session.commitMeshEdit("Merge Vertices", &error));
    REQUIRE(session.nextUndoName() == "Merge Vertices");
    REQUIRE(session.undo());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 8U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 7U);
}

TEST_CASE("edit mode weld by distance keeps active representative and can cancel") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());

    const auto edge = session.editableMesh()->edges().front();
    const auto* halfEdge = session.editableMesh()->findHalfEdge(edge.halfEdge);
    const auto* next = halfEdge ? session.editableMesh()->findHalfEdge(halfEdge->next) : nullptr;
    REQUIRE(halfEdge != nullptr);
    REQUIRE(next != nullptr);
    REQUIRE(session.selectMeshVertex(halfEdge->origin));
    REQUIRE(session.selectMeshVertex(next->origin, m3d::MeshSelectionAction::Add));
    const auto active = session.meshSelection()->activeVertex();
    REQUIRE(active.has_value());
    REQUIRE(session.weldSelectedMeshVertices(1.01F, &error));
    REQUIRE(session.editableMesh()->vertexCount() == 7U);
    REQUIRE(session.meshSelection()->selectedVertices().size() == 1U);
    REQUIRE(session.meshSelection()->activeVertex() == active);
    REQUIRE(session.cancelMeshEdit());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 8U);
}

TEST_CASE("edit mode weld with no matching distance is a clean no-op") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    const auto vertices = session.editableMesh()->vertices();
    REQUIRE(vertices.size() >= 7U);
    REQUIRE(session.selectMeshVertex(vertices[0].id));
    REQUIRE(session.selectMeshVertex(vertices[6].id, m3d::MeshSelectionAction::Add));
    REQUIRE(!session.weldSelectedMeshVertices(0.01F, &error));
    REQUIRE(!error.empty());
    REQUIRE(session.editableMesh()->vertexCount() == 8U);
    REQUIRE(!session.isDirty());
    REQUIRE(session.cancelMeshEdit());
}
''' + '\n'
    write(path, content)

# Update only claims already backed by implementation and tests.
path = 'docs/ROADMAP.md'
content = read(path)
replacements = {
    '- [ ] Vertex/half-edge(or equivalent topology) representation.': '- [x] Vertex/half-edge(or equivalent topology) representation.',
    '- [ ] Vertex/Edge/Face selection.': '- [x] Vertex/Edge/Face selection.',
    '- [ ] Mesh edit transaction system.': '- [x] Mesh edit transaction system.',
    '- [ ] Extrude.': '- [x] Extrude.',
    '- [ ] Inset.': '- [x] Inset.',
    '- [ ] Merge/Weld.': '- [x] Merge/Weld.',
    '- [ ] Subdivide.': '- [x] Subdivide.',
}
for old, new in replacements.items():
    if content.count(old) != 1:
        raise SystemExit(f'roadmap expected one item: {old}')
    content = content.replace(old, new, 1)
write(path, content)

# Normalize touched generated/appended test files for git diff --check.
for path in [Path('tests/test_mesh_operations.cpp'), Path('tests/test_mesh_edit_operators.cpp')]:
    path.write_text(path.read_text().rstrip() + '\n')

print('merge/weld editor UI roadmap patch prepared')
