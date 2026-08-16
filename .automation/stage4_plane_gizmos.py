from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}")
    file.write_text(text.replace(old, new, 1), encoding="utf-8")

renderer = "src/app/qt/vulkan_viewport_renderer.cpp"
viewport = "src/app/qt/vulkan_viewport.cpp"

replace_once(renderer,
'''[[nodiscard]] std::array<float, 4> gizmoAxisColor(
    m3d::TransformConstraint axis,
    const std::optional<m3d::TransformConstraint>& active) noexcept {
    if (active && *active == axis) return {1.0F, 0.82F, 0.12F, 1.0F};
    switch (axis) {
    case m3d::TransformConstraint::X: return {0.92F, 0.16F, 0.18F, 1.0F};
    case m3d::TransformConstraint::Y: return {0.20F, 0.82F, 0.28F, 1.0F};
    case m3d::TransformConstraint::Z: return {0.22F, 0.42F, 0.96F, 1.0F};
    default: return {0.90F, 0.90F, 0.92F, 1.0F};
    }
}
''',
'''[[nodiscard]] std::array<float, 4> gizmoAxisColor(
    m3d::TransformConstraint axis,
    const std::optional<m3d::TransformConstraint>& active) noexcept {
    if (active && *active == axis) return {1.0F, 0.82F, 0.12F, 1.0F};
    switch (axis) {
    case m3d::TransformConstraint::X: return {0.92F, 0.16F, 0.18F, 1.0F};
    case m3d::TransformConstraint::Y: return {0.20F, 0.82F, 0.28F, 1.0F};
    case m3d::TransformConstraint::Z: return {0.22F, 0.42F, 0.96F, 1.0F};
    default: return {0.90F, 0.90F, 0.92F, 1.0F};
    }
}

[[nodiscard]] std::array<float, 4> gizmoPlaneColor(
    m3d::TransformConstraint plane,
    const std::optional<m3d::TransformConstraint>& active) noexcept {
    if (active && *active == plane) return {1.0F, 0.82F, 0.12F, 1.0F};
    switch (plane) {
    case m3d::TransformConstraint::XY: return {0.82F, 0.72F, 0.18F, 1.0F};
    case m3d::TransformConstraint::XZ: return {0.82F, 0.24F, 0.68F, 1.0F};
    case m3d::TransformConstraint::YZ: return {0.18F, 0.76F, 0.76F, 1.0F};
    default: return {0.90F, 0.90F, 0.92F, 1.0F};
    }
}
''')

replace_once(renderer,
'''void appendRotationRing(std::vector<LineVertex>& vertices,
''',
'''void appendPlaneHandle(std::vector<LineVertex>& vertices,
                       const VulkanGizmoPresentation& gizmo,
                       m3d::Vec3 axisA, m3d::Vec3 axisB,
                       m3d::TransformConstraint constraint) {
    const auto color = gizmoPlaneColor(constraint, gizmo.activeConstraint);
    const float inner = gizmo.worldSize * 0.22F;
    const float outer = gizmo.worldSize * 0.42F;
    const auto corner = [&](float a, float b) {
        return addVector(gizmo.pivotWorld,
                         addVector(scaleVector(axisA, a), scaleVector(axisB, b)));
    };
    const m3d::Vec3 p00 = corner(inner, inner);
    const m3d::Vec3 p10 = corner(outer, inner);
    const m3d::Vec3 p11 = corner(outer, outer);
    const m3d::Vec3 p01 = corner(inner, outer);
    appendGizmoLine(vertices, p00, p10, color);
    appendGizmoLine(vertices, p10, p11, color);
    appendGizmoLine(vertices, p11, p01, color);
    appendGizmoLine(vertices, p01, p00, color);
}

void appendRotationRing(std::vector<LineVertex>& vertices,
''')

replace_once(renderer,
'''    vertices.reserve(gizmo.tool == m3d::TransformTool::Rotate ? 384U : 40U);
''',
'''    vertices.reserve(gizmo.tool == m3d::TransformTool::Rotate ? 384U : 72U);
''')

