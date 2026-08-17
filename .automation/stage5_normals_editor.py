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

# EditorSession normal orientation operators.
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool deleteSelectedMeshElements(std::string* error = nullptr);\n''',
'''    [[nodiscard]] bool deleteSelectedMeshElements(std::string* error = nullptr);\n    [[nodiscard]] bool flipSelectedMeshNormalComponents(std::string* error = nullptr);\n    [[nodiscard]] bool recalculateMeshNormalsOutside(std::string* error = nullptr);\n''')

path = 'src/editor/src/editor_mesh_operators.cpp'
content = read(path)
closing = '\n} // namespace m3d\n'
append = r'''

bool EditorSession::flipSelectedMeshNormalComponents(std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Face) {
        if (error) *error = "Flip Normals requires Face selection mode";
        return false;
    }
    const auto selected = selection.selectedFaces();
    if (selected.empty()) {
        if (error) *error = "Flip Normals requires at least one selected face";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto flipped = candidate.flipFaceComponents(selected, error);
    if (!flipped || flipped->empty() || !applyMeshEditPreview(candidate, error)) return false;

    selection.clear();
    selection.setMode(MeshSelectionMode::Face);
    bool first = true;
    for (const auto face : *flipped) {
        (void)selection.select(meshEditTransaction_->working, face,
                               first ? MeshSelectionAction::Replace : MeshSelectionAction::Add);
        first = false;
    }
    ++selectionRevision_;
    ++uiRevision_;
    if (error) error->clear();
    return true;
}

bool EditorSession::recalculateMeshNormalsOutside(std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    EditableMesh candidate = meshEditTransaction_->working;
    const auto flippedComponents = candidate.recalculateOutside(error);
    if (!flippedComponents) return false;
    if (*flippedComponents == 0U) {
        if (error) error->clear();
        return true;
    }
    return applyMeshEditPreview(candidate, error);
}
'''
if 'EditorSession::flipSelectedMeshNormalComponents' not in content:
    if not content.endswith(closing): raise SystemExit('editor mesh operators namespace closing not found')
    write(path, content[:-len(closing)] + append + closing)

# Controller actions.
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_INVOKABLE bool deleteSelectedMeshElements();\n''',
'''    Q_INVOKABLE bool deleteSelectedMeshElements();\n    Q_INVOKABLE bool flipSelectedNormals();\n    Q_INVOKABLE bool recalculateNormalsOutside();\n''')

path = 'src/app/qt/editor_controller.cpp'
replace_once(path,
'''bool EditorController::deleteSelectedMeshElements() {\n    std::string error;\n    if (!session_.deleteSelectedMeshElements(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Selected mesh elements deleted."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''',
'''bool EditorController::deleteSelectedMeshElements() {\n    std::string error;\n    if (!session_.deleteSelectedMeshElements(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Selected mesh elements deleted."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::flipSelectedNormals() {\n    std::string error;\n    if (!session_.flipSelectedMeshNormalComponents(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Connected face component normals flipped."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::recalculateNormalsOutside() {\n    std::string error;\n    if (!session_.recalculateMeshNormalsOutside(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Closed component normals recalculated outside."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''')

