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

# EditorSession dispatches delete by current component mode.
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool loopCutSelectedMeshEdge(std::string* error = nullptr);\n''',
'''    [[nodiscard]] bool loopCutSelectedMeshEdge(std::string* error = nullptr);\n    [[nodiscard]] bool deleteSelectedMeshElements(std::string* error = nullptr);\n''')

path = 'src/editor/src/editor_mesh_operators.cpp'
content = read(path)
closing = '\n} // namespace m3d\n'
append = r'''

bool EditorSession::deleteSelectedMeshElements(std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.emptyCurrentMode()) {
        if (error) *error = "Delete requires selected mesh elements";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    bool deleted = false;
    switch (selection.mode()) {
    case MeshSelectionMode::Vertex:
        deleted = candidate.deleteVertices(selection.selectedVertices(), error);
        break;
    case MeshSelectionMode::Edge:
        deleted = candidate.deleteEdges(selection.selectedEdges(), error);
        break;
    case MeshSelectionMode::Face:
        deleted = candidate.deleteFaces(selection.selectedFaces(), error);
        break;
    }
    if (!deleted || !applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}
'''
if 'EditorSession::deleteSelectedMeshElements' not in content:
    if not content.endswith(closing): raise SystemExit('editor mesh operators namespace closing not found')
    write(path, content[:-len(closing)] + append + closing)

# Controller API.
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_INVOKABLE bool loopCutSelectedEdge();\n''',
'''    Q_INVOKABLE bool loopCutSelectedEdge();\n    Q_INVOKABLE bool deleteSelectedMeshElements();\n''')

path = 'src/app/qt/editor_controller.cpp'
replace_once(path,
'''bool EditorController::loopCutSelectedEdge() {\n    std::string error;\n    if (!session_.loopCutSelectedMeshEdge(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Centered quad-ring Loop Cut created."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''',
'''bool EditorController::loopCutSelectedEdge() {\n    std::string error;\n    if (!session_.loopCutSelectedMeshEdge(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Centered quad-ring Loop Cut created."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::deleteSelectedMeshElements() {\n    std::string error;\n    if (!session_.deleteSelectedMeshElements(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Selected mesh elements deleted."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''')

# Touch toolbar always exposes Delete while Edit Mode has a selection.
path = 'src/app/qml/ViewportPlaceholder.qml'
replace_once(path,
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Cancel"\n            onClicked: root.controller.cancelEditMode()\n        }\n''',
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Delete"\n            enabled: root.controller.selectedMeshElementCount > 0\n            onClicked: root.controller.deleteSelectedMeshElements()\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Cancel"\n            onClicked: root.controller.cancelEditMode()\n        }\n''')

# Hardware keyboard Delete/Backspace on desktop/tablet keyboards.
path = 'src/app/qt/vulkan_viewport.cpp'
replace_once(path,
'''void VulkanViewport::keyPressEvent(QKeyEvent* event) {\n    if (transformInteraction_ && event->key() == Qt::Key_Escape) {\n        finishGizmoTransform(false);\n        event->accept();\n        return;\n    }\n    QQuickItem::keyPressEvent(event);\n}\n''',
'''void VulkanViewport::keyPressEvent(QKeyEvent* event) {\n    if (transformInteraction_ && event->key() == Qt::Key_Escape) {\n        finishGizmoTransform(false);\n        event->accept();\n        return;\n    }\n    if (controller_ && controller_->editMode() &&\n        (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)) {\n        (void)controller_->deleteSelectedMeshElements();\n        event->accept();\n        return;\n    }\n    QQuickItem::keyPressEvent(event);\n}\n''')

# Transaction tests per selection mode + invalid final-face deletion.
path = 'tests/test_mesh_edit_operators.cpp'
content = read(path)
if 'edit mode delete face previews and undo restores exact authored topology' not in content:
    content = content.rstrip() + r'''

