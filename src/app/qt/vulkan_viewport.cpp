#include "vulkan_viewport.hpp"

#include "editor_controller.hpp"

#include <QQuickWindow>
#include <QRect>
#include <QRectF>
#include <QSGRendererInterface>
#include <QVulkanDeviceFunctions>
#include <QVulkanInstance>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

QString graphicsApiName(QSGRendererInterface::GraphicsApi api) {
    switch (api) {
    case QSGRendererInterface::Software: return QStringLiteral("Software");
    case QSGRendererInterface::OpenVG: return QStringLiteral("OpenVG");
    case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D 11");
    case QSGRendererInterface::Direct3D12: return QStringLiteral("Direct3D 12");
    case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("Metal");
    case QSGRendererInterface::Null: return QStringLiteral("Null");
    case QSGRendererInterface::Unknown: break;
    }
    return QStringLiteral("Unknown");
}

class VulkanViewportRenderer final {
public:
    void synchronize(QRect viewportPixels, m3d::RenderSceneSnapshot snapshot) {
        viewportPixels_ = viewportPixels;
        snapshot_ = std::move(snapshot);
    }

    void record(QQuickWindow* window) {
        if (!window || viewportPixels_.isEmpty()) {
            return;
        }

        auto* rendererInterface = window->rendererInterface();
        if (!rendererInterface || rendererInterface->graphicsApi() != QSGRendererInterface::Vulkan) {
            return;
        }

        auto* instance = static_cast<QVulkanInstance*>(rendererInterface->getResource(
            window, QSGRendererInterface::VulkanInstanceResource));
        auto* deviceHandle = static_cast<VkDevice*>(rendererInterface->getResource(
            window, QSGRendererInterface::DeviceResource));
        auto* commandBufferHandle = static_cast<VkCommandBuffer*>(rendererInterface->getResource(
            window, QSGRendererInterface::CommandListResource));
        if (!instance || !deviceHandle || !commandBufferHandle || !*deviceHandle || !*commandBufferHandle) {
            return;
        }

        auto* deviceFunctions = instance->deviceFunctions(*deviceHandle);
        if (!deviceFunctions) {
            return;
        }

        const qreal dpr = window->devicePixelRatio();
        const QSize framebufferSize{
            static_cast<int>(std::ceil(static_cast<double>(window->width()) * dpr)),
            static_cast<int>(std::ceil(static_cast<double>(window->height()) * dpr))
        };
        const QRect framebufferRect(QPoint(0, 0), framebufferSize);
        const QRect clearArea = viewportPixels_.intersected(framebufferRect);
        if (clearArea.isEmpty()) {
            return;
        }

        const bool hasSelection = std::any_of(snapshot_.objects().cbegin(), snapshot_.objects().cend(),
                                              [](const m3d::RenderObjectSnapshot& object) {
                                                  return object.selected;
                                              });
        const float objectLift = std::min(static_cast<float>(snapshot_.size()) * 0.003F, 0.035F);

        VkClearAttachment clearAttachment{};
        clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearAttachment.colorAttachment = 0;
        clearAttachment.clearValue.color.float32[0] = 0.018F;
        clearAttachment.clearValue.color.float32[1] = 0.026F + objectLift;
        clearAttachment.clearValue.color.float32[2] = (hasSelection ? 0.105F : 0.058F) + objectLift;
        clearAttachment.clearValue.color.float32[3] = 1.0F;

        VkClearRect clearRect{};
        clearRect.rect.offset.x = clearArea.x();
        clearRect.rect.offset.y = clearArea.y();
        clearRect.rect.extent.width = static_cast<std::uint32_t>(clearArea.width());
        clearRect.rect.extent.height = static_cast<std::uint32_t>(clearArea.height());
        clearRect.baseArrayLayer = 0;
        clearRect.layerCount = 1;

        deviceFunctions->vkCmdClearAttachments(*commandBufferHandle, 1, &clearAttachment, 1, &clearRect);
    }

    [[nodiscard]] std::size_t snapshotObjectCount() const noexcept { return snapshot_.size(); }

private:
    QRect viewportPixels_;
    m3d::RenderSceneSnapshot snapshot_;
};

} // namespace

