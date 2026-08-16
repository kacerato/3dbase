from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")

# Scene mutation API.
replace_once(
    "src/core/include/mobile3d/core/scene.hpp",
    "    [[nodiscard]] bool rename(ObjectId id, std::string name);\n    [[nodiscard]] bool setTransform(ObjectId id, const Transform& transform);\n",
    "    [[nodiscard]] bool rename(ObjectId id, std::string name);\n    [[nodiscard]] bool setVisible(ObjectId id, bool visible);\n    [[nodiscard]] bool setLocked(ObjectId id, bool locked);\n    [[nodiscard]] bool setTransform(ObjectId id, const Transform& transform);\n",
)
replace_once(
    "src/core/src/scene.cpp",
    "bool Scene::setTransform(ObjectId id, const Transform& transform) {",
    "bool Scene::setVisible(ObjectId id, bool visible) {\n    auto* object = find(id);\n    if (!object) return false;\n    object->visible = visible;\n    return true;\n}\n\nbool Scene::setLocked(ObjectId id, bool locked) {\n    auto* object = find(id);\n    if (!object) return false;\n    object->locked = locked;\n    return true;\n}\n\nbool Scene::setTransform(ObjectId id, const Transform& transform) {",
)

# Command API.
replace_once(
    "src/core/include/mobile3d/core/commands/object_commands.hpp",
    "class RenameObjectCommand final : public EditorCommand {",
    "class SetObjectVisibilityCommand final : public EditorCommand {\npublic:\n    SetObjectVisibilityCommand(Scene& scene, ObjectId object, bool visible);\n\n    [[nodiscard]] std::string_view name() const noexcept override { return \"Set Visibility\"; }\n    [[nodiscard]] bool execute() override;\n    [[nodiscard]] bool undo() override;\n\nprivate:\n    Scene& scene_;\n    ObjectId object_{};\n    bool previous_{true};\n    bool visible_{true};\n    bool captured_{false};\n};\n\nclass SetObjectLockedCommand final : public EditorCommand {\npublic:\n    SetObjectLockedCommand(Scene& scene, ObjectId object, bool locked);\n\n    [[nodiscard]] std::string_view name() const noexcept override { return \"Set Lock\"; }\n    [[nodiscard]] bool execute() override;\n    [[nodiscard]] bool undo() override;\n\nprivate:\n    Scene& scene_;\n    ObjectId object_{};\n    bool previous_{false};\n    bool locked_{false};\n    bool captured_{false};\n};\n\nclass RenameObjectCommand final : public EditorCommand {",
)

replace_once(
    "src/core/src/commands/object_commands.cpp",
    "RenameObjectCommand::RenameObjectCommand(Scene& scene, ObjectId object, std::string newName)",
    "SetObjectVisibilityCommand::SetObjectVisibilityCommand(Scene& scene, ObjectId object, bool visible)\n    : scene_(scene), object_(object), visible_(visible) {}\n\nbool SetObjectVisibilityCommand::execute() {\n    const auto* object = scene_.find(object_);\n    if (!object) return false;\n    if (!captured_) {\n        previous_ = object->visible;\n        captured_ = true;\n    }\n    return scene_.setVisible(object_, visible_);\n}\n\nbool SetObjectVisibilityCommand::undo() {\n    return captured_ && scene_.setVisible(object_, previous_);\n}\n\nSetObjectLockedCommand::SetObjectLockedCommand(Scene& scene, ObjectId object, bool locked)\n    : scene_(scene), object_(object), locked_(locked) {}\n\nbool SetObjectLockedCommand::execute() {\n    const auto* object = scene_.find(object_);\n    if (!object) return false;\n    if (!captured_) {\n        previous_ = object->locked;\n        captured_ = true;\n    }\n    return scene_.setLocked(object_, locked_);\n}\n\nbool SetObjectLockedCommand::undo() {\n    return captured_ && scene_.setLocked(object_, previous_);\n}\n\nRenameObjectCommand::RenameObjectCommand(Scene& scene, ObjectId object, std::string newName)",
)

