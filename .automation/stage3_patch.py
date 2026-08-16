from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "src/app/qt/vulkan_viewport_renderer.hpp",
    "struct VulkanRecordStats final {\n    bool recorded{false};\n    std::uint32_t meshDraws{0};\n};",
    "struct VulkanRecordStats final {\n    bool recorded{false};\n    std::uint32_t meshDraws{0};\n    std::uint32_t outlineDraws{0};\n};",
)

replace_once(
    "src/app/qt/vulkan_viewport.hpp",
    "    [[nodiscard]] std::uint64_t recordedMeshDrawCount() const noexcept {\n        return recordedMeshDrawCount_.load(std::memory_order_relaxed);\n    }\n    [[nodiscard]] std::uint64_t completedPickCount() const noexcept {",
    "    [[nodiscard]] std::uint64_t recordedMeshDrawCount() const noexcept {\n        return recordedMeshDrawCount_.load(std::memory_order_relaxed);\n    }\n    [[nodiscard]] std::uint64_t recordedOutlineDrawCount() const noexcept {\n        return recordedOutlineDrawCount_.load(std::memory_order_relaxed);\n    }\n    [[nodiscard]] std::uint64_t completedPickCount() const noexcept {",
)

replace_once(
    "src/app/qt/vulkan_viewport.hpp",
    "    std::atomic_uint64_t recordedFrameCount_{0};\n    std::atomic_uint64_t recordedMeshDrawCount_{0};\n    std::atomic_uint64_t completedPickCount_{0};",
    "    std::atomic_uint64_t recordedFrameCount_{0};\n    std::atomic_uint64_t recordedMeshDrawCount_{0};\n    std::atomic_uint64_t recordedOutlineDrawCount_{0};\n    std::atomic_uint64_t completedPickCount_{0};",
)

replace_once(
    "src/app/qt/vulkan_viewport.cpp",
    "    if (stats.meshDraws > 0) {\n        recordedMeshDrawCount_.fetch_add(stats.meshDraws, std::memory_order_relaxed);\n    }\n}",
    "    if (stats.meshDraws > 0) {\n        recordedMeshDrawCount_.fetch_add(stats.meshDraws, std::memory_order_relaxed);\n    }\n    if (stats.outlineDraws > 0) {\n        recordedOutlineDrawCount_.fetch_add(stats.outlineDraws, std::memory_order_relaxed);\n    }\n}",
)

