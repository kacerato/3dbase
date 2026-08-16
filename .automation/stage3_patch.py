from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")


replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    '#include <QByteArray>\n#include <QDebug>\n#include <QQuickWindow>\n',
    '#include <QByteArray>\n#include <QDebug>\n#include <QDir>\n#include <QFile>\n#include <QQuickWindow>\n#include <QSaveFile>\n#include <QStandardPaths>\n',
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.hpp",
    "    bool recorded{false};\n    bool needsAnotherFrame{false};\n    std::uint32_t meshDraws{0};",
    "    bool recorded{false};\n    bool needsAnotherFrame{false};\n    bool pipelineCacheLoaded{false};\n    std::uint32_t meshDraws{0};",
)

replace_once(
    "src/app/qt/vulkan_viewport.hpp",
    "    [[nodiscard]] std::uint64_t recordedOutlineDrawCount() const noexcept {\n        return recordedOutlineDrawCount_.load(std::memory_order_relaxed);\n    }\n    [[nodiscard]] std::uint64_t completedPickCount() const noexcept {",
    "    [[nodiscard]] std::uint64_t recordedOutlineDrawCount() const noexcept {\n        return recordedOutlineDrawCount_.load(std::memory_order_relaxed);\n    }\n    [[nodiscard]] bool pipelineCacheLoaded() const noexcept {\n        return pipelineCacheLoaded_.load(std::memory_order_relaxed);\n    }\n    [[nodiscard]] std::uint64_t completedPickCount() const noexcept {",
)

replace_once(
    "src/app/qt/vulkan_viewport.hpp",
    "    std::atomic_uint64_t recordedOutlineDrawCount_{0};\n    std::atomic_uint64_t completedPickCount_{0};",
    "    std::atomic_uint64_t recordedOutlineDrawCount_{0};\n    std::atomic_bool pipelineCacheLoaded_{false};\n    std::atomic_uint64_t completedPickCount_{0};",
)

replace_once(
    "src/app/qt/vulkan_viewport.cpp",
    "    if (stats.outlineDraws > 0) {\n        recordedOutlineDrawCount_.fetch_add(stats.outlineDraws, std::memory_order_relaxed);\n    }\n    if (stats.needsAnotherFrame) scheduleNextFrame();",
    "    if (stats.outlineDraws > 0) {\n        recordedOutlineDrawCount_.fetch_add(stats.outlineDraws, std::memory_order_relaxed);\n    }\n    if (stats.pipelineCacheLoaded) {\n        pipelineCacheLoaded_.store(true, std::memory_order_relaxed);\n    }\n    if (stats.needsAnotherFrame) scheduleNextFrame();",
)

replace_once(
    "src/app/main.cpp",
    "    const bool vulkanSmokeTest = std::find(arguments.cbegin(), arguments.cend(),\n                                           QStringLiteral(\"--vulkan-smoke-test\")) != arguments.cend();",
    "    const bool vulkanSmokeTest = std::find(arguments.cbegin(), arguments.cend(),\n                                           QStringLiteral(\"--vulkan-smoke-test\")) != arguments.cend();\n    const bool requirePipelineCache = std::find(arguments.cbegin(), arguments.cend(),\n                                                QStringLiteral(\"--require-pipeline-cache\")) != arguments.cend();",
)

replace_once(
    "src/app/main.cpp",
    "                                viewport->recordedOutlineDrawCount() > 0 &&\n                                viewport->completedPickCount() > 0 &&\n                                viewport->successfulPickCount() > 0;",
    "                                viewport->recordedOutlineDrawCount() > 0 &&\n                                viewport->completedPickCount() > 0 &&\n                                viewport->successfulPickCount() > 0 &&\n                                (!requirePipelineCache || viewport->pipelineCacheLoaded());",
)

