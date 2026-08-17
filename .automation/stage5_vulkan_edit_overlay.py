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

# Renderer contract carries the immutable edit snapshot into the render thread.
path = 'src/app/qt/vulkan_viewport_renderer.hpp'
replace_once(path,
'''#include "mobile3d/editor/transform_gizmo.hpp"\n''',
'''#include "mobile3d/editor/mesh_edit_snapshot.hpp"\n#include "mobile3d/editor/transform_gizmo.hpp"\n''')
replace_once(path,
'''    std::uint32_t gizmoDraws{0};\n''',
'''    std::uint32_t gizmoDraws{0};\n    std::uint32_t editOverlayDraws{0};\n''')
replace_once(path,
'''    void synchronize(QRect viewportPixels, m3d::RenderSceneSnapshot snapshot,\n                     m3d::Mat4 viewProjection, VulkanGizmoPresentation gizmo);\n''',
'''    void synchronize(QRect viewportPixels, m3d::RenderSceneSnapshot snapshot,\n                     m3d::MeshEditPresentationSnapshot editMesh,\n                     m3d::Mat4 viewProjection, VulkanGizmoPresentation gizmo);\n''')
replace_once(path,
'''    m3d::RenderSceneSnapshot snapshot_;\n    m3d::Mat4 viewProjection_{};\n''',
'''    m3d::RenderSceneSnapshot snapshot_;\n    m3d::MeshEditPresentationSnapshot editMesh_;\n    m3d::Mat4 viewProjection_{};\n''')

# Viewport observability for smoke/debug counters.
path = 'src/app/qt/vulkan_viewport.hpp'
replace_once(path,
'''    [[nodiscard]] std::uint64_t recordedGizmoDrawCount() const noexcept {\n        return recordedGizmoDrawCount_.load(std::memory_order_relaxed);\n    }\n''',
'''    [[nodiscard]] std::uint64_t recordedGizmoDrawCount() const noexcept {\n        return recordedGizmoDrawCount_.load(std::memory_order_relaxed);\n    }\n    [[nodiscard]] std::uint64_t recordedEditOverlayDrawCount() const noexcept {\n        return recordedEditOverlayDrawCount_.load(std::memory_order_relaxed);\n    }\n''')
replace_once(path,
'''    std::atomic_uint64_t recordedGizmoDrawCount_{0};\n''',
'''    std::atomic_uint64_t recordedGizmoDrawCount_{0};\n    std::atomic_uint64_t recordedEditOverlayDrawCount_{0};\n''')

path = 'src/app/qt/vulkan_viewport.cpp'
replace_once(path,
'''    VulkanGizmoPresentation presentation;\n    if (!controller) return presentation;\n''',
'''    VulkanGizmoPresentation presentation;\n    if (!controller || controller->editMode()) return presentation;\n''')
replace_once(path,
'''    m3d::RenderSceneSnapshot snapshot;\n    if (controller_) snapshot = controller_->renderSnapshot();\n''',
'''    m3d::RenderSceneSnapshot snapshot;\n    m3d::MeshEditPresentationSnapshot editMesh;\n    if (controller_) {\n        snapshot = controller_->renderSnapshot();\n        editMesh = controller_->meshEditSnapshot();\n    }\n''')
replace_once(path,
'''    renderer_->synchronize(QRect(left, top, pixelWidth, pixelHeight), std::move(snapshot),\n                           viewProjection, gizmo);\n''',
'''    renderer_->synchronize(QRect(left, top, pixelWidth, pixelHeight), std::move(snapshot),\n                           std::move(editMesh), viewProjection, gizmo);\n''')
replace_once(path,
'''    if (stats.gizmoDraws > 0) {\n        recordedGizmoDrawCount_.fetch_add(stats.gizmoDraws, std::memory_order_relaxed);\n    }\n''',
'''    if (stats.gizmoDraws > 0) {\n        recordedGizmoDrawCount_.fetch_add(stats.gizmoDraws, std::memory_order_relaxed);\n    }\n    if (stats.editOverlayDraws > 0) {\n        recordedEditOverlayDrawCount_.fetch_add(stats.editOverlayDraws, std::memory_order_relaxed);\n    }\n''')

