from pathlib import Path
p=Path('src/core/src/scene_serializer.cpp')
s=p.read_text()
old='''            std::size_t collectionCount = 0;\n            SceneLayer layer;\n            if (!(input >> record >> std::quoted(idText) >> std::quoted(layer.name)\n                  >> enabled >> collectionCount) || record != "layer") {'''
new='''            std::size_t layerCollectionCount = 0;\n            SceneLayer layer;\n            if (!(input >> record >> std::quoted(idText) >> std::quoted(layer.name)\n                  >> enabled >> layerCollectionCount) || record != "layer") {'''
if s.count(old)!=1: raise SystemExit('layer count declaration anchor mismatch')
s=s.replace(old,new,1)
s=s.replace('layer.collections.reserve(collectionCount);\n            for (std::size_t member = 0; member < collectionCount; ++member) {','layer.collections.reserve(layerCollectionCount);\n            for (std::size_t member = 0; member < layerCollectionCount; ++member) {',1)
p.write_text(s)
print('fixed layerCollectionCount shadowing')
