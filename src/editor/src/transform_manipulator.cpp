#include "mobile3d/editor/transform_manipulator.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace m3d {
namespace {

using Matrix3 = std::array<float, 9>;
constexpr float kEpsilon = 1.0e-7F;

[[nodiscard]] Matrix3 identityMatrix() noexcept {
    return {1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 1.0F};
}

[[nodiscard]] Quat normalized(Quat value) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y +
                                value.z * value.z + value.w * value.w;
    if (lengthSquared <= kEpsilon) return {};
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return {value.x * inverseLength, value.y * inverseLength,
            value.z * inverseLength, value.w * inverseLength};
}

[[nodiscard]] Quat multipliedRaw(Quat left, Quat right) noexcept {
    return {
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
    };
}

[[nodiscard]] Quat multiplied(Quat left, Quat right) noexcept {
    return normalized(multipliedRaw(left, right));
}

[[nodiscard]] Quat conjugated(Quat value) noexcept {
    const Quat q = normalized(value);
    return {-q.x, -q.y, -q.z, q.w};
}

[[nodiscard]] Vec3 added(Vec3 left, Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] Vec3 subtracted(Vec3 left, Vec3 right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] Vec3 scaled(Vec3 value, float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

[[nodiscard]] Vec3 rotated(Vec3 value, Quat rotation) noexcept {
    const Quat q = normalized(rotation);
    const Quat vector{value.x, value.y, value.z, 0.0F};
    const Quat result = multipliedRaw(multipliedRaw(q, vector), conjugated(q));
    return {result.x, result.y, result.z};
}

[[nodiscard]] bool isAxisConstraint(TransformConstraint constraint) noexcept {
    return constraint == TransformConstraint::X ||
           constraint == TransformConstraint::Y ||
           constraint == TransformConstraint::Z;
}

[[nodiscard]] Matrix3 rotationMatrix(Quat quaternion) noexcept {
    const Quat q = normalized(quaternion);
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;
    return {
        1.0F - 2.0F * (yy + zz), 2.0F * (xy - wz), 2.0F * (xz + wy),
        2.0F * (xy + wz), 1.0F - 2.0F * (xx + zz), 2.0F * (yz - wx),
        2.0F * (xz - wy), 2.0F * (yz + wx), 1.0F - 2.0F * (xx + yy),
    };
}

[[nodiscard]] Matrix3 localLinear(const Transform& transform) noexcept {
    Matrix3 matrix = rotationMatrix(transform.rotation);
    matrix[0] *= transform.scale.x;
    matrix[3] *= transform.scale.x;
    matrix[6] *= transform.scale.x;
    matrix[1] *= transform.scale.y;
    matrix[4] *= transform.scale.y;
    matrix[7] *= transform.scale.y;
    matrix[2] *= transform.scale.z;
    matrix[5] *= transform.scale.z;
    matrix[8] *= transform.scale.z;
    return matrix;
}

[[nodiscard]] Matrix3 multiplied(const Matrix3& left, const Matrix3& right) noexcept {
    Matrix3 result{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            float value = 0.0F;
            for (std::size_t k = 0; k < 3; ++k) {
                value += left[row * 3 + k] * right[k * 3 + column];
            }
            result[row * 3 + column] = value;
        }
    }
    return result;
}

[[nodiscard]] Vec3 multiplied(const Matrix3& matrix, Vec3 vector) noexcept {
    return {
        matrix[0] * vector.x + matrix[1] * vector.y + matrix[2] * vector.z,
        matrix[3] * vector.x + matrix[4] * vector.y + matrix[5] * vector.z,
        matrix[6] * vector.x + matrix[7] * vector.y + matrix[8] * vector.z,
    };
}

[[nodiscard]] std::optional<Matrix3> inverse(const Matrix3& matrix) noexcept {
    const float determinant =
        matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
        matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
        matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
    if (std::fabs(determinant) <= kEpsilon) return std::nullopt;
    const float inverseDeterminant = 1.0F / determinant;
    return Matrix3{
        (matrix[4] * matrix[8] - matrix[5] * matrix[7]) * inverseDeterminant,
        (matrix[2] * matrix[7] - matrix[1] * matrix[8]) * inverseDeterminant,
        (matrix[1] * matrix[5] - matrix[2] * matrix[4]) * inverseDeterminant,
        (matrix[5] * matrix[6] - matrix[3] * matrix[8]) * inverseDeterminant,
        (matrix[0] * matrix[8] - matrix[2] * matrix[6]) * inverseDeterminant,
        (matrix[2] * matrix[3] - matrix[0] * matrix[5]) * inverseDeterminant,
        (matrix[3] * matrix[7] - matrix[4] * matrix[6]) * inverseDeterminant,
        (matrix[1] * matrix[6] - matrix[0] * matrix[7]) * inverseDeterminant,
        (matrix[0] * matrix[4] - matrix[1] * matrix[3]) * inverseDeterminant,
    };
}

