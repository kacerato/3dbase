from pathlib import Path

path = Path('tests/test_mesh_edit_operators.cpp')
content = path.read_text()
old = '''    REQUIRE(session.deleteSelectedMeshElements(&error));\n    REQUIRE(session.commitMeshEdit("Open Mesh", &error));\n    REQUIRE(session.beginMeshEdit(*object, &error));\n    REQUIRE(session.recalculateMeshNormalsOutside(&error));\n    REQUIRE(!session.isDirty());\n'''
new = '''    REQUIRE(session.deleteSelectedMeshElements(&error));\n    REQUIRE(session.commitMeshEdit("Open Mesh", &error));\n    REQUIRE(session.saveProject(&error));\n    REQUIRE(!session.isDirty());\n    REQUIRE(session.beginMeshEdit(*object, &error));\n    REQUIRE(session.recalculateMeshNormalsOutside(&error));\n    REQUIRE(!session.isDirty());\n'''
count = content.count(old)
if count != 1:
    raise SystemExit(f'expected one normal no-op test block, found {count}')
path.write_text(content.replace(old, new, 1).rstrip() + '\n')
print('normal no-op dirty checkpoint test fixed')
