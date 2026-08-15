#include "vulkan_viewport_renderer.hpp"

#include "vulkan_shader_library.hpp"

#include <QByteArray>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <rhi/qrhi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

template <typename T>
[[nodiscard]] T vkInfo(VkStructureType type) noexcept {
    T value{};
    value.sType = type;
    return value;
}

[[nodiscard]] VkSampleCountFlagBits vulkanSampleCount(int sampleCount) noexcept {
    switch (sampleCount) {
    case 2: return VK_SAMPLE_COUNT_2_BIT;
    case 4: return VK_SAMPLE_COUNT_4_BIT;
    case 8: return VK_SAMPLE_COUNT_8_BIT;
    case 16: return VK_SAMPLE_COUNT_16_BIT;
    case 32: return VK_SAMPLE_COUNT_32_BIT;
    case 64: return VK_SAMPLE_COUNT_64_BIT;
    default: return VK_SAMPLE_COUNT_1_BIT;
    }
}

struct LineVertex final {
    float x;
    float y;
    float z;
    float r;
    float g;
    float b;
    float a;
};

struct MeshPushConstants final {
    m3d::Mat4 modelViewProjection;
    std::array<float, 4> color;
};

static_assert(sizeof(MeshPushConstants) == 80U);

struct GpuBuffer final {
    VkBuffer buffer{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
};

struct GpuMesh final {
    GpuBuffer vertex;
    GpuBuffer index;
    std::uint32_t indexCount{0};
    std::uint64_t contentHash{0};
};

[[nodiscard]] std::vector<LineVertex> makeGridVertices() {
    std::vector<LineVertex> vertices;
    constexpr int extent = 10;
    constexpr float minor = 0.18F;
    constexpr float major = 0.30F;
    vertices.reserve(4U * static_cast<std::size_t>(extent * 2 + 1) + 6U);

    for (int coordinate = -extent; coordinate <= extent; ++coordinate) {
        if (coordinate == 0) continue;
        const float value = static_cast<float>(coordinate);
        const float shade = coordinate % 5 == 0 ? major : minor;
        vertices.push_back({value, 0.0F, -static_cast<float>(extent), shade, shade, shade, 1.0F});
        vertices.push_back({value, 0.0F, static_cast<float>(extent), shade, shade, shade, 1.0F});
        vertices.push_back({-static_cast<float>(extent), 0.0F, value, shade, shade, shade, 1.0F});
        vertices.push_back({static_cast<float>(extent), 0.0F, value, shade, shade, shade, 1.0F});
    }

    vertices.push_back({-static_cast<float>(extent), 0.0F, 0.0F, 0.82F, 0.18F, 0.18F, 1.0F});
    vertices.push_back({static_cast<float>(extent), 0.0F, 0.0F, 0.82F, 0.18F, 0.18F, 1.0F});
    vertices.push_back({0.0F, 0.0F, -static_cast<float>(extent), 0.18F, 0.38F, 0.90F, 1.0F});
    vertices.push_back({0.0F, 0.0F, static_cast<float>(extent), 0.18F, 0.38F, 0.90F, 1.0F});
    vertices.push_back({0.0F, 0.0F, 0.0F, 0.20F, 0.82F, 0.30F, 1.0F});
    vertices.push_back({0.0F, 3.0F, 0.0F, 0.20F, 0.82F, 0.30F, 1.0F});
    return vertices;
}

[[nodiscard]] std::optional<std::uint32_t> findMemoryType(QVulkanFunctions* functions,
                                                           VkPhysicalDevice physicalDevice,
                                                           std::uint32_t typeBits,
                                                           VkMemoryPropertyFlags required) {
    VkPhysicalDeviceMemoryProperties properties{};
    functions->vkGetPhysicalDeviceMemoryProperties(physicalDevice, &properties);
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        const bool supported = (typeBits & (1U << index)) != 0U;
        if (supported && (properties.memoryTypes[index].propertyFlags & required) == required) return index;
    }
    return std::nullopt;
}

void destroyBuffer(QVulkanDeviceFunctions* functions, VkDevice device, GpuBuffer& buffer) {
    if (functions && device) {
        if (buffer.buffer) functions->vkDestroyBuffer(device, buffer.buffer, nullptr);
        if (buffer.memory) functions->vkFreeMemory(device, buffer.memory, nullptr);
    }
    buffer = {};
}

