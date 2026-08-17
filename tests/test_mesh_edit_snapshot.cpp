#include "test_harness.hpp"

#include "mobile3d/editor/editor_session.hpp"

#include <filesystem>
#include <string>

namespace {
std::filesystem::path snapshotProjectPath() {
    return std::filesystem::temp_directory_path() /
           ("mobile3d-edit-snapshot-" + m3d::ObjectId::generate().toString());
}
struct Cleanup final {
    explicit Cleanup(std::filesystem::path value) : path(std::move(value)) {}
    ~Cleanup() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};
} // namespace

TEST_CASE("mesh edit presentation snapshot is immutable editor render data") {
    const auto path = snapshotProjectPath();
    Cleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Edit Snapshot", &error));
    const auto object = session.createObject(m3d::ObjectType::Mesh, "Cube");
    REQUIRE(object.has_value());
    REQUIRE(session.beginMeshEdit(*object, &error));

    const auto initial = session.meshEditPresentationSnapshot();
    REQUIRE(initial.active());
    REQUIRE(initial.object == *object);
    REQUIRE(initial.vertices.size() == 8U);
    REQUIRE(initial.edges.size() == 12U);
    REQUIRE(initial.faces.size() == 6U);
    REQUIRE(initial.mode == m3d::MeshSelectionMode::Vertex);

    const auto face = initial.faces.front().id;
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Face));
    REQUIRE(session.selectMeshFace(face));
    const auto selected = session.meshEditPresentationSnapshot();
    REQUIRE(selected.mode == m3d::MeshSelectionMode::Face);
    REQUIRE(selected.revision > initial.revision);
    bool foundSelected = false;
    for (const auto& item : selected.faces) {
        if (item.id == face) foundSelected = item.selected;
    }
    REQUIRE(foundSelected);

    REQUIRE(session.extrudeSelectedMeshFace(0.25F, &error));
    const auto extruded = session.meshEditPresentationSnapshot();
    REQUIRE(extruded.vertices.size() > initial.vertices.size());
    REQUIRE(extruded.faces.size() > initial.faces.size());
    REQUIRE(extruded.revision > selected.revision);
    REQUIRE(session.cancelMeshEdit());
    REQUIRE(!session.meshEditPresentationSnapshot().active());
}
