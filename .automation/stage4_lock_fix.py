from pathlib import Path
p=Path('src/editor/src/editor_session.cpp')
s=p.read_text()
old='''    if (!visible && selection_.contains(object)) {\n        selection_.remove(object);\n        ++selectionRevision_;\n    }\n'''
new='''    if (!visible && selection_.contains(object)) {\n        const bool removed = selection_.remove(object);\n        if (removed) ++selectionRevision_;\n    }\n'''
if s.count(old)!=1: raise SystemExit('selection removal anchor mismatch')
p.write_text(s.replace(old,new,1))
print('handled selection removal return value')
