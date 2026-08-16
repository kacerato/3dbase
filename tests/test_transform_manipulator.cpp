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
