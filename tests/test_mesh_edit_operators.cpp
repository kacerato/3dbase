#include "test_harness.hpp"

#include "mobile3d/editor/editor_session.hpp"

#include <array>
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

TEST_CASE("edit mode merge to active previews and commits as one mesh undo") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());

    const auto edge = session.editableMesh()->edges().front();
    const auto* halfEdge = session.editableMesh()->findHalfEdge(edge.halfEdge);
    const auto* next = halfEdge ? session.editableMesh()->findHalfEdge(halfEdge->next) : nullptr;
    REQUIRE(halfEdge != nullptr);
    REQUIRE(next != nullptr);
    const auto first = halfEdge->origin;
    const auto active = next->origin;
    REQUIRE(session.selectMeshVertex(first));
    REQUIRE(session.selectMeshVertex(active, m3d::MeshSelectionAction::Add));
    REQUIRE(session.meshSelection()->activeVertex() == active);
    REQUIRE(session.mergeSelectedMeshVertices(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->vertexCount() == 7U);
    REQUIRE(session.editableMesh()->findVertex(first) == nullptr);
    REQUIRE(session.editableMesh()->findVertex(active) != nullptr);
    REQUIRE(session.meshSelection()->selectedVertices().size() == 1U);
    REQUIRE(session.meshSelection()->activeVertex() == active);

    REQUIRE(session.commitMeshEdit("Merge Vertices", &error));
    REQUIRE(session.nextUndoName() == "Merge Vertices");
    REQUIRE(session.undo());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 8U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 7U);
}

TEST_CASE("edit mode weld by distance keeps active representative and can cancel") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());

    const auto edge = session.editableMesh()->edges().front();
    const auto* halfEdge = session.editableMesh()->findHalfEdge(edge.halfEdge);
    const auto* next = halfEdge ? session.editableMesh()->findHalfEdge(halfEdge->next) : nullptr;
    REQUIRE(halfEdge != nullptr);
    REQUIRE(next != nullptr);
    REQUIRE(session.selectMeshVertex(halfEdge->origin));
    REQUIRE(session.selectMeshVertex(next->origin, m3d::MeshSelectionAction::Add));
    const auto active = session.meshSelection()->activeVertex();
    REQUIRE(active.has_value());
    REQUIRE(session.weldSelectedMeshVertices(1.01F, &error));
    REQUIRE(session.editableMesh()->vertexCount() == 7U);
    REQUIRE(session.meshSelection()->selectedVertices().size() == 1U);
    REQUIRE(session.meshSelection()->activeVertex() == active);
    REQUIRE(session.cancelMeshEdit());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 8U);
}

TEST_CASE("edit mode weld with no matching distance is a clean no-op") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    const auto vertices = session.editableMesh()->vertices();
    REQUIRE(vertices.size() >= 7U);
    REQUIRE(session.selectMeshVertex(vertices[0].id));
    REQUIRE(session.selectMeshVertex(vertices[6].id, m3d::MeshSelectionAction::Add));
    REQUIRE(!session.weldSelectedMeshVertices(0.01F, &error));
    REQUIRE(!error.empty());
    REQUIRE(session.editableMesh()->vertexCount() == 8U);
    REQUIRE(!session.isDirty());
    REQUIRE(session.cancelMeshEdit());
}

TEST_CASE("edit mode fill closes a selected boundary loop and commits through undo") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Fill Operator", &error));

    m3d::MeshResource resource = m3d::MeshResource::makeCube("Open Cube", 1.0F);
    REQUIRE(resource.authoring.has_value());
    REQUIRE(resource.authoring->removeFace(resource.authoring->faces().front().id, &error));
    REQUIRE(resource.rebuildFromAuthoring(&error));
    const auto object = session.createMeshObject(std::move(resource), "Open Cube");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.beginMeshEdit(*object, &error));
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));

    bool first = true;
    std::size_t boundaryCount = 0U;
    for (const auto& edge : session.editableMesh()->edges()) {
        const auto* halfEdge = session.editableMesh()->findHalfEdge(edge.halfEdge);
        if (!halfEdge || !halfEdge->twin.isNull()) continue;
        REQUIRE(session.selectMeshEdge(edge.id, first ? m3d::MeshSelectionAction::Replace
                                                      : m3d::MeshSelectionAction::Add));
        first = false;
        ++boundaryCount;
    }
    REQUIRE(boundaryCount == 4U);
    REQUIRE(session.fillSelectedMeshBoundary(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->faceCount() == 6U);
    REQUIRE(session.meshSelection()->mode() == m3d::MeshSelectionMode::Face);
    REQUIRE(session.meshSelection()->selectedFaces().size() == 1U);

    REQUIRE(session.commitMeshEdit("Fill Boundary", &error));
    REQUIRE(session.nextUndoName() == "Fill Boundary");
    REQUIRE(session.undo());
    const auto resourceId = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 5U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 6U);
}

