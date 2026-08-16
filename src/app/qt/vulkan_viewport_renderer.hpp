#pragma once

#include "mobile3d/render/render_snapshot.hpp"
#include "mobile3d/render/viewport_camera.hpp"

#include <QPoint>
#include <QRect>

#include <cstdint>
#include <memory>
#include <optional>

class QQuickWindow;

struct VulkanRecordStats final {
    bool recorded{false};
    std::uint32_t meshDraws{0};
    std::uint32_t outlineDraws{0};
};

struct VulkanPickResult final {
    bool resultReady{false};
    bool waitingForReadback{false};
    std::optional<m3d::ObjectId> object;
};

class VulkanViewportRenderer final {
public:
    struct Impl;

    VulkanViewportRenderer();
    ~VulkanViewportRenderer();

    VulkanViewportRenderer(const VulkanViewportRenderer&) = delete;
    VulkanViewportRenderer& operator=(const VulkanViewportRenderer&) = delete;

    void synchronize(QRect viewportPixels, m3d::RenderSceneSnapshot snapshot,
                     m3d::Mat4 viewProjection);
    void requestPick(QPoint viewportPixel);
    [[nodiscard]] VulkanPickResult recordPicking(QQuickWindow* window);
    [[nodiscard]] VulkanRecordStats record(QQuickWindow* window);
    void release();

private:
    std::unique_ptr<Impl> impl_;
    QRect viewportPixels_;
    m3d::RenderSceneSnapshot snapshot_;
    m3d::Mat4 viewProjection_{};
    std::optional<QPoint> pendingPickPixel_;
};
