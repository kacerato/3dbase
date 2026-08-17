#include "mobile3d/editor/mesh_edit_snapshot.hpp"

namespace m3d {

MeshEditPresentationSnapshot MeshEditSnapshotBuilder::build(
    const EditableMesh& mesh,
    const MeshSelectionModel& selection,
    ObjectId object,
    ResourceId resource,
    std::uint64_t revision) {
    MeshEditPresentationSnapshot snapshot;
    snapshot.object = object;
    snapshot.resource = resource;
    snapshot.mode = selection.mode();
    snapshot.revision = revision;

    const auto vertices = mesh.vertices();
    snapshot.vertices.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        snapshot.vertices.push_back(MeshEditVertexSnapshot{
            .id = vertex.id,
            .position = vertex.position,
            .selected = selection.contains(vertex.id),
        });
    }

    const auto edges = mesh.edges();
    snapshot.edges.reserve(edges.size());
    for (const auto& edge : edges) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        const auto* next = halfEdge ? mesh.findHalfEdge(halfEdge->next) : nullptr;
        if (!halfEdge || !next) continue;
        snapshot.edges.push_back(MeshEditEdgeSnapshot{
            .id = edge.id,
            .first = halfEdge->origin,
            .second = next->origin,
            .selected = selection.contains(edge.id),
        });
    }

    const auto faces = mesh.faces();
    snapshot.faces.reserve(faces.size());
    for (const auto& face : faces) {
        snapshot.faces.push_back(MeshEditFaceSnapshot{
            .id = face.id,
            .vertices = mesh.faceVertices(face.id),
            .selected = selection.contains(face.id),
        });
    }
    return snapshot;
}

} // namespace m3d
