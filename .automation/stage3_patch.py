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
    "struct VulkanRecordStats final {\n    bool recorded{false};\n    std::uint32_t meshDraws{0};\n    std::uint32_t outlineDraws{0};\n};",
    "struct VulkanRecordStats final {\n    bool recorded{false};\n    bool needsAnotherFrame{false};\n    std::uint32_t meshDraws{0};\n    std::uint32_t outlineDraws{0};\n};",
)

replace_once(
    "src/app/qt/vulkan_viewport.cpp",
    "    if (stats.outlineDraws > 0) {\n        recordedOutlineDrawCount_.fetch_add(stats.outlineDraws, std::memory_order_relaxed);\n    }\n}",
    "    if (stats.outlineDraws > 0) {\n        recordedOutlineDrawCount_.fetch_add(stats.outlineDraws, std::memory_order_relaxed);\n    }\n    if (stats.needsAnotherFrame) scheduleNextFrame();\n}",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "struct GpuMesh final {\n    GpuBuffer vertex;\n    GpuBuffer index;\n    std::uint32_t indexCount{0};\n    std::uint64_t contentHash{0};\n};\n\nstruct PickReadbackSlot final {",
    "struct GpuMesh final {\n    GpuBuffer vertex;\n    GpuBuffer index;\n    std::uint32_t indexCount{0};\n    std::uint64_t contentHash{0};\n};\n\nstruct FrameUploadGarbage final {\n    std::vector<GpuBuffer> stagingBuffers;\n    std::vector<GpuMesh> retiredMeshes;\n};\n\nstruct PickReadbackSlot final {",
)

old_host = '''[[nodiscard]] bool createHostBuffer(QVulkanFunctions* functions,
                                    QVulkanDeviceFunctions* deviceFunctions,
                                    VkPhysicalDevice physicalDevice,
                                    VkDevice device,
                                    VkBufferUsageFlags usage,
                                    const void* data,
                                    VkDeviceSize size,
                                    GpuBuffer& output) {
    output = {};
    if (!functions || !deviceFunctions || !physicalDevice || !device || !data || size == 0U) return false;

    auto bufferInfo = vkInfo<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (deviceFunctions->vkCreateBuffer(device, &bufferInfo, nullptr, &output.buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements{};
    deviceFunctions->vkGetBufferMemoryRequirements(device, output.buffer, &requirements);
    const auto memoryType = findMemoryType(functions, physicalDevice, requirements.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!memoryType) {
        destroyBuffer(deviceFunctions, device, output);
        return false;
    }

    auto allocation = vkInfo<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = *memoryType;
    if (deviceFunctions->vkAllocateMemory(device, &allocation, nullptr, &output.memory) != VK_SUCCESS) {
        destroyBuffer(deviceFunctions, device, output);
        return false;
    }
    if (deviceFunctions->vkBindBufferMemory(device, output.buffer, output.memory, 0) != VK_SUCCESS) {
        destroyBuffer(deviceFunctions, device, output);
        return false;
    }

    void* mapped = nullptr;
    if (deviceFunctions->vkMapMemory(device, output.memory, 0, size, 0, &mapped) != VK_SUCCESS || !mapped) {
        destroyBuffer(deviceFunctions, device, output);
        return false;
    }
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    deviceFunctions->vkUnmapMemory(device, output.memory);
    return true;
}
'''
new_host = '''[[nodiscard]] bool createBuffer(QVulkanFunctions* functions,
                                QVulkanDeviceFunctions* deviceFunctions,
                                VkPhysicalDevice physicalDevice,
                                VkDevice device,
                                VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags memoryProperties,
                                VkDeviceSize size,
                                GpuBuffer& output) {
    output = {};
    if (!functions || !deviceFunctions || !physicalDevice || !device || size == 0U) return false;

    auto bufferInfo = vkInfo<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (deviceFunctions->vkCreateBuffer(device, &bufferInfo, nullptr, &output.buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements{};
    deviceFunctions->vkGetBufferMemoryRequirements(device, output.buffer, &requirements);
    const auto memoryType = findMemoryType(functions, physicalDevice, requirements.memoryTypeBits,
                                           memoryProperties);
    if (!memoryType) {
        destroyBuffer(deviceFunctions, device, output);
        return false;
    }

    auto allocation = vkInfo<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = *memoryType;
    if (deviceFunctions->vkAllocateMemory(device, &allocation, nullptr, &output.memory) != VK_SUCCESS) {
        destroyBuffer(deviceFunctions, device, output);
        return false;
    }
    if (deviceFunctions->vkBindBufferMemory(device, output.buffer, output.memory, 0) != VK_SUCCESS) {
        destroyBuffer(deviceFunctions, device, output);
        return false;
    }
    return true;
}

[[nodiscard]] bool createHostBuffer(QVulkanFunctions* functions,
                                    QVulkanDeviceFunctions* deviceFunctions,
                                    VkPhysicalDevice physicalDevice,
                                    VkDevice device,
                                    VkBufferUsageFlags usage,
                                    const void* data,
                                    VkDeviceSize size,
                                    GpuBuffer& output) {
    if (!data || !createBuffer(functions, deviceFunctions, physicalDevice, device, usage,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               size, output)) return false;

    void* mapped = nullptr;
    if (deviceFunctions->vkMapMemory(device, output.memory, 0, size, 0, &mapped) != VK_SUCCESS || !mapped) {
        destroyBuffer(deviceFunctions, device, output);
        return false;
    }
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    deviceFunctions->vkUnmapMemory(device, output.memory);
    return true;
}
'''
replace_once("src/app/qt/vulkan_viewport_renderer.cpp", old_host, new_host)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "    std::unordered_map<m3d::ResourceId, GpuMesh, m3d::ResourceIdHash> meshCache;\n    PickTarget pickTarget;",
    "    std::unordered_map<m3d::ResourceId, GpuMesh, m3d::ResourceIdHash> meshCache;\n    std::vector<FrameUploadGarbage> frameGarbage;\n    PickTarget pickTarget;",
)