replace_once(renderer,
'''    if (gizmo.tool == m3d::TransformTool::Translate) {
        appendAxisArrow(vertices, gizmo, x, y, z, m3d::TransformConstraint::X);
        appendAxisArrow(vertices, gizmo, y, x, z, m3d::TransformConstraint::Y);
        appendAxisArrow(vertices, gizmo, z, x, y, m3d::TransformConstraint::Z);
    } else if (gizmo.tool == m3d::TransformTool::Rotate) {
        appendRotationRing(vertices, gizmo, y, z, m3d::TransformConstraint::X);
        appendRotationRing(vertices, gizmo, x, z, m3d::TransformConstraint::Y);
        appendRotationRing(vertices, gizmo, x, y, m3d::TransformConstraint::Z);
    } else {
        appendScaleAxis(vertices, gizmo, x, y, z, m3d::TransformConstraint::X);
        appendScaleAxis(vertices, gizmo, y, x, z, m3d::TransformConstraint::Y);
        appendScaleAxis(vertices, gizmo, z, x, y, m3d::TransformConstraint::Z);
    }
''',
'''    if (gizmo.tool == m3d::TransformTool::Translate) {
        appendAxisArrow(vertices, gizmo, x, y, z, m3d::TransformConstraint::X);
        appendAxisArrow(vertices, gizmo, y, x, z, m3d::TransformConstraint::Y);
        appendAxisArrow(vertices, gizmo, z, x, y, m3d::TransformConstraint::Z);
        appendPlaneHandle(vertices, gizmo, x, y, m3d::TransformConstraint::XY);
        appendPlaneHandle(vertices, gizmo, x, z, m3d::TransformConstraint::XZ);
        appendPlaneHandle(vertices, gizmo, y, z, m3d::TransformConstraint::YZ);
    } else if (gizmo.tool == m3d::TransformTool::Rotate) {
        appendRotationRing(vertices, gizmo, y, z, m3d::TransformConstraint::X);
        appendRotationRing(vertices, gizmo, x, z, m3d::TransformConstraint::Y);
        appendRotationRing(vertices, gizmo, x, y, m3d::TransformConstraint::Z);
    } else if (gizmo.space == m3d::TransformSpace::Local &&
               gizmo.pivotMode == m3d::PivotMode::IndividualOrigins) {
        appendScaleAxis(vertices, gizmo, x, y, z, m3d::TransformConstraint::X);
        appendScaleAxis(vertices, gizmo, y, x, z, m3d::TransformConstraint::Y);
        appendScaleAxis(vertices, gizmo, z, x, y, m3d::TransformConstraint::Z);
        appendPlaneHandle(vertices, gizmo, x, y, m3d::TransformConstraint::XY);
        appendPlaneHandle(vertices, gizmo, x, z, m3d::TransformConstraint::XZ);
        appendPlaneHandle(vertices, gizmo, y, z, m3d::TransformConstraint::YZ);
    }
''')

replace_once(viewport,
'''struct GizmoHit final {
    m3d::TransformConstraint constraint{m3d::TransformConstraint::Free};
    QPointF screenDirection{};
    float screenLength{1.0F};
};
''',
'''struct GizmoHit final {
    m3d::TransformConstraint constraint{m3d::TransformConstraint::Free};
    QPointF screenDirection{};
    float screenLength{1.0F};
    QPointF planeVectorA{};
    QPointF planeVectorB{};
};
''')

