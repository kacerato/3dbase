from pathlib import Path

def replace_once(path, old, new):
    p=Path(path); s=p.read_text(); c=s.count(old)
    if c!=1: raise SystemExit(f'{path}: expected one match, got {c}')
    p.write_text(s.replace(old,new,1))

# Vec scaling helper.
replace_once('src/editor/src/transform_manipulator.cpp',
'''[[nodiscard]] Vec3 subtracted(Vec3 left, Vec3 right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}
''',
'''[[nodiscard]] Vec3 subtracted(Vec3 left, Vec3 right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] Vec3 scaled(Vec3 value, float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}
''')

# Insert scale methods before commit.
replace_once('src/editor/src/transform_manipulator.cpp',
'''bool TransformManipulator::commit() {''',
'''bool TransformManipulator::beginScale(EditorSession& session,
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

bool TransformManipulator::commit() {''')

# Append tests.
p=Path('tests/test_transform_manipulator.cpp'); s=p.read_text()
s += r'''

TEST_CASE("uniform scale around median changes origins and local scale in one undo") {
    const auto path = uniqueManipulatorProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Median Scale", &error));
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
    REQUIRE(manipulator.beginScale(session, m3d::TransformSpace::Global,
                                   m3d::TransformConstraint::Free,
                                   m3d::PivotMode::Median));
    REQUIRE(manipulator.updateScale(2.0F));
    const auto leftPreview = session.scene()->find(*left)->localTransform;
    const auto rightPreview = session.scene()->find(*right)->localTransform;
    REQUIRE(near(leftPreview.position.x, -2.0F));
    REQUIRE(near(rightPreview.position.x, 2.0F));
    REQUIRE(near(leftPreview.scale.x, 2.0F));
    REQUIRE(near(leftPreview.scale.y, 2.0F));
    REQUIRE(near(leftPreview.scale.z, 2.0F));
    REQUIRE(manipulator.commit());
    REQUIRE(session.nextUndoName() == "Scale Objects");
    REQUIRE(session.undo());
    REQUIRE(session.scene()->find(*left)->localTransform == leftTransform);
    REQUIRE(session.scene()->find(*right)->localTransform == rightTransform);
}

TEST_CASE("local non uniform scale uses individual origins and preserves position") {
    const auto path = uniqueManipulatorProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Local Axis Scale", &error));
    const auto object = session.createObject(m3d::ObjectType::Empty, "Object");
    REQUIRE(object.has_value());
    auto before = session.scene()->find(*object)->localTransform;
    before.position = {3.0F, 4.0F, 5.0F};
    before.scale = {2.0F, 3.0F, 4.0F};
    REQUIRE(session.transformObject(*object, before));
    REQUIRE(session.select(*object, m3d::SelectionMode::Replace));

    m3d::TransformSnapSettings snapping;
    snapping.scaleEnabled = true;
    snapping.scaleStep = 0.1F;
    m3d::TransformManipulator manipulator;
    REQUIRE(manipulator.beginScale(session, m3d::TransformSpace::Local,
                                   m3d::TransformConstraint::X,
                                   m3d::PivotMode::IndividualOrigins,
                                   snapping));
    REQUIRE(manipulator.updateScale(1.26F));
    const auto preview = session.scene()->find(*object)->localTransform;
    REQUIRE(preview.position == before.position);
    REQUIRE(near(preview.scale.x, 2.6F));
    REQUIRE(near(preview.scale.y, 3.0F));
    REQUIRE(near(preview.scale.z, 4.0F));
    REQUIRE(manipulator.cancel());
    REQUIRE(session.scene()->find(*object)->localTransform == before);
}

TEST_CASE("non uniform scale rejects shear producing transform policies") {
    const auto path = uniqueManipulatorProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Scale Policy", &error));
    const auto object = session.createObject(m3d::ObjectType::Empty, "Object");
    REQUIRE(object.has_value());
    REQUIRE(session.select(*object, m3d::SelectionMode::Replace));

    m3d::TransformManipulator manipulator;
    REQUIRE(!manipulator.beginScale(session, m3d::TransformSpace::Global,
                                    m3d::TransformConstraint::X,
                                    m3d::PivotMode::IndividualOrigins));
    REQUIRE(!manipulator.active());
    REQUIRE(!manipulator.beginScale(session, m3d::TransformSpace::Local,
                                    m3d::TransformConstraint::XY,
                                    m3d::PivotMode::Median));
    REQUIRE(!manipulator.active());
}

TEST_CASE("uniform scale around active pivot keeps active origin fixed") {
    const auto path = uniqueManipulatorProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Active Scale", &error));
    const auto other = session.createObject(m3d::ObjectType::Empty, "Other");
    const auto active = session.createObject(m3d::ObjectType::Empty, "Active");
    REQUIRE(other.has_value());
    REQUIRE(active.has_value());
    auto otherBefore = session.scene()->find(*other)->localTransform;
    auto activeBefore = session.scene()->find(*active)->localTransform;
    otherBefore.position = {2.0F, 0.0F, 0.0F};
    activeBefore.position = {1.0F, 0.0F, 0.0F};
    REQUIRE(session.transformObject(*other, otherBefore));
    REQUIRE(session.transformObject(*active, activeBefore));
    REQUIRE(session.select(*other, m3d::SelectionMode::Replace));
    REQUIRE(session.select(*active, m3d::SelectionMode::Add));

    m3d::TransformManipulator manipulator;
    REQUIRE(manipulator.beginScale(session, m3d::TransformSpace::Global,
                                   m3d::TransformConstraint::Free,
                                   m3d::PivotMode::Active));
    REQUIRE(manipulator.updateScale(3.0F));
    const auto activePreview = session.scene()->find(*active)->localTransform;
    const auto otherPreview = session.scene()->find(*other)->localTransform;
    REQUIRE(activePreview.position == activeBefore.position);
    REQUIRE(near(otherPreview.position.x, 4.0F));
    REQUIRE(near(activePreview.scale.x, 3.0F));
    REQUIRE(manipulator.cancel());
}
'''
p.write_text(s)
print('safe scale manipulator applied')