[[nodiscard]] Matrix3 worldLinear(const Scene& scene, ObjectId objectId) noexcept {
    const SceneObject* object = scene.find(objectId);
    if (!object) return identityMatrix();
    const Matrix3 local = localLinear(object->localTransform);
    if (!object->parent) return local;
    return multiplied(worldLinear(scene, *object->parent), local);
}

[[nodiscard]] Vec3 worldPosition(const Scene& scene, ObjectId objectId) noexcept {
    const SceneObject* object = scene.find(objectId);
    if (!object) return {};
    if (!object->parent) return object->localTransform.position;
    const Vec3 parentPosition = worldPosition(scene, *object->parent);
    const Matrix3 parentLinear = worldLinear(scene, *object->parent);
    return added(parentPosition, multiplied(parentLinear, object->localTransform.position));
}

[[nodiscard]] Quat worldRotation(const Scene& scene, ObjectId objectId) noexcept {
    const SceneObject* object = scene.find(objectId);
    if (!object) return {};
    if (!object->parent) return normalized(object->localTransform.rotation);
    return multiplied(worldRotation(scene, *object->parent), object->localTransform.rotation);
}

[[nodiscard]] bool hasSelectedAncestor(const Scene& scene,
                                       const SelectionModel& selection,
                                       ObjectId objectId) noexcept {
    const SceneObject* object = scene.find(objectId);
    if (!object) return false;
    auto parent = object->parent;
    while (parent) {
        if (selection.contains(*parent)) return true;
        const SceneObject* parentObject = scene.find(*parent);
        if (!parentObject) break;
        parent = parentObject->parent;
    }
    return false;
}

} // namespace

bool TransformManipulator::beginTranslate(EditorSession& session,
                                          TransformSpace space,
                                          TransformConstraint constraint,
                                          TransformSnapSettings snapping) {
    if (active() || !session.hasProject() || session.hasTransformTransaction() ||
        session.selection().empty()) return false;
    const Scene* scene = session.scene();
    if (!scene) return false;

    const auto activeObject = session.selection().active();
    const ObjectId basisObject = activeObject.value_or(session.selection().selected().front());
    const GizmoBasis newBasis = makeGizmoBasis(space, worldRotation(*scene, basisObject));

    std::vector<Target> newTargets;
    std::vector<ObjectId> transactionObjects;
    newTargets.reserve(session.selection().size());
    transactionObjects.reserve(session.selection().size());
    for (const ObjectId objectId : session.selection().selected()) {
        if (hasSelectedAncestor(*scene, session.selection(), objectId)) continue;
        const SceneObject* object = scene->find(objectId);
        if (!object) return false;
        Matrix3 parentLinear = identityMatrix();
        Vec3 parentPosition{};
        Quat parentRotation{};
        if (object->parent) {
            parentLinear = worldLinear(*scene, *object->parent);
            parentPosition = worldPosition(*scene, *object->parent);
            parentRotation = worldRotation(*scene, *object->parent);
        }
        const auto inverseParent = inverse(parentLinear);
        if (!inverseParent) return false;
        const Vec3 initialWorldPosition = added(
            parentPosition, multiplied(parentLinear, object->localTransform.position));
        const Quat initialWorldRotation = multiplied(parentRotation, object->localTransform.rotation);
        newTargets.push_back(Target{objectId, object->localTransform, *inverseParent,
                                    parentPosition, initialWorldPosition,
                                    parentRotation, initialWorldRotation});
        transactionObjects.push_back(objectId);
    }
    if (newTargets.empty()) return false;
    if (!session.beginTransformTransaction(transactionObjects, "Move Objects")) return false;

    session_ = &session;
    tool_ = TransformTool::Translate;
    space_ = space;
    constraint_ = constraint;
    snapping_ = snapping;
    basis_ = newBasis;
    targets_ = std::move(newTargets);
    return true;
}

bool TransformManipulator::updateTranslation(Vec3 gizmoComponents) {
    if (!session_ || tool_ != TransformTool::Translate) return false;
    const Vec3 worldDelta = composeTranslationDelta(gizmoComponents, constraint_, basis_, snapping_);
    for (const auto& target : targets_) {
        const Vec3 localDelta = multiplied(target.inverseParentWorldLinear, worldDelta);
        Transform preview = target.initialLocal;
        preview.position.x += localDelta.x;
        preview.position.y += localDelta.y;
        preview.position.z += localDelta.z;
        if (!session_->previewTransform(target.object, preview)) {
            (void)session_->cancelTransformTransaction();
            reset();
            return false;
        }
    }
    return true;
}