TEST_CASE("edit mode delete face previews and undo restores exact authored topology") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Face));
    const auto face = session.editableMesh()->faces().front().id;
    REQUIRE(session.selectMeshFace(face));
    REQUIRE(session.deleteSelectedMeshElements(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->faceCount() == 5U);
    REQUIRE(session.meshSelection()->empty());
    REQUIRE(session.commitMeshEdit("Delete Face", &error));
    REQUIRE(session.nextUndoName() == "Delete Face");
    REQUIRE(session.undo());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 6U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 5U);
}

TEST_CASE("edit mode delete edge removes its incident faces and cancel restores cube") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));
    const auto edge = session.editableMesh()->edges().front().id;
    REQUIRE(session.selectMeshEdge(edge));
    REQUIRE(session.deleteSelectedMeshElements(&error));
    REQUIRE(session.editableMesh()->faceCount() == 4U);
    REQUIRE(session.editableMesh()->edgeCount() == 11U);
    REQUIRE(session.cancelMeshEdit());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 6U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->edgeCount() == 12U);
}

TEST_CASE("edit mode delete vertex removes incident topology and keeps remaining mesh valid") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    const auto vertex = session.editableMesh()->vertices().front().id;
    REQUIRE(session.selectMeshVertex(vertex));
    REQUIRE(session.deleteSelectedMeshElements(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->vertexCount() == 7U);
    REQUIRE(session.editableMesh()->faceCount() == 3U);
    REQUIRE(session.editableMesh()->findVertex(vertex) == nullptr);
    REQUIRE(session.cancelMeshEdit());
}

TEST_CASE("edit mode refuses deleting the last editable face") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Single Face Delete", &error));
    m3d::EditableMesh authored;
    const auto a = authored.addVertex({0.0F,0.0F,0.0F});
    const auto b = authored.addVertex({1.0F,0.0F,0.0F});
    const auto c = authored.addVertex({0.0F,1.0F,0.0F});
    const std::array<m3d::EditableVertexId,3> triangle{a,b,c};
    REQUIRE(authored.addFace(triangle,&error).has_value());
    m3d::MeshResource resource;
    resource.id = m3d::ResourceId::generate();
    resource.name = "Triangle";
    resource.authoring = authored;
    REQUIRE(resource.rebuildFromAuthoring(&error));
    const auto object = session.createMeshObject(std::move(resource), "Triangle");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.beginMeshEdit(*object,&error));
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Face));
    REQUIRE(session.selectMeshFace(session.editableMesh()->faces().front().id));
    REQUIRE(!session.deleteSelectedMeshElements(&error));
    REQUIRE(!error.empty());
    REQUIRE(session.editableMesh()->faceCount() == 1U);
    REQUIRE(!session.isDirty());
    REQUIRE(session.cancelMeshEdit());
}
''' + '\n'
    write(path, content)

# Roadmap note: deletion is an enabling baseline, not a separate checkbox.
path = 'docs/ROADMAP.md'
content = read(path)
old = '''Current modeling baselines not yet considered checklist-complete: centered Loop Cut propagates across complete quad rings, but edge slide and multiple cuts remain pending. Single closed-loop Fill is implemented; Bridge supports two disjoint closed loops with equal vertex counts and automatic minimum-distance alignment. Grid Fill and unequal-count bridge policies remain pending.\n'''
new = '''Current modeling baselines not yet considered checklist-complete: centered Loop Cut propagates across complete quad rings, but edge slide and multiple cuts remain pending. Single closed-loop Fill is implemented; Bridge supports two disjoint closed loops with equal vertex counts and automatic minimum-distance alignment. Grid Fill and unequal-count bridge policies remain pending. Vertex/Edge/Face delete is available with topology-aware incident-face removal, allowing open boundaries to be authored directly in Edit Mode.\n'''
if 'Vertex/Edge/Face delete is available' not in content:
    if content.count(old) != 1: raise SystemExit('roadmap modeling baseline note marker missing')
    content = content.replace(old, new, 1)
    write(path, content)

Path('tests/test_mesh_edit_operators.cpp').write_text(Path('tests/test_mesh_edit_operators.cpp').read_text().rstrip() + '\n')
print('component delete editor UI roadmap patch prepared')
