#include "test_harness.hpp"

#include "mobile3d/editor/mesh_selection.hpp"

TEST_CASE("mesh selection keeps topology ids separate by element type") {
    const auto mesh = m3d::EditableMesh::makeCube();
    m3d::MeshSelectionModel selection;

    const auto vertex = mesh.vertices().front().id;
    const auto edge = mesh.edges().front().id;
    const auto face = mesh.faces().front().id;

    REQUIRE(selection.select(mesh, vertex));
    REQUIRE(selection.mode() == m3d::MeshSelectionMode::Vertex);
    REQUIRE(selection.contains(vertex));
    REQUIRE(selection.activeVertex() == vertex);
    REQUIRE(selection.selectedEdges().empty());
    REQUIRE(selection.selectedFaces().empty());

    REQUIRE(selection.select(mesh, edge, m3d::MeshSelectionAction::Add));
    REQUIRE(selection.mode() == m3d::MeshSelectionMode::Edge);
    REQUIRE(selection.contains(vertex));
    REQUIRE(selection.contains(edge));
    REQUIRE(selection.activeEdge() == edge);

    REQUIRE(selection.select(mesh, face, m3d::MeshSelectionAction::Replace));
    REQUIRE(selection.mode() == m3d::MeshSelectionMode::Face);
    REQUIRE(!selection.contains(vertex));
    REQUIRE(!selection.contains(edge));
    REQUIRE(selection.contains(face));
    REQUIRE(selection.activeFace() == face);
}

TEST_CASE("mesh selection add toggle remove and invalid ids are deterministic") {
    const auto mesh = m3d::EditableMesh::makeCube();
    const auto vertices = mesh.vertices();
    REQUIRE(vertices.size() >= 2U);

    m3d::MeshSelectionModel selection;
    REQUIRE(selection.select(mesh, vertices[0].id));
    REQUIRE(selection.select(mesh, vertices[1].id, m3d::MeshSelectionAction::Add));
    REQUIRE(selection.selectedVertices().size() == 2U);

    REQUIRE(selection.select(mesh, vertices[0].id, m3d::MeshSelectionAction::Toggle));
    REQUIRE(!selection.contains(vertices[0].id));
    REQUIRE(selection.contains(vertices[1].id));

    REQUIRE(selection.select(mesh, vertices[1].id, m3d::MeshSelectionAction::Remove));
    REQUIRE(selection.empty());
    REQUIRE(!selection.select(mesh, m3d::EditableVertexId{999999U}));
    REQUIRE(selection.empty());
}

TEST_CASE("mesh selection mode can change without inventing topology selection") {
    const auto mesh = m3d::EditableMesh::makeCube();
    m3d::MeshSelectionModel selection;
    const auto vertex = mesh.vertices().front().id;
    REQUIRE(selection.select(mesh, vertex));
    selection.setMode(m3d::MeshSelectionMode::Edge);
    REQUIRE(selection.mode() == m3d::MeshSelectionMode::Edge);
    REQUIRE(selection.emptyCurrentMode());
    REQUIRE(!selection.empty());
    selection.clear();
    REQUIRE(selection.empty());
}
