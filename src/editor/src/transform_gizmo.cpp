#include "mobile3d/editor/transform_gizmo.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace m3d {
namespace {

constexpr float kEpsilon = 1.0e-6F;
constexpr float kMinScale = 1.0e-4F;

[[nodiscard]] float lengthSquared(Vec3 value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

[[nodiscard]] Vec3 scaled(Vec3 value, float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] Vec3 added(Vec3 left, Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] Quat normalized(Quat value) noexcept {
    const float magnitudeSquared = value.x * value.x + value.y * value.y +
                                   value.z * value.z + value.w * value.w;
    if (magnitudeSquared <= kEpsilon) return {};
    const float inverseMagnitude = 1.0F / std::sqrt(magnitudeSquared);
    return {
        value.x * inverseMagnitude,
        value.y * inverseMagnitude,
        value.z * inverseMagnitude,
        value.w * inverseMagnitude,
    };
}

[[nodiscard]] Quat multiplied(Quat left, Quat right) noexcept {
    return {
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
    };
}

[[nodiscard]] Vec3 rotated(Vec3 value, Quat rotation) noexcept {
    const Quat unit = normalized(rotation);
    const Quat vector{value.x, value.y, value.z, 0.0F};
    const Quat conjugate{-unit.x, -unit.y, -unit.z, unit.w};
    const Quat result = multiplied(multiplied(unit, vector), conjugate);
    return {result.x, result.y, result.z};
}

[[nodiscard]] Vec3 normalized(Vec3 value) noexcept {
    const float magnitudeSquared = lengthSquared(value);
    if (magnitudeSquared <= kEpsilon) return {};
    return scaled(value, 1.0F / std::sqrt(magnitudeSquared));
}

[[nodiscard]] float snapped(float value, bool enabled, float step) noexcept {
    if (!enabled || step <= kEpsilon) return value;
    return std::round(value / step) * step;
}

[[nodiscard]] Vec3 constrainedComponents(Vec3 value,
                                         TransformConstraint constraint) noexcept {
    switch (constraint) {
    case TransformConstraint::Free: return value;
    case TransformConstraint::X: return {value.x, 0.0F, 0.0F};
    case TransformConstraint::Y: return {0.0F, value.y, 0.0F};
    case TransformConstraint::Z: return {0.0F, 0.0F, value.z};
    case TransformConstraint::XY: return {value.x, value.y, 0.0F};
    case TransformConstraint::XZ: return {value.x, 0.0F, value.z};
    case TransformConstraint::YZ: return {0.0F, value.y, value.z};
    }
    return value;
}

[[nodiscard]] std::optional<Vec3> rotationAxis(TransformConstraint constraint,
                                               const GizmoBasis& basis) noexcept {
    switch (constraint) {
    case TransformConstraint::X: return basis.x;
    case TransformConstraint::Y: return basis.y;
    case TransformConstraint::Z: return basis.z;
    default: return std::nullopt;
    }
}

[[nodiscard]] Quat axisAngle(Vec3 axis, float radians) noexcept {
    axis = normalized(axis);
    if (lengthSquared(axis) <= kEpsilon) return {};
    const float half = radians * 0.5F;
    const float sine = std::sin(half);
    return normalized({
        axis.x * sine,
        axis.y * sine,
        axis.z * sine,
        std::cos(half),
    });
}

[[nodiscard]] float snappedScale(float factor,
                                 const TransformSnapSettings& snapping) noexcept {
    factor = std::max(factor, kMinScale);
    if (!snapping.scaleEnabled || snapping.scaleStep <= kEpsilon) return factor;
    const float delta = factor - 1.0F;
    return std::max(1.0F + std::round(delta / snapping.scaleStep) * snapping.scaleStep,
                    kMinScale);
}

} // namespace

GizmoBasis makeGizmoBasis(TransformSpace space, Quat objectWorldRotation) noexcept {
    if (space == TransformSpace::Global) return {};
    return {
        normalized(rotated({1.0F, 0.0F, 0.0F}, objectWorldRotation)),
        normalized(rotated({0.0F, 1.0F, 0.0F}, objectWorldRotation)),
        normalized(rotated({0.0F, 0.0F, 1.0F}, objectWorldRotation)),
    };
}

Vec3 composeTranslationDelta(Vec3 gizmoComponents,
                             TransformConstraint constraint,
                             const GizmoBasis& basis,
                             const TransformSnapSettings& snapping) noexcept {
    gizmoComponents = constrainedComponents(gizmoComponents, constraint);
    gizmoComponents.x = snapped(gizmoComponents.x, snapping.translationEnabled,
                                snapping.translationStep);
    gizmoComponents.y = snapped(gizmoComponents.y, snapping.translationEnabled,
                                snapping.translationStep);
    gizmoComponents.z = snapped(gizmoComponents.z, snapping.translationEnabled,
                                snapping.translationStep);
    return added(added(scaled(basis.x, gizmoComponents.x),
                       scaled(basis.y, gizmoComponents.y)),
                 scaled(basis.z, gizmoComponents.z));
}

Quat composeRotationDelta(float angleRadians,
                          TransformConstraint axisConstraint,
                          const GizmoBasis& basis,
                          const TransformSnapSettings& snapping) noexcept {
    const auto axis = rotationAxis(axisConstraint, basis);
    if (!axis) return {};
    const float angle = snapped(angleRadians, snapping.rotationEnabled,
                                snapping.rotationStepRadians);
    return axisAngle(*axis, angle);
}

Vec3 composeScaleFactors(float factor,
                         TransformConstraint constraint,
                         const TransformSnapSettings& snapping) noexcept {
    const float value = snappedScale(factor, snapping);
    switch (constraint) {
    case TransformConstraint::Free: return {value, value, value};
    case TransformConstraint::X: return {value, 1.0F, 1.0F};
    case TransformConstraint::Y: return {1.0F, value, 1.0F};
    case TransformConstraint::Z: return {1.0F, 1.0F, value};
    case TransformConstraint::XY: return {value, value, 1.0F};
    case TransformConstraint::XZ: return {value, 1.0F, value};
    case TransformConstraint::YZ: return {1.0F, value, value};
    }
    return {value, value, value};
}

} // namespace m3d
