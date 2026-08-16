from pathlib import Path

def replace_once(path, old, new):
    p=Path(path); s=p.read_text(); c=s.count(old)
    if c!=1: raise SystemExit(f'{path}: expected one match, got {c}')
    p.write_text(s.replace(old,new,1))

# Header: API + target context + pivot state.
replace_once('src/editor/include/mobile3d/editor/transform_manipulator.hpp',
'''    [[nodiscard]] bool updateTranslation(Vec3 gizmoComponents);
    [[nodiscard]] bool commit();''',
'''    [[nodiscard]] bool updateTranslation(Vec3 gizmoComponents);
    [[nodiscard]] bool beginRotate(EditorSession& session,
                                   TransformSpace space,
                                   TransformConstraint axisConstraint,
                                   PivotMode pivotMode,
                                   TransformSnapSettings snapping = {});
    [[nodiscard]] bool updateRotation(float angleRadians);
    [[nodiscard]] bool commit();''')
replace_once('src/editor/include/mobile3d/editor/transform_manipulator.hpp',
'''    [[nodiscard]] TransformConstraint constraint() const noexcept { return constraint_; }
    [[nodiscard]] const GizmoBasis& basis() const noexcept { return basis_; }
''',
'''    [[nodiscard]] TransformConstraint constraint() const noexcept { return constraint_; }
    [[nodiscard]] PivotMode pivotMode() const noexcept { return pivotMode_; }
    [[nodiscard]] Vec3 pivotWorld() const noexcept { return pivotWorld_; }
    [[nodiscard]] const GizmoBasis& basis() const noexcept { return basis_; }
''')
replace_once('src/editor/include/mobile3d/editor/transform_manipulator.hpp',
'''        Transform initialLocal{};
        std::array<float, 9> inverseParentWorldLinear{};
    };''',
'''        Transform initialLocal{};
        std::array<float, 9> inverseParentWorldLinear{};
        Vec3 parentWorldPosition{};
        Vec3 initialWorldPosition{};
        Quat parentWorldRotation{};
        Quat initialWorldRotation{};
    };''')
replace_once('src/editor/include/mobile3d/editor/transform_manipulator.hpp',
'''    TransformConstraint constraint_{TransformConstraint::Free};
    TransformSnapSettings snapping_{};
    GizmoBasis basis_{};''',
'''    TransformConstraint constraint_{TransformConstraint::Free};
    PivotMode pivotMode_{PivotMode::Median};
    Vec3 pivotWorld_{};
    TransformSnapSettings snapping_{};
    GizmoBasis basis_{};''')

# CPP helpers.
replace_once('src/editor/src/transform_manipulator.cpp',
'''[[nodiscard]] Quat multiplied(Quat left, Quat right) noexcept {
    return normalized({
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
    });
}
''',
'''[[nodiscard]] Quat multiplied(Quat left, Quat right) noexcept {
    return normalized({
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
    });
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

[[nodiscard]] Vec3 rotated(Vec3 value, Quat rotation) noexcept {
    const Quat q = normalized(rotation);
    const Quat vector{value.x, value.y, value.z, 0.0F};
    const Quat result = multiplied(multiplied(q, vector), conjugated(q));
    return {result.x, result.y, result.z};
}

[[nodiscard]] bool isAxisConstraint(TransformConstraint constraint) noexcept {
    return constraint == TransformConstraint::X ||
           constraint == TransformConstraint::Y ||
           constraint == TransformConstraint::Z;
}
''')
replace_once('src/editor/src/transform_manipulator.cpp',
'''[[nodiscard]] Quat worldRotation(const Scene& scene, ObjectId objectId) noexcept {''',
'''[[nodiscard]] Vec3 worldPosition(const Scene& scene, ObjectId objectId) noexcept {
    const SceneObject* object = scene.find(objectId);
    if (!object) return {};
    if (!object->parent) return object->localTransform.position;
    const Vec3 parentPosition = worldPosition(scene, *object->parent);
    const Matrix3 parentLinear = worldLinear(scene, *object->parent);
    return added(parentPosition, multiplied(parentLinear, object->localTransform.position));
}

[[nodiscard]] Quat worldRotation(const Scene& scene, ObjectId objectId) noexcept {''')

# Expand beginTranslate target capture.
replace_once('src/editor/src/transform_manipulator.cpp',
'''        Matrix3 parentLinear = identityMatrix();
        if (object->parent) parentLinear = worldLinear(*scene, *object->parent);
        const auto inverseParent = inverse(parentLinear);
        if (!inverseParent) return false;
        newTargets.push_back(Target{objectId, object->localTransform, *inverseParent});''',
'''        Matrix3 parentLinear = identityMatrix();
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
                                    parentRotation, initialWorldRotation});''')

# Add beginRotate/updateRotation before commit.
replace_once('src/editor/src/transform_manipulator.cpp',
'''bool TransformManipulator::commit() {''',
'''bool TransformManipulator::beginRotate(EditorSession& session,
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

bool TransformManipulator::commit() {''')
replace_once('src/editor/src/transform_manipulator.cpp',
'''    constraint_ = TransformConstraint::Free;
    snapping_ = {};
    basis_ = {};''',
'''    constraint_ = TransformConstraint::Free;
    pivotMode_ = PivotMode::Median;
    pivotWorld_ = {};
    snapping_ = {};
    basis_ = {};''')

