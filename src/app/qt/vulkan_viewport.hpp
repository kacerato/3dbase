#pragma once

#include "mobile3d/render/viewport_camera.hpp"

#include <QMetaObject>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <cstdint>

class EditorController;
class QMouseEvent;
class QQuickWindow;
class QTouchEvent;
class QWheelEvent;
class VulkanViewportRenderer;

class VulkanViewport : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(bool vulkanActive READ vulkanActive NOTIFY backendChanged)
    Q_PROPERTY(QString backendName READ backendName NOTIFY backendChanged)
    Q_PROPERTY(QString projectionName READ projectionName NOTIFY cameraChanged)
    Q_PROPERTY(double cameraDistance READ cameraDistance NOTIFY cameraChanged)

public:
    explicit VulkanViewport(QQuickItem* parent = nullptr);
    ~VulkanViewport() override;

    [[nodiscard]] QObject* controller() const noexcept;
    void setController(QObject* controller);

    [[nodiscard]] bool vulkanActive() const noexcept { return vulkanActive_; }
    [[nodiscard]] QString backendName() const { return backendName_; }
    [[nodiscard]] QString projectionName() const;
    [[nodiscard]] double cameraDistance() const noexcept { return static_cast<double>(camera_.distance()); }
    [[nodiscard]] std::uint64_t recordedFrameCount() const noexcept {
        return recordedFrameCount_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t recordedMeshDrawCount() const noexcept {
        return recordedMeshDrawCount_.load(std::memory_order_relaxed);
    }

    Q_INVOKABLE void toggleProjection();
    Q_INVOKABLE void resetCamera();

signals:
    void controllerChanged();
    void backendChanged();
    void cameraChanged();

protected:
    void releaseResources() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void touchEvent(QTouchEvent* event) override;

private:
    void handleWindowChanged(QQuickWindow* window);
    void sync();
    void cleanup();
    void recordVulkanCommands();
    void updateBackendState(QQuickWindow* window);
    void orbitByPixels(QPointF delta);
    void panByPixels(QPointF delta);
    [[nodiscard]] float worldUnitsPerPixel() const noexcept;
    void cameraMutated();

    QPointer<EditorController> controller_;
    QPointer<QQuickWindow> connectedWindow_;
    VulkanViewportRenderer* renderer_{nullptr};
    QMetaObject::Connection controllerProjectConnection_;
    QMetaObject::Connection controllerSelectionConnection_;
    m3d::ViewportCamera camera_;
    QPointF lastMousePosition_;
    QPointF lastTouchCentroid_;
    float lastTouchSpan_{0.0F};
    qsizetype lastTouchPointCount_{0};
    Qt::MouseButton navigationButton_{Qt::NoButton};
    std::atomic_uint64_t recordedFrameCount_{0};
    std::atomic_uint64_t recordedMeshDrawCount_{0};
    bool vulkanActive_{false};
    QString backendName_{QStringLiteral("Unavailable")};
};