TEST_CASE("edit mode bridge connects two equal boundary loops and cancel restores input") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Bridge Operator", &error));

    m3d::EditableMesh authored;
    const std::array<m3d::EditableVertexId, 4> bottom{
        authored.addVertex({-1.0F,-1.0F,0.0F}), authored.addVertex({1.0F,-1.0F,0.0F}),
        authored.addVertex({1.0F,1.0F,0.0F}), authored.addVertex({-1.0F,1.0F,0.0F})
    };
    const std::array<m3d::EditableVertexId, 4> top{
        authored.addVertex({-1.0F,-1.0F,2.0F}), authored.addVertex({1.0F,-1.0F,2.0F}),
        authored.addVertex({1.0F,1.0F,2.0F}), authored.addVertex({-1.0F,1.0F,2.0F})
    };
    const std::array<m3d::EditableVertexId, 4> bottomWinding{bottom[0],bottom[3],bottom[2],bottom[1]};
    const std::array<m3d::EditableVertexId, 4> topWinding{top[0],top[1],top[2],top[3]};
    REQUIRE(authored.addFace(bottomWinding, &error).has_value());
    REQUIRE(authored.addFace(topWinding, &error).has_value());

    m3d::MeshResource resource;
    resource.id = m3d::ResourceId::generate();
    resource.name = "Two Loops";
    resource.authoring = authored;
    REQUIRE(resource.rebuildFromAuthoring(&error));
    const auto object = session.createMeshObject(std::move(resource), "Two Loops");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.beginMeshEdit(*object, &error));
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));

    bool first = true;
    for (const auto& edge : session.editableMesh()->edges()) {
        REQUIRE(session.selectMeshEdge(edge.id, first ? m3d::MeshSelectionAction::Replace
                                                      : m3d::MeshSelectionAction::Add));
        first = false;
    }
    REQUIRE(session.meshSelection()->selectedEdges().size() == 8U);
    REQUIRE(session.bridgeSelectedMeshBoundaries(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->faceCount() == 6U);
    REQUIRE(session.editableMesh()->edgeCount() == 12U);
    REQUIRE(session.meshSelection()->selectedFaces().size() == 4U);
    REQUIRE(session.cancelMeshEdit());
    const auto resourceId = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 2U);
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->edgeCount() == 8U);
}

TEST_CASE("edit mode loop cut selects the generated quad ring and commits through undo") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));
    const auto startEdge = session.editableMesh()->edges().front().id;
    REQUIRE(session.selectMeshEdge(startEdge));
    REQUIRE(session.loopCutSelectedMeshEdge(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->vertexCount() == 12U);
    REQUIRE(session.editableMesh()->edgeCount() == 20U);
    REQUIRE(session.editableMesh()->faceCount() == 10U);
    REQUIRE(session.meshSelection()->mode() == m3d::MeshSelectionMode::Edge);
    REQUIRE(session.meshSelection()->selectedEdges().size() == 4U);

    REQUIRE(session.commitMeshEdit("Loop Cut", &error));
    REQUIRE(session.nextUndoName() == "Loop Cut");
    REQUIRE(session.undo());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 8U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 6U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 12U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 10U);
}

TEST_CASE("edit mode delete face previews and undo restores exact authored topology") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Face));
    const auto face = session.editableMesh()->faces().front().id;
    REQUIRE(session.selectMeshFace(face));
    REQUIRE(session.deleteSelectedMeshElements(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->faceCount() == 5U);
    REQUIRE(session.meshSelection()->empty());
    REQUIRE(session.commitMeshEdit("Delete Face", &error));
    REQUIRE(session.nextUndoName() == "Delete Face");
    REQUIRE(session.undo());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 6U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 5U);
}

