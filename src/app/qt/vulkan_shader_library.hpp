#pragma once

#include <QByteArray>
#include <QString>

class VulkanShaderLibrary final {
public:
    [[nodiscard]] static QByteArray viewportLineVertexSpirv(QString* error = nullptr);
    [[nodiscard]] static QByteArray viewportLineFragmentSpirv(QString* error = nullptr);
    [[nodiscard]] static bool validateViewportLineShaders(QString* error = nullptr);
};
