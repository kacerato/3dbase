#include "test_harness.hpp"

#include "mobile3d/core/command_stack.hpp"
#include "mobile3d/core/commands/object_commands.hpp"

#include <memory>

TEST_CASE("command stack creates undoes and redoes object creation") {
    m3d::Scene scene;
    m3d::CommandStack stack;

    auto command = std::make_unique<m3d::CreateObjectCommand>(scene, m3d::ObjectType::Mesh, "Cube");
    auto* commandView = command.get();
    REQUIRE(stack.execute(std::move(command)));
    const auto id = commandView->createdId();
    REQUIRE(scene.contains(id));
    REQUIRE(stack.nextUndoName() == "Create Object");

    REQUIRE(stack.undo());
    REQUIRE(!scene.contains(id));
    REQUIRE(stack.redo());
    REQUIRE(scene.contains(id));
}

TEST_CASE("transform command preserves exact previous transform") {
    m3d::Scene scene;
    m3d::CommandStack stack;
    const auto id = scene.createObject(m3d::ObjectType::Mesh, "Cube");

    m3d::Transform moved;
    moved.position = {10.0F, 2.0F, -5.0F};
    moved.scale = {2.0F, 2.0F, 2.0F};

    REQUIRE(stack.execute(std::make_unique<m3d::TransformObjectCommand>(scene, id, moved)));
    REQUIRE(scene.find(id)->localTransform == moved);
    REQUIRE(stack.undo());
    REQUIRE(scene.find(id)->localTransform == m3d::Transform{});
    REQUIRE(stack.redo());
    REQUIRE(scene.find(id)->localTransform == moved);
}

TEST_CASE("delete command restores hierarchy on undo") {
    m3d::Scene scene;
    m3d::CommandStack stack;
    const auto root = scene.createObject(m3d::ObjectType::Empty, "Root");
    const auto child = scene.createObject(m3d::ObjectType::Mesh, "Child", root);

    REQUIRE(stack.execute(std::make_unique<m3d::DeleteObjectCommand>(scene, root)));
    REQUIRE(scene.size() == 0);
    REQUIRE(stack.undo());
    REQUIRE(scene.size() == 2);
    REQUIRE(scene.find(child)->parent == root);
}

TEST_CASE("reparent command rejects hierarchy cycles without entering history") {
    m3d::Scene scene;
    m3d::CommandStack stack;
    const auto root = scene.createObject(m3d::ObjectType::Empty, "Root");
    const auto child = scene.createObject(m3d::ObjectType::Mesh, "Child", root);

    REQUIRE(!stack.execute(std::make_unique<m3d::ReparentObjectCommand>(scene, root, child)));
    REQUIRE(stack.undoCount() == 0);
    REQUIRE(!scene.find(root)->parent.has_value());
}

#include "mobile3d/core/composite_command.hpp"

TEST_CASE("composite command groups multiple edits into one undo step") {
    m3d::Scene scene;
    m3d::CommandStack stack;
    const auto first = scene.createObject(m3d::ObjectType::Mesh, "First");
    const auto second = scene.createObject(m3d::ObjectType::Mesh, "Second");

    m3d::Transform firstTransform;
    firstTransform.position = {1.0F, 0.0F, 0.0F};
    m3d::Transform secondTransform;
    secondTransform.position = {2.0F, 0.0F, 0.0F};

    auto transaction = std::make_unique<m3d::CompositeCommand>("Move Selection");
    transaction->add(std::make_unique<m3d::TransformObjectCommand>(scene, first, firstTransform));
    transaction->add(std::make_unique<m3d::TransformObjectCommand>(scene, second, secondTransform));

    REQUIRE(stack.execute(std::move(transaction)));
    REQUIRE(stack.undoCount() == 1);
    REQUIRE(stack.nextUndoName() == "Move Selection");
    REQUIRE(scene.find(first)->localTransform == firstTransform);
    REQUIRE(scene.find(second)->localTransform == secondTransform);

    REQUIRE(stack.undo());
    REQUIRE(scene.find(first)->localTransform == m3d::Transform{});
    REQUIRE(scene.find(second)->localTransform == m3d::Transform{});
    REQUIRE(stack.redo());
    REQUIRE(scene.find(first)->localTransform == firstTransform);
    REQUIRE(scene.find(second)->localTransform == secondTransform);
}

TEST_CASE("command stack dirty state follows save checkpoints across branching history") {
    m3d::Scene scene;
    m3d::CommandStack stack;
    const auto object = scene.createObject(m3d::ObjectType::Mesh, "Cube");

    REQUIRE(!stack.isDirty());
    REQUIRE(stack.execute(std::make_unique<m3d::RenameObjectCommand>(scene, object, "Saved Name")));
    REQUIRE(stack.isDirty());
    stack.markSaved();
    REQUIRE(!stack.isDirty());

    REQUIRE(stack.execute(std::make_unique<m3d::RenameObjectCommand>(scene, object, "Temporary")));
    REQUIRE(stack.isDirty());
    REQUIRE(stack.undo());
    REQUIRE(!stack.isDirty());

    REQUIRE(stack.undo());
    REQUIRE(stack.isDirty());
    REQUIRE(stack.execute(std::make_unique<m3d::RenameObjectCommand>(scene, object, "New Branch")));
    REQUIRE(stack.isDirty());
}
