#include "test_harness.hpp"

#include "mobile3d/editor/editor_session.hpp"

#include <filesystem>
#include <string>

namespace {

std::filesystem::path uniqueProjectPath() {
    return std::filesystem::temp_directory_path() /
           ("mobile3d-editor-session-" + m3d::ObjectId::generate().toString());
}

struct ProjectCleanup final {
    explicit ProjectCleanup(std::filesystem::path value) : path(std::move(value)) {}
    ~ProjectCleanup() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

} // namespace

TEST_CASE("editor session owns project lifecycle and dirty state") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;

    REQUIRE(session.createProject(path, "Session Test", &error));
    REQUIRE(session.hasProject());
    REQUIRE(!session.isDirty());
    REQUIRE(session.scene() != nullptr);
    REQUIRE(session.scene()->size() == 0);

    const auto object = session.createObject(m3d::ObjectType::Mesh, "Cube");
    REQUIRE(object.has_value());
    REQUIRE(session.scene()->contains(*object));
    REQUIRE(session.scene()->meshResourceCount() == 1);
    REQUIRE(session.scene()->find(*object)->meshResource.has_value());
    REQUIRE(session.isDirty());
    REQUIRE(session.selection().active() == object);
    REQUIRE(session.canUndo());

    REQUIRE(session.undo());
    REQUIRE(!session.scene()->contains(*object));
    REQUIRE(session.scene()->meshResourceCount() == 0);
    REQUIRE(session.canRedo());
    REQUIRE(session.redo());
    REQUIRE(session.scene()->contains(*object));
    REQUIRE(session.scene()->meshResourceCount() == 1);
    REQUIRE(session.saveProject(&error));
    REQUIRE(!session.isDirty());
}

TEST_CASE("mesh primitive creation keeps object and geometry in one undoable command") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Mesh Command", &error));

    const auto object = session.createMeshObject(m3d::MeshResource::makeCube("Cube Geometry", 2.0F), "Cube");
    REQUIRE(object.has_value());
    REQUIRE(session.scene()->size() == 1);
    REQUIRE(session.scene()->meshResourceCount() == 1);
    const auto* sceneObject = session.scene()->find(*object);
    REQUIRE(sceneObject != nullptr);
    REQUIRE(sceneObject->meshResource.has_value());
    const auto resource = *sceneObject->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource) != nullptr);

    REQUIRE(session.undo());
    REQUIRE(session.scene()->size() == 0);
    REQUIRE(session.scene()->meshResourceCount() == 0);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->contains(*object));
    REQUIRE(session.scene()->meshResourceCount() == 1);
    REQUIRE(session.scene()->find(*object)->meshResource == resource);
}

TEST_CASE("delete mesh releases orphan geometry and undo restores it") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Delete Mesh", &error));
    const auto object = session.createObject(m3d::ObjectType::Mesh, "Cube");
    REQUIRE(object.has_value());
    const auto resource = session.scene()->find(*object)->meshResource;
    REQUIRE(resource.has_value());
    REQUIRE(session.scene()->meshResourceCount() == 1);

    REQUIRE(session.deleteObject(*object));
    REQUIRE(session.scene()->size() == 0);
    REQUIRE(session.scene()->meshResourceCount() == 0);
    REQUIRE(session.undo());
    REQUIRE(session.scene()->contains(*object));
    REQUIRE(session.scene()->meshResourceCount() == 1);
    REQUIRE(session.scene()->find(*object)->meshResource == resource);
}

TEST_CASE("editor session autosave recovery stays dirty until primary save") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    std::string error;
    {
        m3d::EditorSession session;
        REQUIRE(session.createProject(path, "Recovery Test", &error));
        REQUIRE(session.createObject(m3d::ObjectType::Mesh, "Recovered Mesh").has_value());
        REQUIRE(session.writeAutosave(&error));
        REQUIRE(session.hasAutosave());
    }
    m3d::EditorSession reopened;
    REQUIRE(reopened.openProject(path, &error));
    REQUIRE(reopened.scene()->size() == 0);
    REQUIRE(reopened.hasAutosave());
    REQUIRE(reopened.recoverAutosave(&error));
    REQUIRE(reopened.scene()->size() == 1);
    REQUIRE(reopened.scene()->meshResourceCount() == 1);
    REQUIRE(reopened.isDirty());
    REQUIRE(reopened.saveProject(&error));
    REQUIRE(!reopened.isDirty());
    REQUIRE(!reopened.hasAutosave());
}

