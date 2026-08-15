#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <QtQml/qqmlregistration.h>

class EditorController;
class QQuickWindow;
class VulkanViewportRenderer;

class VulkanViewport : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(bool vulkanActive READ vulkanActive NOTIFY backendChanged)
    Q_PROPERTY(QString backendName READ backendName NOTIFY backendChanged)

public:
    explicit VulkanViewport(QQuickItem* parent = nullptr);
    ~VulkanViewport() override;

    [[nodiscard]] QObject* controller() const noexcept;
    void setController(QObject* controller);

    [[nodiscard]] bool vulkanActive() const noexcept { return vulkanActive_; }
    [[nodiscard]] QString backendName() const { return backendName_; }

signals:
    void controllerChanged();
    void backendChanged();

protected:
    void releaseResources() override;

private:
    void handleWindowChanged(QQuickWindow* window);
    void sync();
    void cleanup();
    void recordVulkanCommands();
    void updateBackendState(QQuickWindow* window);

    QPointer<EditorController> controller_;
    QPointer<QQuickWindow> connectedWindow_;
    VulkanViewportRenderer* renderer_{nullptr};
    QMetaObject::Connection controllerProjectConnection_;
    QMetaObject::Connection controllerSelectionConnection_;
    bool vulkanActive_{false};
    QString backendName_{QStringLiteral("Unavailable")};
};
