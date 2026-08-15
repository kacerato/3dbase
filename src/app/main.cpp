#include "editor_controller.hpp"
#include "vulkan_shader_library.hpp"
#include "vulkan_viewport.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickGraphicsConfiguration>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVariantMap>

#include <algorithm>

namespace {

int requestedMsaaSamples() {
    bool ok = false;
    const int configured = qEnvironmentVariableIntValue("MOBILE3D_MSAA_SAMPLES", &ok);
    if (!ok) return 4;

    switch (configured) {
    case 1:
    case 2:
    case 4:
    case 8:
        return configured;
    default:
        qWarning() << "Ignoring unsupported MOBILE3D_MSAA_SAMPLES value" << configured
                   << "and requesting 4x MSAA instead";
        return 4;
    }
}

} // namespace

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
    if (!smokeTest) QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
#else
    if (!smokeTest && (requestedVulkan || vulkanSmokeTest)) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    }
#endif

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    if (vulkanSmokeTest) {
        QString shaderError;
        if (!VulkanShaderLibrary::validateViewportShaders(&shaderError)) {
            qCritical().noquote() << shaderError;
            return 5;
        }
    }

    EditorController controller;
    if (vulkanSmokeTest) {
        if (!controller.createProject(QStringLiteral("Vulkan Smoke Test")) ||
            !controller.addObject(QStringLiteral("Mesh"))) return 3;
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
    if (engine.rootObjects().isEmpty()) return -1;

    auto* rootWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (!rootWindow) return -2;
    if (smokeTest) return 0;

    auto requestedFormat = rootWindow->requestedFormat();
    requestedFormat.setSamples(requestedMsaaSamples());
    rootWindow->setFormat(requestedFormat);

    auto graphicsConfiguration = rootWindow->graphicsConfiguration();
    graphicsConfiguration.setDepthBufferFor2D(false);
    rootWindow->setGraphicsConfiguration(graphicsConfiguration);
    rootWindow->show();

    if (vulkanSmokeTest) {
        QObject* root = rootWindow;
        QTimer::singleShot(500, &app, [root] {
            auto* viewport = root->findChild<VulkanViewport*>(QStringLiteral("nativeVulkanViewport"));
            if (viewport && viewport->width() > 0.0 && viewport->height() > 0.0) {
                viewport->requestPickAt(viewport->width() * 0.5, viewport->height() * 0.5);
            }
        });
        QTimer::singleShot(2500, &app, [&app, root] {
            auto* viewport = root->findChild<VulkanViewport*>(QStringLiteral("nativeVulkanViewport"));
            const bool passed = viewport && viewport->vulkanActive() &&
                                viewport->recordedFrameCount() > 0 &&
                                viewport->recordedMeshDrawCount() > 0 &&
                                viewport->completedPickCount() > 0 &&
                                viewport->successfulPickCount() > 0;
            app.exit(passed ? 0 : 4);
        });
    }

    return app.exec();
}