bool TransformManipulator::beginRotate(EditorSession& session,
                                       TransformSpace space,
                                       TransformConstraint axisConstraint,
                                       PivotMode pivotMode,
                                       TransformSnapSettings snapping) {
    if (active() || !isAxisConstraint(axisConstraint) || !session.hasProject() ||
        session.hasTransformTransaction() || session.selection().empty()) return false;
    const Scene* scene = session.scene();
    if (!scene) return false;

    const auto activeObject = session.selection().active();
    const ObjectId basisObject = activeObject.value_or(session.selection().selected().front());
    const GizmoBasis newBasis = makeGizmoBasis(space, worldRotation(*scene, basisObject));

    std::vector<Target> newTargets;
    std::vector<ObjectId> transactionObjects;
    newTargets.reserve(session.selection().size());
    transactionObjects.reserve(session.selection().size());
    for (const ObjectId objectId : session.selection().selected()) {
        if (hasSelectedAncestor(*scene, session.selection(), objectId)) continue;
        const SceneObject* object = scene->find(objectId);
        if (!object) return false;
        Matrix3 parentLinear = identityMatrix();
        Vec3 parentPosition{};
        Quat parentRotation{};
        if (object->parent) {
            parentLinear = worldLinear(*scene, *object->parent);
            parentPosition = worldPosition(*scene, *object->parent);
            parentRotation = worldRotation(*scene, *object->parent);
        }
        const auto inverseParent = inverse(parentLinear);
        if (!inverseParent) return false;
        const Vec3 initialWorldPosition = added(
            parentPosition, multiplied(parentLinear, object->localTransform.position));
        const Quat initialWorldRotation = multiplied(parentRotation, object->localTransform.rotation);
        newTargets.push_back(Target{objectId, object->localTransform, *inverseParent,
                                    parentPosition, initialWorldPosition,
                                    parentRotation, initialWorldRotation});
        transactionObjects.push_back(objectId);
    }
    if (newTargets.empty()) return false;

    Vec3 pivot{};
    if (pivotMode == PivotMode::Active) {
        pivot = worldPosition(*scene, basisObject);
    } else if (pivotMode == PivotMode::Median) {
        for (const auto& target : newTargets) pivot = added(pivot, target.initialWorldPosition);
        const float inverseCount = 1.0F / static_cast<float>(newTargets.size());
        pivot.x *= inverseCount;
        pivot.y *= inverseCount;
        pivot.z *= inverseCount;
    }

    if (!session.beginTransformTransaction(transactionObjects, "Rotate Objects")) return false;
    session_ = &session;
    tool_ = TransformTool::Rotate;
    space_ = space;
    constraint_ = axisConstraint;
    pivotMode_ = pivotMode;
    pivotWorld_ = pivot;
    snapping_ = snapping;
    basis_ = newBasis;
    targets_ = std::move(newTargets);
    return true;
}

bool TransformManipulator::updateRotation(float angleRadians) {
    if (!session_ || tool_ != TransformTool::Rotate) return false;
    const Quat worldDelta = composeRotationDelta(angleRadians, constraint_, basis_, snapping_);
    for (const auto& target : targets_) {
        const Vec3 pivot = pivotMode_ == PivotMode::IndividualOrigins
            ? target.initialWorldPosition : pivotWorld_;
        const Vec3 worldPositionAfter = added(
            pivot, rotated(subtracted(target.initialWorldPosition, pivot), worldDelta));
        const Vec3 localPosition = multiplied(
            target.inverseParentWorldLinear,
            subtracted(worldPositionAfter, target.parentWorldPosition));
        const Quat worldRotationAfter = multiplied(worldDelta, target.initialWorldRotation);
        const Quat localRotation = multiplied(conjugated(target.parentWorldRotation),
                                              worldRotationAfter);
        Transform preview = target.initialLocal;
        preview.position = localPosition;
        preview.rotation = localRotation;
        if (!session_->previewTransform(target.object, preview)) {
            (void)session_->cancelTransformTransaction();
            reset();
            return false;
        }
    }
    return true;
}