replace_once(viewport,
'''[[nodiscard]] float distanceToSegment(QPointF point, QPointF start, QPointF end,
                                      QPointF* direction = nullptr,
                                      float* segmentLength = nullptr) noexcept {
''',
'''[[nodiscard]] float cross2D(QPointF a, QPointF b, QPointF c) noexcept {
    return static_cast<float>((b.x() - a.x()) * (c.y() - a.y()) -
                              (b.y() - a.y()) * (c.x() - a.x()));
}

[[nodiscard]] bool pointInTriangle(QPointF point, QPointF a, QPointF b, QPointF c) noexcept {
    const float c1 = cross2D(a, b, point);
    const float c2 = cross2D(b, c, point);
    const float c3 = cross2D(c, a, point);
    const bool hasNegative = c1 < 0.0F || c2 < 0.0F || c3 < 0.0F;
    const bool hasPositive = c1 > 0.0F || c2 > 0.0F || c3 > 0.0F;
    return !(hasNegative && hasPositive);
}

[[nodiscard]] bool pointInQuad(QPointF point, QPointF a, QPointF b,
                               QPointF c, QPointF d) noexcept {
    return pointInTriangle(point, a, b, c) || pointInTriangle(point, a, c, d);
}

[[nodiscard]] bool isPlaneConstraint(m3d::TransformConstraint constraint) noexcept {
    return constraint == m3d::TransformConstraint::XY ||
           constraint == m3d::TransformConstraint::XZ ||
           constraint == m3d::TransformConstraint::YZ;
}

[[nodiscard]] float distanceToSegment(QPointF point, QPointF start, QPointF end,
                                      QPointF* direction = nullptr,
                                      float* segmentLength = nullptr) noexcept {
''')

plane_insert = '''    struct PlaneCandidate final {
        m3d::TransformConstraint constraint;
        m3d::Vec3 axisA;
        m3d::Vec3 axisB;
    };
    const std::array<PlaneCandidate, 3> planes{{
        {m3d::TransformConstraint::XY, gizmo.basis.x, gizmo.basis.y},
        {m3d::TransformConstraint::XZ, gizmo.basis.x, gizmo.basis.z},
        {m3d::TransformConstraint::YZ, gizmo.basis.y, gizmo.basis.z},
    }};
    constexpr float planeInner = 0.22F;
    constexpr float planeOuter = 0.42F;
    for (const auto& plane : planes) {
        const auto worldCorner = [&](float a, float b) {
            return addVector(gizmo.pivotWorld,
                             addVector(scaleVector(plane.axisA, gizmo.worldSize * a),
                                       scaleVector(plane.axisB, gizmo.worldSize * b)));
        };
        const auto p00 = projectWorld(viewProjection, worldCorner(planeInner, planeInner), width, height);
        const auto p10 = projectWorld(viewProjection, worldCorner(planeOuter, planeInner), width, height);
        const auto p11 = projectWorld(viewProjection, worldCorner(planeOuter, planeOuter), width, height);
        const auto p01 = projectWorld(viewProjection, worldCorner(planeInner, planeOuter), width, height);
        const auto axisAEnd = projectWorld(
            viewProjection, addVector(gizmo.pivotWorld, scaleVector(plane.axisA, gizmo.worldSize)),
            width, height);
        const auto axisBEnd = projectWorld(
            viewProjection, addVector(gizmo.pivotWorld, scaleVector(plane.axisB, gizmo.worldSize)),
            width, height);
        if (!p00 || !p10 || !p11 || !p01 || !axisAEnd || !axisBEnd) continue;
        if (pointInQuad(point, *p00, *p10, *p11, *p01)) {
            return GizmoHit{plane.constraint, {}, 1.0F,
                            *axisAEnd - *pivotScreen, *axisBEnd - *pivotScreen};
        }
    }

'''
replace_once(viewport,
'''    struct Candidate final {
        m3d::TransformConstraint constraint;
        m3d::Vec3 axis;
    };
''', plane_insert + '''    struct Candidate final {
        m3d::TransformConstraint constraint;
        m3d::Vec3 axis;
    };
''')

replace_once(viewport,
'''    transformAxisScreenUnit_ = hit->screenDirection;
    transformAxisScreenLength_ = std::max(hit->screenLength, 1.0F);
    transformWorldSize_ = gizmo.worldSize;
''',
'''    transformAxisScreenUnit_ = hit->screenDirection;
    transformAxisScreenLength_ = std::max(hit->screenLength, 1.0F);
    transformPlaneScreenVectorA_ = hit->planeVectorA;
    transformPlaneScreenVectorB_ = hit->planeVectorB;
    transformWorldSize_ = gizmo.worldSize;
''')

