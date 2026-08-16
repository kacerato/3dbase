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

[[nodiscard]] Quat multiplied(Quat left, Quat right) noexcept {
    return normalized({
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
    });
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
        if (object->parent) parentLinear = worldLinear(*scene, *object->parent);
        const auto inverseParent = inverse(parentLinear);
        if (!inverseParent) return false;
        newTargets.push_back(Target{objectId, object->localTransform, *inverseParent});
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
    snapping_ = {};
    basis_ = {};
    targets_.clear();
}

} // namespace m3d
