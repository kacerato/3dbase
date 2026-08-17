from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text()


def write(path: str, content: str) -> None:
    Path(path).write_text(content)


def replace_once(path: str, old: str, new: str) -> None:
    content = read(path)
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected 1 match, found {count}: {old[:120]!r}")
    write(path, content.replace(old, new, 1))


path = 'src/core/include/mobile3d/core/editable_mesh.hpp'
replace_once(path,
'''    [[nodiscard]] std::optional<EditableLoopCutResult> loopCut(\n        EditableEdgeId edge, std::string* error = nullptr);\n''',
'''    [[nodiscard]] std::optional<EditableLoopCutResult> loopCut(\n        EditableEdgeId edge, std::string* error = nullptr);\n    [[nodiscard]] bool deleteFaces(std::span<const EditableFaceId> faces,\n                                   std::string* error = nullptr);\n    [[nodiscard]] bool deleteEdges(std::span<const EditableEdgeId> edges,\n                                   std::string* error = nullptr);\n    [[nodiscard]] bool deleteVertices(std::span<const EditableVertexId> vertices,\n                                      std::string* error = nullptr);\n''')

path = 'src/core/src/editable_mesh_operations.cpp'
content = read(path)
closing = '\n} // namespace m3d\n'
append = r'''

bool EditableMesh::deleteFaces(std::span<const EditableFaceId> facesToDelete,
                               std::string* error) {
    std::set<EditableFaceId> unique(facesToDelete.begin(), facesToDelete.end());
    if (unique.empty()) {
        if (error) *error = "Delete Faces requires at least one selected face";
        return false;
    }
    for (const auto face : unique) {
        if (!findFace(face)) {
            if (error) *error = "Delete Faces selection contains a missing face";
            return false;
        }
    }
    if (unique.size() >= faceCount_) {
        if (error) *error = "Delete Faces cannot remove the last editable face";
        return false;
    }

    EditableMesh working = *this;
    for (const auto face : unique) {
        if (!working.removeFace(face, error)) return false;
    }
    if (!working.validate(error)) return false;
    *this = std::move(working);
    if (error) error->clear();
    return true;
}

bool EditableMesh::deleteEdges(std::span<const EditableEdgeId> edgesToDelete,
                               std::string* error) {
    std::set<EditableEdgeId> unique(edgesToDelete.begin(), edgesToDelete.end());
    if (unique.empty()) {
        if (error) *error = "Delete Edges requires at least one selected edge";
        return false;
    }

    std::set<EditableFaceId> incidentFaces;
    for (const auto edgeId : unique) {
        const auto* edge = findEdge(edgeId);
        const auto* halfEdge = edge ? findHalfEdge(edge->halfEdge) : nullptr;
        if (!edge || !halfEdge) {
            if (error) *error = "Delete Edges selection contains a missing edge";
            return false;
        }
        incidentFaces.insert(halfEdge->face);
        if (!halfEdge->twin.isNull()) {
            const auto* twin = findHalfEdge(halfEdge->twin);
            if (!twin) {
                if (error) *error = "Delete Edges encountered an invalid twin";
                return false;
            }
            incidentFaces.insert(twin->face);
        }
    }
    if (incidentFaces.empty()) {
        if (error) *error = "Delete Edges found no incident editable faces";
        return false;
    }
    if (incidentFaces.size() >= faceCount_) {
        if (error) *error = "Delete Edges cannot remove the last editable face";
        return false;
    }

    EditableMesh working = *this;
    for (const auto face : incidentFaces) {
        if (working.findFace(face) && !working.removeFace(face, error)) return false;
    }
    if (!working.validate(error)) return false;
    *this = std::move(working);
    if (error) error->clear();
    return true;
}

bool EditableMesh::deleteVertices(std::span<const EditableVertexId> verticesToDelete,
                                  std::string* error) {
    std::set<EditableVertexId> unique(verticesToDelete.begin(), verticesToDelete.end());
    if (unique.empty()) {
        if (error) *error = "Delete Vertices requires at least one selected vertex";
        return false;
    }
    for (const auto vertex : unique) {
        if (!findVertex(vertex)) {
            if (error) *error = "Delete Vertices selection contains a missing vertex";
            return false;
        }
    }
    if (unique.size() >= vertexCount_) {
        if (error) *error = "Delete Vertices cannot remove every editable vertex";
        return false;
    }

    std::set<EditableFaceId> incidentFaces;
    for (const auto& face : faces()) {
        const auto loop = faceVertices(face.id);
        if (std::any_of(loop.cbegin(), loop.cend(), [&unique](EditableVertexId vertex) {
                return unique.contains(vertex);
            })) {
            incidentFaces.insert(face.id);
        }
    }
    if (incidentFaces.size() >= faceCount_) {
        if (error) *error = "Delete Vertices cannot remove the last editable face";
        return false;
    }

    EditableMesh working = *this;
    for (const auto face : incidentFaces) {
        if (working.findFace(face) && !working.removeFace(face, error)) return false;
    }

    for (const auto vertexId : unique) {
        for (const auto& halfEdge : working.halfEdges()) {
            if (halfEdge.origin == vertexId || working.destination(halfEdge.id) == vertexId) {
                if (error) *error = "Delete Vertices left a selected vertex referenced by topology";
                return false;
            }
        }
        if (vertexId.isNull() || static_cast<std::size_t>(vertexId.value) > working.vertices_.size()) {
            if (error) *error = "Delete Vertices encountered an invalid vertex slot";
            return false;
        }
        auto& slot = working.vertices_[static_cast<std::size_t>(vertexId.value - 1U)];
        if (!slot) {
            if (error) *error = "Delete Vertices encountered an empty vertex slot";
            return false;
        }
        slot.reset();
        --working.vertexCount_;
    }

    if (!working.validate(error)) return false;
    *this = std::move(working);
    if (error) error->clear();
    return true;
}
'''
if 'EditableMesh::deleteFaces' not in content:
    if not content.endswith(closing): raise SystemExit('editable mesh operations namespace closing not found')
    write(path, content[:-len(closing)] + append + closing)