replace_once(
    "src/app/main.cpp",
    "                                viewport->recordedMeshDrawCount() > 0 &&\n                                viewport->completedPickCount() > 0 &&",
    "                                viewport->recordedMeshDrawCount() > 0 &&\n                                viewport->recordedOutlineDrawCount() > 0 &&\n                                viewport->completedPickCount() > 0 &&",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "[[nodiscard]] m3d::PickId decodePickColor(const std::array<std::uint8_t, 4>& value) noexcept {\n    return static_cast<m3d::PickId>(value[0]) |\n           (static_cast<m3d::PickId>(value[1]) << 8U) |\n           (static_cast<m3d::PickId>(value[2]) << 16U) |\n           (static_cast<m3d::PickId>(value[3]) << 24U);\n}\n",
    "[[nodiscard]] m3d::PickId decodePickColor(const std::array<std::uint8_t, 4>& value) noexcept {\n    return static_cast<m3d::PickId>(value[0]) |\n           (static_cast<m3d::PickId>(value[1]) << 8U) |\n           (static_cast<m3d::PickId>(value[2]) << 16U) |\n           (static_cast<m3d::PickId>(value[3]) << 24U);\n}\n\n[[nodiscard]] m3d::Mat4 scaleAroundBounds(const m3d::Mat4& world,\n                                          const m3d::Bounds3& bounds,\n                                          float scale) noexcept {\n    const m3d::Vec3 center{\n        (bounds.min.x + bounds.max.x) * 0.5F,\n        (bounds.min.y + bounds.max.y) * 0.5F,\n        (bounds.min.z + bounds.max.z) * 0.5F,\n    };\n    m3d::Mat4 local = m3d::Mat4::identity();\n    local.at(0, 0) = scale;\n    local.at(1, 1) = scale;\n    local.at(2, 2) = scale;\n    local.at(0, 3) = center.x * (1.0F - scale);\n    local.at(1, 3) = center.y * (1.0F - scale);\n    local.at(2, 3) = center.z * (1.0F - scale);\n    return m3d::multiply(world, local);\n}\n",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "    VkPipelineLayout meshPipelineLayout{VK_NULL_HANDLE};\n    VkPipeline meshPipeline{VK_NULL_HANDLE};\n    std::unordered_map<m3d::ResourceId, GpuMesh, m3d::ResourceIdHash> meshCache;",
    "    VkPipelineLayout meshPipelineLayout{VK_NULL_HANDLE};\n    VkPipeline meshPipeline{VK_NULL_HANDLE};\n    VkPipelineLayout outlinePipelineLayout{VK_NULL_HANDLE};\n    VkPipeline outlinePipeline{VK_NULL_HANDLE};\n    std::unordered_map<m3d::ResourceId, GpuMesh, m3d::ResourceIdHash> meshCache;",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "bool createPipeline(VulkanViewportRenderer::Impl& impl,\n                    VkRenderPass renderPass,\n                    VkSampleCountFlagBits sampleCount,\n                    bool linePipeline,\n                    VkPipelineLayout& layout,\n                    VkPipeline& pipeline) {",
    "bool createPipeline(VulkanViewportRenderer::Impl& impl,\n                    VkRenderPass renderPass,\n                    VkSampleCountFlagBits sampleCount,\n                    bool linePipeline,\n                    VkPipelineLayout& layout,\n                    VkPipeline& pipeline,\n                    bool depthWrite = true,\n                    VkCullModeFlags cullMode = VK_CULL_MODE_NONE) {",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "    PipelineCommonState common(!linePipeline, sampleCount);\n\n    auto pipelineInfo = vkInfo<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);",
    "    PipelineCommonState common(!linePipeline, sampleCount);\n    common.raster.cullMode = cullMode;\n    if (!linePipeline) {\n        common.depthStencil.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;\n    }\n\n    auto pipelineInfo = vkInfo<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "    if (impl.meshPipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.meshPipeline, nullptr);\n    if (impl.meshPipelineLayout) impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, impl.meshPipelineLayout, nullptr);",
    "    if (impl.outlinePipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.outlinePipeline, nullptr);\n    if (impl.outlinePipelineLayout) impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, impl.outlinePipelineLayout, nullptr);\n    if (impl.meshPipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.meshPipeline, nullptr);\n    if (impl.meshPipelineLayout) impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, impl.meshPipelineLayout, nullptr);",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "            !createPipeline(impl, impl.renderPass, vulkanSampleCount(impl.sampleCount), false,\n                            impl.meshPipelineLayout, impl.meshPipeline)) {",
    "            !createPipeline(impl, impl.renderPass, vulkanSampleCount(impl.sampleCount), false,\n                            impl.meshPipelineLayout, impl.meshPipeline) ||\n            !createPipeline(impl, impl.renderPass, vulkanSampleCount(impl.sampleCount), false,\n                            impl.outlinePipelineLayout, impl.outlinePipeline, false,\n                            VK_CULL_MODE_FRONT_BIT)) {",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "    deviceFunctions->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.meshPipeline);\n    for (const auto& object : snapshot_.objects()) {",
    "    constexpr float outlineScale = 1.035F;\n    constexpr std::array<float, 4> outlineColor{1.0F, 0.48F, 0.08F, 1.0F};\n    deviceFunctions->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.outlinePipeline);\n    for (const auto& object : snapshot_.objects()) {\n        if (!object.visible || !object.selected || object.type != m3d::ObjectType::Mesh ||\n            !object.meshResource) continue;\n        const auto mesh = impl.meshCache.find(*object.meshResource);\n        const auto* geometry = snapshot_.findMesh(*object.meshResource);\n        if (mesh == impl.meshCache.end() || !geometry || !geometry->bounds) continue;\n        const auto outlineWorld = scaleAroundBounds(object.worldTransform, *geometry->bounds, outlineScale);\n        const MeshPushConstants push{\n            m3d::multiply(viewProjection_, outlineWorld),\n            outlineColor,\n        };\n        deviceFunctions->vkCmdPushConstants(commandBuffer, impl.outlinePipelineLayout,\n                                            VK_SHADER_STAGE_VERTEX_BIT, 0,\n                                            static_cast<std::uint32_t>(sizeof(push)), &push);\n        deviceFunctions->vkCmdBindVertexBuffers(commandBuffer, 0, 1,\n                                                &mesh->second.vertex.buffer, &zeroOffset);\n        deviceFunctions->vkCmdBindIndexBuffer(commandBuffer, mesh->second.index.buffer,\n                                              0, VK_INDEX_TYPE_UINT32);\n        deviceFunctions->vkCmdDrawIndexed(commandBuffer, mesh->second.indexCount, 1, 0, 0, 0);\n        ++stats.outlineDraws;\n    }\n\n    deviceFunctions->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.meshPipeline);\n    for (const auto& object : snapshot_.objects()) {",
)

print("Stage 3 selection outline transformation applied successfully")