path = 'src/app/qt/vulkan_viewport_renderer.cpp'
replace_once(path,
'''void VulkanViewportRenderer::synchronize(QRect viewportPixels, m3d::RenderSceneSnapshot snapshot,\n                                         m3d::Mat4 viewProjection, VulkanGizmoPresentation gizmo) {\n    viewportPixels_ = viewportPixels;\n    snapshot_ = std::move(snapshot);\n    viewProjection_ = viewProjection;\n    gizmo_ = std::move(gizmo);\n}\n''',
'''void VulkanViewportRenderer::synchronize(QRect viewportPixels, m3d::RenderSceneSnapshot snapshot,\n                                         m3d::MeshEditPresentationSnapshot editMesh,\n                                         m3d::Mat4 viewProjection, VulkanGizmoPresentation gizmo) {\n    viewportPixels_ = viewportPixels;\n    snapshot_ = std::move(snapshot);\n    editMesh_ = std::move(editMesh);\n    viewProjection_ = viewProjection;\n    gizmo_ = std::move(gizmo);\n}\n''')

# Separate depth-tested line pipeline for authored topology. Grid/gizmo keep their existing overlay semantics.
replace_once(path,
'''    VkPipelineLayout linePipelineLayout{VK_NULL_HANDLE};\n    VkPipeline linePipeline{VK_NULL_HANDLE};\n''',
'''    VkPipelineLayout linePipelineLayout{VK_NULL_HANDLE};\n    VkPipeline linePipeline{VK_NULL_HANDLE};\n    VkPipelineLayout editLinePipelineLayout{VK_NULL_HANDLE};\n    VkPipeline editLinePipeline{VK_NULL_HANDLE};\n''')

replace_once(path,
'''                    VkPipelineLayout& layout,\n                    VkPipeline& pipeline,\n                    bool depthWrite = true,\n                    VkCullModeFlags cullMode = VK_CULL_MODE_NONE) {\n''',
'''                    VkPipelineLayout& layout,\n                    VkPipeline& pipeline,\n                    bool depthWrite = true,\n                    VkCullModeFlags cullMode = VK_CULL_MODE_NONE,\n                    bool lineDepthTest = false) {\n''')
replace_once(path,
'''    PipelineCommonState common(!linePipeline, sampleCount);\n    common.raster.cullMode = cullMode;\n    if (!linePipeline) {\n        common.depthStencil.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;\n    }\n''',
'''    PipelineCommonState common(!linePipeline || lineDepthTest, sampleCount);\n    common.raster.cullMode = cullMode;\n    if (!linePipeline) {\n        common.depthStencil.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;\n    } else if (lineDepthTest) {\n        common.depthStencil.depthWriteEnable = VK_FALSE;\n    }\n''')

