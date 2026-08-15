#include "editor_controller.hpp"
#include "vulkan_viewport.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("kacerato"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("kacerato.dev"));
    QCoreApplication::setApplicationName(QStringLiteral("Mobile3DStudio"));

    const auto arguments = QCoreApplication::arguments();
    const bool smokeTest = std::find(arguments.cbegin(), arguments.cend(),
                                     QStringLiteral("--smoke-test")) != arguments.cend();
    const bool vulkanSmokeTest = std::find(arguments.cbegin(), arguments.cend(),
                                           QStringLiteral("--vulkan-smoke-test")) != arguments.cend();
#if !defined(Q_OS_ANDROID)
    const bool requestedVulkan = std::find(arguments.cbegin(), arguments.cend(),
                                           QStringLiteral("--vulkan")) != arguments.cend();
#endif

#if defined(Q_OS_ANDROID)
    if (!smokeTest) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    }
#else
    if (!smokeTest && (requestedVulkan || vulkanSmokeTest)) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    }
#endif

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    EditorController controller;
    if (vulkanSmokeTest) {
        if (!controller.createProject(QStringLiteral("Vulkan Smoke Test")) ||
            !controller.addObject(QStringLiteral("Mesh"))) {
            return 3;
        }
    }

    QObject::connect(&app, &QGuiApplication::applicationStateChanged,
                     &controller, &EditorController::handleApplicationState);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &controller, [&controller] {
        (void)controller.autosaveNow();
    });

    QQmlApplicationEngine engine;
    QVariantMap initialProperties;
    initialProperties.insert(QStringLiteral("controller"), QVariant::fromValue(&controller));
    engine.setInitialProperties(initialProperties);
    engine.loadFromModule(QStringLiteral("Mobile3D"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    if (smokeTest) {
        return 0;
    }

    if (vulkanSmokeTest) {
        QObject* root = engine.rootObjects().constFirst();
        QTimer::singleShot(1500, &app, [&app, root] {
            auto* viewport = root ? root->findChild<VulkanViewport*>(QStringLiteral("nativeVulkanViewport"))
                                  : nullptr;
            const bool passed = viewport && viewport->vulkanActive() &&
                                viewport->recordedFrameCount() > 0;
            app.exit(passed ? 0 : 4);
        });
    }

    return app.exec();
}