TEST_CASE("editor session can discard recovery without touching primary scene") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Discard Test", &error));
    REQUIRE(session.createObject(m3d::ObjectType::Mesh, "Unsaved Mesh").has_value());
    REQUIRE(session.writeAutosave(&error));
    REQUIRE(session.hasAutosave());
    REQUIRE(session.discardAutosave(&error));
    REQUIRE(!session.hasAutosave());
    REQUIRE(session.isDirty());
}

TEST_CASE("delete selection handles selected parent and child as one valid transaction") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Delete Test", &error));
    const auto parent = session.createObject(m3d::ObjectType::Empty, "Parent");
    REQUIRE(parent.has_value());
    const auto child = session.createObject(m3d::ObjectType::Mesh, "Child", *parent);
    REQUIRE(child.has_value());
    REQUIRE(session.scene()->size() == 2);
    REQUIRE(session.scene()->meshResourceCount() == 1);
    REQUIRE(session.select(*parent, m3d::SelectionMode::Replace));
    REQUIRE(session.select(*child, m3d::SelectionMode::Add));
    REQUIRE(session.selection().size() == 2);
    REQUIRE(session.deleteSelection());
    REQUIRE(session.scene()->size() == 0);
    REQUIRE(session.scene()->meshResourceCount() == 0);
    REQUIRE(session.selection().empty());
    REQUIRE(session.undo());
    REQUIRE(session.scene()->size() == 2);
    REQUIRE(session.scene()->meshResourceCount() == 1);
    REQUIRE(session.scene()->find(*child)->parent == parent);
}

TEST_CASE("workspace changes are editor state and do not dirty the scene") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Workspace Test", &error));
    const auto previousRevision = session.uiRevision();
    session.setWorkspace(m3d::Workspace::Modeling);
    REQUIRE(session.workspace() == m3d::Workspace::Modeling);
    REQUIRE(m3d::workspaceName(session.workspace()) == "Modeling");
    REQUIRE(session.uiRevision() == previousRevision + 1);
    REQUIRE(!session.isDirty());
}


TEST_CASE("transform transaction previews many objects but commits one undo step") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Transform Transaction", &error));
    const auto first = session.createObject(m3d::ObjectType::Empty, "First");
    const auto second = session.createObject(m3d::ObjectType::Empty, "Second");
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(session.saveProject(&error));

    const auto firstBefore = session.scene()->find(*first)->localTransform;
    const auto secondBefore = session.scene()->find(*second)->localTransform;
    REQUIRE(session.beginTransformTransaction({*first, *second}, "Move Objects"));
    REQUIRE(session.hasTransformTransaction());
    REQUIRE(!session.isDirty());

    auto firstPreview = firstBefore;
    firstPreview.position = {1.0F, 2.0F, 3.0F};
    auto secondPreview = secondBefore;
    secondPreview.position = {-4.0F, 5.0F, 6.0F};
    REQUIRE(session.previewTransform(*first, firstPreview));
    REQUIRE(session.previewTransform(*second, secondPreview));
    REQUIRE(session.isDirty());
    REQUIRE(session.scene()->find(*first)->localTransform == firstPreview);
    REQUIRE(session.scene()->find(*second)->localTransform == secondPreview);
    REQUIRE(!session.saveProject(&error));
    REQUIRE(!session.writeAutosave(&error));

    REQUIRE(session.commitTransformTransaction());
    REQUIRE(!session.hasTransformTransaction());
    REQUIRE(session.nextUndoName() == "Move Objects");
    REQUIRE(session.undo());
    REQUIRE(session.scene()->find(*first)->localTransform == firstBefore);
    REQUIRE(session.scene()->find(*second)->localTransform == secondBefore);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->find(*first)->localTransform == firstPreview);
    REQUIRE(session.scene()->find(*second)->localTransform == secondPreview);
}

TEST_CASE("cancelled transform transaction restores preview and does not create history") {
    const auto path = uniqueProjectPath();
    ProjectCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Cancel Transform", &error));
    const auto object = session.createObject(m3d::ObjectType::Empty, "Object");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    const auto previousUndo = session.nextUndoName();
    const auto before = session.scene()->find(*object)->localTransform;
    REQUIRE(session.beginTransformTransaction({*object}, "Move Object"));
    auto preview = before;
    preview.position.x = 12.0F;
    REQUIRE(session.previewTransform(*object, preview));
    REQUIRE(session.isDirty());
    REQUIRE(!session.select(*object, m3d::SelectionMode::Replace));
    REQUIRE(!session.undo());
    REQUIRE(session.cancelTransformTransaction());
    REQUIRE(session.scene()->find(*object)->localTransform == before);
    REQUIRE(session.nextUndoName() == previousUndo);
    REQUIRE(!session.isDirty());
}
