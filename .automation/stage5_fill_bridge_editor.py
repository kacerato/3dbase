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

# Editor session operator API.
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool weldSelectedMeshVertices(float distance, std::string* error = nullptr);\n''',
'''    [[nodiscard]] bool weldSelectedMeshVertices(float distance, std::string* error = nullptr);\n    [[nodiscard]] bool fillSelectedMeshBoundary(std::string* error = nullptr);\n    [[nodiscard]] bool bridgeSelectedMeshBoundaries(std::string* error = nullptr);\n''')

path = 'src/editor/src/editor_mesh_operators.cpp'
content = read(path)
closing = '\n} // namespace m3d\n'
append = r'''

bool EditorSession::fillSelectedMeshBoundary(std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Edge) {
        if (error) *error = "Fill requires Edge selection mode";
        return false;
    }
    const auto selected = selection.selectedEdges();
    if (selected.size() < 3U) {
        if (error) *error = "Fill requires a closed boundary loop with at least three selected edges";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto face = candidate.fillBoundaryLoop(selected, error);
    if (!face || !applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    selection.setMode(MeshSelectionMode::Face);
    (void)selection.select(meshEditTransaction_->working, *face, MeshSelectionAction::Replace);
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}

bool EditorSession::bridgeSelectedMeshBoundaries(std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Edge) {
        if (error) *error = "Bridge requires Edge selection mode";
        return false;
    }
    const auto selected = selection.selectedEdges();
    if (selected.size() < 6U) {
        if (error) *error = "Bridge requires two closed boundary loops";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto faces = candidate.bridgeBoundaryLoops(selected, error);
    if (!faces || faces->empty() || !applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    selection.setMode(MeshSelectionMode::Face);
    bool first = true;
    for (const auto face : *faces) {
        (void)selection.select(meshEditTransaction_->working, face,
                               first ? MeshSelectionAction::Replace : MeshSelectionAction::Add);
        first = false;
    }
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}
'''
if 'EditorSession::fillSelectedMeshBoundary' not in content:
    if not content.endswith(closing): raise SystemExit('editor mesh operators namespace closing not found')
    write(path, content[:-len(closing)] + append + closing)

# Controller actions.
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_INVOKABLE bool weldSelectedVertices(double distance = 0.01);\n''',
'''    Q_INVOKABLE bool weldSelectedVertices(double distance = 0.01);\n    Q_INVOKABLE bool fillSelectedBoundary();\n    Q_INVOKABLE bool bridgeSelectedBoundaries();\n''')

path = 'src/app/qt/editor_controller.cpp'
replace_once(path,
'''bool EditorController::weldSelectedVertices(double distance) {\n    std::string error;\n    if (!session_.weldSelectedMeshVertices(static_cast<float>(distance), &error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Selected vertices welded."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''',
'''bool EditorController::weldSelectedVertices(double distance) {\n    std::string error;\n    if (!session_.weldSelectedMeshVertices(static_cast<float>(distance), &error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Selected vertices welded."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::fillSelectedBoundary() {\n    std::string error;\n    if (!session_.fillSelectedMeshBoundary(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Boundary loop filled."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::bridgeSelectedBoundaries() {\n    std::string error;\n    if (!session_.bridgeSelectedMeshBoundaries(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Boundary loops bridged."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''')

# Touch toolbar.
path = 'src/app/qml/ViewportPlaceholder.qml'
replace_once(path,
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Weld"\n            enabled: root.controller.meshSelectionMode === "Vertex"\n                     && root.controller.selectedMeshElementCount >= 2\n            onClicked: root.controller.weldSelectedVertices(0.01)\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Cancel"\n''',
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Weld"\n            enabled: root.controller.meshSelectionMode === "Vertex"\n                     && root.controller.selectedMeshElementCount >= 2\n            onClicked: root.controller.weldSelectedVertices(0.01)\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Fill"\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount >= 3\n            onClicked: root.controller.fillSelectedBoundary()\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Bridge"\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount >= 6\n            onClicked: root.controller.bridgeSelectedBoundaries()\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Cancel"\n''')

# Editor tests build open authoring resources, so Fill/Bridge are verified through preview/Undo lifecycle.
path = 'tests/test_mesh_edit_operators.cpp'
content = read(path)
if 'edit mode fill closes a selected boundary loop and commits through undo' not in content:
    content = content.rstrip() + r'''

TEST_CASE("edit mode fill closes a selected boundary loop and commits through undo") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Fill Operator", &error));

    m3d::MeshResource resource = m3d::MeshResource::makeCube("Open Cube", 1.0F);
    REQUIRE(resource.authoring.has_value());
    REQUIRE(resource.authoring->removeFace(resource.authoring->faces().front().id, &error));
    REQUIRE(resource.rebuildFromAuthoring(&error));
    const auto object = session.createMeshObject(std::move(resource), "Open Cube");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.beginMeshEdit(*object, &error));
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));

    bool first = true;
    std::size_t boundaryCount = 0U;
    for (const auto& edge : session.editableMesh()->edges()) {
        const auto* halfEdge = session.editableMesh()->findHalfEdge(edge.halfEdge);
        if (!halfEdge || !halfEdge->twin.isNull()) continue;
        REQUIRE(session.selectMeshEdge(edge.id, first ? m3d::MeshSelectionAction::Replace
                                                      : m3d::MeshSelectionAction::Add));
        first = false;
        ++boundaryCount;
    }
    REQUIRE(boundaryCount == 4U);
    REQUIRE(session.fillSelectedMeshBoundary(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->faceCount() == 6U);
    REQUIRE(session.meshSelection()->mode() == m3d::MeshSelectionMode::Face);
    REQUIRE(session.meshSelection()->selectedFaces().size() == 1U);

    REQUIRE(session.commitMeshEdit("Fill Boundary", &error));
    REQUIRE(session.nextUndoName() == "Fill Boundary");
    REQUIRE(session.undo());
    const auto resourceId = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 5U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 6U);
}

TEST_CASE("edit mode bridge connects two equal boundary loops and cancel restores input") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Bridge Operator", &error));

    m3d::EditableMesh authored;
    const std::array<m3d::EditableVertexId, 4> bottom{
        authored.addVertex({-1.0F,-1.0F,0.0F}), authored.addVertex({1.0F,-1.0F,0.0F}),
        authored.addVertex({1.0F,1.0F,0.0F}), authored.addVertex({-1.0F,1.0F,0.0F})
    };
    const std::array<m3d::EditableVertexId, 4> top{
        authored.addVertex({-1.0F,-1.0F,2.0F}), authored.addVertex({1.0F,-1.0F,2.0F}),
        authored.addVertex({1.0F,1.0F,2.0F}), authored.addVertex({-1.0F,1.0F,2.0F})
    };
    const std::array<m3d::EditableVertexId, 4> bottomWinding{bottom[0],bottom[3],bottom[2],bottom[1]};
    const std::array<m3d::EditableVertexId, 4> topWinding{top[0],top[1],top[2],top[3]};
    REQUIRE(authored.addFace(bottomWinding, &error).has_value());
    REQUIRE(authored.addFace(topWinding, &error).has_value());

    m3d::MeshResource resource;
    resource.id = m3d::ResourceId::generate();
    resource.name = "Two Loops";
    resource.authoring = authored;
    REQUIRE(resource.rebuildFromAuthoring(&error));
    const auto object = session.createMeshObject(std::move(resource), "Two Loops");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.beginMeshEdit(*object, &error));
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));

    bool first = true;
    for (const auto& edge : session.editableMesh()->edges()) {
        REQUIRE(session.selectMeshEdge(edge.id, first ? m3d::MeshSelectionAction::Replace
                                                      : m3d::MeshSelectionAction::Add));
        first = false;
    }
    REQUIRE(session.meshSelection()->selectedEdges().size() == 8U);
    REQUIRE(session.bridgeSelectedMeshBoundaries(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->faceCount() == 6U);
    REQUIRE(session.editableMesh()->edgeCount() == 12U);
    REQUIRE(session.meshSelection()->selectedFaces().size() == 4U);
    REQUIRE(session.cancelMeshEdit());
    const auto resourceId = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 2U);
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->edgeCount() == 8U);
}
''' + '\n'
    write(path, content)

# Test needs array include.
replace_once('tests/test_mesh_edit_operators.cpp',
'''#include <filesystem>\n#include <string>\n''',
'''#include <array>\n#include <filesystem>\n#include <string>\n''')

# Roadmap note is intentionally not a completed checkbox until Grid Fill and unequal-loop bridge policies exist.
path = 'docs/ROADMAP.md'
content = read(path)
marker = '''- [ ] Normals tools.\n\nExit criterion: useful polygon modeling can be completed entirely on mobile.\n'''
note = '''- [ ] Normals tools.\n\nCurrent boundary-tool baseline: single closed-loop Fill is implemented; Bridge supports two disjoint closed loops with equal vertex counts and automatic minimum-distance alignment. Grid Fill and unequal-count bridge policies remain pending before those checklist items are considered complete.\n\nExit criterion: useful polygon modeling can be completed entirely on mobile.\n'''
if 'Current boundary-tool baseline:' not in content:
    if content.count(marker) != 1: raise SystemExit('roadmap Stage 5 note marker not found')
    content = content.replace(marker, note, 1)
    write(path, content)

Path('tests/test_mesh_edit_operators.cpp').write_text(Path('tests/test_mesh_edit_operators.cpp').read_text().rstrip() + '\n')
print('fill and bridge editor UI roadmap patch prepared')