# Local-space topology builder. It only consumes the immutable snapshot.
marker = '''[[nodiscard]] std::vector<LineVertex> makeGridVertices() {\n'''
content = read(path)
if 'makeEditMeshVertices' not in content:
    helper = r'''[[nodiscard]] std::uint64_t topologyEdgeKey(std::uint32_t first, std::uint32_t second) noexcept {
    const auto low = std::min(first, second);
    const auto high = std::max(first, second);
    return (static_cast<std::uint64_t>(low) << 32U) | static_cast<std::uint64_t>(high);
}

[[nodiscard]] std::vector<LineVertex> makeEditMeshVertices(
    const m3d::MeshEditPresentationSnapshot& editMesh) {
    std::vector<LineVertex> output;
    if (!editMesh.active() || editMesh.vertices.empty()) return output;

    std::unordered_map<std::uint32_t, m3d::Vec3> positions;
    positions.reserve(editMesh.vertices.size());
    m3d::Vec3 minimum = editMesh.vertices.front().position;
    m3d::Vec3 maximum = minimum;
    for (const auto& vertex : editMesh.vertices) {
        positions.emplace(vertex.id.value, vertex.position);
        minimum.x = std::min(minimum.x, vertex.position.x);
        minimum.y = std::min(minimum.y, vertex.position.y);
        minimum.z = std::min(minimum.z, vertex.position.z);
        maximum.x = std::max(maximum.x, vertex.position.x);
        maximum.y = std::max(maximum.y, vertex.position.y);
        maximum.z = std::max(maximum.z, vertex.position.z);
    }

    std::unordered_set<std::uint64_t> selectedFaceEdges;
    for (const auto& face : editMesh.faces) {
        if (!face.selected || face.vertices.size() < 2U) continue;
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const auto first = face.vertices[index].value;
            const auto second = face.vertices[(index + 1U) % face.vertices.size()].value;
            selectedFaceEdges.insert(topologyEdgeKey(first, second));
        }
    }

    constexpr std::array<float, 4> regularEdge{0.64F, 0.69F, 0.76F, 1.0F};
    constexpr std::array<float, 4> selectedElement{1.0F, 0.50F, 0.08F, 1.0F};
    output.reserve(editMesh.edges.size() * 2U + editMesh.vertices.size() * 6U);
    for (const auto& edge : editMesh.edges) {
        const auto first = positions.find(edge.first.value);
        const auto second = positions.find(edge.second.value);
        if (first == positions.end() || second == positions.end()) continue;
        const bool selected = edge.selected ||
            selectedFaceEdges.contains(topologyEdgeKey(edge.first.value, edge.second.value));
        appendGizmoLine(output, first->second, second->second,
                        selected ? selectedElement : regularEdge);
    }

    if (editMesh.mode == m3d::MeshSelectionMode::Vertex) {
        const float spanX = maximum.x - minimum.x;
        const float spanY = maximum.y - minimum.y;
        const float spanZ = maximum.z - minimum.z;
        const float marker = std::max(0.006F, std::max({spanX, spanY, spanZ, 0.1F}) * 0.018F);
        const m3d::Vec3 x{marker, 0.0F, 0.0F};
        const m3d::Vec3 y{0.0F, marker, 0.0F};
        const m3d::Vec3 z{0.0F, 0.0F, marker};
        for (const auto& vertex : editMesh.vertices) {
            const auto color = vertex.selected ? selectedElement : regularEdge;
            appendGizmoLine(output, subtractVector(vertex.position, x), addVector(vertex.position, x), color);
            appendGizmoLine(output, subtractVector(vertex.position, y), addVector(vertex.position, y), color);
            appendGizmoLine(output, subtractVector(vertex.position, z), addVector(vertex.position, z), color);
        }
    }
    return output;
}

'''
    if marker not in content:
        raise SystemExit('renderer: grid marker not found')
    write(path, content.replace(marker, helper + marker, 1))

# addVector/scaleVector already exist; makeEditMeshVertices also needs subtractVector.
replace_once(path,
'''[[nodiscard]] m3d::Vec3 scaleVector(m3d::Vec3 value, float scale) noexcept {\n    return {value.x * scale, value.y * scale, value.z * scale};\n}\n''',
'''[[nodiscard]] m3d::Vec3 scaleVector(m3d::Vec3 value, float scale) noexcept {\n    return {value.x * scale, value.y * scale, value.z * scale};\n}\n\n[[nodiscard]] m3d::Vec3 subtractVector(m3d::Vec3 left, m3d::Vec3 right) noexcept {\n    return {left.x - right.x, left.y - right.y, left.z - right.z};\n}\n''')

