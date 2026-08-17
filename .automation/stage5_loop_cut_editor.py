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

# Editor operator.
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool bridgeSelectedMeshBoundaries(std::string* error = nullptr);\n''',
'''    [[nodiscard]] bool bridgeSelectedMeshBoundaries(std::string* error = nullptr);\n    [[nodiscard]] bool loopCutSelectedMeshEdge(std::string* error = nullptr);\n''')

path = 'src/editor/src/editor_mesh_operators.cpp'
content = read(path)
closing = '\n} // namespace m3d\n'
append = r'''

bool EditorSession::loopCutSelectedMeshEdge(std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Edge) {
        if (error) *error = "Loop Cut requires Edge selection mode";
        return false;
    }
    const auto selected = selection.selectedEdges();
    if (selected.size() != 1U) {
        if (error) *error = "Loop Cut requires exactly one selected start edge";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto result = candidate.loopCut(selected.front(), error);
    if (!result || result->edges.empty() || !applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    selection.setMode(MeshSelectionMode::Edge);
    bool first = true;
    for (const auto edge : result->edges) {
        (void)selection.select(meshEditTransaction_->working, edge,
                               first ? MeshSelectionAction::Replace : MeshSelectionAction::Add);
        first = false;
    }
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}
'''
if 'EditorSession::loopCutSelectedMeshEdge' not in content:
    if not content.endswith(closing): raise SystemExit('editor mesh operators namespace closing not found')
    write(path, content[:-len(closing)] + append + closing)

# Controller API and status.
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_INVOKABLE bool bridgeSelectedBoundaries();\n''',
'''    Q_INVOKABLE bool bridgeSelectedBoundaries();\n    Q_INVOKABLE bool loopCutSelectedEdge();\n''')

path = 'src/app/qt/editor_controller.cpp'
replace_once(path,
'''bool EditorController::bridgeSelectedBoundaries() {\n    std::string error;\n    if (!session_.bridgeSelectedMeshBoundaries(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Boundary loops bridged."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''',
'''bool EditorController::bridgeSelectedBoundaries() {\n    std::string error;\n    if (!session_.bridgeSelectedMeshBoundaries(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Boundary loops bridged."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::loopCutSelectedEdge() {\n    std::string error;\n    if (!session_.loopCutSelectedMeshEdge(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Centered quad-ring Loop Cut created."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''')

# Touch button in Edge mode.
path = 'src/app/qml/ViewportPlaceholder.qml'
replace_once(path,
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Fill"\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount >= 3\n            onClicked: root.controller.fillSelectedBoundary()\n        }\n''',
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Loop Cut"\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount === 1\n            onClicked: root.controller.loopCutSelectedEdge()\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Fill"\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount >= 3\n            onClicked: root.controller.fillSelectedBoundary()\n        }\n''')

# Editor transaction test.
path = 'tests/test_mesh_edit_operators.cpp'
content = read(path)
if 'edit mode loop cut selects the generated quad ring and commits through undo' not in content:
    content = content.rstrip() + r'''

TEST_CASE("edit mode loop cut selects the generated quad ring and commits through undo") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));
    const auto startEdge = session.editableMesh()->edges().front().id;
    REQUIRE(session.selectMeshEdge(startEdge));
    REQUIRE(session.loopCutSelectedMeshEdge(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->vertexCount() == 12U);
    REQUIRE(session.editableMesh()->edgeCount() == 20U);
    REQUIRE(session.editableMesh()->faceCount() == 10U);
    REQUIRE(session.meshSelection()->mode() == m3d::MeshSelectionMode::Edge);
    REQUIRE(session.meshSelection()->selectedEdges().size() == 4U);

    REQUIRE(session.commitMeshEdit("Loop Cut", &error));
    REQUIRE(session.nextUndoName() == "Loop Cut");
    REQUIRE(session.undo());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 8U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 6U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 12U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 10U);
}
''' + '\n'
    write(path, content)

# Roadmap remains unchecked until slide/multi-cut are implemented; document baseline precisely.
path = 'docs/ROADMAP.md'
content = read(path)
old = '''Current boundary-tool baseline: single closed-loop Fill is implemented; Bridge supports two disjoint closed loops with equal vertex counts and automatic minimum-distance alignment. Grid Fill and unequal-count bridge policies remain pending before those checklist items are considered complete.\n'''
new = '''Current modeling baselines not yet considered checklist-complete: centered Loop Cut propagates across complete quad rings, but edge slide and multiple cuts remain pending. Single closed-loop Fill is implemented; Bridge supports two disjoint closed loops with equal vertex counts and automatic minimum-distance alignment. Grid Fill and unequal-count bridge policies remain pending.\n'''
if 'centered Loop Cut propagates' not in content:
    if content.count(old) != 1: raise SystemExit('roadmap baseline note marker missing')
    content = content.replace(old, new, 1)
    write(path, content)

Path('tests/test_mesh_edit_operators.cpp').write_text(Path('tests/test_mesh_edit_operators.cpp').read_text().rstrip() + '\n')
print('quad ring loop cut editor UI roadmap patch prepared')
