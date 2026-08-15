#include "vulkan_shader_library.hpp"

#include <QFile>
#include <rhi/qshader.h>

#include <cstdint>
#include <cstring>

namespace {

constexpr std::uint32_t kSpirvMagic = 0x07230203U;

[[nodiscard]] QByteArray loadSpirv(const QString& resourcePath,
                                   QShader::Stage expectedStage,
                                   QString* error) {
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Could not open embedded shader resource: %1").arg(resourcePath);
        }
        return {};
    }

    const QShader shader = QShader::fromSerialized(file.readAll());
    if (!shader.isValid()) {
        if (error) {
            *error = QStringLiteral("Embedded qsb shader is invalid: %1").arg(resourcePath);
        }
        return {};
    }
    if (shader.stage() != expectedStage) {
        if (error) {
            *error = QStringLiteral("Embedded shader stage mismatch: %1").arg(resourcePath);
        }
        return {};
    }

    for (const auto& key : shader.availableShaders()) {
        if (key.source() != QShader::SpirvShader ||
            key.sourceVariant() != QShader::StandardShader) {
            continue;
        }

        const QByteArray code = shader.shader(key).shader();
        if (code.size() < static_cast<qsizetype>(sizeof(std::uint32_t)) ||
            code.size() % static_cast<qsizetype>(sizeof(std::uint32_t)) != 0) {
            continue;
        }

        std::uint32_t magic = 0;
        std::memcpy(&magic, code.constData(), sizeof(magic));
        if (magic == kSpirvMagic) {
            return code;
        }
    }

    if (error) {
        *error = QStringLiteral("No valid SPIR-V payload found in: %1").arg(resourcePath);
    }
    return {};
}

} // namespace

QByteArray VulkanShaderLibrary::viewportLineVertexSpirv(QString* error) {
    return loadSpirv(QStringLiteral(":/shaders/viewport_line.vert.qsb"),
                     QShader::VertexStage, error);
}

QByteArray VulkanShaderLibrary::viewportLineFragmentSpirv(QString* error) {
    return loadSpirv(QStringLiteral(":/shaders/viewport_line.frag.qsb"),
                     QShader::FragmentStage, error);
}

bool VulkanShaderLibrary::validateViewportLineShaders(QString* error) {
    QString localError;
    if (viewportLineVertexSpirv(&localError).isEmpty()) {
        if (error) {
            *error = localError;
        }
        return false;
    }
    if (viewportLineFragmentSpirv(&localError).isEmpty()) {
        if (error) {
            *error = localError;
        }
        return false;
    }
    if (error) {
        error->clear();
    }
    return true;
}
