#include "test_harness.hpp"

#include "mobile3d/editor/editor_session.hpp"

#include <filesystem>
#include <string>

namespace {

std::filesystem::path meshEditProjectPath() {
    return std::filesystem::temp_directory_path() /
           ("mobile3d-mesh-edit-" + m3d::ObjectId::generate().toString());
}

struct MeshEditCleanup final {
    explicit MeshEditCleanup(std::filesystem::path value) : path(std::move(value)) {}
    ~MeshEditCleanup() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

} // namespace

TEST_CASE("mesh edit preview commits as one undoable resource replacement") {
    const auto path = meshEditProjectPath();
    MeshEditCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Mesh Edit", &error));
    const auto object = session.createObject(m3d::ObjectType::Mesh, "Cube");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));

    const auto resourceId = session.scene()->find(*object)->meshResource;
    REQUIRE(resourceId.has_value());
    const auto* beforeResource = session.scene()->findMeshResource(*resourceId);
    REQUIRE(beforeResource != nullptr);
    REQUIRE(beforeResource->authoring.has_value());
    const auto beforeTopology = beforeResource->authoring->snapshot();
    const auto beforeVertices = beforeResource->vertices;
    const auto beforeIndices = beforeResource->indices;

    REQUIRE(session.beginMeshEdit(*object, &error));
    REQUIRE(session.hasMeshEditTransaction());
    REQUIRE(session.editableMesh() != nullptr);
    const auto vertex = session.editableMesh()->vertices().front().id;
    const auto originalPosition = session.editableMesh()->findVertex(vertex)->position;
    REQUIRE(session.selectMeshVertex(vertex));
    REQUIRE(session.moveSelectedMeshVertices({0.5F, 0.0F, 0.0F}, &error));

    const auto* preview = session.scene()->findMeshResource(*resourceId);
    REQUIRE(preview != nullptr);
    REQUIRE(preview->authoring.has_value());
    REQUIRE(preview->authoring->findVertex(vertex)->position.x == originalPosition.x + 0.5F);
    REQUIRE(preview->vertices != beforeVertices);

    REQUIRE(session.commitMeshEdit("Move Vertex", &error));
    REQUIRE(!session.hasMeshEditTransaction());
    REQUIRE(session.canUndo());
    REQUIRE(session.nextUndoName() == "Move Vertex");

    REQUIRE(session.undo());
    const auto* undone = session.scene()->findMeshResource(*resourceId);
    REQUIRE(undone != nullptr);
    REQUIRE(undone->authoring.has_value());
    REQUIRE(undone->authoring->snapshot().vertices == beforeTopology.vertices);
    REQUIRE(undone->authoring->snapshot().halfEdges == beforeTopology.halfEdges);
    REQUIRE(undone->authoring->snapshot().edges == beforeTopology.edges);
    REQUIRE(undone->authoring->snapshot().faces == beforeTopology.faces);
    REQUIRE(undone->vertices == beforeVertices);
    REQUIRE(undone->indices == beforeIndices);

    REQUIRE(session.redo());
    const auto* redone = session.scene()->findMeshResource(*resourceId);
    REQUIRE(redone != nullptr);
    REQUIRE(redone->authoring->findVertex(vertex)->position.x == originalPosition.x + 0.5F);
}

TEST_CASE("mesh edit cancel restores authored topology and derived render cache") {
    const auto path = meshEditProjectPath();
    MeshEditCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Mesh Cancel", &error));
    const auto object = session.createObject(m3d::ObjectType::Mesh, "Cube");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));

    const auto resourceId = *session.scene()->find(*object)->meshResource;
    const auto before = *session.scene()->findMeshResource(resourceId);
    REQUIRE(before.authoring.has_value());

    REQUIRE(session.beginMeshEdit(*object, &error));
    const auto vertex = session.editableMesh()->vertices().front().id;
    REQUIRE(session.selectMeshVertex(vertex));
    REQUIRE(session.moveSelectedMeshVertices({0.0F, 1.0F, 0.0F}, &error));
    REQUIRE(session.cancelMeshEdit());

    const auto* restored = session.scene()->findMeshResource(resourceId);
    REQUIRE(restored != nullptr);
    REQUIRE(restored->authoring.has_value());
    REQUIRE(restored->authoring->snapshot().vertices == before.authoring->snapshot().vertices);
    REQUIRE(restored->authoring->snapshot().halfEdges == before.authoring->snapshot().halfEdges);
    REQUIRE(restored->authoring->snapshot().edges == before.authoring->snapshot().edges);
    REQUIRE(restored->authoring->snapshot().faces == before.authoring->snapshot().faces);
    REQUIRE(restored->vertices == before.vertices);
    REQUIRE(restored->indices == before.indices);
    REQUIRE(!session.isDirty());
}

TEST_CASE("no-op mesh edit preserves the existing undo history") {
    const auto path = meshEditProjectPath();
    MeshEditCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Mesh Noop", &error));
    const auto object = session.createObject(m3d::ObjectType::Mesh, "Cube");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.canUndo());
    const std::string previousUndoName(session.nextUndoName());

    REQUIRE(session.beginMeshEdit(*object, &error));
    REQUIRE(session.commitMeshEdit("No-op Mesh Edit", &error));
    REQUIRE(session.canUndo());
    REQUIRE(session.nextUndoName() == previousUndoName);
    REQUIRE(!session.isDirty());
}
