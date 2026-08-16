#include "test_harness.hpp"

#include "mobile3d/editor/transform_manipulator.hpp"

#include <cmath>
#include <filesystem>
#include <string>

namespace {

constexpr float kPi = 3.14159265358979323846F;

std::filesystem::path uniqueManipulatorProjectPath() {
    return std::filesystem::temp_directory_path() /
           ("mobile3d-transform-manipulator-" + m3d::ObjectId::generate().toString());
}

struct ProjectCleanup final {
    explicit ProjectCleanup(std::filesystem::path value) : path(std::move(value)) {}
    ~ProjectCleanup() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

bool near(float left, float right, float epsilon = 1.0e-4F) {
    return std::fabs(left - right) <= epsilon;
}

} // namespace

TEST_CASE("global move converts world delta through scaled parent space") {
    const auto path = uniqueManipulatorProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Scaled Parent", &error));
    const auto parent = session.createObject(m3d::ObjectType::Empty, "Parent");
    REQUIRE(parent.has_value());
    const auto child = session.createObject(m3d::ObjectType::Empty, "Child", *parent);
    REQUIRE(child.has_value());

    auto parentTransform = session.scene()->find(*parent)->localTransform;
    parentTransform.scale = {2.0F, 1.0F, 1.0F};
    REQUIRE(session.transformObject(*parent, parentTransform));
    REQUIRE(session.select(*child, m3d::SelectionMode::Replace));

    const auto before = session.scene()->find(*child)->localTransform;
    m3d::TransformManipulator manipulator;
    REQUIRE(manipulator.beginTranslate(session, m3d::TransformSpace::Global,
                                       m3d::TransformConstraint::X));
    REQUIRE(manipulator.updateTranslation({2.0F, 99.0F, 99.0F}));
    const auto preview = session.scene()->find(*child)->localTransform;
    REQUIRE(near(preview.position.x, before.position.x + 1.0F));
    REQUIRE(near(preview.position.y, before.position.y));
    REQUIRE(near(preview.position.z, before.position.z));
    REQUIRE(manipulator.cancel());
    REQUIRE(session.scene()->find(*child)->localTransform == before);
}

TEST_CASE("global move respects rotated parent when producing child local position") {
    const auto path = uniqueManipulatorProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Rotated Parent", &error));
    const auto parent = session.createObject(m3d::ObjectType::Empty, "Parent");
    const auto child = parent ? session.createObject(m3d::ObjectType::Empty, "Child", *parent)
                              : std::optional<m3d::ObjectId>{};
    REQUIRE(parent.has_value());
    REQUIRE(child.has_value());

    auto parentTransform = session.scene()->find(*parent)->localTransform;
    const float halfAngle = kPi * 0.25F;
    parentTransform.rotation = {0.0F, 0.0F, std::sin(halfAngle), std::cos(halfAngle)};
    REQUIRE(session.transformObject(*parent, parentTransform));
    REQUIRE(session.select(*child, m3d::SelectionMode::Replace));

    const auto before = session.scene()->find(*child)->localTransform;
    m3d::TransformManipulator manipulator;
    REQUIRE(manipulator.beginTranslate(session, m3d::TransformSpace::Global,
                                       m3d::TransformConstraint::X));
    REQUIRE(manipulator.updateTranslation({2.0F, 0.0F, 0.0F}));
    const auto preview = session.scene()->find(*child)->localTransform;
    REQUIRE(near(preview.position.x, before.position.x));
    REQUIRE(near(preview.position.y, before.position.y - 2.0F));
    REQUIRE(manipulator.cancel());
}

TEST_CASE("local move uses active object world orientation and commits one undo") {
    const auto path = uniqueManipulatorProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Local Move", &error));
    const auto object = session.createObject(m3d::ObjectType::Empty, "Object");
    REQUIRE(object.has_value());

    auto transform = session.scene()->find(*object)->localTransform;
    const float halfAngle = kPi * 0.25F;
    transform.rotation = {0.0F, 0.0F, std::sin(halfAngle), std::cos(halfAngle)};
    REQUIRE(session.transformObject(*object, transform));
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.select(*object, m3d::SelectionMode::Replace));

    m3d::TransformManipulator manipulator;
    REQUIRE(manipulator.beginTranslate(session, m3d::TransformSpace::Local,
                                       m3d::TransformConstraint::X));
    REQUIRE(manipulator.updateTranslation({1.0F, 0.0F, 0.0F}));
    const auto preview = session.scene()->find(*object)->localTransform;
    REQUIRE(near(preview.position.x, transform.position.x));
    REQUIRE(near(preview.position.y, transform.position.y + 1.0F));
    REQUIRE(manipulator.commit());
    REQUIRE(session.nextUndoName() == "Move Objects");
    REQUIRE(session.undo());
    REQUIRE(session.scene()->find(*object)->localTransform == transform);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->find(*object)->localTransform == preview);
}

TEST_CASE("selected child is not double translated when its selected parent moves") {
    const auto path = uniqueManipulatorProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Selected Hierarchy", &error));
    const auto parent = session.createObject(m3d::ObjectType::Empty, "Parent");
    const auto child = parent ? session.createObject(m3d::ObjectType::Empty, "Child", *parent)
                              : std::optional<m3d::ObjectId>{};
    REQUIRE(parent.has_value());
    REQUIRE(child.has_value());
    REQUIRE(session.select(*parent, m3d::SelectionMode::Replace));
    REQUIRE(session.select(*child, m3d::SelectionMode::Add));

    const auto parentBefore = session.scene()->find(*parent)->localTransform;
    const auto childBefore = session.scene()->find(*child)->localTransform;
    m3d::TransformManipulator manipulator;
    REQUIRE(manipulator.beginTranslate(session, m3d::TransformSpace::Global,
                                       m3d::TransformConstraint::X));
    REQUIRE(manipulator.updateTranslation({3.0F, 0.0F, 0.0F}));
    REQUIRE(near(session.scene()->find(*parent)->localTransform.position.x,
                 parentBefore.position.x + 3.0F));
    REQUIRE(session.scene()->find(*child)->localTransform == childBefore);
    REQUIRE(manipulator.commit());
    REQUIRE(session.undo());
    REQUIRE(session.scene()->find(*parent)->localTransform == parentBefore);
    REQUIRE(session.scene()->find(*child)->localTransform == childBefore);
}


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