# Lifecycle of the new depth-tested line pipeline.
replace_once(path,
'''    if (impl.outlinePipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.outlinePipeline, nullptr);\n''',
'''    if (impl.editLinePipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.editLinePipeline, nullptr);\n    if (impl.editLinePipelineLayout) impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, impl.editLinePipelineLayout, nullptr);\n    if (impl.outlinePipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.outlinePipeline, nullptr);\n''')
replace_once(path,
'''        if (!createPipelineCache(impl) || !createGridResources(impl) ||\n            !createPipeline(impl, impl.renderPass, vulkanSampleCount(impl.sampleCount), true,\n                            impl.linePipelineLayout, impl.linePipeline) ||\n''',
'''        if (!createPipelineCache(impl) || !createGridResources(impl) ||\n            !createPipeline(impl, impl.renderPass, vulkanSampleCount(impl.sampleCount), true,\n                            impl.linePipelineLayout, impl.linePipeline) ||\n            !createPipeline(impl, impl.renderPass, vulkanSampleCount(impl.sampleCount), true,\n                            impl.editLinePipelineLayout, impl.editLinePipeline, false,\n                            VK_CULL_MODE_NONE, true) ||\n''')

# Object-mode outline should not obscure component selection.
replace_once(path,
'''        if (!object.visible || !object.selected || object.type != m3d::ObjectType::Mesh ||\n            !object.meshResource) continue;\n''',
'''        if (!object.visible || !object.selected || object.type != m3d::ObjectType::Mesh ||\n            !object.meshResource || (editMesh_.active() && object.id == editMesh_.object)) continue;\n''')

# Edited object remains neutral; component overlay owns the selection highlight.
replace_once(path,
'''            object.selected\n                ? std::array<float, 4>{0.93F, 0.55F, 0.18F, 1.0F}\n                : std::array<float, 4>{0.62F, 0.66F, 0.72F, 1.0F},\n''',
'''            (editMesh_.active() && object.id == editMesh_.object)\n                ? std::array<float, 4>{0.48F, 0.52F, 0.58F, 1.0F}\n                : object.selected\n                    ? std::array<float, 4>{0.93F, 0.55F, 0.18F, 1.0F}\n                    : std::array<float, 4>{0.62F, 0.66F, 0.72F, 1.0F},\n''')

# Draw topology after surfaces and before gizmos.
anchor = '''    const auto gizmoVertices = makeGizmoVertices(gizmo_);\n'''
content = read(path)
if 'stats.editOverlayDraws' not in content:
    overlay = r'''    if (editMesh_.active()) {
        const auto* editObject = snapshot_.find(editMesh_.object);
        const auto overlayVertices = makeEditMeshVertices(editMesh_);
        if (editObject && editObject->visible && !overlayVertices.empty()) {
            const auto stateInfo = window->graphicsStateInfo();
            ensureFrameGarbage(impl, stateInfo.framesInFlight);
            auto* garbage = frameGarbageFor(impl, stateInfo.currentFrameSlot);
            GpuBuffer overlayBuffer;
            if (garbage && createHostBuffer(impl.functions, impl.deviceFunctions,
                                            impl.physicalDevice, impl.device,
                                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                            overlayVertices.data(),
                                            static_cast<VkDeviceSize>(overlayVertices.size() * sizeof(LineVertex)),
                                            overlayBuffer)) {
                garbage->stagingBuffers.push_back(overlayBuffer);
                const m3d::Mat4 editMvp = m3d::multiply(viewProjection_, editObject->worldTransform);
                deviceFunctions->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                                   impl.editLinePipeline);
                deviceFunctions->vkCmdBindVertexBuffers(commandBuffer, 0, 1,
                                                        &overlayBuffer.buffer, &zeroOffset);
                deviceFunctions->vkCmdPushConstants(commandBuffer, impl.editLinePipelineLayout,
                                                    VK_SHADER_STAGE_VERTEX_BIT, 0,
                                                    static_cast<std::uint32_t>(sizeof(m3d::Mat4)),
                                                    editMvp.values.data());
                deviceFunctions->vkCmdDraw(commandBuffer,
                                           static_cast<std::uint32_t>(overlayVertices.size()),
                                           1, 0, 0);
                ++stats.editOverlayDraws;
            }
        }
    }

'''
    if anchor not in content:
        raise SystemExit('renderer: gizmo anchor not found')
    write(path, content.replace(anchor, overlay + anchor, 1))

print('Stage 5 Vulkan edit overlay patch prepared')
