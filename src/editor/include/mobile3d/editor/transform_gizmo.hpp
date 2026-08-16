#pragma once

#include "mobile3d/core/math.hpp"

namespace m3d {

enum class TransformTool {
    Translate,
    Rotate,
    Scale,
};

enum class TransformSpace {
    Global,
    Local,
};

enum class TransformConstraint {
    Free,
    X,
    Y,
    Z,
    XY,
    XZ,
    YZ,
};

enum class PivotMode {
    Median,
    Active,
    IndividualOrigins,
};

struct GizmoBasis final {
    Vec3 x{1.0F, 0.0F, 0.0F};
    Vec3 y{0.0F, 1.0F, 0.0F};
    Vec3 z{0.0F, 0.0F, 1.0F};
};

struct TransformSnapSettings final {
    bool translationEnabled{false};
    float translationStep{1.0F};
    bool rotationEnabled{false};
    float rotationStepRadians{0.261799388F}; // 15 degrees.
    bool scaleEnabled{false};
    float scaleStep{0.1F};
};

[[nodiscard]] GizmoBasis makeGizmoBasis(TransformSpace space,
                                        Quat objectWorldRotation) noexcept;

// Components are expressed along the gizmo's X/Y/Z axes. The returned delta
// is in world space, after axis/plane constraints and optional snapping.
[[nodiscard]] Vec3 composeTranslationDelta(
    Vec3 gizmoComponents,
    TransformConstraint constraint,
    const GizmoBasis& basis,
    const TransformSnapSettings& snapping = {}) noexcept;

// Rotation currently accepts the three axis constraints. Free/plane rotation
// returns identity until a trackball/arcball solver is introduced.
[[nodiscard]] Quat composeRotationDelta(
    float angleRadians,
    TransformConstraint axisConstraint,
    const GizmoBasis& basis,
    const TransformSnapSettings& snapping = {}) noexcept;

// factor is a multiplicative scale (1 = unchanged). Free means uniform scale;
// axis and plane constraints affect only the requested components.
[[nodiscard]] Vec3 composeScaleFactors(
    float factor,
    TransformConstraint constraint,
    const TransformSnapSettings& snapping = {}) noexcept;

} // namespace m3d