VulkanViewport::VulkanViewport(QQuickItem* parent)
    : QQuickItem(parent) {
    setFlag(ItemHasContents, false);
    connect(this, &QQuickItem::windowChanged, this, &VulkanViewport::handleWindowChanged);
}

VulkanViewport::~VulkanViewport() {
    if (connectedWindow_) {
        disconnect(connectedWindow_, nullptr, this, nullptr);
    }
    delete renderer_;
}

QObject* VulkanViewport::controller() const noexcept {
    return controller_.data();
}

void VulkanViewport::setController(QObject* controller) {
    auto* typedController = qobject_cast<EditorController*>(controller);
    if (controller_.data() == typedController) {
        return;
    }

    if (controllerProjectConnection_) {
        disconnect(controllerProjectConnection_);
    }
    if (controllerSelectionConnection_) {
        disconnect(controllerSelectionConnection_);
    }

    controller_ = typedController;
    if (controller_) {
        controllerProjectConnection_ = connect(controller_, &EditorController::projectStateChanged,
                                               this, &QQuickItem::update);
        controllerSelectionConnection_ = connect(controller_, &EditorController::selectionChanged,
                                                 this, &QQuickItem::update);
    }

    emit controllerChanged();
    update();
}

void VulkanViewport::releaseResources() {
    // GPU/device-owned state is released from sceneGraphInvalidated() on the render
    // thread. This hook intentionally does not introduce GUI-thread Vulkan access.
}

void VulkanViewport::handleWindowChanged(QQuickWindow* window) {
    if (connectedWindow_) {
        disconnect(connectedWindow_, nullptr, this, nullptr);
    }
    connectedWindow_ = window;

    if (!window) {
        if (vulkanActive_ || backendName_ != QStringLiteral("Unavailable")) {
            vulkanActive_ = false;
            backendName_ = QStringLiteral("Unavailable");
            emit backendChanged();
        }
        return;
    }

    connect(window, &QQuickWindow::beforeSynchronizing,
            this, &VulkanViewport::sync, Qt::DirectConnection);
    connect(window, &QQuickWindow::beforeRenderPassRecording,
            this, &VulkanViewport::recordVulkanCommands, Qt::DirectConnection);
    connect(window, &QQuickWindow::sceneGraphInvalidated,
            this, &VulkanViewport::cleanup, Qt::DirectConnection);

    updateBackendState(window);
    update();
}

void VulkanViewport::sync() {
    auto* currentWindow = window();
    if (!currentWindow) {
        return;
    }
    if (!renderer_) {
        renderer_ = new VulkanViewportRenderer;
    }

    const QRectF sceneRect = mapRectToScene(boundingRect());
    const qreal dpr = currentWindow->devicePixelRatio();
    const int left = static_cast<int>(std::floor(static_cast<double>(sceneRect.left()) * dpr));
    const int top = static_cast<int>(std::floor(static_cast<double>(sceneRect.top()) * dpr));
    const int right = static_cast<int>(std::ceil(static_cast<double>(sceneRect.right()) * dpr));
    const int bottom = static_cast<int>(std::ceil(static_cast<double>(sceneRect.bottom()) * dpr));
    const QRect viewportPixels(QPoint(left, top), QPoint(right, bottom));

    m3d::RenderSceneSnapshot snapshot;
    if (controller_) {
        snapshot = controller_->renderSnapshot();
    }
    renderer_->synchronize(viewportPixels, std::move(snapshot));
}

void VulkanViewport::cleanup() {
    delete renderer_;
    renderer_ = nullptr;
}

void VulkanViewport::recordVulkanCommands() {
    if (renderer_) {
        renderer_->record(window());
    }
}

void VulkanViewport::updateBackendState(QQuickWindow* window) {
    const auto api = window && window->rendererInterface()
        ? window->rendererInterface()->graphicsApi()
        : QSGRendererInterface::Unknown;
    const bool isVulkan = api == QSGRendererInterface::Vulkan;
    const QString name = graphicsApiName(api);
    if (vulkanActive_ == isVulkan && backendName_ == name) {
        return;
    }
    vulkanActive_ = isVulkan;
    backendName_ = name;
    emit backendChanged();
}