old_translate = '''        } else {
            const float pixels = dotPoint(screenDelta, transformAxisScreenUnit_);
            const float component = pixels / transformAxisScreenLength_ * transformWorldSize_;
            if (transformDragConstraint_ == m3d::TransformConstraint::X) components.x = component;
            if (transformDragConstraint_ == m3d::TransformConstraint::Y) components.y = component;
            if (transformDragConstraint_ == m3d::TransformConstraint::Z) components.z = component;
        }
'''
new_translate = '''        } else if (isPlaneConstraint(transformDragConstraint_)) {
            const float ax = static_cast<float>(transformPlaneScreenVectorA_.x());
            const float ay = static_cast<float>(transformPlaneScreenVectorA_.y());
            const float bx = static_cast<float>(transformPlaneScreenVectorB_.x());
            const float by = static_cast<float>(transformPlaneScreenVectorB_.y());
            const float dx = static_cast<float>(screenDelta.x());
            const float dy = static_cast<float>(screenDelta.y());
            const float determinant = ax * by - ay * bx;
            if (std::fabs(determinant) <= 1.0e-5F) return false;
            const float componentA = (dx * by - dy * bx) / determinant * transformWorldSize_;
            const float componentB = (ax * dy - ay * dx) / determinant * transformWorldSize_;
            if (transformDragConstraint_ == m3d::TransformConstraint::XY) {
                components.x = componentA; components.y = componentB;
            } else if (transformDragConstraint_ == m3d::TransformConstraint::XZ) {
                components.x = componentA; components.z = componentB;
            } else {
                components.y = componentA; components.z = componentB;
            }
        } else {
            const float pixels = dotPoint(screenDelta, transformAxisScreenUnit_);
            const float component = pixels / transformAxisScreenLength_ * transformWorldSize_;
            if (transformDragConstraint_ == m3d::TransformConstraint::X) components.x = component;
            if (transformDragConstraint_ == m3d::TransformConstraint::Y) components.y = component;
            if (transformDragConstraint_ == m3d::TransformConstraint::Z) components.z = component;
        }
'''
replace_once(viewport, old_translate, new_translate)

old_scale = '''        if (transformDragConstraint_ == m3d::TransformConstraint::Free) {
            const float diagonal = static_cast<float>(screenDelta.x() - screenDelta.y());
            factor = std::exp(diagonal * 0.006F);
        } else {
            const float pixels = dotPoint(screenDelta, transformAxisScreenUnit_);
            factor = std::max(0.01F, 1.0F + pixels / transformAxisScreenLength_);
        }
'''
new_scale = '''        if (transformDragConstraint_ == m3d::TransformConstraint::Free) {
            const float diagonal = static_cast<float>(screenDelta.x() - screenDelta.y());
            factor = std::exp(diagonal * 0.006F);
        } else if (isPlaneConstraint(transformDragConstraint_)) {
            const float ax = static_cast<float>(transformPlaneScreenVectorA_.x());
            const float ay = static_cast<float>(transformPlaneScreenVectorA_.y());
            const float bx = static_cast<float>(transformPlaneScreenVectorB_.x());
            const float by = static_cast<float>(transformPlaneScreenVectorB_.y());
            const float dx = static_cast<float>(screenDelta.x());
            const float dy = static_cast<float>(screenDelta.y());
            const float determinant = ax * by - ay * bx;
            if (std::fabs(determinant) <= 1.0e-5F) return false;
            const float a = (dx * by - dy * bx) / determinant;
            const float b = (ax * dy - ay * dx) / determinant;
            factor = std::max(0.01F, 1.0F + (a + b) * 0.5F);
        } else {
            const float pixels = dotPoint(screenDelta, transformAxisScreenUnit_);
            factor = std::max(0.01F, 1.0F + pixels / transformAxisScreenLength_);
        }
'''
replace_once(viewport, old_scale, new_scale)

replace_once(viewport,
'''    transformAxisScreenUnit_ = {};
    transformAxisScreenLength_ = 1.0F;
''',
'''    transformAxisScreenUnit_ = {};
    transformPlaneScreenVectorA_ = {};
    transformPlaneScreenVectorB_ = {};
    transformAxisScreenLength_ = 1.0F;
''')

print("plane gizmo rendering and hit testing applied")
