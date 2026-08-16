#include "vulkan_viewport_renderer.hpp"

#include "vulkan_shader_library.hpp"

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QQuickWindow>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSGRendererInterface>
#include <QSize>
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
#include <limits>
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

struct GpuImage final {
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
};

struct GpuMesh final {
    GpuBuffer vertex;
    GpuBuffer index;
    std::uint32_t indexCount{0};
    std::uint64_t contentHash{0};
};

struct FrameUploadGarbage final {
    std::vector<GpuBuffer> stagingBuffers;
    std::vector<GpuMesh> retiredMeshes;
};

struct PickReadbackSlot final {
    GpuBuffer buffer;
    bool pending{false};
    std::vector<m3d::ObjectId> pickObjects;
};

struct PickTarget final {
    QSize size;
    VkFormat depthFormat{VK_FORMAT_UNDEFINED};
    GpuImage color;
    GpuImage depth;
    VkRenderPass renderPass{VK_NULL_HANDLE};
    VkFramebuffer framebuffer{VK_NULL_HANDLE};
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkPipeline pipeline{VK_NULL_HANDLE};
    std::vector<PickReadbackSlot> readbacks;
};

[[nodiscard]] m3d::Vec3 addVector(m3d::Vec3 left, m3d::Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] m3d::Vec3 scaleVector(m3d::Vec3 value, float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] std::array<float, 4> gizmoAxisColor(
    m3d::TransformConstraint axis,
    const std::optional<m3d::TransformConstraint>& active) noexcept {
    if (active && *active == axis) return {1.0F, 0.82F, 0.12F, 1.0F};
    switch (axis) {
    case m3d::TransformConstraint::X: return {0.92F, 0.16F, 0.18F, 1.0F};
    case m3d::TransformConstraint::Y: return {0.20F, 0.82F, 0.28F, 1.0F};
    case m3d::TransformConstraint::Z: return {0.22F, 0.42F, 0.96F, 1.0F};
    default: return {0.90F, 0.90F, 0.92F, 1.0F};
    }
}

void appendGizmoLine(std::vector<LineVertex>& vertices,
                     m3d::Vec3 start, m3d::Vec3 end,
                     const std::array<float, 4>& color) {
    vertices.push_back({start.x, start.y, start.z, color[0], color[1], color[2], color[3]});
    vertices.push_back({end.x, end.y, end.z, color[0], color[1], color[2], color[3]});
}

void appendAxisArrow(std::vector<LineVertex>& vertices,
                     const VulkanGizmoPresentation& gizmo,
                     m3d::Vec3 axis, m3d::Vec3 sideA, m3d::Vec3 sideB,
                     m3d::TransformConstraint constraint) {
    const auto color = gizmoAxisColor(constraint, gizmo.activeConstraint);
    const float size = gizmo.worldSize;
    const m3d::Vec3 end = addVector(gizmo.pivotWorld, scaleVector(axis, size));
    appendGizmoLine(vertices, gizmo.pivotWorld, end, color);
    const m3d::Vec3 base = addVector(end, scaleVector(axis, -0.14F * size));
    const float wing = 0.055F * size;
    appendGizmoLine(vertices, end, addVector(base, scaleVector(sideA, wing)), color);
    appendGizmoLine(vertices, end, addVector(base, scaleVector(sideA, -wing)), color);
    appendGizmoLine(vertices, end, addVector(base, scaleVector(sideB, wing)), color);
    appendGizmoLine(vertices, end, addVector(base, scaleVector(sideB, -wing)), color);
}

void appendScaleAxis(std::vector<LineVertex>& vertices,
                     const VulkanGizmoPresentation& gizmo,
                     m3d::Vec3 axis, m3d::Vec3 sideA, m3d::Vec3 sideB,
                     m3d::TransformConstraint constraint) {
    const auto color = gizmoAxisColor(constraint, gizmo.activeConstraint);
    const float size = gizmo.worldSize;
    const m3d::Vec3 end = addVector(gizmo.pivotWorld, scaleVector(axis, size));
    appendGizmoLine(vertices, gizmo.pivotWorld, end, color);
    const float marker = 0.055F * size;
    appendGizmoLine(vertices, addVector(end, scaleVector(sideA, -marker)),
                    addVector(end, scaleVector(sideA, marker)), color);
    appendGizmoLine(vertices, addVector(end, scaleVector(sideB, -marker)),
                    addVector(end, scaleVector(sideB, marker)), color);
}

void appendRotationRing(std::vector<LineVertex>& vertices,
                        const VulkanGizmoPresentation& gizmo,
                        m3d::Vec3 tangentA, m3d::Vec3 tangentB,
                        m3d::TransformConstraint constraint) {
    constexpr int segments = 64;
    constexpr float twoPi = 6.28318530717958647692F;
    const auto color = gizmoAxisColor(constraint, gizmo.activeConstraint);
    m3d::Vec3 previous = addVector(gizmo.pivotWorld, scaleVector(tangentA, gizmo.worldSize));
    for (int index = 1; index <= segments; ++index) {
        const float angle = twoPi * static_cast<float>(index) / static_cast<float>(segments);
        const m3d::Vec3 radial = addVector(scaleVector(tangentA, std::cos(angle)),
                                           scaleVector(tangentB, std::sin(angle)));
        const m3d::Vec3 current = addVector(gizmo.pivotWorld, scaleVector(radial, gizmo.worldSize));
        appendGizmoLine(vertices, previous, current, color);
        previous = current;
    }
}

