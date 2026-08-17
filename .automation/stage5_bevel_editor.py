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

# EditorSession one-edge hard-surface bevel baseline.
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool deleteSelectedMeshElements(std::string* error = nullptr);\n''',
'''    [[nodiscard]] bool deleteSelectedMeshElements(std::string* error = nullptr);\n    [[nodiscard]] bool bevelSelectedMeshEdge(float width, std::string* error = nullptr);\n''')

path = 'src/editor/src/editor_mesh_operators.cpp'
content = read(path)
closing = '\n} // namespace m3d\n'
append = r'''

bool EditorSession::bevelSelectedMeshEdge(float width, std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Edge) {
        if (error) *error = "Bevel requires Edge selection mode";
        return false;
    }
    const auto selected = selection.selectedEdges();
    if (selected.size() != 1U) {
        if (error) *error = "Current Bevel baseline requires exactly one selected edge";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto result = candidate.bevelEdge(selected.front(), width, error);
    if (!result || !applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    selection.setMode(MeshSelectionMode::Face);
    (void)selection.select(meshEditTransaction_->working, result->bevelFace,
                           MeshSelectionAction::Replace);
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}
'''
if 'EditorSession::bevelSelectedMeshEdge' not in content:
    if not content.endswith(closing): raise SystemExit('editor mesh operators namespace closing not found')
    write(path, content[:-len(closing)] + append + closing)

# Controller action.
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_INVOKABLE bool deleteSelectedMeshElements();\n''',
'''    Q_INVOKABLE bool deleteSelectedMeshElements();\n    Q_INVOKABLE bool bevelSelectedEdge(double width);\n''')

path = 'src/app/qt/editor_controller.cpp'
replace_once(path,
'''bool EditorController::deleteSelectedMeshElements() {\n    std::string error;\n    if (!session_.deleteSelectedMeshElements(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Selected mesh elements deleted."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''',
'''bool EditorController::deleteSelectedMeshElements() {\n    std::string error;\n    if (!session_.deleteSelectedMeshElements(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Selected mesh elements deleted."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::bevelSelectedEdge(double width) {\n    std::string error;\n    if (!session_.bevelSelectedMeshEdge(static_cast<float>(width), &error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Single manifold edge beveled. Chains and segments remain pending."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''')

# Mobile controls expose width explicitly.
path = 'src/app/qml/ViewportPlaceholder.qml'
replace_once(path,
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Loop Cut ×" + loopCutCount.value\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount === 1\n            onClicked: root.controller.loopCutSelectedEdge(loopCutCount.value)\n        }\n''',
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Loop Cut ×" + loopCutCount.value\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount === 1\n            onClicked: root.controller.loopCutSelectedEdge(loopCutCount.value)\n        }\n        SpinBox {\n            id: bevelWidth\n            height: 38\n            width: 86\n            visible: root.controller.editMode && root.controller.meshSelectionMode === "Edge"\n            from: 1\n            to: 100\n            value: 10\n            editable: false\n            textFromValue: function(value, locale) {\n                return (value / 100.0).toFixed(2)\n            }\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Bevel " + (bevelWidth.value / 100.0).toFixed(2)\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount === 1\n            onClicked: root.controller.bevelSelectedEdge(bevelWidth.value / 100.0)\n        }\n''')

# Editor transaction tests.
path = 'tests/test_mesh_edit_operators.cpp'
content = read(path)
if 'edit mode single edge bevel previews and commits through one undo' not in content:
    content = content.rstrip() + r'''

TEST_CASE("edit mode single edge bevel previews and commits through one undo") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));
    const auto edge = session.editableMesh()->edges().front().id;
    REQUIRE(session.selectMeshEdge(edge));
    REQUIRE(session.bevelSelectedMeshEdge(0.1F, &error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->vertexCount() == 10U);
    REQUIRE(session.editableMesh()->edgeCount() == 15U);
    REQUIRE(session.editableMesh()->faceCount() == 7U);
    REQUIRE(session.meshSelection()->mode() == m3d::MeshSelectionMode::Face);
    REQUIRE(session.meshSelection()->selectedFaces().size() == 1U);

    REQUIRE(session.commitMeshEdit("Bevel Edge", &error));
    REQUIRE(session.nextUndoName() == "Bevel Edge");
    REQUIRE(session.undo());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 8U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 6U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 10U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 7U);
}

TEST_CASE("edit mode bevel failure leaves working mesh and selection unchanged") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));
    const auto edge = session.editableMesh()->edges().front().id;
    REQUIRE(session.selectMeshEdge(edge));
    const auto before = session.editableMesh()->snapshot();
    REQUIRE(!session.bevelSelectedMeshEdge(1.0F, &error));
    REQUIRE(!error.empty());
    REQUIRE(session.editableMesh()->snapshot().vertices == before.vertices);
    REQUIRE(session.editableMesh()->snapshot().halfEdges == before.halfEdges);
    REQUIRE(session.editableMesh()->snapshot().edges == before.edges);
    REQUIRE(session.editableMesh()->snapshot().faces == before.faces);
    REQUIRE(session.meshSelection()->selectedEdges().size() == 1U);
    REQUIRE(!session.isDirty());
    REQUIRE(session.cancelMeshEdit());
}
''' + '\n'
    write(path, content)

# Keep Bevel unchecked; document exactly what is implemented and what is pending.
path = 'docs/ROADMAP.md'
content = read(path)
marker = '''Current modeling baselines: Loop Cut propagates across complete quad rings with 1–32 evenly spaced cuts; Edge Slide remains a separate follow-up operator.'''
replacement = '''Current modeling baselines: Loop Cut propagates across complete quad rings with 1–32 evenly spaced cuts; Edge Slide remains a separate follow-up operator. Bevel now has a strong-copy single-edge hard-surface baseline for closed manifold edges with valence-3 endpoints and width validation; chains/loops, endpoint policies and multi-segment profiles remain pending before the Bevel checklist item is complete.'''
if 'Bevel now has a strong-copy single-edge hard-surface baseline' not in content:
    if content.count(marker) != 1: raise SystemExit('roadmap modeling baseline marker missing for bevel note')
    content = content.replace(marker, replacement, 1)
    write(path, content)

Path('tests/test_mesh_edit_operators.cpp').write_text(Path('tests/test_mesh_edit_operators.cpp').read_text().rstrip() + '\n')
print('single edge hard surface bevel editor UI roadmap patch prepared')