bool TransformManipulator::beginScale(EditorSession& session,
                                      TransformSpace space,
                                      TransformConstraint constraint,
                                      PivotMode pivotMode,
                                      TransformSnapSettings snapping) {
    if (active() || !session.hasProject() || session.hasTransformTransaction() ||
        session.selection().empty()) return false;

    const bool nonUniform = constraint != TransformConstraint::Free;
    // Arbitrary non-uniform world scaling can introduce shear, which cannot be
    // represented by the current position/quaternion/scale Transform. Keep the
    // supported operation explicit instead of silently decomposing/shearing.
    if (nonUniform &&
        (space != TransformSpace::Local || pivotMode != PivotMode::IndividualOrigins)) {
        return false;
    }

    const Scene* scene = session.scene();
    if (!scene) return false;
    const auto activeObject = session.selection().active();
    const ObjectId basisObject = activeObject.value_or(session.selection().selected().front());
    const GizmoBasis newBasis = makeGizmoBasis(space, worldRotation(*scene, basisObject));

    std::vector<Target> newTargets;
    std::vector<ObjectId> transactionObjects;
    newTargets.reserve(session.selection().size());
    transactionObjects.reserve(session.selection().size());
    for (const ObjectId objectId : session.selection().selected()) {
        if (hasSelectedAncestor(*scene, session.selection(), objectId)) continue;
        const SceneObject* object = scene->find(objectId);
        if (!object) return false;
        Matrix3 parentLinear = identityMatrix();
        Vec3 parentPosition{};
        Quat parentRotation{};
        if (object->parent) {
            parentLinear = worldLinear(*scene, *object->parent);
            parentPosition = worldPosition(*scene, *object->parent);
            parentRotation = worldRotation(*scene, *object->parent);
        }
        const auto inverseParent = inverse(parentLinear);
        if (!inverseParent) return false;
        const Vec3 initialWorldPosition = added(
            parentPosition, multiplied(parentLinear, object->localTransform.position));
        const Quat initialWorldRotation = multiplied(parentRotation, object->localTransform.rotation);
        newTargets.push_back(Target{objectId, object->localTransform, *inverseParent,
                                    parentPosition, initialWorldPosition,
                                    parentRotation, initialWorldRotation});
        transactionObjects.push_back(objectId);
    }
    if (newTargets.empty()) return false;

    Vec3 pivot{};
    if (pivotMode == PivotMode::Active) {
        pivot = worldPosition(*scene, basisObject);
    } else if (pivotMode == PivotMode::Median) {
        for (const auto& target : newTargets) pivot = added(pivot, target.initialWorldPosition);
        const float inverseCount = 1.0F / static_cast<float>(newTargets.size());
        pivot = scaled(pivot, inverseCount);
    }

    if (!session.beginTransformTransaction(transactionObjects, "Scale Objects")) return false;
    session_ = &session;
    tool_ = TransformTool::Scale;
    space_ = space;
    constraint_ = constraint;
    pivotMode_ = pivotMode;
    pivotWorld_ = pivot;
    snapping_ = snapping;
    basis_ = newBasis;
    targets_ = std::move(newTargets);
    return true;
}

bool TransformManipulator::updateScale(float factor) {
    if (!session_ || tool_ != TransformTool::Scale) return false;
    const Vec3 factors = composeScaleFactors(factor, constraint_, snapping_);
    const bool uniform = constraint_ == TransformConstraint::Free;

    for (const auto& target : targets_) {
        Transform preview = target.initialLocal;
        if (uniform) {
            const float uniformFactor = factors.x;
            const Vec3 pivot = pivotMode_ == PivotMode::IndividualOrigins
                ? target.initialWorldPosition : pivotWorld_;
            const Vec3 worldPositionAfter = added(
                pivot, scaled(subtracted(target.initialWorldPosition, pivot), uniformFactor));
            preview.position = multiplied(
                target.inverseParentWorldLinear,
                subtracted(worldPositionAfter, target.parentWorldPosition));
            preview.scale = {
                target.initialLocal.scale.x * uniformFactor,
                target.initialLocal.scale.y * uniformFactor,
                target.initialLocal.scale.z * uniformFactor,
            };
        } else {
            // Non-uniform scaling is intentionally local + individual-origin
            // only, so no shear-inducing world decomposition is necessary.
            preview.position = target.initialLocal.position;
            preview.scale = {
                target.initialLocal.scale.x * factors.x,
                target.initialLocal.scale.y * factors.y,
                target.initialLocal.scale.z * factors.z,
            };
        }

        if (!session_->previewTransform(target.object, preview)) {
            (void)session_->cancelTransformTransaction();
            reset();
            return false;
        }
    }
    return true;
}

bool TransformManipulator::commit() {
    if (!session_) return false;
    EditorSession* session = session_;
    reset();
    return session->commitTransformTransaction();
}

bool TransformManipulator::cancel() {
    if (!session_) return false;
    EditorSession* session = session_;
    reset();
    return session->cancelTransformTransaction();
}

void TransformManipulator::reset() noexcept {
    session_ = nullptr;
    tool_ = TransformTool::Translate;
    space_ = TransformSpace::Global;
    constraint_ = TransformConstraint::Free;
    pivotMode_ = PivotMode::Median;
    pivotWorld_ = {};
    snapping_ = {};
    basis_ = {};
    targets_.clear();
}

} // namespace m3d
