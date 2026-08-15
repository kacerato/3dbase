#pragma once

#include <QByteArray>
#include <QString>

class VulkanShaderLibrary final {
public:
    [[nodiscard]] static QByteArray viewportLineVertexSpirv(QString* error = nullptr);
    [[nodiscard]] static QByteArray viewportLineFragmentSpirv(QString* error = nullptr);
    [[nodiscard]] static QByteArray viewportMeshVertexSpirv(QString* error = nullptr);
    [[nodiscard]] static QByteArray viewportMeshFragmentSpirv(QString* error = nullptr);
    [[nodiscard]] static bool validateViewportShaders(QString* error = nullptr);
};