# Editor Session API.
replace_once(
    "src/editor/include/mobile3d/editor/editor_session.hpp",
    "    [[nodiscard]] bool duplicateSelection();\n    [[nodiscard]] bool renameObject(ObjectId object, std::string name);\n",
    "    [[nodiscard]] bool duplicateSelection();\n    [[nodiscard]] bool setObjectVisible(ObjectId object, bool visible);\n    [[nodiscard]] bool setObjectLocked(ObjectId object, bool locked);\n    [[nodiscard]] bool renameObject(ObjectId object, std::string name);\n",
)

# Lock helpers + visibility/lock operations.
replace_once(
    "src/editor/src/editor_session.cpp",
    "bool EditorSession::renameObject(ObjectId object, std::string name) {",
    "bool EditorSession::setObjectVisible(ObjectId object, bool visible) {\n    if (transformTransaction_ || !document_) return false;\n    const auto* current = document_->scene.find(object);\n    if (!current || current->visible == visible) return false;\n    auto command = std::make_unique<SetObjectVisibilityCommand>(document_->scene, object, visible);\n    if (!commands_.execute(std::move(command))) return false;\n    if (!visible && selection_.contains(object)) {\n        selection_.remove(object);\n        ++selectionRevision_;\n    }\n    sceneMutated(false);\n    return true;\n}\n\nbool EditorSession::setObjectLocked(ObjectId object, bool locked) {\n    if (transformTransaction_ || !document_) return false;\n    const auto* current = document_->scene.find(object);\n    if (!current || current->locked == locked) return false;\n    auto command = std::make_unique<SetObjectLockedCommand>(document_->scene, object, locked);\n    if (!commands_.execute(std::move(command))) return false;\n    sceneMutated(false);\n    return true;\n}\n\nbool EditorSession::renameObject(ObjectId object, std::string name) {",
)

# Block edit operations on locked objects.
replace_once(
    "src/editor/src/editor_session.cpp",
    "bool EditorSession::deleteObject(ObjectId object) {\n    if (transformTransaction_) return false;\n    if (!document_ || !document_->scene.contains(object)) return false;",
    "bool EditorSession::deleteObject(ObjectId object) {\n    if (transformTransaction_) return false;\n    if (!document_ || !document_->scene.contains(object)) return false;\n    const auto* target = document_->scene.find(object);\n    if (!target || target->locked) return false;",
)
replace_once(
    "src/editor/src/editor_session.cpp",
    "bool EditorSession::deleteSelection() {\n    if (transformTransaction_) return false;\n    if (!document_ || selection_.empty()) return false;",
    "bool EditorSession::deleteSelection() {\n    if (transformTransaction_) return false;\n    if (!document_ || selection_.empty()) return false;\n    for (const auto id : selection_.selected()) {\n        const auto* object = document_->scene.find(id);\n        if (!object || object->locked) return false;\n    }",
)
replace_once(
    "src/editor/src/editor_session.cpp",
    "bool EditorSession::duplicateSelection() {\n    if (transformTransaction_ || !document_ || selection_.empty()) return false;",
    "bool EditorSession::duplicateSelection() {\n    if (transformTransaction_ || !document_ || selection_.empty()) return false;\n    for (const auto id : selection_.selected()) {\n        const auto* object = document_->scene.find(id);\n        if (!object || object->locked) return false;\n    }",
)
replace_once(
    "src/editor/src/editor_session.cpp",
    "bool EditorSession::renameObject(ObjectId object, std::string name) {\n    if (transformTransaction_) return false;\n    if (!document_ || !document_->scene.contains(object)) return false;",
    "bool EditorSession::renameObject(ObjectId object, std::string name) {\n    if (transformTransaction_) return false;\n    if (!document_ || !document_->scene.contains(object)) return false;\n    const auto* current = document_->scene.find(object);\n    if (!current || current->locked) return false;",
)
replace_once(
    "src/editor/src/editor_session.cpp",
    "bool EditorSession::transformObject(ObjectId object, const Transform& transform) {\n    if (transformTransaction_) return false;\n    if (!document_ || !document_->scene.contains(object)) return false;",
    "bool EditorSession::transformObject(ObjectId object, const Transform& transform) {\n    if (transformTransaction_) return false;\n    if (!document_ || !document_->scene.contains(object)) return false;\n    const auto* current = document_->scene.find(object);\n    if (!current || current->locked) return false;",
)
replace_once(
    "src/editor/src/editor_session.cpp",
    "bool EditorSession::reparentObject(ObjectId object, std::optional<ObjectId> parent) {\n    if (transformTransaction_) return false;\n    if (!document_ || !document_->scene.contains(object)) return false;",
    "bool EditorSession::reparentObject(ObjectId object, std::optional<ObjectId> parent) {\n    if (transformTransaction_) return false;\n    if (!document_ || !document_->scene.contains(object)) return false;\n    const auto* current = document_->scene.find(object);\n    if (!current || current->locked) return false;",
)
replace_once(
    "src/editor/src/editor_session.cpp",
    "    for (const auto objectId : objects) {\n        const auto* object = document_->scene.find(objectId);\n        if (!object) return false;",
    "    for (const auto objectId : objects) {\n        const auto* object = document_->scene.find(objectId);\n        if (!object || object->locked) return false;",
)