TEST_CASE("edit mode delete edge removes its incident faces and cancel restores cube") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));
    const auto edge = session.editableMesh()->edges().front().id;
    REQUIRE(session.selectMeshEdge(edge));
    REQUIRE(session.deleteSelectedMeshElements(&error));
    REQUIRE(session.editableMesh()->faceCount() == 4U);
    REQUIRE(session.editableMesh()->edgeCount() == 11U);
    REQUIRE(session.cancelMeshEdit());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 6U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->edgeCount() == 12U);
}

TEST_CASE("edit mode delete vertex removes incident topology and keeps remaining mesh valid") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    const auto vertex = session.editableMesh()->vertices().front().id;
    REQUIRE(session.selectMeshVertex(vertex));
    REQUIRE(session.deleteSelectedMeshElements(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->vertexCount() == 7U);
    REQUIRE(session.editableMesh()->faceCount() == 3U);
    REQUIRE(session.editableMesh()->findVertex(vertex) == nullptr);
    REQUIRE(session.cancelMeshEdit());
}

TEST_CASE("edit mode refuses deleting the last editable face") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Single Face Delete", &error));
    m3d::EditableMesh authored;
    const auto a = authored.addVertex({0.0F,0.0F,0.0F});
    const auto b = authored.addVertex({1.0F,0.0F,0.0F});
    const auto c = authored.addVertex({0.0F,1.0F,0.0F});
    const std::array<m3d::EditableVertexId,3> triangle{a,b,c};
    REQUIRE(authored.addFace(triangle,&error).has_value());
    m3d::MeshResource resource;
    resource.id = m3d::ResourceId::generate();
    resource.name = "Triangle";
    resource.authoring = authored;
    REQUIRE(resource.rebuildFromAuthoring(&error));
    const auto object = session.createMeshObject(std::move(resource), "Triangle");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.beginMeshEdit(*object,&error));
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Face));
    REQUIRE(session.selectMeshFace(session.editableMesh()->faces().front().id));
    REQUIRE(!session.deleteSelectedMeshElements(&error));
    REQUIRE(!error.empty());
    REQUIRE(session.editableMesh()->faceCount() == 1U);
    REQUIRE(!session.isDirty());
    REQUIRE(session.cancelMeshEdit());
}

TEST_CASE("edit mode multi loop cut remains one committed mesh transaction") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));
    const auto startEdge = session.editableMesh()->edges().front().id;
    REQUIRE(session.selectMeshEdge(startEdge));
    REQUIRE(session.loopCutSelectedMeshEdge(3U, &error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->vertexCount() == 20U);
    REQUIRE(session.editableMesh()->edgeCount() == 36U);
    REQUIRE(session.editableMesh()->faceCount() == 18U);
    REQUIRE(session.meshSelection()->selectedEdges().size() == 12U);

    REQUIRE(session.commitMeshEdit("Loop Cut x3", &error));
    REQUIRE(session.nextUndoName() == "Loop Cut x3");
    REQUIRE(session.undo());
    const auto resource = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 8U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 6U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->vertexCount() == 20U);
    REQUIRE(session.scene()->findMeshResource(resource)->authoring->faceCount() == 18U);
}

TEST_CASE("edit mode flip normals updates live render normals and cancel restores outward cube") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Face));
    REQUIRE(session.selectMeshFace(session.editableMesh()->faces().front().id));
    REQUIRE(session.flipSelectedMeshNormalComponents(&error));
    REQUIRE(error.empty());
    REQUIRE(session.meshSelection()->selectedFaces().size() == 6U);

    const auto resourceId = *session.scene()->find(*object)->meshResource;
    const auto* preview = session.scene()->findMeshResource(resourceId);
    REQUIRE(preview != nullptr);
    for (const auto& vertex : preview->vertices) {
        const float orientation = vertex.normal.x * vertex.position.x +
                                  vertex.normal.y * vertex.position.y +
                                  vertex.normal.z * vertex.position.z;
        REQUIRE(orientation < 0.0F);
    }
    REQUIRE(session.cancelMeshEdit());
    const auto* restored = session.scene()->findMeshResource(resourceId);
    for (const auto& vertex : restored->vertices) {
        const float orientation = vertex.normal.x * vertex.position.x +
                                  vertex.normal.y * vertex.position.y +
                                  vertex.normal.z * vertex.position.z;
        REQUIRE(orientation > 0.0F);
    }
}