replace_once(
    "src/app/main.cpp",
    "        QTimer::singleShot(2500, &app, [&app, root] {",
    "        QTimer::singleShot(2500, &app, [&app, root, requirePipelineCache] {",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "    VkPipelineLayout outlinePipelineLayout{VK_NULL_HANDLE};\n    VkPipeline outlinePipeline{VK_NULL_HANDLE};\n    std::unordered_map<m3d::ResourceId, GpuMesh, m3d::ResourceIdHash> meshCache;",
    "    VkPipelineLayout outlinePipelineLayout{VK_NULL_HANDLE};\n    VkPipeline outlinePipeline{VK_NULL_HANDLE};\n    VkPipelineCache pipelineCache{VK_NULL_HANDLE};\n    QString pipelineCachePath;\n    bool pipelineCacheLoaded{false};\n    std::unordered_map<m3d::ResourceId, GpuMesh, m3d::ResourceIdHash> meshCache;",
)

anchor = '''VkPipelineColorBlendAttachmentState opaqueBlendState() {
    VkPipelineColorBlendAttachmentState state{};
    state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    return state;
}
'''
cache_helpers = '''VkPipelineColorBlendAttachmentState opaqueBlendState() {
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

    QByteArray data(static_cast<qsizetype>(size), '\\0');
    std::size_t writtenSize = size;
    if (impl.deviceFunctions->vkGetPipelineCacheData(
            impl.device, impl.pipelineCache, &writtenSize, data.data()) != VK_SUCCESS ||
        writtenSize == 0 || writtenSize > size) return false;
    data.resize(static_cast<qsizetype>(writtenSize));

    QSaveFile file(impl.pipelineCachePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) return false;
    return file.commit();
}
'''
replace_once("src/app/qt/vulkan_viewport_renderer.cpp", anchor, cache_helpers)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "        impl.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) == VK_SUCCESS;",
    "        impl.device, impl.pipelineCache, 1, &pipelineInfo, nullptr, &pipeline) == VK_SUCCESS;",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "    if (impl.outlinePipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.outlinePipeline, nullptr);",
    "    (void)persistPipelineCache(impl);\n    if (impl.outlinePipeline) impl.deviceFunctions->vkDestroyPipeline(impl.device, impl.outlinePipeline, nullptr);",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "    if (impl.linePipelineLayout) impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, impl.linePipelineLayout, nullptr);\n    destroyBuffer(impl.deviceFunctions, impl.device, impl.gridBuffer);",
    "    if (impl.linePipelineLayout) impl.deviceFunctions->vkDestroyPipelineLayout(impl.device, impl.linePipelineLayout, nullptr);\n    if (impl.pipelineCache) impl.deviceFunctions->vkDestroyPipelineCache(impl.device, impl.pipelineCache, nullptr);\n    destroyBuffer(impl.deviceFunctions, impl.device, impl.gridBuffer);",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "        qInfo() << \"Vulkan viewport effective MSAA samples:\" << impl.sampleCount;\n        if (!createGridResources(impl) ||",
    "        qInfo() << \"Vulkan viewport effective MSAA samples:\" << impl.sampleCount;\n        if (!createPipelineCache(impl) || !createGridResources(impl) ||",
)

replace_once(
    "src/app/qt/vulkan_viewport_renderer.cpp",
    "            release();\n            return stats;\n        }\n    }\n    if (!meshCacheReady(impl, snapshot_)) {",
    "            release();\n            return stats;\n        }\n        (void)persistPipelineCache(impl);\n    }\n    stats.pipelineCacheLoaded = impl.pipelineCacheLoaded;\n    if (!meshCacheReady(impl, snapshot_)) {",
)

replace_once(
    ".github/workflows/host-tests.yml",
    "          export VK_DRIVER_FILES=\"$VULKAN_ICD\"\n          xvfb-run -a ./build-qt/src/app/Mobile3DStudio --vulkan-smoke-test",
    "          export VK_DRIVER_FILES=\"$VULKAN_ICD\"\n          export XDG_CACHE_HOME=\"$RUNNER_TEMP/mobile3d-vulkan-cache\"\n          rm -rf \"$XDG_CACHE_HOME\"\n          mkdir -p \"$XDG_CACHE_HOME\"\n          xvfb-run -a ./build-qt/src/app/Mobile3DStudio --vulkan-smoke-test\n          find \"$XDG_CACHE_HOME\" -type f -name 'pipeline-*.bin' -size +0c -print -quit | grep -q .\n          xvfb-run -a ./build-qt/src/app/Mobile3DStudio --vulkan-smoke-test --require-pipeline-cache",
)

print("Persistent Vulkan pipeline cache transformation applied successfully")