# Snapshot carries lock so gizmo can hide for locked roots.
replace_once(
    "src/render/include/mobile3d/render/render_snapshot.hpp",
    "    bool visible{true};\n    bool selected{false};\n",
    "    bool visible{true};\n    bool locked{false};\n    bool selected{false};\n",
)
replace_once(
    "src/render/src/render_snapshot.cpp",
    "            .visible = object.visible,\n            .selected = selection.contains(object.id),",
    "            .visible = object.visible,\n            .locked = object.locked,\n            .selected = selection.contains(object.id),",
)
replace_once(
    "src/app/qt/vulkan_viewport.cpp",
    "    for (const auto& object : snapshot.objects()) {\n        if (object.selected && !hasSelectedAncestor(snapshot, object)) roots.push_back(&object);\n    }\n    if (roots.empty()) return presentation;",
    "    for (const auto& object : snapshot.objects()) {\n        if (object.selected && !hasSelectedAncestor(snapshot, object)) roots.push_back(&object);\n    }\n    if (roots.empty() || std::any_of(roots.cbegin(), roots.cend(),\n                                     [](const auto* object) { return object->locked; })) {\n        return presentation;\n    }",
)

# Outliner roles.
replace_once(
    "src/app/qt/outliner_model.hpp",
    "        ActiveRole,\n        HasChildrenRole,\n",
    "        ActiveRole,\n        HasChildrenRole,\n        VisibleRole,\n        LockedRole,\n",
)
replace_once(
    "src/app/qt/outliner_model.cpp",
    "    case HasChildrenRole:\n        return row.hasChildren;\n",
    "    case HasChildrenRole:\n        return row.hasChildren;\n    case VisibleRole:\n        return object->visible;\n    case LockedRole:\n        return object->locked;\n",
)
replace_once(
    "src/app/qt/outliner_model.cpp",
    "        {HasChildrenRole, \"hasChildren\"},\n",
    "        {HasChildrenRole, \"hasChildren\"},\n        {VisibleRole, \"visible\"},\n        {LockedRole, \"locked\"},\n",
)

# Controller methods.
replace_once(
    "src/app/qt/editor_controller.hpp",
    "    Q_INVOKABLE bool duplicateSelection();\n    Q_INVOKABLE bool selectObject(const QString& objectId, bool toggle = false);\n",
    "    Q_INVOKABLE bool duplicateSelection();\n    Q_INVOKABLE bool setObjectVisible(const QString& objectId, bool visible);\n    Q_INVOKABLE bool setObjectLocked(const QString& objectId, bool locked);\n    Q_INVOKABLE bool selectObject(const QString& objectId, bool toggle = false);\n",
)
replace_once(
    "src/app/qt/editor_controller.cpp",
    "bool EditorController::selectObject(const QString& objectId, bool toggle) {",
    "bool EditorController::setObjectVisible(const QString& objectId, bool visible) {\n    const auto id = m3d::ObjectId::fromString(objectId.toStdString());\n    if (!id || !session_.setObjectVisible(*id, visible)) return false;\n    setStatus(visible ? QStringLiteral(\"Object shown.\") : QStringLiteral(\"Object hidden.\"));\n    refreshUi();\n    return true;\n}\n\nbool EditorController::setObjectLocked(const QString& objectId, bool locked) {\n    const auto id = m3d::ObjectId::fromString(objectId.toStdString());\n    if (!id || !session_.setObjectLocked(*id, locked)) return false;\n    setStatus(locked ? QStringLiteral(\"Object locked.\") : QStringLiteral(\"Object unlocked.\"));\n    refreshUi();\n    return true;\n}\n\nbool EditorController::selectObject(const QString& objectId, bool toggle) {",
)

