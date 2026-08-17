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

# Use adaptive bridge for the Edit Mode action. Equal-loop behavior remains compatible.
replace_once('src/editor/src/editor_mesh_operators.cpp',
'''    const auto faces = candidate.bridgeBoundaryLoops(selected, error);\n''',
'''    const auto faces = candidate.bridgeBoundaryLoopsAdaptive(selected, error);\n''')

# Editor-level unequal-loop bridge and transaction coverage.
path = 'tests/test_mesh_edit_operators.cpp'
content = read(path)
if 'edit mode bridge adapts triangle and quad boundary loops in one transaction' not in content:
    content = content.rstrip() + r'''

TEST_CASE("edit mode bridge adapts triangle and quad boundary loops in one transaction") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Adaptive Bridge", &error));

    m3d::EditableMesh authored;
    const std::array<m3d::EditableVertexId,3> triangle{
        authored.addVertex({0.0F,-1.2F,0.0F}), authored.addVertex({1.1F,0.8F,0.0F}),
        authored.addVertex({-1.1F,0.8F,0.0F})
    };
    const std::array<m3d::EditableVertexId,4> quad{
        authored.addVertex({-1.2F,-1.2F,2.0F}), authored.addVertex({1.2F,-1.2F,2.0F}),
        authored.addVertex({1.2F,1.2F,2.0F}), authored.addVertex({-1.2F,1.2F,2.0F})
    };
    const std::array<m3d::EditableVertexId,3> triangleWinding{triangle[0],triangle[2],triangle[1]};
    REQUIRE(authored.addFace(triangleWinding,&error).has_value());
    REQUIRE(authored.addFace(quad,&error).has_value());

    m3d::MeshResource resource;
    resource.id = m3d::ResourceId::generate();
    resource.name = "Triangle Quad Loops";
    resource.authoring = authored;
    REQUIRE(resource.rebuildFromAuthoring(&error));
    const auto object = session.createMeshObject(std::move(resource), "Triangle Quad Loops");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.beginMeshEdit(*object,&error));
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));

    bool first = true;
    for (const auto& edge : session.editableMesh()->edges()) {
        REQUIRE(session.selectMeshEdge(edge.id, first ? m3d::MeshSelectionAction::Replace
                                                      : m3d::MeshSelectionAction::Add));
        first = false;
    }
    REQUIRE(session.meshSelection()->selectedEdges().size() == 7U);
    REQUIRE(session.bridgeSelectedMeshBoundaries(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->faceCount() == 8U);
    REQUIRE(session.editableMesh()->edgeCount() == 13U);
    REQUIRE(session.meshSelection()->mode() == m3d::MeshSelectionMode::Face);
    REQUIRE(session.meshSelection()->selectedFaces().size() == 6U);

    REQUIRE(session.commitMeshEdit("Bridge Loops", &error));
    REQUIRE(session.nextUndoName() == "Bridge Loops");
    REQUIRE(session.undo());
    const auto resourceId = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 2U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 8U);
}
''' + '\n'
    write(path, content)

# Bridge loops is complete at Stage 5 baseline: equal and unequal loop counts are supported.
path = 'docs/ROADMAP.md'
content = read(path)
if '- [ ] Bridge loops.' in content:
    content = content.replace('- [ ] Bridge loops.', '- [x] Bridge loops.', 1)
old = '''Single closed-loop Fill is implemented; Bridge supports two disjoint closed loops with equal vertex counts and automatic minimum-distance alignment. Grid Fill and unequal-count bridge policies remain pending.'''
new = '''Single closed-loop Fill is implemented; Bridge supports two disjoint closed loops with equal or unequal vertex counts, using cyclic minimum-distance alignment and a validated quad/triangle zipper strip. Grid Fill remains pending.'''
if old in content:
    content = content.replace(old, new, 1)
write(path, content)

Path('tests/test_mesh_edit_operators.cpp').write_text(Path('tests/test_mesh_edit_operators.cpp').read_text().rstrip() + '\n')
print('adaptive unequal bridge editor roadmap patch prepared')