[[nodiscard]] bool createHostBuffer(QVulkanFunctions* functions,
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

[[nodiscard]] VkShaderModule createShaderModule(QVulkanDeviceFunctions* functions,
                                                VkDevice device,
                                                const QByteArray& bytecode) {
    if (!functions || !device || bytecode.isEmpty() ||
        bytecode.size() % static_cast<qsizetype>(sizeof(std::uint32_t)) != 0) return VK_NULL_HANDLE;
    std::vector<std::uint32_t> words(static_cast<std::size_t>(bytecode.size()) / sizeof(std::uint32_t));
    std::memcpy(words.data(), bytecode.constData(), static_cast<std::size_t>(bytecode.size()));
    auto info = vkInfo<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    info.codeSize = static_cast<std::size_t>(bytecode.size());
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    return functions->vkCreateShaderModule(device, &info, nullptr, &module) == VK_SUCCESS
        ? module : VK_NULL_HANDLE;
}

VkPipelineColorBlendAttachmentState opaqueBlendState() {
    VkPipelineColorBlendAttachmentState state{};
    state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    return state;
}

struct PipelineCommonState final {
    VkPipelineViewportStateCreateInfo viewport{};
    VkPipelineRasterizationStateCreateInfo raster{};
    VkPipelineMultisampleStateCreateInfo multisample{};
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    VkPipelineColorBlendAttachmentState blendAttachment{};
    VkPipelineColorBlendStateCreateInfo blend{};
    std::array<VkDynamicState, 2> dynamicStates{};
    VkPipelineDynamicStateCreateInfo dynamic{};

    PipelineCommonState(bool depthEnabled, VkSampleCountFlagBits sampleCount) {
        viewport = vkInfo<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;

        raster = vkInfo<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0F;

        multisample = vkInfo<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        multisample.rasterizationSamples = sampleCount;

        depthStencil = vkInfo<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
        depthStencil.depthTestEnable = depthEnabled ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = depthEnabled ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        blendAttachment = opaqueBlendState();
        blend = vkInfo<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;

        dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        dynamic = vkInfo<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamic.pDynamicStates = dynamicStates.data();
    }
};

} // namespace

