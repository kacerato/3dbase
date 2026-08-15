#include "vulkan_viewport.hpp"

#include "editor_controller.hpp"

#include <QEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QRect>
#include <QRectF>
#include <QSGRendererInterface>
#include <QTouchEvent>
#include <QVulkanDeviceFunctions>
#include <QVulkanInstance>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

namespace {

constexpr float kOrbitRadiansPerPixel = 0.006F;
constexpr float kWheelZoomExponentPerUnit = 0.0015F;
constexpr float kDegreesToRadians = 0.01745329251994329577F;

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

[[nodiscard]] QPointF touchCentroid(const QList<QEventPoint>& points) {
    if (points.isEmpty()) {
        return {};
    }
    QPointF sum;
    for (const auto& point : points) {
        sum += point.position();
    }
    return sum / static_cast<qreal>(points.size());
}

[[nodiscard]] float touchSpan(const QList<QEventPoint>& points) {
    if (points.size() < 2) {
        return 0.0F;
    }
    const QPointF delta = points.at(1).position() - points.at(0).position();
    return std::hypot(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
}

} // namespace

class VulkanViewportRenderer final {
public:
    void synchronize(QRect viewportPixels, m3d::RenderSceneSnapshot snapshot,
                     m3d::Mat4 viewProjection) {
        viewportPixels_ = viewportPixels;
        snapshot_ = std::move(snapshot);
        viewProjection_ = viewProjection;
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
        const float cameraLift = std::min(std::abs(viewProjection_.at(0, 0)) * 0.002F, 0.015F);

        VkClearAttachment clearAttachment{};
        clearAttachment.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearAttachment.colorAttachment = 0;
        clearAttachment.clearValue.color.float32[0] = 0.018F + cameraLift;
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

private:
    QRect viewportPixels_;
    m3d::RenderSceneSnapshot snapshot_;
    m3d::Mat4 viewProjection_{};
};

VulkanViewport::VulkanViewport(QQuickItem* parent)
    : QQuickItem(parent) {
    setFlag(ItemHasContents, false);
    setAcceptTouchEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
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

QString VulkanViewport::projectionName() const {
    return camera_.projection() == m3d::CameraProjection::Perspective
        ? QStringLiteral("Perspective")
        : QStringLiteral("Orthographic");
}

void VulkanViewport::toggleProjection() {
    camera_.setProjection(camera_.projection() == m3d::CameraProjection::Perspective
                              ? m3d::CameraProjection::Orthographic
                              : m3d::CameraProjection::Perspective);
    cameraMutated();
}

void VulkanViewport::resetCamera() {
    camera_ = m3d::ViewportCamera{};
    cameraMutated();
}

void VulkanViewport::releaseResources() {
    // GPU/device-owned state is released from sceneGraphInvalidated() on the render
    // thread. This hook intentionally does not introduce GUI-thread Vulkan access.
}

void VulkanViewport::mousePressEvent(QMouseEvent* event) {
    navigationButton_ = event->button();
    lastMousePosition_ = event->position();
    event->accept();
}

void VulkanViewport::mouseMoveEvent(QMouseEvent* event) {
    if (navigationButton_ == Qt::NoButton) {
        event->ignore();
        return;
    }

    const QPointF delta = event->position() - lastMousePosition_;
    lastMousePosition_ = event->position();
    if (navigationButton_ == Qt::LeftButton) {
        orbitByPixels(delta);
    } else {
        panByPixels(delta);
    }
    event->accept();
}

void VulkanViewport::mouseReleaseEvent(QMouseEvent* event) {
    navigationButton_ = Qt::NoButton;
    event->accept();
}

void VulkanViewport::wheelEvent(QWheelEvent* event) {
    const int wheelUnits = event->angleDelta().y();
    if (wheelUnits == 0) {
        event->ignore();
        return;
    }
    const float factor = std::exp(static_cast<float>(wheelUnits) * kWheelZoomExponentPerUnit);
    camera_.zoom(factor);
    cameraMutated();
    event->accept();
}

void VulkanViewport::touchEvent(QTouchEvent* event) {
    const auto points = event->points();
    const int pointCount = points.size();

    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel || pointCount == 0) {
        lastTouchPointCount_ = 0;
        lastTouchSpan_ = 0.0F;
        lastTouchCentroid_ = {};
        event->accept();
        return;
    }

    const QPointF centroid = touchCentroid(points);
    const float span = touchSpan(points);
    if (event->type() == QEvent::TouchBegin || pointCount != lastTouchPointCount_) {
        lastTouchPointCount_ = pointCount;
        lastTouchCentroid_ = centroid;
        lastTouchSpan_ = span;
        event->accept();
        return;
    }

    if (pointCount == 1) {
        orbitByPixels(centroid - lastTouchCentroid_);
    } else {
        panByPixels(centroid - lastTouchCentroid_);
        if (lastTouchSpan_ > 1.0F && span > 1.0F) {
            camera_.zoom(span / lastTouchSpan_);
            cameraMutated();
        }
    }

    lastTouchCentroid_ = centroid;
    lastTouchSpan_ = span;
    lastTouchPointCount_ = pointCount;
    event->accept();
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
    const int pixelWidth = std::max(0, right - left);
    const int pixelHeight = std::max(0, bottom - top);
    const QRect viewportPixels(left, top, pixelWidth, pixelHeight);

    m3d::RenderSceneSnapshot snapshot;
    if (controller_) {
        snapshot = controller_->renderSnapshot();
    }
    const float aspect = pixelHeight > 0
        ? static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight)
        : 1.0F;
    renderer_->synchronize(viewportPixels, std::move(snapshot), camera_.viewProjectionMatrix(aspect));
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

void VulkanViewport::orbitByPixels(QPointF delta) {
    camera_.orbit(-static_cast<float>(delta.x()) * kOrbitRadiansPerPixel,
                  -static_cast<float>(delta.y()) * kOrbitRadiansPerPixel);
    cameraMutated();
}

void VulkanViewport::panByPixels(QPointF delta) {
    const float units = worldUnitsPerPixel();
    camera_.pan(-static_cast<float>(delta.x()) * units,
                static_cast<float>(delta.y()) * units);
    cameraMutated();
}

float VulkanViewport::worldUnitsPerPixel() const noexcept {
    const float viewportHeight = std::max(static_cast<float>(height()), 1.0F);
    if (camera_.projection() == m3d::CameraProjection::Orthographic) {
        return camera_.orthographicHeight() / viewportHeight;
    }
    const float fovRadians = camera_.perspectiveFovDegrees() * kDegreesToRadians;
    const float visibleHeight = 2.0F * camera_.distance() * std::tan(fovRadians * 0.5F);
    return visibleHeight / viewportHeight;
}

void VulkanViewport::cameraMutated() {
    emit cameraChanged();
    update();
}
