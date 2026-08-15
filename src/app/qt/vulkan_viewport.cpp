#include "vulkan_viewport.hpp"

#include "editor_controller.hpp"
#include "vulkan_viewport_renderer.hpp"

#include <QEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QRect>
#include <QRectF>
#include <QSGRendererInterface>
#include <QTouchEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr float kOrbitRadiansPerPixel = 0.006F;
constexpr float kWheelZoomExponentPerUnit = 0.0015F;
constexpr float kDegreesToRadians = 0.01745329251994329577F;
constexpr qreal kTapSlop = 7.0;

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
    if (points.isEmpty()) return {};
    QPointF sum;
    for (const auto& point : points) sum += point.position();
    return sum / static_cast<qreal>(points.size());
}

[[nodiscard]] float touchSpan(const QList<QEventPoint>& points) {
    if (points.size() < 2) return 0.0F;
    const QPointF delta = points.at(1).position() - points.at(0).position();
    return std::hypot(static_cast<float>(delta.x()), static_cast<float>(delta.y()));
}

[[nodiscard]] bool exceedsTapSlop(QPointF delta) noexcept {
    return std::hypot(delta.x(), delta.y()) > kTapSlop;
}

} // namespace

VulkanViewport::VulkanViewport(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, false);
    setAcceptTouchEvents(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    connect(this, &QQuickItem::windowChanged, this, &VulkanViewport::handleWindowChanged);
}

VulkanViewport::~VulkanViewport() {
    if (connectedWindow_) disconnect(connectedWindow_, nullptr, this, nullptr);
    delete renderer_;
}

QObject* VulkanViewport::controller() const noexcept { return controller_.data(); }