old_upload = '''bool uploadMesh(VulkanViewportRenderer::Impl& impl,
                const m3d::RenderMeshSnapshot& source,
                GpuMesh& mesh) {
    if (source.vertices.empty() || source.indices.empty()) return false;
    if (!createHostBuffer(impl.functions, impl.deviceFunctions, impl.physicalDevice, impl.device,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, source.vertices.data(),
                          static_cast<VkDeviceSize>(source.vertices.size() * sizeof(m3d::MeshVertex)), mesh.vertex)) return false;
    if (!createHostBuffer(impl.functions, impl.deviceFunctions, impl.physicalDevice, impl.device,
                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT, source.indices.data(),
                          static_cast<VkDeviceSize>(source.indices.size() * sizeof(std::uint32_t)), mesh.index)) {
        destroyBuffer(impl.deviceFunctions, impl.device, mesh.vertex);
        return false;
    }
    mesh.indexCount = static_cast<std::uint32_t>(source.indices.size());
    mesh.contentHash = source.contentHash;
    return true;
}

bool synchronizeMeshCache(VulkanViewportRenderer::Impl& impl,
                          const m3d::RenderSceneSnapshot& snapshot) {
    std::unordered_set<m3d::ResourceId, m3d::ResourceIdHash> live;
    for (const auto& source : snapshot.meshes()) {
        live.insert(source.id);
        auto found = impl.meshCache.find(source.id);
        if (found != impl.meshCache.end() && found->second.contentHash == source.contentHash) continue;
        if (found != impl.meshCache.end()) {
            destroyGpuMesh(impl, found->second);
            impl.meshCache.erase(found);
        }
        GpuMesh uploaded;
        if (!uploadMesh(impl, source, uploaded)) return false;
        impl.meshCache.emplace(source.id, std::move(uploaded));
    }
    for (auto iterator = impl.meshCache.begin(); iterator != impl.meshCache.end();) {
        if (!live.contains(iterator->first)) {
            destroyGpuMesh(impl, iterator->second);
            iterator = impl.meshCache.erase(iterator);
        } else {
            ++iterator;
        }
    }
    return true;
}
'''
new_upload = '''void destroyFrameGarbage(VulkanViewportRenderer::Impl& impl, FrameUploadGarbage& garbage) {
    for (auto& buffer : garbage.stagingBuffers) {
        destroyBuffer(impl.deviceFunctions, impl.device, buffer);
    }
    garbage.stagingBuffers.clear();
    for (auto& mesh : garbage.retiredMeshes) destroyGpuMesh(impl, mesh);
    garbage.retiredMeshes.clear();
}

void ensureFrameGarbage(VulkanViewportRenderer::Impl& impl, int framesInFlight) {
    const std::size_t required = static_cast<std::size_t>(std::max(framesInFlight, 1));
    if (impl.frameGarbage.size() == required) return;
    for (auto& garbage : impl.frameGarbage) destroyFrameGarbage(impl, garbage);
    impl.frameGarbage.clear();
    impl.frameGarbage.resize(required);
}

[[nodiscard]] FrameUploadGarbage* frameGarbageFor(VulkanViewportRenderer::Impl& impl,
                                                   int frameSlot) noexcept {
    if (frameSlot < 0 || static_cast<std::size_t>(frameSlot) >= impl.frameGarbage.size()) return nullptr;
    return &impl.frameGarbage[static_cast<std::size_t>(frameSlot)];
}

bool uploadDeviceLocalBuffer(VulkanViewportRenderer::Impl& impl,
                             VkCommandBuffer commandBuffer,
                             int frameSlot,
                             VkBufferUsageFlags finalUsage,
                             VkAccessFlags finalAccess,
                             const void* data,
                             VkDeviceSize size,
                             GpuBuffer& output) {
    auto* garbage = frameGarbageFor(impl, frameSlot);
    if (!garbage || !commandBuffer || !data || size == 0U) return false;

    GpuBuffer staging;
    if (!createHostBuffer(impl.functions, impl.deviceFunctions, impl.physicalDevice, impl.device,
                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT, data, size, staging)) return false;
    if (!createBuffer(impl.functions, impl.deviceFunctions, impl.physicalDevice, impl.device,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | finalUsage,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, size, output)) {
        destroyBuffer(impl.deviceFunctions, impl.device, staging);
        return false;
    }

    VkBufferCopy copy{};
    copy.size = size;
    impl.deviceFunctions->vkCmdCopyBuffer(commandBuffer, staging.buffer, output.buffer, 1, &copy);

    auto barrier = vkInfo<VkBufferMemoryBarrier>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = finalAccess;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = output.buffer;
    barrier.offset = 0;
    barrier.size = size;
    impl.deviceFunctions->vkCmdPipelineBarrier(commandBuffer,
                                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                                                0, 0, nullptr, 1, &barrier, 0, nullptr);
    garbage->stagingBuffers.push_back(std::move(staging));
    return true;
}

bool uploadMesh(VulkanViewportRenderer::Impl& impl,
                VkCommandBuffer commandBuffer,
                int frameSlot,
                const m3d::RenderMeshSnapshot& source,
                GpuMesh& mesh) {
    if (source.vertices.empty() || source.indices.empty()) return false;
    const VkDeviceSize vertexBytes =
        static_cast<VkDeviceSize>(source.vertices.size() * sizeof(m3d::MeshVertex));
    const VkDeviceSize indexBytes =
        static_cast<VkDeviceSize>(source.indices.size() * sizeof(std::uint32_t));
    if (!uploadDeviceLocalBuffer(impl, commandBuffer, frameSlot,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                 VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
                                 source.vertices.data(), vertexBytes, mesh.vertex)) return false;
    if (!uploadDeviceLocalBuffer(impl, commandBuffer, frameSlot,
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 VK_ACCESS_INDEX_READ_BIT,
                                 source.indices.data(), indexBytes, mesh.index)) {
        auto* garbage = frameGarbageFor(impl, frameSlot);
        if (garbage) garbage->retiredMeshes.push_back(std::move(mesh));
        return false;
    }
    mesh.indexCount = static_cast<std::uint32_t>(source.indices.size());
    mesh.contentHash = source.contentHash;
    return true;
}

bool synchronizeMeshCache(VulkanViewportRenderer::Impl& impl,
                          const m3d::RenderSceneSnapshot& snapshot,
                          VkCommandBuffer commandBuffer,
                          int frameSlot) {
    auto* garbage = frameGarbageFor(impl, frameSlot);
    if (!garbage) return false;

    std::unordered_set<m3d::ResourceId, m3d::ResourceIdHash> live;
    for (const auto& source : snapshot.meshes()) {
        live.insert(source.id);
        auto found = impl.meshCache.find(source.id);
        if (found != impl.meshCache.end() && found->second.contentHash == source.contentHash) continue;

        GpuMesh uploaded;
        if (!uploadMesh(impl, commandBuffer, frameSlot, source, uploaded)) return false;
        if (found != impl.meshCache.end()) {
            garbage->retiredMeshes.push_back(std::move(found->second));
            found->second = std::move(uploaded);
        } else {
            impl.meshCache.emplace(source.id, std::move(uploaded));
        }
    }
    for (auto iterator = impl.meshCache.begin(); iterator != impl.meshCache.end();) {
        if (!live.contains(iterator->first)) {
            garbage->retiredMeshes.push_back(std::move(iterator->second));
            iterator = impl.meshCache.erase(iterator);
        } else {
            ++iterator;
        }
    }
    return true;
}
'''
replace_once("src/app/qt/vulkan_viewport_renderer.cpp", old_upload, new_upload)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "    destroyPickTarget(impl);\n    for (auto& [id, mesh] : impl.meshCache) {",
    "    destroyPickTarget(impl);\n    for (auto& garbage : impl.frameGarbage) destroyFrameGarbage(impl, garbage);\n    impl.frameGarbage.clear();\n    for (auto& [id, mesh] : impl.meshCache) {",
)

