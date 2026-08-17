#pragma once

#include "mobile3d/editor/mesh_edit_snapshot.hpp"
#include "mobile3d/editor/transform_gizmo.hpp"
#include "mobile3d/render/render_snapshot.hpp"
#include "mobile3d/render/viewport_camera.hpp"

#include <QPoint>
#include <QRect>

#include <cstdint>
#include <memory>
#include <optional>

class QQuickWindow;

struct VulkanGizmoPresentation final {
    bool visible{false};
    m3d::TransformTool tool{m3d::TransformTool::Translate};
    m3d::TransformSpace space{m3d::TransformSpace::Global};
    m3d::PivotMode pivotMode{m3d::PivotMode::Median};
    m3d::Vec3 pivotWorld{};
    m3d::GizmoBasis basis{};
    float worldSize{1.0F};
    std::optional<m3d::TransformConstraint> activeConstraint;
};

struct VulkanRecordStats final {
    bool recorded{false};
    bool needsAnotherFrame{false};
    bool pipelineCacheLoaded{false};
    std::uint32_t meshDraws{0};
    std::uint32_t outlineDraws{0};
    std::uint32_t gizmoDraws{0};
    std::uint32_t editOverlayDraws{0};
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
                     m3d::MeshEditPresentationSnapshot editMesh,
                     m3d::Mat4 viewProjection, VulkanGizmoPresentation gizmo);
    void requestPick(QPoint viewportPixel);
    [[nodiscard]] VulkanPickResult recordPicking(QQuickWindow* window);
    [[nodiscard]] VulkanRecordStats record(QQuickWindow* window);
    void release();

private:
    std::unique_ptr<Impl> impl_;
    QRect viewportPixels_;
    m3d::RenderSceneSnapshot snapshot_;
    m3d::MeshEditPresentationSnapshot editMesh_;
    m3d::Mat4 viewProjection_{};
    VulkanGizmoPresentation gizmo_{};
    std::optional<QPoint> pendingPickPixel_;
};