void VulkanViewport::setController(QObject* controller) {
    auto* typedController = qobject_cast<EditorController*>(controller);
    if (controller_.data() == typedController) return;
    if (controllerProjectConnection_) disconnect(controllerProjectConnection_);
    if (controllerSelectionConnection_) disconnect(controllerSelectionConnection_);
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
        ? QStringLiteral("Perspective") : QStringLiteral("Orthographic");
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

void VulkanViewport::requestPickAt(double x, double y) {
    if (x < 0.0 || y < 0.0 || x >= width() || y >= height()) return;
    pendingPickPosition_ = QPointF(x, y);
    pickRequested_ = true;
    update();
}

void VulkanViewport::releaseResources() {
    // Device-owned state is released from sceneGraphInvalidated on the render thread.
}

void VulkanViewport::mousePressEvent(QMouseEvent* event) {
    navigationButton_ = event->button();
    lastMousePosition_ = event->position();
    mousePressPosition_ = event->position();
    mouseDragExceeded_ = false;
    event->accept();
}

void VulkanViewport::mouseMoveEvent(QMouseEvent* event) {
    if (navigationButton_ == Qt::NoButton) {
        event->ignore();
        return;
    }
    const QPointF delta = event->position() - lastMousePosition_;
    lastMousePosition_ = event->position();
    if (!mouseDragExceeded_ && exceedsTapSlop(event->position() - mousePressPosition_)) {
        mouseDragExceeded_ = true;
    }
    if (navigationButton_ == Qt::LeftButton) orbitByPixels(delta); else panByPixels(delta);
    event->accept();
}

void VulkanViewport::mouseReleaseEvent(QMouseEvent* event) {
    const bool shouldPick = navigationButton_ == Qt::LeftButton && !mouseDragExceeded_;
    navigationButton_ = Qt::NoButton;
    if (shouldPick) requestPickAt(event->position().x(), event->position().y());
    event->accept();
}

void VulkanViewport::wheelEvent(QWheelEvent* event) {
    const int wheelUnits = event->angleDelta().y();
    if (wheelUnits == 0) {
        event->ignore();
        return;
    }
    camera_.zoom(std::exp(static_cast<float>(wheelUnits) * kWheelZoomExponentPerUnit));
    cameraMutated();
    event->accept();
}

void VulkanViewport::touchEvent(QTouchEvent* event) {
    const auto points = event->points();
    const qsizetype pointCount = points.size();
    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel || pointCount == 0) {
        if (event->type() == QEvent::TouchEnd && lastTouchPointCount_ == 1 && !touchDragExceeded_) {
            requestPickAt(lastTouchCentroid_.x(), lastTouchCentroid_.y());
        }
        lastTouchPointCount_ = 0;
        lastTouchSpan_ = 0.0F;
        lastTouchCentroid_ = {};
        touchStartCentroid_ = {};
        touchDragExceeded_ = false;
        event->accept();
        return;
    }

    const QPointF centroid = touchCentroid(points);
    const float span = touchSpan(points);
    if (event->type() == QEvent::TouchBegin || pointCount != lastTouchPointCount_) {
        lastTouchPointCount_ = pointCount;
        lastTouchCentroid_ = centroid;
        touchStartCentroid_ = centroid;
        lastTouchSpan_ = span;
        touchDragExceeded_ = pointCount > 1;
        event->accept();
        return;
    }

    if (pointCount == 1) {
        if (!touchDragExceeded_ && exceedsTapSlop(centroid - touchStartCentroid_)) {
            touchDragExceeded_ = true;
        }
        orbitByPixels(centroid - lastTouchCentroid_);
    } else {
        touchDragExceeded_ = true;
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
    if (connectedWindow_) disconnect(connectedWindow_, nullptr, this, nullptr);
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
    connect(window, &QQuickWindow::beforeRendering,
            this, &VulkanViewport::recordPickCommands, Qt::DirectConnection);
    connect(window, &QQuickWindow::beforeRenderPassRecording,
            this, &VulkanViewport::recordVulkanCommands, Qt::DirectConnection);
    connect(window, &QQuickWindow::sceneGraphInvalidated,
            this, &VulkanViewport::cleanup, Qt::DirectConnection);
    updateBackendState(window);
    update();
}

void VulkanViewport::sync() {
    auto* currentWindow = window();
    if (!currentWindow) return;
    if (!renderer_) renderer_ = new VulkanViewportRenderer;

    const QRectF sceneRect = mapRectToScene(boundingRect());
    const qreal dpr = currentWindow->devicePixelRatio();
    const int left = static_cast<int>(std::floor(static_cast<double>(sceneRect.left()) * dpr));
    const int top = static_cast<int>(std::floor(static_cast<double>(sceneRect.top()) * dpr));
    const int right = static_cast<int>(std::ceil(static_cast<double>(sceneRect.right()) * dpr));
    const int bottom = static_cast<int>(std::ceil(static_cast<double>(sceneRect.bottom()) * dpr));
    const int pixelWidth = std::max(0, right - left);
    const int pixelHeight = std::max(0, bottom - top);

    m3d::RenderSceneSnapshot snapshot;
    if (controller_) snapshot = controller_->renderSnapshot();
    const float aspect = pixelHeight > 0
        ? static_cast<float>(pixelWidth) / static_cast<float>(pixelHeight) : 1.0F;
    renderer_->synchronize(QRect(left, top, pixelWidth, pixelHeight), std::move(snapshot),
                           camera_.viewProjectionMatrix(aspect));

    if (pickRequested_ && pixelWidth > 0 && pixelHeight > 0) {
        const int x = std::clamp(
            static_cast<int>(std::floor(static_cast<double>(pendingPickPosition_.x()) * dpr)),
            0, pixelWidth - 1);
        const int y = std::clamp(
            static_cast<int>(std::floor(static_cast<double>(pendingPickPosition_.y()) * dpr)),
            0, pixelHeight - 1);
        renderer_->requestPick(QPoint(x, y));
        pickRequested_ = false;
    }
}

void VulkanViewport::cleanup() {
    if (renderer_) renderer_->release();
    delete renderer_;
    renderer_ = nullptr;
}

void VulkanViewport::recordPickCommands() {
    if (!renderer_) return;
    const auto result = renderer_->recordPicking(window());
    if (result.resultReady) {
        completedPickCount_.fetch_add(1, std::memory_order_relaxed);
        if (result.object) successfulPickCount_.fetch_add(1, std::memory_order_relaxed);
        const auto pickedObject = result.object;
        QMetaObject::invokeMethod(this, [this, pickedObject] {
            if (!controller_) return;
            if (pickedObject) {
                (void)controller_->selectObject(QString::fromStdString(pickedObject->toString()), false);
            } else {
                controller_->clearSelection();
            }
        }, Qt::QueuedConnection);
    }
    if (result.waitingForReadback) scheduleNextFrame();
}

void VulkanViewport::recordVulkanCommands() {
    if (!renderer_) return;
    const auto stats = renderer_->record(window());
    if (stats.recorded) recordedFrameCount_.fetch_add(1, std::memory_order_relaxed);
    if (stats.meshDraws > 0) {
        recordedMeshDrawCount_.fetch_add(stats.meshDraws, std::memory_order_relaxed);
    }
}

void VulkanViewport::updateBackendState(QQuickWindow* window) {
    const auto api = window && window->rendererInterface()
        ? window->rendererInterface()->graphicsApi() : QSGRendererInterface::Unknown;
    const bool isVulkan = api == QSGRendererInterface::Vulkan;
    const QString name = graphicsApiName(api);
    if (vulkanActive_ == isVulkan && backendName_ == name) return;
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
    return 2.0F * camera_.distance() * std::tan(fovRadians * 0.5F) / viewportHeight;
}

void VulkanViewport::cameraMutated() {
    emit cameraChanged();
    update();
}

void VulkanViewport::scheduleNextFrame() {
    QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);
}
