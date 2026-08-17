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

# EditorSession Grid Fill action.
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool fillSelectedMeshBoundary(std::string* error = nullptr);\n''',
'''    [[nodiscard]] bool fillSelectedMeshBoundary(std::string* error = nullptr);\n    [[nodiscard]] bool gridFillSelectedMeshBoundary(std::uint32_t span, std::uint32_t offset,\n                                                    std::string* error = nullptr);\n''')

path = 'src/editor/src/editor_mesh_operators.cpp'
content = read(path)
closing = '\n} // namespace m3d\n'
append = r'''

bool EditorSession::gridFillSelectedMeshBoundary(std::uint32_t span, std::uint32_t offset,
                                                  std::string* error) {
    if (!meshEditTransaction_) {
        if (error) *error = "Edit Mode is not active";
        return false;
    }
    auto& selection = meshEditTransaction_->selection;
    if (selection.mode() != MeshSelectionMode::Edge) {
        if (error) *error = "Grid Fill requires Edge selection mode";
        return false;
    }
    const auto selected = selection.selectedEdges();
    if (selected.size() < 4U) {
        if (error) *error = "Grid Fill requires a closed boundary loop with at least four selected edges";
        return false;
    }

    EditableMesh candidate = meshEditTransaction_->working;
    const auto faces = candidate.gridFillBoundaryLoop(selected, span, offset, error);
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
if 'EditorSession::gridFillSelectedMeshBoundary' not in content:
    if not content.endswith(closing): raise SystemExit('editor mesh operators namespace closing not found')
    write(path, content[:-len(closing)] + append + closing)

# Controller action.
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_INVOKABLE bool fillSelectedBoundary();\n''',
'''    Q_INVOKABLE bool fillSelectedBoundary();\n    Q_INVOKABLE bool gridFillSelectedBoundary(int span, int offset = 0);\n''')

path = 'src/app/qt/editor_controller.cpp'
replace_once(path,
'''bool EditorController::fillSelectedBoundary() {\n    std::string error;\n    if (!session_.fillSelectedMeshBoundary(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Boundary loop filled."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''',
'''bool EditorController::fillSelectedBoundary() {\n    std::string error;\n    if (!session_.fillSelectedMeshBoundary(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Boundary loop filled."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n\nbool EditorController::gridFillSelectedBoundary(int span, int offset) {\n    if (span < 1 || offset < 0) {\n        setStatus(QStringLiteral("Grid Fill span must be positive and offset cannot be negative."));\n        return false;\n    }\n    std::string error;\n    if (!session_.gridFillSelectedMeshBoundary(static_cast<std::uint32_t>(span),\n                                               static_cast<std::uint32_t>(offset), &error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Boundary grid-filled with span %1 and offset %2.").arg(span).arg(offset));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''')

# Mobile controls expose Span and Offset explicitly instead of hiding topology assumptions.
path = 'src/app/qml/ViewportPlaceholder.qml'
replace_once(path,
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Fill"\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount >= 3\n            onClicked: root.controller.fillSelectedBoundary()\n        }\n''',
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Fill"\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount >= 3\n            onClicked: root.controller.fillSelectedBoundary()\n        }\n        SpinBox {\n            id: gridFillSpan\n            height: 38\n            width: 78\n            visible: root.controller.editMode && root.controller.meshSelectionMode === "Edge"\n            from: 1\n            to: Math.max(1, Math.floor(root.controller.selectedMeshElementCount / 2) - 1)\n            value: Math.min(2, to)\n            editable: false\n        }\n        SpinBox {\n            id: gridFillOffset\n            height: 38\n            width: 78\n            visible: root.controller.editMode && root.controller.meshSelectionMode === "Edge"\n            from: 0\n            to: Math.max(0, root.controller.selectedMeshElementCount - 1)\n            value: 0\n            editable: false\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Grid Fill"\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount >= 4\n                     && root.controller.selectedMeshElementCount % 2 === 0\n            onClicked: root.controller.gridFillSelectedBoundary(gridFillSpan.value, gridFillOffset.value)\n        }\n''')

# Editor transaction coverage using a 3x2 top boundary.
path = 'tests/test_mesh_edit_operators.cpp'
content = read(path)
if 'edit mode grid fill builds a structured quad patch and remains one undo' not in content:
    content = content.rstrip() + r'''

TEST_CASE("edit mode grid fill builds a structured quad patch and remains one undo") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Grid Fill Operator", &error));

    m3d::EditableMesh authored;
    const std::array<m3d::Vec3,10> ringPositions{
        m3d::Vec3{-1.5F,-1.0F,1.0F}, m3d::Vec3{-0.5F,-1.0F,1.0F},
        m3d::Vec3{0.5F,-1.0F,1.0F}, m3d::Vec3{1.5F,-1.0F,1.0F},
        m3d::Vec3{1.5F,0.0F,1.0F}, m3d::Vec3{1.5F,1.0F,1.0F},
        m3d::Vec3{0.5F,1.0F,1.0F}, m3d::Vec3{-0.5F,1.0F,1.0F},
        m3d::Vec3{-1.5F,1.0F,1.0F}, m3d::Vec3{-1.5F,0.0F,1.0F}
    };
    std::array<m3d::EditableVertexId,10> top{};
    std::array<m3d::EditableVertexId,10> bottom{};
    for (std::size_t index=0; index<ringPositions.size(); ++index) {
        top[index]=authored.addVertex(ringPositions[index]);
        auto position=ringPositions[index]; position.z=-1.0F;
        bottom[index]=authored.addVertex(position);
    }
    REQUIRE(authored.addFace(bottom,&error).has_value());
    for (std::size_t index=0; index<top.size(); ++index) {
        const std::size_t next=(index+1U)%top.size();
        const std::array<m3d::EditableVertexId,4> side{top[index],top[next],bottom[next],bottom[index]};
        REQUIRE(authored.addFace(side,&error).has_value());
    }
    m3d::MeshResource resource;
    resource.id=m3d::ResourceId::generate();
    resource.name="Grid Fill Open Prism";
    resource.authoring=authored;
    REQUIRE(resource.rebuildFromAuthoring(&error));
    const auto object=session.createMeshObject(std::move(resource),"Grid Fill Open Prism");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.beginMeshEdit(*object,&error));
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));

    bool first=true;
    std::size_t boundaryCount=0U;
    for (const auto& edge:session.editableMesh()->edges()) {
        const auto* halfEdge=session.editableMesh()->findHalfEdge(edge.halfEdge);
        if (!halfEdge || !halfEdge->twin.isNull()) continue;
        REQUIRE(session.selectMeshEdge(edge.id, first ? m3d::MeshSelectionAction::Replace
                                                      : m3d::MeshSelectionAction::Add));
        first=false; ++boundaryCount;
    }
    REQUIRE(boundaryCount==10U);
    REQUIRE(session.gridFillSelectedMeshBoundary(3U,0U,&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->vertexCount()==22U);
    REQUIRE(session.editableMesh()->faceCount()==17U);
    REQUIRE(session.meshSelection()->mode()==m3d::MeshSelectionMode::Face);
    REQUIRE(session.meshSelection()->selectedFaces().size()==6U);

    REQUIRE(session.commitMeshEdit("Grid Fill",&error));
    REQUIRE(session.nextUndoName()=="Grid Fill");
    REQUIRE(session.undo());
    const auto resourceId=*session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->vertexCount()==20U);
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount()==11U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->vertexCount()==22U);
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount()==17U);
}
''' + '\n'
    write(path, content)

# Fill/Grid Fill checklist closes now; note documents Span/Offset semantics.
path = 'docs/ROADMAP.md'
content = read(path)
if '- [ ] Fill/Grid Fill.' in content:
    content = content.replace('- [ ] Fill/Grid Fill.', '- [x] Fill/Grid Fill.', 1)
old = '''Single closed-loop Fill is implemented; Bridge supports two disjoint closed loops with equal or unequal vertex counts, using cyclic minimum-distance alignment and a validated quad/triangle zipper strip. Grid Fill remains pending.'''
new = '''Single-loop Fill and structured Grid Fill are implemented. Grid Fill uses explicit Span/Offset and a Coons-style interior patch to create quads while preserving the selected boundary. Bridge supports two disjoint closed loops with equal or unequal vertex counts, using cyclic minimum-distance alignment and a validated quad/triangle zipper strip.'''
if old in content:
    content = content.replace(old, new, 1)
write(path, content)

Path('tests/test_mesh_edit_operators.cpp').write_text(Path('tests/test_mesh_edit_operators.cpp').read_text().rstrip() + '\n')
print('grid fill editor UI roadmap patch prepared')
