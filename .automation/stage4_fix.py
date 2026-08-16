from pathlib import Path
p=Path('src/editor/src/editor_session.cpp')
s=p.read_text()
old='''std::optional<ObjectId> EditorSession::createObject(ObjectType type, std::string name,\n    if (transformTransaction_) return std::nullopt;\n                                                     std::optional<ObjectId> parent) {\n    if (!document_) return std::nullopt;'''
new='''std::optional<ObjectId> EditorSession::createObject(ObjectType type, std::string name,\n                                                     std::optional<ObjectId> parent) {\n    if (transformTransaction_) return std::nullopt;\n    if (!document_) return std::nullopt;'''
if s.count(old)!=1: raise SystemExit('createObject anchor mismatch')
s=s.replace(old,new,1)
old='''std::optional<ObjectId> EditorSession::createMeshObject(MeshResource resource, std::string name,\n    if (transformTransaction_) return std::nullopt;\n                                                         std::optional<ObjectId> parent) {\n    if (!document_) return std::nullopt;'''
new='''std::optional<ObjectId> EditorSession::createMeshObject(MeshResource resource, std::string name,\n                                                         std::optional<ObjectId> parent) {\n    if (transformTransaction_) return std::nullopt;\n    if (!document_) return std::nullopt;'''
if s.count(old)!=1: raise SystemExit('createMeshObject anchor mismatch')
s=s.replace(old,new,1)
p.write_text(s)
print('fixed transaction guards after function signatures')
