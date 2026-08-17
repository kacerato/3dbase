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

# Test include for std::set.
replace_once('tests/test_mesh_operations.cpp',
'''#include <cmath>\n#include <string>\n''',
'''#include <cmath>\n#include <set>\n#include <string>\n''')

# Keep old EditorSession one-cut API, add multi-cut overload.
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool loopCutSelectedMeshEdge(std::string* error = nullptr);\n''',
'''    [[nodiscard]] bool loopCutSelectedMeshEdge(std::string* error = nullptr);\n    [[nodiscard]] bool loopCutSelectedMeshEdge(std::uint32_t cuts,\n                                               std::string* error = nullptr);\n''')

path = 'src/editor/src/editor_mesh_operators.cpp'
content = read(path)
closing = '\n} // namespace m3d\n'
append = r'''

bool EditorSession::loopCutSelectedMeshEdge(std::uint32_t cuts, std::string* error) {
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
    const auto result = candidate.loopCut(selected.front(), cuts, error);
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
if 'EditorSession::loopCutSelectedMeshEdge(std::uint32_t cuts' not in content:
    if not content.endswith(closing): raise SystemExit('editor mesh operators namespace closing not found')
    write(path, content[:-len(closing)] + append + closing)

# Controller accepts a requested cut count.
replace_once('src/app/qt/editor_controller.hpp',
'''    Q_INVOKABLE bool loopCutSelectedEdge();\n''',
'''    Q_INVOKABLE bool loopCutSelectedEdge(int cuts = 1);\n''')

path = 'src/app/qt/editor_controller.cpp'
replace_once(path,
'''bool EditorController::loopCutSelectedEdge() {\n    std::string error;\n    if (!session_.loopCutSelectedMeshEdge(&error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Centered quad-ring Loop Cut created."));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''',
'''bool EditorController::loopCutSelectedEdge(int cuts) {\n    if (cuts < 1 || cuts > 32) {\n        setStatus(QStringLiteral("Loop Cut count must be between 1 and 32."));\n        return false;\n    }\n    std::string error;\n    if (!session_.loopCutSelectedMeshEdge(static_cast<std::uint32_t>(cuts), &error)) {\n        setStatus(QString::fromStdString(error));\n        return false;\n    }\n    setStatus(QStringLiteral("Loop Cut created with %1 cut(s).").arg(cuts));\n    refreshUi();\n    emit editModeChanged();\n    return true;\n}\n''')

# Mobile controls: explicit number of cuts next to operator.
path = 'src/app/qml/ViewportPlaceholder.qml'
replace_once(path,
'''        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Loop Cut"\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount === 1\n            onClicked: root.controller.loopCutSelectedEdge()\n        }\n''',
'''        SpinBox {\n            id: loopCutCount\n            height: 38\n            width: 86\n            visible: root.controller.editMode && root.controller.meshSelectionMode === "Edge"\n            from: 1\n            to: 32\n            value: 1\n            editable: false\n        }\n        Button {\n            height: 38\n            visible: root.controller.editMode\n            text: "Loop Cut ×" + loopCutCount.value\n            enabled: root.controller.meshSelectionMode === "Edge"\n                     && root.controller.selectedMeshElementCount === 1\n            onClicked: root.controller.loopCutSelectedEdge(loopCutCount.value)\n        }\n''')

# Transaction test verifies selected generated rings and one history entry.
path = 'tests/test_mesh_edit_operators.cpp'
content = read(path)
if 'edit mode multi loop cut remains one committed mesh transaction' not in content:
    content = content.rstrip() + r'''

TEST_CASE("edit mode multi loop cut remains one committed mesh transaction") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));
    const auto startEdge = session.editableMesh()->edges().front().id;
    REQUIRE(session.selectMeshEdge(startEdge));
    REQUIRE(session.loopCutSelectedMeshEdge(3U, &error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->vertexCount() == 20U);
    REQUIRE(session.editableMesh()->edgeCount() == 36U);
    REQUIRE(session.editableMesh()->faceCount() == 18U);
    REQUIRE(session.meshSelection()->selectedEdges().size() == 12U);

    REQUIRE(session.commitMeshEdit("Loop Cut x3", &error));
    REQUIRE(session.nextUndoName() == "Loop Cut x3");
    REQUIRE(session.undo());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 8U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 6U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 20U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 18U);
}
''' + '\n'
    write(path, content)

# Loop Cut checklist can now close; Edge Slide is tracked separately as a future enhancement.
path = 'docs/ROADMAP.md'
content = read(path)
if '- [ ] Loop cut.' in content:
    content = content.replace('- [ ] Loop cut.', '- [x] Loop cut.', 1)
old = '''Current modeling baselines not yet considered checklist-complete: centered Loop Cut propagates across complete quad rings, but edge slide and multiple cuts remain pending. Single closed-loop Fill is implemented; Bridge supports two disjoint closed loops with equal vertex counts and automatic minimum-distance alignment. Grid Fill and unequal-count bridge policies remain pending. Vertex/Edge/Face delete is available with topology-aware incident-face removal, allowing open boundaries to be authored directly in Edit Mode.\n'''
new = '''Current modeling baselines: Loop Cut propagates across complete quad rings with 1–32 evenly spaced cuts; Edge Slide remains a separate follow-up operator. Single closed-loop Fill is implemented; Bridge supports two disjoint closed loops with equal vertex counts and automatic minimum-distance alignment. Grid Fill and unequal-count bridge policies remain pending. Vertex/Edge/Face delete is available with topology-aware incident-face removal, allowing open boundaries to be authored directly in Edit Mode.\n'''
if old in content:
    content = content.replace(old, new, 1)
write(path, content)

for path in [Path('tests/test_mesh_operations.cpp'), Path('tests/test_mesh_edit_operators.cpp')]:
    path.write_text(path.read_text().rstrip() + '\n')
print('multi ring loop cut editor UI roadmap patch prepared')