TEST_CASE("flip then recalculate outside commits as one normals undo") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Face));
    REQUIRE(session.selectMeshFace(session.editableMesh()->faces().front().id));
    REQUIRE(session.flipSelectedMeshNormalComponents(&error));
    REQUIRE(session.recalculateMeshNormalsOutside(&error));

    const auto resourceId = *session.scene()->find(*object)->meshResource;
    const auto* preview = session.scene()->findMeshResource(resourceId);
    for (const auto& vertex : preview->vertices) {
        const float orientation = vertex.normal.x * vertex.position.x +
                                  vertex.normal.y * vertex.position.y +
                                  vertex.normal.z * vertex.position.z;
        REQUIRE(orientation > 0.0F);
    }
    REQUIRE(session.commitMeshEdit("Recalculate Normals", &error));
    REQUIRE(session.nextUndoName() == "Recalculate Normals");
    REQUIRE(session.undo());
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 6U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 6U);
}

TEST_CASE("recalculate outside on open mesh is a clean no-op") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    const auto object = createEditableCube(session, path, error);
    REQUIRE(object.has_value());
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Face));
    REQUIRE(session.selectMeshFace(session.editableMesh()->faces().front().id));
    REQUIRE(session.deleteSelectedMeshElements(&error));
    REQUIRE(session.commitMeshEdit("Open Mesh", &error));
    REQUIRE(session.saveProject(&error));
    REQUIRE(!session.isDirty());
    REQUIRE(session.beginMeshEdit(*object, &error));
    REQUIRE(session.recalculateMeshNormalsOutside(&error));
    REQUIRE(!session.isDirty());
    REQUIRE(session.cancelMeshEdit());
}

TEST_CASE("edit mode bridge adapts triangle and quad boundary loops in one transaction") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Adaptive Bridge", &error));

    m3d::EditableMesh authored;
    const std::array<m3d::EditableVertexId,3> triangle{
        authored.addVertex({0.0F,-1.2F,0.0F}), authored.addVertex({1.1F,0.8F,0.0F}),
        authored.addVertex({-1.1F,0.8F,0.0F})
    };
    const std::array<m3d::EditableVertexId,4> quad{
        authored.addVertex({-1.2F,-1.2F,2.0F}), authored.addVertex({1.2F,-1.2F,2.0F}),
        authored.addVertex({1.2F,1.2F,2.0F}), authored.addVertex({-1.2F,1.2F,2.0F})
    };
    const std::array<m3d::EditableVertexId,3> triangleWinding{triangle[0],triangle[2],triangle[1]};
    REQUIRE(authored.addFace(triangleWinding,&error).has_value());
    REQUIRE(authored.addFace(quad,&error).has_value());

    m3d::MeshResource resource;
    resource.id = m3d::ResourceId::generate();
    resource.name = "Triangle Quad Loops";
    resource.authoring = authored;
    REQUIRE(resource.rebuildFromAuthoring(&error));
    const auto object = session.createMeshObject(std::move(resource), "Triangle Quad Loops");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.beginMeshEdit(*object,&error));
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));

    bool first = true;
    for (const auto& edge : session.editableMesh()->edges()) {
        REQUIRE(session.selectMeshEdge(edge.id, first ? m3d::MeshSelectionAction::Replace
                                                      : m3d::MeshSelectionAction::Add));
        first = false;
    }
    REQUIRE(session.meshSelection()->selectedEdges().size() == 7U);
    REQUIRE(session.bridgeSelectedMeshBoundaries(&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->faceCount() == 8U);
    REQUIRE(session.editableMesh()->edgeCount() == 13U);
    REQUIRE(session.meshSelection()->mode() == m3d::MeshSelectionMode::Face);
    REQUIRE(session.meshSelection()->selectedFaces().size() == 6U);

    REQUIRE(session.commitMeshEdit("Bridge Loops", &error));
    REQUIRE(session.nextUndoName() == "Bridge Loops");
    REQUIRE(session.undo());
    const auto resourceId = *session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 2U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount() == 8U);
}