# Outliner controls: expose two required roles and add independent buttons above MouseArea.
replace_once(
    "src/app/qml/OutlinerPanel.qml",
    "                required property bool active\n                required property bool hasChildren\n",
    "                required property bool active\n                required property bool hasChildren\n                required property bool visible\n                required property bool locked\n",
)
replace_once(
    "src/app/qml/OutlinerPanel.qml",
    "                    ColumnLayout {\n                        Layout.fillWidth: true\n",
    "                    ColumnLayout {\n                        Layout.fillWidth: true\n",
)
replace_once(
    "src/app/qml/OutlinerPanel.qml",
    "                    }\n                }\n\n                MouseArea {\n                    id: mouseArea\n                    anchors.fill: parent\n",
    "                    }\n\n                    ToolButton {\n                        id: visibilityButton\n                        Layout.preferredWidth: 36\n                        Layout.preferredHeight: 36\n                        text: rowRoot.visible ? \"◉\" : \"○\"\n                        onClicked: root.controller.setObjectVisible(rowRoot.objectId, !rowRoot.visible)\n                    }\n\n                    ToolButton {\n                        id: lockButton\n                        Layout.preferredWidth: 36\n                        Layout.preferredHeight: 36\n                        text: rowRoot.locked ? \"🔒\" : \"🔓\"\n                        onClicked: root.controller.setObjectLocked(rowRoot.objectId, !rowRoot.locked)\n                    }\n                }\n\n                MouseArea {\n                    id: mouseArea\n                    anchors.left: parent.left\n                    anchors.right: visibilityButton.left\n                    anchors.top: parent.top\n                    anchors.bottom: parent.bottom\n",
)

# Tests.
tests = Path("tests/test_editor_session.cpp")
text = tests.read_text(encoding="utf-8")
text += r'''

TEST_CASE("visibility is command based and hidden selection is cleared") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Visibility", &error));
    const auto object = session.createObject(m3d::ObjectType::Empty, "Object");
    REQUIRE(object.has_value());
    REQUIRE(session.select(*object, m3d::SelectionMode::Replace));
    REQUIRE(session.setObjectVisible(*object, false));
    REQUIRE(!session.scene()->find(*object)->visible);
    REQUIRE(session.selection().empty());
    REQUIRE(session.nextUndoName() == "Set Visibility");
    REQUIRE(session.undo());
    REQUIRE(session.scene()->find(*object)->visible);
    REQUIRE(session.redo());
    REQUIRE(!session.scene()->find(*object)->visible);
}

TEST_CASE("locked objects remain inspectable but reject editor mutations") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Locking", &error));
    const auto object = session.createObject(m3d::ObjectType::Empty, "Object");
    REQUIRE(object.has_value());
    REQUIRE(session.setObjectLocked(*object, true));
    REQUIRE(session.scene()->find(*object)->locked);
    REQUIRE(session.select(*object, m3d::SelectionMode::Replace));

    auto transform = session.scene()->find(*object)->localTransform;
    transform.position.x = 3.0F;
    REQUIRE(!session.transformObject(*object, transform));
    REQUIRE(!session.renameObject(*object, "Renamed"));
    REQUIRE(!session.reparentObject(*object, std::nullopt));
    REQUIRE(!session.deleteObject(*object));
    REQUIRE(!session.deleteSelection());
    REQUIRE(!session.duplicateSelection());
    REQUIRE(!session.beginTransformTransaction({*object}, "Locked Transform"));

    REQUIRE(session.setObjectLocked(*object, false));
    REQUIRE(!session.scene()->find(*object)->locked);
    REQUIRE(session.transformObject(*object, transform));
}
'''
tests.write_text(text, encoding="utf-8")

print("visibility and edit locking applied")