[[nodiscard]] std::vector<LineVertex> makeGizmoVertices(const VulkanGizmoPresentation& gizmo) {
    std::vector<LineVertex> vertices;
    if (!gizmo.visible || gizmo.worldSize <= 0.0F) return vertices;
    vertices.reserve(gizmo.tool == m3d::TransformTool::Rotate ? 384U : 40U);

    const auto& x = gizmo.basis.x;
    const auto& y = gizmo.basis.y;
    const auto& z = gizmo.basis.z;
    if (gizmo.tool == m3d::TransformTool::Translate) {
        appendAxisArrow(vertices, gizmo, x, y, z, m3d::TransformConstraint::X);
        appendAxisArrow(vertices, gizmo, y, x, z, m3d::TransformConstraint::Y);
        appendAxisArrow(vertices, gizmo, z, x, y, m3d::TransformConstraint::Z);
    } else if (gizmo.tool == m3d::TransformTool::Rotate) {
        appendRotationRing(vertices, gizmo, y, z, m3d::TransformConstraint::X);
        appendRotationRing(vertices, gizmo, x, z, m3d::TransformConstraint::Y);
        appendRotationRing(vertices, gizmo, x, y, m3d::TransformConstraint::Z);
    } else {
        appendScaleAxis(vertices, gizmo, x, y, z, m3d::TransformConstraint::X);
        appendScaleAxis(vertices, gizmo, y, x, z, m3d::TransformConstraint::Y);
        appendScaleAxis(vertices, gizmo, z, x, y, m3d::TransformConstraint::Z);
    }

    const std::array<float, 4> centerColor =
        gizmo.activeConstraint && *gizmo.activeConstraint == m3d::TransformConstraint::Free
            ? std::array<float, 4>{1.0F, 0.82F, 0.12F, 1.0F}
            : std::array<float, 4>{0.92F, 0.92F, 0.94F, 1.0F};
    const float centerSize = gizmo.worldSize * 0.055F;
    appendGizmoLine(vertices, addVector(gizmo.pivotWorld, scaleVector(x, -centerSize)),
                    addVector(gizmo.pivotWorld, scaleVector(x, centerSize)), centerColor);
    appendGizmoLine(vertices, addVector(gizmo.pivotWorld, scaleVector(y, -centerSize)),
                    addVector(gizmo.pivotWorld, scaleVector(y, centerSize)), centerColor);
    appendGizmoLine(vertices, addVector(gizmo.pivotWorld, scaleVector(z, -centerSize)),
                    addVector(gizmo.pivotWorld, scaleVector(z, centerSize)), centerColor);
    return vertices;
}

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

[[nodiscard]] std::array<float, 4> encodePickColor(m3d::PickId pickId) noexcept {
    constexpr float scale = 1.0F / 255.0F;
    return {
        static_cast<float>(pickId & 0xFFU) * scale,
        static_cast<float>((pickId >> 8U) & 0xFFU) * scale,
        static_cast<float>((pickId >> 16U) & 0xFFU) * scale,
        static_cast<float>((pickId >> 24U) & 0xFFU) * scale,
    };
}

[[nodiscard]] m3d::PickId decodePickColor(const std::array<std::uint8_t, 4>& value) noexcept {
    return static_cast<m3d::PickId>(value[0]) |
           (static_cast<m3d::PickId>(value[1]) << 8U) |
           (static_cast<m3d::PickId>(value[2]) << 16U) |
           (static_cast<m3d::PickId>(value[3]) << 24U);
}