struct VulkanViewportRenderer::Impl final {
    VkDevice device{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkRenderPass renderPass{VK_NULL_HANDLE};
    QVulkanFunctions* functions{nullptr};
    QVulkanDeviceFunctions* deviceFunctions{nullptr};
    int sampleCount{1};
    GpuBuffer gridBuffer;
    std::uint32_t gridVertexCount{0};
    VkPipelineLayout linePipelineLayout{VK_NULL_HANDLE};
    VkPipeline linePipeline{VK_NULL_HANDLE};
    VkPipelineLayout meshPipelineLayout{VK_NULL_HANDLE};
    VkPipeline meshPipeline{VK_NULL_HANDLE};
    std::unordered_map<m3d::ResourceId, GpuMesh, m3d::ResourceIdHash> meshCache;
};

VulkanViewportRenderer::VulkanViewportRenderer() : impl_(std::make_unique<Impl>()) {}
VulkanViewportRenderer::~VulkanViewportRenderer() = default;

void VulkanViewportRenderer::synchronize(QRect viewportPixels, m3d::RenderSceneSnapshot snapshot,
                                         m3d::Mat4 viewProjection) {
    viewportPixels_ = viewportPixels;
    snapshot_ = std::move(snapshot);
    viewProjection_ = viewProjection;
}

namespace {

bool createGridResources(VulkanViewportRenderer::Impl& impl) {
    const auto vertices = makeGridVertices();
    if (!createHostBuffer(impl.functions, impl.deviceFunctions, impl.physicalDevice, impl.device,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data(),
                          static_cast<VkDeviceSize>(vertices.size() * sizeof(LineVertex)), impl.gridBuffer)) return false;
    impl.gridVertexCount = static_cast<std::uint32_t>(vertices.size());
    return true;
}

bool createLinePipeline(VulkanViewportRenderer::Impl& impl) {
    QString error;
    const auto vertexCode = VulkanShaderLibrary::viewportLineVertexSpirv(&error);
    const auto fragmentCode = VulkanShaderLibrary::viewportLineFragmentSpirv(&error);
    const VkShaderModule vertexModule = createShaderModule(impl.deviceFunctions, impl.device, vertexCode);
    const VkShaderModule fragmentModule = createShaderModule(impl.deviceFunctions, impl.device, fragmentCode);
    if (!vertexModule || !fragmentModule) {
        if (vertexModule) impl.deviceFunctions->vkDestroyShaderModule(impl.device, vertexModule, nullptr);
        if (fragmentModule) impl.deviceFunctions->vkDestroyShaderModule(impl.device, fragmentModule, nullptr);
        return false;
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size = static_cast<std::uint32_t>(sizeof(m3d::Mat4));
    auto layoutInfo = vkInfo<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (impl.deviceFunctions->vkCreatePipelineLayout(impl.device, &layoutInfo, nullptr,
                                                      &impl.linePipelineLayout) != VK_SUCCESS) {
        impl.deviceFunctions->vkDestroyShaderModule(impl.device, vertexModule, nullptr);
        impl.deviceFunctions->vkDestroyShaderModule(impl.device, fragmentModule, nullptr);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = static_cast<std::uint32_t>(sizeof(LineVertex));
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 2> attributes{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<std::uint32_t>(offsetof(LineVertex, x))};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<std::uint32_t>(offsetof(LineVertex, r))};

    auto vertexInput = vkInfo<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();
    auto assembly = vkInfo<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    PipelineCommonState common(false, vulkanSampleCount(impl.sampleCount));

    auto pipeline = vkInfo<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    pipeline.stageCount = static_cast<std::uint32_t>(stages.size());
    pipeline.pStages = stages.data();
    pipeline.pVertexInputState = &vertexInput;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &common.viewport;
    pipeline.pRasterizationState = &common.raster;
    pipeline.pMultisampleState = &common.multisample;
    pipeline.pDepthStencilState = &common.depthStencil;
    pipeline.pColorBlendState = &common.blend;
    pipeline.pDynamicState = &common.dynamic;
    pipeline.layout = impl.linePipelineLayout;
    pipeline.renderPass = impl.renderPass;
    pipeline.subpass = 0;
    const bool success = impl.deviceFunctions->vkCreateGraphicsPipelines(
        impl.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &impl.linePipeline) == VK_SUCCESS;
    impl.deviceFunctions->vkDestroyShaderModule(impl.device, vertexModule, nullptr);
    impl.deviceFunctions->vkDestroyShaderModule(impl.device, fragmentModule, nullptr);
    return success;
}

bool createMeshPipeline(VulkanViewportRenderer::Impl& impl) {
    QString error;
    const auto vertexCode = VulkanShaderLibrary::viewportMeshVertexSpirv(&error);
    const auto fragmentCode = VulkanShaderLibrary::viewportMeshFragmentSpirv(&error);
    const VkShaderModule vertexModule = createShaderModule(impl.deviceFunctions, impl.device, vertexCode);
    const VkShaderModule fragmentModule = createShaderModule(impl.deviceFunctions, impl.device, fragmentCode);
    if (!vertexModule || !fragmentModule) {
        if (vertexModule) impl.deviceFunctions->vkDestroyShaderModule(impl.device, vertexModule, nullptr);
        if (fragmentModule) impl.deviceFunctions->vkDestroyShaderModule(impl.device, fragmentModule, nullptr);
        return false;
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size = static_cast<std::uint32_t>(sizeof(MeshPushConstants));
    auto layoutInfo = vkInfo<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (impl.deviceFunctions->vkCreatePipelineLayout(impl.device, &layoutInfo, nullptr,
                                                      &impl.meshPipelineLayout) != VK_SUCCESS) {
        impl.deviceFunctions->vkDestroyShaderModule(impl.device, vertexModule, nullptr);
        impl.deviceFunctions->vkDestroyShaderModule(impl.device, fragmentModule, nullptr);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = static_cast<std::uint32_t>(sizeof(m3d::MeshVertex));
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attribute{};
    attribute.location = 0;
    attribute.binding = 0;
    attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute.offset = static_cast<std::uint32_t>(offsetof(m3d::MeshVertex, position));
    auto vertexInput = vkInfo<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;
    auto assembly = vkInfo<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PipelineCommonState common(true, vulkanSampleCount(impl.sampleCount));

    auto pipeline = vkInfo<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    pipeline.stageCount = static_cast<std::uint32_t>(stages.size());
    pipeline.pStages = stages.data();
    pipeline.pVertexInputState = &vertexInput;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &common.viewport;
    pipeline.pRasterizationState = &common.raster;
    pipeline.pMultisampleState = &common.multisample;
    pipeline.pDepthStencilState = &common.depthStencil;
    pipeline.pColorBlendState = &common.blend;
    pipeline.pDynamicState = &common.dynamic;
    pipeline.layout = impl.meshPipelineLayout;
    pipeline.renderPass = impl.renderPass;
    pipeline.subpass = 0;
    const bool success = impl.deviceFunctions->vkCreateGraphicsPipelines(
        impl.device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &impl.meshPipeline) == VK_SUCCESS;
    impl.deviceFunctions->vkDestroyShaderModule(impl.device, vertexModule, nullptr);
    impl.deviceFunctions->vkDestroyShaderModule(impl.device, fragmentModule, nullptr);
    return success;
}

void destroyGpuMesh(VulkanViewportRenderer::Impl& impl, GpuMesh& mesh) {
    destroyBuffer(impl.deviceFunctions, impl.device, mesh.vertex);
    destroyBuffer(impl.deviceFunctions, impl.device, mesh.index);
}

bool uploadMesh(VulkanViewportRenderer::Impl& impl,
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

} // namespace

void VulkanViewportRenderer::release() {
    auto& impl = *impl_;
    if (!impl.deviceFunctions || !impl.device) {
        impl = Impl{};
        return;
    }
    for (auto& [id, mesh] : impl.meshCache) {
        (void)id;
        destroyGpuMesh(impl, mesh);
    }
    impl.meshCache.clear();
    if (impl.meshPipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.meshPipeline, nullptr);
    if (impl.meshPipelineLayout) impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, impl.meshPipelineLayout, nullptr);
    if (impl.linePipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.linePipeline, nullptr);
    if (impl.linePipelineLayout) impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, impl.linePipelineLayout, nullptr);
    destroyBuffer(impl.deviceFunctions, impl.device, impl.gridBuffer);
    impl = Impl{};
}

VulkanRecordStats VulkanViewportRenderer::record(QQuickWindow* window) {
    VulkanRecordStats stats;
    if (!window || viewportPixels_.isEmpty()) return stats;
    auto* rendererInterface = window->rendererInterface();
    if (!rendererInterface || rendererInterface->graphicsApi() != QSGRendererInterface::Vulkan) return stats;

    auto* instance = static_cast<QVulkanInstance*>(rendererInterface->getResource(window, QSGRendererInterface::VulkanInstanceResource));
    auto* deviceHandle = static_cast<VkDevice*>(rendererInterface->getResource(window, QSGRendererInterface::DeviceResource));
    auto* physicalHandle = static_cast<VkPhysicalDevice*>(rendererInterface->getResource(window, QSGRendererInterface::PhysicalDeviceResource));
    auto* renderPassHandle = static_cast<VkRenderPass*>(rendererInterface->getResource(window, QSGRendererInterface::RenderPassResource));
    auto* swapchain = static_cast<QRhiSwapChain*>(rendererInterface->getResource(window, QSGRendererInterface::RhiSwapchainResource));
    if (!instance || !deviceHandle || !physicalHandle || !renderPassHandle || !swapchain ||
        !*deviceHandle || !*physicalHandle || !*renderPassHandle) return stats;

    auto* deviceFunctions = instance->deviceFunctions(*deviceHandle);
    auto* functions = instance->functions();
    if (!deviceFunctions || !functions) return stats;

    const int effectiveSampleCount = std::max(1, swapchain->sampleCount());
    auto& impl = *impl_;
    const bool changedDevice = impl.device &&
        (impl.device != *deviceHandle || impl.renderPass != *renderPassHandle ||
         impl.sampleCount != effectiveSampleCount);
    if (changedDevice) release();
    if (!impl.device) {
        impl.device = *deviceHandle;
        impl.physicalDevice = *physicalHandle;
        impl.renderPass = *renderPassHandle;
        impl.deviceFunctions = deviceFunctions;
        impl.functions = functions;
        impl.sampleCount = effectiveSampleCount;
        qInfo() << "Vulkan viewport effective MSAA samples:" << impl.sampleCount;
        if (!createGridResources(impl) || !createLinePipeline(impl) || !createMeshPipeline(impl)) {
            release();
            return stats;
        }
    }
    if (!synchronizeMeshCache(impl, snapshot_)) return stats;

    const qreal dpr = window->devicePixelRatio();
    const QSize framebufferSize{
        static_cast<int>(std::ceil(static_cast<double>(window->width()) * dpr)),
        static_cast<int>(std::ceil(static_cast<double>(window->height()) * dpr))};
    const QRect drawArea = viewportPixels_.intersected(QRect(QPoint(0, 0), framebufferSize));
    if (drawArea.isEmpty()) return stats;

    std::array<VkClearAttachment, 2> clearAttachments{};
    clearAttachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    clearAttachments[0].colorAttachment = 0;
    clearAttachments[0].clearValue.color.float32[0] = 0.025F;
    clearAttachments[0].clearValue.color.float32[1] = 0.032F;
    clearAttachments[0].clearValue.color.float32[2] = 0.045F;
    clearAttachments[0].clearValue.color.float32[3] = 1.0F;
    clearAttachments[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    clearAttachments[1].clearValue.depthStencil.depth = 1.0F;
    clearAttachments[1].clearValue.depthStencil.stencil = 0;

    VkClearRect clearRect{};
    clearRect.rect.offset.x = drawArea.x();
    clearRect.rect.offset.y = drawArea.y();
    clearRect.rect.extent.width = static_cast<std::uint32_t>(drawArea.width());
    clearRect.rect.extent.height = static_cast<std::uint32_t>(drawArea.height());
    clearRect.layerCount = 1;

    window->beginExternalCommands();
    auto* commandBufferHandle = static_cast<VkCommandBuffer*>(rendererInterface->getResource(window, QSGRendererInterface::CommandListResource));
    if (!commandBufferHandle || !*commandBufferHandle) {
        window->endExternalCommands();
        return stats;
    }
    const VkCommandBuffer commandBuffer = *commandBufferHandle;
    deviceFunctions->vkCmdClearAttachments(commandBuffer,
                                            static_cast<std::uint32_t>(clearAttachments.size()),
                                            clearAttachments.data(), 1, &clearRect);

    VkViewport viewport{};
    viewport.x = static_cast<float>(drawArea.x());
    viewport.y = static_cast<float>(drawArea.y());
    viewport.width = static_cast<float>(drawArea.width());
    viewport.height = static_cast<float>(drawArea.height());
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    VkRect2D scissor{};
    scissor.offset = clearRect.rect.offset;
    scissor.extent = clearRect.rect.extent;
    deviceFunctions->vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    deviceFunctions->vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    deviceFunctions->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.linePipeline);
    const VkDeviceSize zeroOffset = 0;
    deviceFunctions->vkCmdBindVertexBuffers(commandBuffer, 0, 1, &impl.gridBuffer.buffer, &zeroOffset);
    deviceFunctions->vkCmdPushConstants(commandBuffer, impl.linePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                        0, static_cast<std::uint32_t>(sizeof(m3d::Mat4)), viewProjection_.values.data());
    deviceFunctions->vkCmdDraw(commandBuffer, impl.gridVertexCount, 1, 0, 0);

    deviceFunctions->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.meshPipeline);
    for (const auto& object : snapshot_.objects()) {
        if (!object.visible || object.type != m3d::ObjectType::Mesh || !object.meshResource) continue;
        const auto mesh = impl.meshCache.find(*object.meshResource);
        if (mesh == impl.meshCache.end()) continue;
        const m3d::Mat4 mvp = m3d::multiply(viewProjection_, object.worldTransform);
        const std::array<float, 4> color = object.selected
            ? std::array<float, 4>{0.93F, 0.55F, 0.18F, 1.0F}
            : std::array<float, 4>{0.62F, 0.66F, 0.72F, 1.0F};
        const MeshPushConstants push{mvp, color};
        deviceFunctions->vkCmdPushConstants(commandBuffer, impl.meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                            0, static_cast<std::uint32_t>(sizeof(push)), &push);
        deviceFunctions->vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh->second.vertex.buffer, &zeroOffset);
        deviceFunctions->vkCmdBindIndexBuffer(commandBuffer, mesh->second.index.buffer, 0, VK_INDEX_TYPE_UINT32);
        deviceFunctions->vkCmdDrawIndexed(commandBuffer, mesh->second.indexCount, 1, 0, 0, 0);
        ++stats.meshDraws;
    }

    window->endExternalCommands();
    stats.recorded = true;
    return stats;
}