TEST_CASE("edit mode grid fill builds a structured quad patch and remains one undo") {
    const auto path = meshOperatorProjectPath();
    MeshOperatorCleanup cleanup(path);
    m3d::EditorSession session;
    std::string error;
    REQUIRE(session.createProject(path, "Grid Fill Operator", &error));

    m3d::EditableMesh authored;
    const std::array<m3d::Vec3,10> ringPositions{
        m3d::Vec3{-1.5F,-1.0F,1.0F}, m3d::Vec3{-0.5F,-1.0F,1.0F},
        m3d::Vec3{0.5F,-1.0F,1.0F}, m3d::Vec3{1.5F,-1.0F,1.0F},
        m3d::Vec3{1.5F,0.0F,1.0F}, m3d::Vec3{1.5F,1.0F,1.0F},
        m3d::Vec3{0.5F,1.0F,1.0F}, m3d::Vec3{-0.5F,1.0F,1.0F},
        m3d::Vec3{-1.5F,1.0F,1.0F}, m3d::Vec3{-1.5F,0.0F,1.0F}
    };
    std::array<m3d::EditableVertexId,10> top{};
    std::array<m3d::EditableVertexId,10> bottom{};
    for (std::size_t index=0; index<ringPositions.size(); ++index) {
        top[index]=authored.addVertex(ringPositions[index]);
        auto position=ringPositions[index]; position.z=-1.0F;
        bottom[index]=authored.addVertex(position);
    }
    REQUIRE(authored.addFace(bottom,&error).has_value());
    for (std::size_t index=0; index<top.size(); ++index) {
        const std::size_t next=(index+1U)%top.size();
        const std::array<m3d::EditableVertexId,4> side{top[index],top[next],bottom[next],bottom[index]};
        REQUIRE(authored.addFace(side,&error).has_value());
    }
    m3d::MeshResource resource;
    resource.id=m3d::ResourceId::generate();
    resource.name="Grid Fill Open Prism";
    resource.authoring=authored;
    REQUIRE(resource.rebuildFromAuthoring(&error));
    const auto object=session.createMeshObject(std::move(resource),"Grid Fill Open Prism");
    REQUIRE(object.has_value());
    REQUIRE(session.saveProject(&error));
    REQUIRE(session.beginMeshEdit(*object,&error));
    REQUIRE(session.setMeshSelectionMode(m3d::MeshSelectionMode::Edge));

    bool first=true;
    std::size_t boundaryCount=0U;
    for (const auto& edge:session.editableMesh()->edges()) {
        const auto* halfEdge=session.editableMesh()->findHalfEdge(edge.halfEdge);
        if (!halfEdge || !halfEdge->twin.isNull()) continue;
        REQUIRE(session.selectMeshEdge(edge.id, first ? m3d::MeshSelectionAction::Replace
                                                      : m3d::MeshSelectionAction::Add));
        first=false; ++boundaryCount;
    }
    REQUIRE(boundaryCount==10U);
    REQUIRE(session.gridFillSelectedMeshBoundary(3U,0U,&error));
    REQUIRE(error.empty());
    REQUIRE(session.editableMesh()->vertexCount()==22U);
    REQUIRE(session.editableMesh()->faceCount()==17U);
    REQUIRE(session.meshSelection()->mode()==m3d::MeshSelectionMode::Face);
    REQUIRE(session.meshSelection()->selectedFaces().size()==6U);

    REQUIRE(session.commitMeshEdit("Grid Fill",&error));
    REQUIRE(session.nextUndoName()=="Grid Fill");
    REQUIRE(session.undo());
    const auto resourceId=*session.scene()->find(*object)->meshResource;
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->vertexCount()==20U);
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount()==11U);
    REQUIRE(session.redo());
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->vertexCount()==22U);
    REQUIRE(session.scene()->findMeshResource(resourceId)->authoring->faceCount()==17U);
}