old_pick_prefix = '''    const auto stateInfo = window->graphicsStateInfo();
    if (!impl.pickTarget.readbacks.empty()) {
        result = consumeReadback(impl, stateInfo.currentFrameSlot);
    }
    if (!pendingPickPixel_) {
        result.waitingForReadback = result.waitingForReadback || anyPendingReadback(impl.pickTarget);
        return result;
    }

    auto* rendererInterface = window->rendererInterface();
    auto* swapchain = rendererInterface
        ? static_cast<QRhiSwapChain*>(rendererInterface->getResource(
              window, QSGRendererInterface::RhiSwapchainResource))
        : nullptr;
    if (!rendererInterface || rendererInterface->graphicsApi() != QSGRendererInterface::Vulkan ||
        !swapchain || impl.sampleCount != std::max(1, swapchain->sampleCount()) ||
        !meshCacheReady(impl, snapshot_)) {
        result.waitingForReadback = true;
        return result;
    }
'''
new_pick_prefix = '''    const auto stateInfo = window->graphicsStateInfo();
    ensureFrameGarbage(impl, stateInfo.framesInFlight);
    auto* frameGarbage = frameGarbageFor(impl, stateInfo.currentFrameSlot);
    if (!frameGarbage) {
        result.waitingForReadback = pendingPickPixel_.has_value();
        return result;
    }
    destroyFrameGarbage(impl, *frameGarbage);

    if (!impl.pickTarget.readbacks.empty()) {
        result = consumeReadback(impl, stateInfo.currentFrameSlot);
    }

    auto* rendererInterface = window->rendererInterface();
    auto* swapchain = rendererInterface
        ? static_cast<QRhiSwapChain*>(rendererInterface->getResource(
              window, QSGRendererInterface::RhiSwapchainResource))
        : nullptr;
    if (!rendererInterface || rendererInterface->graphicsApi() != QSGRendererInterface::Vulkan ||
        !swapchain || impl.sampleCount != std::max(1, swapchain->sampleCount())) {
        result.waitingForReadback = pendingPickPixel_.has_value() || anyPendingReadback(impl.pickTarget);
        return result;
    }

    if (!meshCacheReady(impl, snapshot_)) {
        window->beginExternalCommands();
        auto* uploadCommandBufferHandle = static_cast<VkCommandBuffer*>(rendererInterface->getResource(
            window, QSGRendererInterface::CommandListResource));
        if (!uploadCommandBufferHandle || !*uploadCommandBufferHandle ||
            !synchronizeMeshCache(impl, snapshot_, *uploadCommandBufferHandle,
                                  stateInfo.currentFrameSlot)) {
            window->endExternalCommands();
            result.waitingForReadback = true;
            return result;
        }
        window->endExternalCommands();
    }

    if (!pendingPickPixel_) {
        result.waitingForReadback = result.waitingForReadback || anyPendingReadback(impl.pickTarget);
        return result;
    }
'''
replace_once("src/app/qt/vulkan_viewport_renderer.cpp", old_pick_prefix, new_pick_prefix)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "    if (!synchronizeMeshCache(impl, snapshot_)) return stats;\n\n    const qreal dpr = window->devicePixelRatio();",
    "    if (!meshCacheReady(impl, snapshot_)) {\n        stats.needsAnotherFrame = true;\n        return stats;\n    }\n\n    const qreal dpr = window->devicePixelRatio();",
)

print("Device-local mesh upload transformation applied successfully")