[[nodiscard]] m3d::Mat4 scaleAroundBounds(const m3d::Mat4& world,
                                          const m3d::Bounds3& bounds,
                                          float scale) noexcept {
    const m3d::Vec3 center{
        (bounds.min.x + bounds.max.x) * 0.5F,
        (bounds.min.y + bounds.max.y) * 0.5F,
        (bounds.min.z + bounds.max.z) * 0.5F,
    };
    m3d::Mat4 local = m3d::Mat4::identity();
    local.at(0, 0) = scale;
    local.at(1, 1) = scale;
    local.at(2, 2) = scale;
    local.at(0, 3) = center.x * (1.0F - scale);
    local.at(1, 3) = center.y * (1.0F - scale);
    local.at(2, 3) = center.z * (1.0F - scale);
    return m3d::multiply(world, local);
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

void destroyImage(QVulkanDeviceFunctions* functions, VkDevice device, GpuImage& image) {
    if (functions && device) {
        if (image.view) functions->vkDestroyImageView(device, image.view, nullptr);
        if (image.image) functions->vkDestroyImage(device, image.image, nullptr);
        if (image.memory) functions->vkFreeMemory(device, image.memory, nullptr);
    }
    image = {};
}

[[nodiscard]] bool createBuffer(QVulkanFunctions* functions,
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

[[nodiscard]] bool createDeviceImage(QVulkanFunctions* functions,
                                     QVulkanDeviceFunctions* deviceFunctions,
                                     VkPhysicalDevice physicalDevice,
                                     VkDevice device,
                                     QSize size,
                                     VkFormat format,
                                     VkImageUsageFlags usage,
                                     VkImageAspectFlags aspect,
                                     GpuImage& output) {
    output = {};
    if (size.isEmpty()) return false;

    auto imageInfo = vkInfo<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent.width = static_cast<std::uint32_t>(size.width());
    imageInfo.extent.height = static_cast<std::uint32_t>(size.height());
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (deviceFunctions->vkCreateImage(device, &imageInfo, nullptr, &output.image) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements{};
    deviceFunctions->vkGetImageMemoryRequirements(device, output.image, &requirements);
    const auto memoryType = findMemoryType(functions, physicalDevice, requirements.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!memoryType) {
        destroyImage(deviceFunctions, device, output);
        return false;
    }

    auto allocation = vkInfo<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = *memoryType;
    if (deviceFunctions->vkAllocateMemory(device, &allocation, nullptr, &output.memory) != VK_SUCCESS) {
        destroyImage(deviceFunctions, device, output);
        return false;
    }
    if (deviceFunctions->vkBindImageMemory(device, output.image, output.memory, 0) != VK_SUCCESS) {
        destroyImage(deviceFunctions, device, output);
        return false;
    }

    auto viewInfo = vkInfo<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    viewInfo.image = output.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (deviceFunctions->vkCreateImageView(device, &viewInfo, nullptr, &output.view) != VK_SUCCESS) {
        destroyImage(deviceFunctions, device, output);
        return false;
    }
    return true;
}

[[nodiscard]] VkFormat chooseDepthFormat(QVulkanFunctions* functions,
                                         VkPhysicalDevice physicalDevice) noexcept {
    constexpr std::array<VkFormat, 3> candidates{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM,
    };
    for (const auto format : candidates) {
        VkFormatProperties properties{};
        functions->vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0U) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
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

constexpr std::size_t kMaxPipelineCacheBytes = 64U * 1024U * 1024U;

[[nodiscard]] QString pipelineCacheFilePath(QVulkanFunctions* functions,
                                            VkPhysicalDevice physicalDevice) {
    if (!functions || !physicalDevice) return {};
    VkPhysicalDeviceProperties properties{};
    functions->vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    const QByteArray uuid(reinterpret_cast<const char*>(properties.pipelineCacheUUID), VK_UUID_SIZE);
    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheRoot.isEmpty()) return {};
    QDir directory(cacheRoot);
    if (!directory.mkpath(QStringLiteral("vulkan"))) return {};
    const QString fileName = QStringLiteral("pipeline-%1-%2-%3-%4.bin")
        .arg(QString::number(properties.vendorID, 16),
             QString::number(properties.deviceID, 16),
             QString::number(properties.driverVersion, 16),
             QString::fromLatin1(uuid.toHex()));
    return directory.filePath(QStringLiteral("vulkan/") + fileName);
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
    VkPipelineLayout outlinePipelineLayout{VK_NULL_HANDLE};
    VkPipeline outlinePipeline{VK_NULL_HANDLE};
    VkPipelineCache pipelineCache{VK_NULL_HANDLE};
    QString pipelineCachePath;
    bool pipelineCacheLoaded{false};
    std::unordered_map<m3d::ResourceId, GpuMesh, m3d::ResourceIdHash> meshCache;
    std::vector<FrameUploadGarbage> frameGarbage;
    PickTarget pickTarget;
};

VulkanViewportRenderer::VulkanViewportRenderer() : impl_(std::make_unique<Impl>()) {}
VulkanViewportRenderer::~VulkanViewportRenderer() = default;

void VulkanViewportRenderer::synchronize(QRect viewportPixels, m3d::RenderSceneSnapshot snapshot,
                                         m3d::Mat4 viewProjection, VulkanGizmoPresentation gizmo) {
    viewportPixels_ = viewportPixels;
    snapshot_ = std::move(snapshot);
    viewProjection_ = viewProjection;
    gizmo_ = std::move(gizmo);
}

void VulkanViewportRenderer::requestPick(QPoint viewportPixel) {
    pendingPickPixel_ = viewportPixel;
}

namespace {

bool createPipelineCache(VulkanViewportRenderer::Impl& impl) {
    impl.pipelineCachePath = pipelineCacheFilePath(impl.functions, impl.physicalDevice);
    QByteArray initialData;
    if (!impl.pipelineCachePath.isEmpty()) {
        QFile file(impl.pipelineCachePath);
        const qint64 size = file.size();
        if (size > 0 && static_cast<std::uint64_t>(size) <= kMaxPipelineCacheBytes &&
            file.open(QIODevice::ReadOnly)) {
            initialData = file.readAll();
        }
    }

    auto info = vkInfo<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
    if (!initialData.isEmpty()) {
        info.initialDataSize = static_cast<std::size_t>(initialData.size());
        info.pInitialData = initialData.constData();
    }
    VkResult result = impl.deviceFunctions->vkCreatePipelineCache(
        impl.device, &info, nullptr, &impl.pipelineCache);
    if (result != VK_SUCCESS && !initialData.isEmpty()) {
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        result = impl.deviceFunctions->vkCreatePipelineCache(
            impl.device, &info, nullptr, &impl.pipelineCache);
        initialData.clear();
    }
    if (result != VK_SUCCESS) return false;
    impl.pipelineCacheLoaded = !initialData.isEmpty();
    qInfo() << "Vulkan pipeline cache:" << (impl.pipelineCacheLoaded ? "loaded" : "cold")
            << impl.pipelineCachePath;
    return true;
}

bool persistPipelineCache(VulkanViewportRenderer::Impl& impl) {
    if (!impl.deviceFunctions || !impl.device || !impl.pipelineCache ||
        impl.pipelineCachePath.isEmpty()) return false;
    std::size_t size = 0;
    if (impl.deviceFunctions->vkGetPipelineCacheData(
            impl.device, impl.pipelineCache, &size, nullptr) != VK_SUCCESS ||
        size == 0 || size > kMaxPipelineCacheBytes) return false;

    QByteArray data(static_cast<qsizetype>(size), '\0');
    std::size_t writtenSize = size;
    if (impl.deviceFunctions->vkGetPipelineCacheData(
            impl.device, impl.pipelineCache, &writtenSize, data.data()) != VK_SUCCESS ||
        writtenSize == 0 || writtenSize > size) return false;
    data.resize(static_cast<qsizetype>(writtenSize));

    QSaveFile file(impl.pipelineCachePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) return false;
    return file.commit();
}

bool createGridResources(VulkanViewportRenderer::Impl& impl) {
    const auto vertices = makeGridVertices();
    if (!createHostBuffer(impl.functions, impl.deviceFunctions, impl.physicalDevice, impl.device,
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices.data(),
                          static_cast<VkDeviceSize>(vertices.size() * sizeof(LineVertex)), impl.gridBuffer)) return false;
    impl.gridVertexCount = static_cast<std::uint32_t>(vertices.size());
    return true;
}

bool createPipeline(VulkanViewportRenderer::Impl& impl,
                    VkRenderPass renderPass,
                    VkSampleCountFlagBits sampleCount,
                    bool linePipeline,
                    VkPipelineLayout& layout,
                    VkPipeline& pipeline,
                    bool depthWrite = true,
                    VkCullModeFlags cullMode = VK_CULL_MODE_NONE) {
    QString error;
    const auto vertexCode = linePipeline
        ? VulkanShaderLibrary::viewportLineVertexSpirv(&error)
        : VulkanShaderLibrary::viewportMeshVertexSpirv(&error);
    const auto fragmentCode = linePipeline
        ? VulkanShaderLibrary::viewportLineFragmentSpirv(&error)
        : VulkanShaderLibrary::viewportMeshFragmentSpirv(&error);
    const VkShaderModule vertexModule = createShaderModule(impl.deviceFunctions, impl.device, vertexCode);
    const VkShaderModule fragmentModule = createShaderModule(impl.deviceFunctions, impl.device, fragmentCode);
    if (!vertexModule || !fragmentModule) {
        if (vertexModule) impl.deviceFunctions->vkDestroyShaderModule(impl.device, vertexModule, nullptr);
        if (fragmentModule) impl.deviceFunctions->vkDestroyShaderModule(impl.device, fragmentModule, nullptr);
        return false;
    }

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size = linePipeline
        ? static_cast<std::uint32_t>(sizeof(m3d::Mat4))
        : static_cast<std::uint32_t>(sizeof(MeshPushConstants));
    auto layoutInfo = vkInfo<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    if (impl.deviceFunctions->vkCreatePipelineLayout(impl.device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
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
    binding.stride = linePipeline
        ? static_cast<std::uint32_t>(sizeof(LineVertex))
        : static_cast<std::uint32_t>(sizeof(m3d::MeshVertex));
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributes{};
    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = linePipeline
        ? static_cast<std::uint32_t>(offsetof(LineVertex, x))
        : static_cast<std::uint32_t>(offsetof(m3d::MeshVertex, position));
    if (linePipeline) {
        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[1].offset = static_cast<std::uint32_t>(offsetof(LineVertex, r));
    }

    auto vertexInput = vkInfo<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = linePipeline ? 2U : 1U;
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    auto assembly = vkInfo<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = linePipeline ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    PipelineCommonState common(!linePipeline, sampleCount);
    common.raster.cullMode = cullMode;
    if (!linePipeline) {
        common.depthStencil.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
    }

    auto pipelineInfo = vkInfo<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &common.viewport;
    pipelineInfo.pRasterizationState = &common.raster;
    pipelineInfo.pMultisampleState = &common.multisample;
    pipelineInfo.pDepthStencilState = &common.depthStencil;
    pipelineInfo.pColorBlendState = &common.blend;
    pipelineInfo.pDynamicState = &common.dynamic;
    pipelineInfo.layout = layout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    const bool success = impl.deviceFunctions->vkCreateGraphicsPipelines(
        impl.device, impl.pipelineCache, 1, &pipelineInfo, nullptr, &pipeline) == VK_SUCCESS;
    impl.deviceFunctions->vkDestroyShaderModule(impl.device, vertexModule, nullptr);
    impl.deviceFunctions->vkDestroyShaderModule(impl.device, fragmentModule, nullptr);
    if (!success) {
        impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, layout, nullptr);
        layout = VK_NULL_HANDLE;
    }
    return success;
}

void destroyGpuMesh(VulkanViewportRenderer::Impl& impl, GpuMesh& mesh) {
    destroyBuffer(impl.deviceFunctions, impl.device, mesh.vertex);
    destroyBuffer(impl.deviceFunctions, impl.device, mesh.index);
}

void destroyFrameGarbage(VulkanViewportRenderer::Impl& impl, FrameUploadGarbage& garbage) {
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

[[nodiscard]] bool meshCacheReady(const VulkanViewportRenderer::Impl& impl,
                                  const m3d::RenderSceneSnapshot& snapshot) {
    for (const auto& source : snapshot.meshes()) {
        const auto found = impl.meshCache.find(source.id);
        if (found == impl.meshCache.end() || found->second.contentHash != source.contentHash) return false;
    }
    return true;
}

void destroyPickTarget(VulkanViewportRenderer::Impl& impl) {
    auto& target = impl.pickTarget;
    for (auto& slot : target.readbacks) destroyBuffer(impl.deviceFunctions, impl.device, slot.buffer);
    target.readbacks.clear();
    if (target.pipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, target.pipeline, nullptr);
    if (target.pipelineLayout) impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, target.pipelineLayout, nullptr);
    if (target.framebuffer) impl.deviceFunctions->vkDestroyFramebuffer(impl.device, target.framebuffer, nullptr);
    if (target.renderPass) impl.deviceFunctions->vkDestroyRenderPass(impl.device, target.renderPass, nullptr);
    destroyImage(impl.deviceFunctions, impl.device, target.depth);
    destroyImage(impl.deviceFunctions, impl.device, target.color);
    target = {};
}

bool createPickTarget(VulkanViewportRenderer::Impl& impl, QSize size, int framesInFlight) {
    destroyPickTarget(impl);
    if (size.isEmpty()) return false;

    auto& target = impl.pickTarget;
    target.size = size;
    target.depthFormat = chooseDepthFormat(impl.functions, impl.physicalDevice);
    if (target.depthFormat == VK_FORMAT_UNDEFINED) return false;

    if (!createDeviceImage(impl.functions, impl.deviceFunctions, impl.physicalDevice, impl.device,
                           size, VK_FORMAT_R8G8B8A8_UNORM,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT, target.color)) {
        destroyPickTarget(impl);
        return false;
    }
    if (!createDeviceImage(impl.functions, impl.deviceFunctions, impl.physicalDevice, impl.device,
                           size, target.depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT, target.depth)) {
        destroyPickTarget(impl);
        return false;
    }

    std::array<VkAttachmentDescription, 2> attachments{};
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    attachments[1].format = target.depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    auto renderPassInfo = vkInfo<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    renderPassInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    if (impl.deviceFunctions->vkCreateRenderPass(impl.device, &renderPassInfo, nullptr,
                                                  &target.renderPass) != VK_SUCCESS) {
        destroyPickTarget(impl);
        return false;
    }

    const std::array<VkImageView, 2> views{target.color.view, target.depth.view};
    auto framebufferInfo = vkInfo<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    framebufferInfo.renderPass = target.renderPass;
    framebufferInfo.attachmentCount = static_cast<std::uint32_t>(views.size());
    framebufferInfo.pAttachments = views.data();
    framebufferInfo.width = static_cast<std::uint32_t>(size.width());
    framebufferInfo.height = static_cast<std::uint32_t>(size.height());
    framebufferInfo.layers = 1;
    if (impl.deviceFunctions->vkCreateFramebuffer(impl.device, &framebufferInfo, nullptr,
                                                   &target.framebuffer) != VK_SUCCESS) {
        destroyPickTarget(impl);
        return false;
    }

    if (!createPipeline(impl, target.renderPass, VK_SAMPLE_COUNT_1_BIT, false,
                        target.pipelineLayout, target.pipeline)) {
        destroyPickTarget(impl);
        return false;
    }

    const int slotCount = std::max(framesInFlight, 1);
    target.readbacks.resize(static_cast<std::size_t>(slotCount));
    constexpr std::array<std::uint8_t, 4> zero{};
    for (auto& slot : target.readbacks) {
        if (!createHostBuffer(impl.functions, impl.deviceFunctions, impl.physicalDevice, impl.device,
                              VK_BUFFER_USAGE_TRANSFER_DST_BIT, zero.data(), zero.size(), slot.buffer)) {
            destroyPickTarget(impl);
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool anyPendingReadback(const PickTarget& target) noexcept {
    return std::any_of(target.readbacks.cbegin(), target.readbacks.cend(),
                       [](const PickReadbackSlot& slot) { return slot.pending; });
}

VulkanPickResult consumeReadback(VulkanViewportRenderer::Impl& impl, int frameSlot) {
    VulkanPickResult result;
    auto& target = impl.pickTarget;
    if (frameSlot < 0 || static_cast<std::size_t>(frameSlot) >= target.readbacks.size()) return result;
    auto& slot = target.readbacks[static_cast<std::size_t>(frameSlot)];
    if (!slot.pending) return result;

    std::array<std::uint8_t, 4> pixel{};
    void* mapped = nullptr;
    const VkResult mapResult = impl.deviceFunctions->vkMapMemory(
        impl.device, slot.buffer.memory, 0, pixel.size(), 0, &mapped);
    if (mapResult == VK_SUCCESS && mapped) {
        std::memcpy(pixel.data(), mapped, pixel.size());
        impl.deviceFunctions->vkUnmapMemory(impl.device, slot.buffer.memory);
        const m3d::PickId pickId = decodePickColor(pixel);
        if (pickId != m3d::backgroundPickId &&
            static_cast<std::size_t>(pickId) < slot.pickObjects.size()) {
            const auto object = slot.pickObjects[static_cast<std::size_t>(pickId)];
            if (!object.isNull()) result.object = object;
        }
        result.resultReady = true;
    }
    slot.pending = false;
    slot.pickObjects.clear();
    result.waitingForReadback = anyPendingReadback(target);
    return result;
}

void populatePickMap(PickReadbackSlot& slot, const m3d::RenderSceneSnapshot& snapshot) {
    m3d::PickId maxPickId = 0;
    for (const auto& object : snapshot.objects()) maxPickId = std::max(maxPickId, object.pickId);
    slot.pickObjects.assign(static_cast<std::size_t>(maxPickId) + 1U, m3d::ObjectId::null());
    for (const auto& object : snapshot.objects()) {
        if (object.pickId != m3d::backgroundPickId &&
            static_cast<std::size_t>(object.pickId) < slot.pickObjects.size()) {
            slot.pickObjects[static_cast<std::size_t>(object.pickId)] = object.id;
        }
    }
}

} // namespace

void VulkanViewportRenderer::release() {
    auto& impl = *impl_;
    if (!impl.deviceFunctions || !impl.device) {
        impl = Impl{};
        return;
    }
    destroyPickTarget(impl);
    for (auto& garbage : impl.frameGarbage) destroyFrameGarbage(impl, garbage);
    impl.frameGarbage.clear();
    for (auto& [id, mesh] : impl.meshCache) {
        (void)id;
        destroyGpuMesh(impl, mesh);
    }
    impl.meshCache.clear();
    (void)persistPipelineCache(impl);
    if (impl.outlinePipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.outlinePipeline, nullptr);
    if (impl.outlinePipelineLayout) impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, impl.outlinePipelineLayout, nullptr);
    if (impl.meshPipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.meshPipeline, nullptr);
    if (impl.meshPipelineLayout) impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, impl.meshPipelineLayout, nullptr);
    if (impl.linePipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.linePipeline, nullptr);
    if (impl.linePipelineLayout) impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, impl.linePipelineLayout, nullptr);
    if (impl.pipelineCache) impl.deviceFunctions->vkDestroyPipelineCache(impl.device, impl.pipelineCache, nullptr);
    destroyBuffer(impl.deviceFunctions, impl.device, impl.gridBuffer);
    impl = Impl{};
}

VulkanPickResult VulkanViewportRenderer::recordPicking(QQuickWindow* window) {
    VulkanPickResult result;
    auto& impl = *impl_;
    if (!window || !impl.device || !impl.deviceFunctions) {
        result.waitingForReadback = pendingPickPixel_.has_value();
        return result;
    }

    const auto stateInfo = window->graphicsStateInfo();
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

    const QSize targetSize(viewportPixels_.width(), viewportPixels_.height());
    if (targetSize.isEmpty()) {
        pendingPickPixel_.reset();
        return result;
    }
    if (impl.pickTarget.size != targetSize ||
        static_cast<int>(impl.pickTarget.readbacks.size()) != std::max(stateInfo.framesInFlight, 1)) {
        if (!createPickTarget(impl, targetSize, stateInfo.framesInFlight)) {
            result.waitingForReadback = true;
            return result;
        }
    }

    const QPoint requested = *pendingPickPixel_;
    if (requested.x() < 0 || requested.y() < 0 ||
        requested.x() >= targetSize.width() || requested.y() >= targetSize.height()) {
        pendingPickPixel_.reset();
        result.resultReady = true;
        result.object.reset();
        return result;
    }
    if (stateInfo.currentFrameSlot < 0 ||
        static_cast<std::size_t>(stateInfo.currentFrameSlot) >= impl.pickTarget.readbacks.size()) {
        result.waitingForReadback = true;
        return result;
    }

    auto& slot = impl.pickTarget.readbacks[static_cast<std::size_t>(stateInfo.currentFrameSlot)];
    if (slot.pending) {
        result.waitingForReadback = true;
        return result;
    }
    populatePickMap(slot, snapshot_);

    window->beginExternalCommands();
    auto* commandBufferHandle = static_cast<VkCommandBuffer*>(rendererInterface->getResource(
        window, QSGRendererInterface::CommandListResource));
    if (!commandBufferHandle || !*commandBufferHandle) {
        window->endExternalCommands();
        result.waitingForReadback = true;
        return result;
    }
    const VkCommandBuffer commandBuffer = *commandBufferHandle;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.float32[0] = 0.0F;
    clearValues[0].color.float32[1] = 0.0F;
    clearValues[0].color.float32[2] = 0.0F;
    clearValues[0].color.float32[3] = 0.0F;
    clearValues[1].depthStencil.depth = 1.0F;
    clearValues[1].depthStencil.stencil = 0;

    auto passInfo = vkInfo<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    passInfo.renderPass = impl.pickTarget.renderPass;
    passInfo.framebuffer = impl.pickTarget.framebuffer;
    passInfo.renderArea.offset = {0, 0};
    passInfo.renderArea.extent = {
        static_cast<std::uint32_t>(targetSize.width()),
        static_cast<std::uint32_t>(targetSize.height()),
    };
    passInfo.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
    passInfo.pClearValues = clearValues.data();
    impl.deviceFunctions->vkCmdBeginRenderPass(commandBuffer, &passInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(targetSize.width());
    viewport.height = static_cast<float>(targetSize.height());
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;
    VkRect2D scissor{};
    scissor.offset.x = requested.x();
    scissor.offset.y = requested.y();
    scissor.extent = {1U, 1U};
    impl.deviceFunctions->vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    impl.deviceFunctions->vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    impl.deviceFunctions->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            impl.pickTarget.pipeline);

    const VkDeviceSize zeroOffset = 0;
    for (const auto& object : snapshot_.objects()) {
        if (!object.visible || object.type != m3d::ObjectType::Mesh || !object.meshResource ||
            object.pickId == m3d::backgroundPickId) continue;
        const auto mesh = impl.meshCache.find(*object.meshResource);
        if (mesh == impl.meshCache.end()) continue;
        const MeshPushConstants push{
            m3d::multiply(viewProjection_, object.worldTransform),
            encodePickColor(object.pickId),
        };
        impl.deviceFunctions->vkCmdPushConstants(commandBuffer, impl.pickTarget.pipelineLayout,
                                                 VK_SHADER_STAGE_VERTEX_BIT, 0,
                                                 static_cast<std::uint32_t>(sizeof(push)), &push);
        impl.deviceFunctions->vkCmdBindVertexBuffers(commandBuffer, 0, 1,
                                                     &mesh->second.vertex.buffer, &zeroOffset);
        impl.deviceFunctions->vkCmdBindIndexBuffer(commandBuffer, mesh->second.index.buffer,
                                                   0, VK_INDEX_TYPE_UINT32);
        impl.deviceFunctions->vkCmdDrawIndexed(commandBuffer, mesh->second.indexCount, 1, 0, 0, 0);
    }
    impl.deviceFunctions->vkCmdEndRenderPass(commandBuffer);

    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {requested.x(), requested.y(), 0};
    copy.imageExtent = {1U, 1U, 1U};
    impl.deviceFunctions->vkCmdCopyImageToBuffer(commandBuffer, impl.pickTarget.color.image,
                                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                 slot.buffer.buffer, 1, &copy);
    window->endExternalCommands();

    slot.pending = true;
    pendingPickPixel_.reset();
    result.waitingForReadback = true;
    return result;
}

VulkanRecordStats VulkanViewportRenderer::record(QQuickWindow* window) {
    VulkanRecordStats stats;
    if (!window || viewportPixels_.isEmpty()) return stats;
    auto* rendererInterface = window->rendererInterface();
    if (!rendererInterface || rendererInterface->graphicsApi() != QSGRendererInterface::Vulkan) return stats;

    auto* instance = static_cast<QVulkanInstance*>(rendererInterface->getResource(
        window, QSGRendererInterface::VulkanInstanceResource));
    auto* deviceHandle = static_cast<VkDevice*>(rendererInterface->getResource(
        window, QSGRendererInterface::DeviceResource));
    auto* physicalHandle = static_cast<VkPhysicalDevice*>(rendererInterface->getResource(
        window, QSGRendererInterface::PhysicalDeviceResource));
    auto* renderPassHandle = static_cast<VkRenderPass*>(rendererInterface->getResource(
        window, QSGRendererInterface::RenderPassResource));
    auto* swapchain = static_cast<QRhiSwapChain*>(rendererInterface->getResource(
        window, QSGRendererInterface::RhiSwapchainResource));
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
        if (!createPipelineCache(impl) || !createGridResources(impl) ||
            !createPipeline(impl, impl.renderPass, vulkanSampleCount(impl.sampleCount), true,
                            impl.linePipelineLayout, impl.linePipeline) ||
            !createPipeline(impl, impl.renderPass, vulkanSampleCount(impl.sampleCount), false,
                            impl.meshPipelineLayout, impl.meshPipeline) ||
            !createPipeline(impl, impl.renderPass, vulkanSampleCount(impl.sampleCount), false,
                            impl.outlinePipelineLayout, impl.outlinePipeline, false,
                            VK_CULL_MODE_FRONT_BIT)) {
            release();
            return stats;
        }
        (void)persistPipelineCache(impl);
    }
    stats.pipelineCacheLoaded = impl.pipelineCacheLoaded;
    if (!meshCacheReady(impl, snapshot_)) {
        stats.needsAnotherFrame = true;
        return stats;
    }

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
    clearRect.baseArrayLayer = 0;
    clearRect.layerCount = 1;

    window->beginExternalCommands();
    auto* commandBufferHandle = static_cast<VkCommandBuffer*>(rendererInterface->getResource(
        window, QSGRendererInterface::CommandListResource));
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

    const VkDeviceSize zeroOffset = 0;
    deviceFunctions->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.linePipeline);
    deviceFunctions->vkCmdBindVertexBuffers(commandBuffer, 0, 1, &impl.gridBuffer.buffer, &zeroOffset);
    deviceFunctions->vkCmdPushConstants(commandBuffer, impl.linePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                        0, static_cast<std::uint32_t>(sizeof(m3d::Mat4)),
                                        viewProjection_.values.data());
    deviceFunctions->vkCmdDraw(commandBuffer, impl.gridVertexCount, 1, 0, 0);

    constexpr float outlineScale = 1.035F;
    constexpr std::array<float, 4> outlineColor{1.0F, 0.48F, 0.08F, 1.0F};
    deviceFunctions->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.outlinePipeline);
    for (const auto& object : snapshot_.objects()) {
        if (!object.visible || !object.selected || object.type != m3d::ObjectType::Mesh ||
            !object.meshResource) continue;
        const auto mesh = impl.meshCache.find(*object.meshResource);
        const auto* geometry = snapshot_.findMesh(*object.meshResource);
        if (mesh == impl.meshCache.end() || !geometry || !geometry->bounds) continue;
        const auto outlineWorld = scaleAroundBounds(object.worldTransform, *geometry->bounds, outlineScale);
        const MeshPushConstants push{
            m3d::multiply(viewProjection_, outlineWorld),
            outlineColor,
        };
        deviceFunctions->vkCmdPushConstants(commandBuffer, impl.outlinePipelineLayout,
                                            VK_SHADER_STAGE_VERTEX_BIT, 0,
                                            static_cast<std::uint32_t>(sizeof(push)), &push);
        deviceFunctions->vkCmdBindVertexBuffers(commandBuffer, 0, 1,
                                                &mesh->second.vertex.buffer, &zeroOffset);
        deviceFunctions->vkCmdBindIndexBuffer(commandBuffer, mesh->second.index.buffer,
                                              0, VK_INDEX_TYPE_UINT32);
        deviceFunctions->vkCmdDrawIndexed(commandBuffer, mesh->second.indexCount, 1, 0, 0, 0);
        ++stats.outlineDraws;
    }

    deviceFunctions->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.meshPipeline);
    for (const auto& object : snapshot_.objects()) {
        if (!object.visible || object.type != m3d::ObjectType::Mesh || !object.meshResource) continue;
        const auto mesh = impl.meshCache.find(*object.meshResource);
        if (mesh == impl.meshCache.end()) continue;
        const MeshPushConstants push{
            m3d::multiply(viewProjection_, object.worldTransform),
            object.selected
                ? std::array<float, 4>{0.93F, 0.55F, 0.18F, 1.0F}
                : std::array<float, 4>{0.62F, 0.66F, 0.72F, 1.0F},
        };
        deviceFunctions->vkCmdPushConstants(commandBuffer, impl.meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                            0, static_cast<std::uint32_t>(sizeof(push)), &push);
        deviceFunctions->vkCmdBindVertexBuffers(commandBuffer, 0, 1, &mesh->second.vertex.buffer, &zeroOffset);
        deviceFunctions->vkCmdBindIndexBuffer(commandBuffer, mesh->second.index.buffer, 0, VK_INDEX_TYPE_UINT32);
        deviceFunctions->vkCmdDrawIndexed(commandBuffer, mesh->second.indexCount, 1, 0, 0, 0);
        ++stats.meshDraws;
    }

    const auto gizmoVertices = makeGizmoVertices(gizmo_);
    if (!gizmoVertices.empty()) {
        const auto stateInfo = window->graphicsStateInfo();
        ensureFrameGarbage(impl, stateInfo.framesInFlight);
        auto* garbage = frameGarbageFor(impl, stateInfo.currentFrameSlot);
        GpuBuffer gizmoBuffer;
        if (garbage && createHostBuffer(impl.functions, impl.deviceFunctions, impl.physicalDevice, impl.device,
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gizmoVertices.data(),
                                        static_cast<VkDeviceSize>(gizmoVertices.size() * sizeof(LineVertex)),
                                        gizmoBuffer)) {
            garbage->stagingBuffers.push_back(gizmoBuffer);
            deviceFunctions->vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, impl.linePipeline);
            deviceFunctions->vkCmdBindVertexBuffers(commandBuffer, 0, 1, &gizmoBuffer.buffer, &zeroOffset);
            deviceFunctions->vkCmdPushConstants(commandBuffer, impl.linePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                                                0, static_cast<std::uint32_t>(sizeof(m3d::Mat4)),
                                                viewProjection_.values.data());
            deviceFunctions->vkCmdDraw(commandBuffer, static_cast<std::uint32_t>(gizmoVertices.size()), 1, 0, 0);
            ++stats.gizmoDraws;
        }
    }

    window->endExternalCommands();
    stats.recorded = true;
    return stats;
}