# Append rotation tests.
p=Path('tests/test_transform_manipulator.cpp'); s=p.read_text()
s += r'''

TEST_CASE("median pivot rotation moves objects around shared center and commits one undo") {
    const auto path = uniqueManipulatorProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Median Rotation", &error));
    const auto left = session.createObject(m3d::ObjectType::Empty, "Left");
    const auto right = session.createObject(m3d::ObjectType::Empty, "Right");
    REQUIRE(left.has_value());
    REQUIRE(right.has_value());
    auto leftTransform = session.scene()->find(*left)->localTransform;
    auto rightTransform = session.scene()->find(*right)->localTransform;
    leftTransform.position.x = -1.0F;
    rightTransform.position.x = 1.0F;
    REQUIRE(session.transformObject(*left, leftTransform));
    REQUIRE(session.transformObject(*right, rightTransform));
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.select(*left, m3d::SelectionMode::Replace));
    REQUIRE(session.select(*right, m3d::SelectionMode::Add));

    m3d::TransformManipulator manipulator;
    REQUIRE(manipulator.beginRotate(session, m3d::TransformSpace::Global,
                                    m3d::TransformConstraint::Z,
                                    m3d::PivotMode::Median));
    REQUIRE(near(manipulator.pivotWorld().x, 0.0F));
    REQUIRE(manipulator.updateRotation(kPi * 0.5F));
    const auto leftPreview = session.scene()->find(*left)->localTransform;
    const auto rightPreview = session.scene()->find(*right)->localTransform;
    REQUIRE(near(leftPreview.position.x, 0.0F));
    REQUIRE(near(leftPreview.position.y, -1.0F));
    REQUIRE(near(rightPreview.position.x, 0.0F));
    REQUIRE(near(rightPreview.position.y, 1.0F));
    REQUIRE(manipulator.commit());
    REQUIRE(session.nextUndoName() == "Rotate Objects");
    REQUIRE(session.undo());
    REQUIRE(session.scene()->find(*left)->localTransform == leftTransform);
    REQUIRE(session.scene()->find(*right)->localTransform == rightTransform);
}

TEST_CASE("individual origins rotate orientation without moving object origins") {
    const auto path = uniqueManipulatorProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Individual Rotation", &error));
    const auto object = session.createObject(m3d::ObjectType::Empty, "Object");
    REQUIRE(object.has_value());
    auto before = session.scene()->find(*object)->localTransform;
    before.position = {3.0F, 4.0F, 5.0F};
    REQUIRE(session.transformObject(*object, before));
    REQUIRE(session.select(*object, m3d::SelectionMode::Replace));

    m3d::TransformSnapSettings snapping;
    snapping.rotationEnabled = true;
    snapping.rotationStepRadians = 15.0F * kPi / 180.0F;
    m3d::TransformManipulator manipulator;
    REQUIRE(manipulator.beginRotate(session, m3d::TransformSpace::Global,
                                    m3d::TransformConstraint::Z,
                                    m3d::PivotMode::IndividualOrigins,
                                    snapping));
    REQUIRE(manipulator.updateRotation(17.0F * kPi / 180.0F));
    const auto preview = session.scene()->find(*object)->localTransform;
    REQUIRE(preview.position == before.position);
    REQUIRE(near(preview.rotation.z, std::sin(7.5F * kPi / 180.0F)));
    REQUIRE(near(preview.rotation.w, std::cos(7.5F * kPi / 180.0F)));
    REQUIRE(manipulator.cancel());
    REQUIRE(session.scene()->find(*object)->localTransform == before);
}

TEST_CASE("active pivot rotation keeps active object origin fixed") {
    const auto path = uniqueManipulatorProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Active Rotation", &error));
    const auto other = session.createObject(m3d::ObjectType::Empty, "Other");
    const auto active = session.createObject(m3d::ObjectType::Empty, "Active");
    REQUIRE(other.has_value());
    REQUIRE(active.has_value());
    auto otherTransform = session.scene()->find(*other)->localTransform;
    auto activeTransform = session.scene()->find(*active)->localTransform;
    otherTransform.position = {1.0F, 0.0F, 0.0F};
    activeTransform.position = {0.0F, 0.0F, 0.0F};
    REQUIRE(session.transformObject(*other, otherTransform));
    REQUIRE(session.transformObject(*active, activeTransform));
    REQUIRE(session.select(*other, m3d::SelectionMode::Replace));
    REQUIRE(session.select(*active, m3d::SelectionMode::Add));

    m3d::TransformManipulator manipulator;
    REQUIRE(manipulator.beginRotate(session, m3d::TransformSpace::Global,
                                    m3d::TransformConstraint::Z,
                                    m3d::PivotMode::Active));
    REQUIRE(manipulator.updateRotation(kPi * 0.5F));
    const auto activePreview = session.scene()->find(*active)->localTransform;
    const auto otherPreview = session.scene()->find(*other)->localTransform;
    REQUIRE(activePreview.position == activeTransform.position);
    REQUIRE(near(otherPreview.position.x, 0.0F));
    REQUIRE(near(otherPreview.position.y, 1.0F));
    REQUIRE(manipulator.cancel());
}
'''
p.write_text(s)
print('pivot aware rotation manipulator applied')
