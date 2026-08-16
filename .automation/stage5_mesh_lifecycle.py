from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text()


def write(path: str, content: str) -> None:
    Path(path).write_text(content)


def replace_once(path: str, old: str, new: str) -> None:
    content = read(path)
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected 1 match, found {count}: {old[:100]!r}")
    write(path, content.replace(old, new, 1))


path = 'src/editor/src/editor_session.cpp'

replace_once(path,
'''void EditorSession::closeProject() noexcept {\n    transformTransaction_.reset();\n    activeLayer_.reset();\n''',
'''void EditorSession::closeProject() noexcept {\n    transformTransaction_.reset();\n    meshEditTransaction_.reset();\n    activeLayer_.reset();\n''')

replace_once(path,
'''    if (transformTransaction_) {\n        if (error) *error = "Cannot save during an active transform transaction";\n        return false;\n    }\n''',
'''    if (hasActiveMutationTransaction()) {\n        if (error) *error = "Cannot save during an active editor transaction";\n        return false;\n    }\n''')

replace_once(path,
'''    if (transformTransaction_) {\n        if (error) *error = "Cannot autosave during an active transform transaction";\n        return false;\n    }\n''',
'''    if (hasActiveMutationTransaction()) {\n        if (error) *error = "Cannot autosave during an active editor transaction";\n        return false;\n    }\n''')

replace_once(path,
'''bool EditorSession::recoverAutosave(std::string* error) {\n    if (!requireProject(error)) return false;\n    auto recovered = ProjectRepository::loadAutosave(document_->root, error);\n''',
'''bool EditorSession::recoverAutosave(std::string* error) {\n    if (!requireProject(error)) return false;\n    if (hasActiveMutationTransaction()) {\n        if (error) *error = "Cannot recover autosave during an active editor transaction";\n        return false;\n    }\n    auto recovered = ProjectRepository::loadAutosave(document_->root, error);\n''')

replace_once(path,
'''    selection_.clear();\n    activeLayer_.reset();\n    recoveredDirty_ = true;\n''',
'''    selection_.clear();\n    transformTransaction_.reset();\n    meshEditTransaction_.reset();\n    activeLayer_.reset();\n    recoveredDirty_ = true;\n''')

replace_once(path,
'''bool EditorSession::discardAutosave(std::string* error) {\n    if (!requireProject(error)) return false;\n    return ProjectRepository::clearAutosave(document_->root, error);\n}\n''',
'''bool EditorSession::discardAutosave(std::string* error) {\n    if (!requireProject(error)) return false;\n    if (hasActiveMutationTransaction()) {\n        if (error) *error = "Cannot discard autosave during an active editor transaction";\n        return false;\n    }\n    return ProjectRepository::clearAutosave(document_->root, error);\n}\n''')

replace_once(path,
'''bool EditorSession::isDirty() const noexcept {\n    return recoveredDirty_ || commands_.isDirty() || transformTransactionHasChanges();\n}\n''',
'''bool EditorSession::isDirty() const noexcept {\n    const bool meshEditDirty = meshEditTransaction_ && meshEditTransaction_->dirty;\n    return recoveredDirty_ || commands_.isDirty() || transformTransactionHasChanges() || meshEditDirty;\n}\n''')

content = read(path)
content = content.replace('if (transformTransaction_) return std::nullopt;',
                          'if (hasActiveMutationTransaction()) return std::nullopt;')
content = content.replace('if (transformTransaction_ ||',
                          'if (hasActiveMutationTransaction() ||')
content = content.replace('if (!document_ || transformTransaction_ ||',
                          'if (!document_ || hasActiveMutationTransaction() ||')
content = content.replace('if (!document_ || transformTransaction_) return false;',
                          'if (!document_ || hasActiveMutationTransaction()) return false;')
content = content.replace('if (transformTransaction_) return false;',
                          'if (hasActiveMutationTransaction()) return false;')
content = content.replace('if (transformTransaction_ || selection_.empty()) return;',
                          'if (hasActiveMutationTransaction() || selection_.empty()) return;')
write(path, content)

# The transform preview/commit/cancel functions must remain available while a
# transform transaction itself is active. The broad lifecycle replacements above
# intentionally do not touch their !transformTransaction_ guards.

replace_once(path,
'''void EditorSession::resetForDocument(bool recoveredDirty) noexcept {\n    transformTransaction_.reset();\n    activeLayer_.reset();\n''',
'''void EditorSession::resetForDocument(bool recoveredDirty) noexcept {\n    transformTransaction_.reset();\n    meshEditTransaction_.reset();\n    activeLayer_.reset();\n''')

# Add lifecycle behavior coverage to the dedicated transaction tests.
test_path = 'tests/test_mesh_edit_transaction.cpp'
tests = read(test_path)
marker = 'mesh edit transaction blocks conflicting editor lifecycle operations'
if marker not in tests:
    tests = tests.rstrip() + r'''

TEST_CASE("mesh edit transaction blocks conflicting editor lifecycle operations") {
    const auto path = meshEditProjectPath();
    MeshEditCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Mesh Lifecycle", &error));
    const auto object = session.createObject(m3d::ObjectType::Mesh, "Cube");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));

    REQUIRE(session.beginMeshEdit(*object, &error));
    const auto vertex = session.editableMesh()->vertices().front().id;
    REQUIRE(session.selectMeshVertex(vertex));
    REQUIRE(session.moveSelectedMeshVertices({0.25F, 0.0F, 0.0F}, &error));
    REQUIRE(session.isDirty());

    error.clear();
    REQUIRE(!session.saveProject(&error));
    REQUIRE(!error.empty());
    error.clear();
    REQUIRE(!session.writeAutosave(&error));
    REQUIRE(!error.empty());
    REQUIRE(!session.createObject(m3d::ObjectType::Empty, "Conflict").has_value());
    REQUIRE(!session.duplicateSelection());
    REQUIRE(!session.deleteSelection());
    REQUIRE(!session.beginTransformTransaction({*object}, "Conflict"));
    REQUIRE(!session.undo());
    REQUIRE(!session.redo());

    REQUIRE(session.cancelMeshEdit());
    REQUIRE(!session.hasMeshEditTransaction());
    REQUIRE(!session.isDirty());
    error.clear();
    REQUIRE(session.saveProject(&error));
}

TEST_CASE("closing a project always clears an active mesh edit transaction") {
    const auto path = meshEditProjectPath();
    MeshEditCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Mesh Close", &error));
    const auto object = session.createObject(m3d::ObjectType::Mesh, "Cube");
    REQUIRE(object.has_value());
    REQUIRE(session.beginMeshEdit(*object, &error));
    REQUIRE(session.hasMeshEditTransaction());
    session.closeProject();
    REQUIRE(!session.hasProject());
    REQUIRE(!session.hasMeshEditTransaction());
}
''' + '\n'
    write(test_path, tests)

print('Mesh edit lifecycle hardening prepared')