# Touch UI: component flip is Face-only; Outside works for any Edit Mode selection.
path = 'src/app/qml/ViewportPlaceholder.qml'
replace_once(path,
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Delete"\n            enabled: root.controller.selectedMeshElementCount > 0\n            onClicked: root.controller.deleteSelectedMeshElements()\n        }\n''',
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Flip Normals"\n            enabled: root.controller.meshSelectionMode === "Face"\n                     && root.controller.selectedMeshElementCount > 0\n            onClicked: root.controller.flipSelectedNormals()\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Normals Out"\n            onClicked: root.controller.recalculateNormalsOutside()\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Delete"\n            enabled: root.controller.selectedMeshElementCount > 0\n            onClicked: root.controller.deleteSelectedMeshElements()\n        }\n''')

# Editor-level preview, selection and single-Undo behavior.
path = 'tests/test_mesh_edit_operators.cpp'
content = read(path)
if 'edit mode flip normals updates live render normals and cancel restores outward cube' not in content:
    content = content.rstrip() + r'''

TEST_CASE("edit mode flip normals updates live render normals and cancel restores outward cube") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Face));
    REQUIRE(session.selectMeshFace(session.editableMesh()->faces().front().id));
    REQUIRE(session.flipSelectedMeshNormalComponents(&error));
    REQUIRE(error.empty());
    REQUIRE(session.meshSelection()->selectedFaces().size() == 6U);

    const auto resourceId = *session.scene()->find(*object)->meshResource;
    const auto* preview = session.scene()->findMeshResource(resourceId);
    REQUIRE(preview != nullptr);
    for (const auto& vertex : preview->vertices) {
        const float orientation = vertex.normal.x * vertex.position.x +
                                  vertex.normal.y * vertex.position.y +
                                  vertex.normal.z * vertex.position.z;
        REQUIRE(orientation < 0.0F);
    }
    REQUIRE(session.cancelMeshEdit());
    const auto* restored = session.scene()->findMeshResource(resourceId);
    for (const auto& vertex : restored->vertices) {
        const float orientation = vertex.normal.x * vertex.position.x +
                                  vertex.normal.y * vertex.position.y +
                                  vertex.normal.z * vertex.position.z;
        REQUIRE(orientation > 0.0F);
    }
}

TEST_CASE("flip then recalculate outside commits as one normals undo") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Face));
    REQUIRE(session.selectMeshFace(session.editableMesh()->faces().front().id));
    REQUIRE(session.flipSelectedMeshNormalComponents(&error));
    REQUIRE(session.recalculateMeshNormalsOutside(&error));

    const auto resourceId = *session.scene()->find(*object)->meshResource;
    const auto* preview = session.scene()->findMeshResource(resourceId);
    for (const auto& vertex : preview->vertices) {
        const float orientation = vertex.normal.x * vertex.position.x +
                                  vertex.normal.y * vertex.position.y +
                                  vertex.normal.z * vertex.position.z;
        REQUIRE(orientation > 0.0F);
    }
    REQUIRE(session.commitMeshEdit("Recalculate Normals", &error));
    REQUIRE(session.nextUndoName() == "Recalculate Normals");
    REQUIRE(session.undo());
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 6U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 6U);
}

TEST_CASE("recalculate outside on open mesh is a clean no-op") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Face));
    REQUIRE(session.selectMeshFace(session.editableMesh()->faces().front().id));
    REQUIRE(session.deleteSelectedMeshElements(&error));
    REQUIRE(session.commitMeshEdit("Open Mesh", &error));
    REQUIRE(session.beginMeshEdit(*object, &error));
    REQUIRE(session.recalculateMeshNormalsOutside(&error));
    REQUIRE(!session.isDirty());
    REQUIRE(session.cancelMeshEdit());
}
''' + '\n'
    write(path, content)

# Stage 5 normals tool item closes for topology orientation; shading normals remain Stage 8.
path = 'docs/ROADMAP.md'
content = read(path)
if '- [ ] Normals tools.' in content:
    content = content.replace('- [ ] Normals tools.', '- [x] Normals tools.', 1)
old = '''Current modeling baselines: Loop Cut propagates across complete quad rings with 1–32 evenly spaced cuts; Edge Slide remains a separate follow-up operator. Single closed-loop Fill is implemented; Bridge supports two disjoint closed loops with equal vertex counts and automatic minimum-distance alignment. Grid Fill and unequal-count bridge policies remain pending. Vertex/Edge/Face delete is available with topology-aware incident-face removal, allowing open boundaries to be authored directly in Edit Mode.\n'''
new = '''Current modeling baselines: Loop Cut propagates across complete quad rings with 1–32 evenly spaced cuts; Edge Slide remains a separate follow-up operator. Single closed-loop Fill is implemented; Bridge supports two disjoint closed loops with equal vertex counts and automatic minimum-distance alignment. Grid Fill and unequal-count bridge policies remain pending. Vertex/Edge/Face delete is topology-aware. Normal orientation tools provide connected-component Flip and closed-component Recalculate Outside; smooth/split/custom shading normals remain in the shading/material stage.\n'''
if old in content:
    content = content.replace(old, new, 1)
write(path, content)

Path('tests/test_mesh_edit_operators.cpp').write_text(Path('tests/test_mesh_edit_operators.cpp').read_text().rstrip() + '\n')
print('normal orientation tools editor UI roadmap patch prepared')
