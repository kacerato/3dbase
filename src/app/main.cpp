#include "editor_controller.hpp"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QVariantMap>

#include <algorithm>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("kacerato"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("kacerato.dev"));
    QCoreApplication::setApplicationName(QStringLiteral("Mobile3DStudio"));

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    EditorController controller;
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

    const auto arguments = QCoreApplication::arguments();
    const bool smokeTest = std::find(arguments.cbegin(), arguments.cend(),
                                     QStringLiteral("--smoke-test")) != arguments.cend();
    if (smokeTest) {
        return 0;
    }

    return app.exec();
}
