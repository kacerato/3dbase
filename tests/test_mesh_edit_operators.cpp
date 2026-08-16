#include "test_harness.hpp"

#include "mobile3d/editor/editor_session.hpp"

#include <filesystem>
#include <string>

namespace {

std::filesystem::path meshOperatorProjectPath() {
    return std::filesystem::temp_directory_path() /
           ("mobile3d-mesh-operator-" + m3d::ObjectId::generate().toString());
}

struct MeshOperatorCleanup final {
    explicit MeshOperatorCleanup(std::filesystem::path value) : path(std::move(value)) {}
    ~MeshOperatorCleanup() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

std::optional<m3d::ObjectId> createEditableCube(m3d::EditorSession& session,
                                                 const std::filesystem::path& path,
                                                 std::string& error) {
    if (!session.createProject(path, "Mesh Operators", &error)) return std::nullopt;
    const auto object = session.createObject(m3d::ObjectType::Mesh, "Cube");
    if (!object || !session.saveProject(&error) || !session.beginMeshEdit(*object, &error)) return std::nullopt;
    return object;
}

} // namespace

TEST_CASE("edit mode face extrude previews and commits through one undo entry") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());

    const auto face = session.editableMesh()->faces().front().id;
    REQUIRE(session.selectMeshFace(face));
    REQUIRE(session.extrudeSelectedMeshFace(1.0F, &error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->vertexCount() == 12U);
    REQUIRE(session.editableMesh()->edgeCount() == 20U);
    REQUIRE(session.editableMesh()->faceCount() == 10U);
    REQUIRE(session.meshSelection()->selectedFaces().size() == 1U);
    REQUIRE(session.meshSelection()->activeFace().has_value());
    REQUIRE(session.editableMesh()->findFace(face) == nullptr);

    REQUIRE(session.commitMeshEdit("Extrude Face", &error));
    REQUIRE(session.nextUndoName() == "Extrude Face");
    REQUIRE(session.undo());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 8U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 6U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 12U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 10U);
}

TEST_CASE("edit mode inset selects the new inner face and can be cancelled") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    const auto face = session.editableMesh()->faces().front().id;
    REQUIRE(session.selectMeshFace(face));
    REQUIRE(session.insetSelectedMeshFace(0.25F, &error));
    REQUIRE(session.editableMesh()->vertexCount() == 12U);
    REQUIRE(session.editableMesh()->faceCount() == 10U);
    REQUIRE(session.meshSelection()->selectedFaces().size() == 1U);
    REQUIRE(session.cancelMeshEdit());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 8U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 6U);
}

TEST_CASE("edit mode subdivide selects all newly created face fan elements") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    const auto face = session.editableMesh()->faces().front().id;
    REQUIRE(session.selectMeshFace(face));
    REQUIRE(session.subdivideSelectedMeshFace(&error));
    REQUIRE(session.editableMesh()->vertexCount() == 9U);
    REQUIRE(session.editableMesh()->faceCount() == 9U);
    REQUIRE(session.meshSelection()->selectedFaces().size() == 4U);
    REQUIRE(session.commitMeshEdit("Subdivide Face", &error));
    REQUIRE(session.nextUndoName() == "Subdivide Face");
}

TEST_CASE("face region operators reject multiple selected faces until region semantics exist") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    const auto faces = session.editableMesh()->faces();
    REQUIRE(faces.size() >= 2U);
    REQUIRE(session.selectMeshFace(faces[0].id));
    REQUIRE(session.selectMeshFace(faces[1].id, m3d::MeshSelectionAction::Add));
    REQUIRE(!session.extrudeSelectedMeshFace(1.0F, &error));
    REQUIRE(!error.empty());
    REQUIRE(session.editableMesh()->faceCount() == 6U);
}
