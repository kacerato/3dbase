from pathlib import Path
p=Path('src/app/qt/vulkan_viewport_renderer.cpp')
s=p.read_text()
start=s.index('bool createPipelineCache(VulkanViewportRenderer::Impl& impl) {')
end_marker='    return file.commit();\n}\n'
end=s.index(end_marker,start)+len(end_marker)
block=s[start:end]
s=s[:start]+s[end:]
anchor='namespace {\n\nbool createGridResources(VulkanViewportRenderer::Impl& impl) {'
if s.count(anchor)!=1: raise SystemExit('anchor mismatch')
s=s.replace(anchor,'namespace {\n\n'+block+'\nbool createGridResources(VulkanViewportRenderer::Impl& impl) {',1)
p.write_text(s)
print('moved pipeline cache helpers after Impl definition')