# Core tests.
path = 'tests/test_mesh_operations.cpp'
content = read(path)
if 'delete face opens topology without deleting its vertices' not in content:
    content = content.rstrip() + r'''

TEST_CASE("delete face opens topology without deleting its vertices") {
    auto mesh = m3d::EditableMesh::makeCube();
    const auto face = mesh.faces().front().id;
    const std::array<m3d::EditableFaceId, 1> selected{face};
    std::string error;
    REQUIRE(mesh.deleteFaces(selected, &error));
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 8U);
    REQUIRE(mesh.edgeCount() == 12U);
    REQUIRE(mesh.halfEdgeCount() == 20U);
    REQUIRE(mesh.faceCount() == 5U);
    REQUIRE(mesh.findFace(face) == nullptr);
    std::size_t boundaryEdges = 0U;
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        if (halfEdge && halfEdge->twin.isNull()) ++boundaryEdges;
    }
    REQUIRE(boundaryEdges == 4U);
}

TEST_CASE("delete edge removes both incident cube faces") {
    auto mesh = m3d::EditableMesh::makeCube();
    const auto edge = mesh.edges().front().id;
    const std::array<m3d::EditableEdgeId, 1> selected{edge};
    std::string error;
    REQUIRE(mesh.deleteEdges(selected, &error));
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 8U);
    REQUIRE(mesh.edgeCount() == 11U);
    REQUIRE(mesh.halfEdgeCount() == 16U);
    REQUIRE(mesh.faceCount() == 4U);
    REQUIRE(mesh.findEdge(edge) == nullptr);
}

TEST_CASE("delete vertex removes every incident face then the isolated vertex") {
    auto mesh = m3d::EditableMesh::makeCube();
    const auto vertex = mesh.vertices().front().id;
    const std::array<m3d::EditableVertexId, 1> selected{vertex};
    std::string error;
    REQUIRE(mesh.deleteVertices(selected, &error));
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 7U);
    REQUIRE(mesh.edgeCount() == 9U);
    REQUIRE(mesh.halfEdgeCount() == 12U);
    REQUIRE(mesh.faceCount() == 3U);
    REQUIRE(mesh.findVertex(vertex) == nullptr);
}

TEST_CASE("delete all faces is rejected atomically") {
    auto mesh = m3d::EditableMesh::makeCube();
    std::vector<m3d::EditableFaceId> selected;
    for (const auto& face : mesh.faces()) selected.push_back(face.id);
    const auto before = mesh.snapshot();
    std::string error;
    REQUIRE(!mesh.deleteFaces(selected, &error));
    REQUIRE(!error.empty());
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}
''' + '\n'
    write(path, content)

Path('tests/test_mesh_operations.cpp').write_text(Path('tests/test_mesh_operations.cpp').read_text().rstrip() + '\n')
print('component delete core patch prepared')
