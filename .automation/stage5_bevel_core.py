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
'''struct EditableLoopCutResult final {\n    std::vector<EditableVertexId> vertices;\n    std::vector<EditableEdgeId> edges;\n    std::vector<EditableFaceId> faces;\n};\n''',
'''struct EditableLoopCutResult final {\n    std::vector<EditableVertexId> vertices;\n    std::vector<EditableEdgeId> edges;\n    std::vector<EditableFaceId> faces;\n};\n\nstruct EditableBevelResult final {\n    EditableFaceId bevelFace{};\n    std::array<EditableVertexId, 4> vertices{};\n};\n''')
replace_once(path,
'''    [[nodiscard]] std::optional<EditableLoopCutResult> loopCut(\n        EditableEdgeId edge, std::uint32_t cuts, std::string* error = nullptr);\n''',
'''    [[nodiscard]] std::optional<EditableLoopCutResult> loopCut(\n        EditableEdgeId edge, std::uint32_t cuts, std::string* error = nullptr);\n    [[nodiscard]] std::optional<EditableBevelResult> bevelEdge(\n        EditableEdgeId edge, float width, std::string* error = nullptr);\n''')

path = 'src/core/src/editable_mesh_operations.cpp'
content = read(path)
anchor = '\n\nbool EditableMesh::deleteFaces('
if 'EditableMesh::bevelEdge' not in content:
    implementation = r'''

std::optional<EditableBevelResult> EditableMesh::bevelEdge(
    EditableEdgeId edgeId, float width, std::string* error) {
    if (!std::isfinite(width) || width <= 1.0e-6F) {
        if (error) *error = "Bevel width must be finite and positive";
        return std::nullopt;
    }
    const auto* edge = findEdge(edgeId);
    const auto* firstHalfEdge = edge ? findHalfEdge(edge->halfEdge) : nullptr;
    const auto* firstNext = firstHalfEdge ? findHalfEdge(firstHalfEdge->next) : nullptr;
    const auto* secondHalfEdge = firstHalfEdge && !firstHalfEdge->twin.isNull()
        ? findHalfEdge(firstHalfEdge->twin) : nullptr;
    const auto* secondNext = secondHalfEdge ? findHalfEdge(secondHalfEdge->next) : nullptr;
    if (!edge || !firstHalfEdge || !firstNext || !secondHalfEdge || !secondNext) {
        if (error) *error = "Bevel baseline requires a closed manifold edge";
        return std::nullopt;
    }

    const EditableVertexId a = firstHalfEdge->origin;
    const EditableVertexId b = firstNext->origin;
    if (secondHalfEdge->origin != b || secondNext->origin != a) {
        if (error) *error = "Bevel edge twin orientation is inconsistent";
        return std::nullopt;
    }
    const EditableFaceId firstFace = firstHalfEdge->face;
    const EditableFaceId secondFace = secondHalfEdge->face;
    const auto firstLoop = faceVertices(firstFace);
    const auto secondLoop = faceVertices(secondFace);
    if (firstLoop.size() < 3U || secondLoop.size() < 3U) {
        if (error) *error = "Bevel requires non-degenerate incident faces";
        return std::nullopt;
    }

    const auto findDirectedIndex = [](const std::vector<EditableVertexId>& loop,
                                      EditableVertexId from, EditableVertexId to)
        -> std::optional<std::size_t> {
        for (std::size_t index = 0; index < loop.size(); ++index) {
            if (loop[index] == from && loop[(index + 1U) % loop.size()] == to) return index;
        }
        return std::nullopt;
    };
    const auto firstIndex = findDirectedIndex(firstLoop, a, b);
    const auto secondIndex = findDirectedIndex(secondLoop, b, a);
    if (!firstIndex || !secondIndex) {
        if (error) *error = "Bevel could not locate the selected edge in both incident faces";
        return std::nullopt;
    }

    const EditableVertexId aNeighborFirst =
        firstLoop[(*firstIndex + firstLoop.size() - 1U) % firstLoop.size()];
    const EditableVertexId bNeighborFirst = firstLoop[(*firstIndex + 2U) % firstLoop.size()];
    const EditableVertexId bNeighborSecond =
        secondLoop[(*secondIndex + secondLoop.size() - 1U) % secondLoop.size()];
    const EditableVertexId aNeighborSecond = secondLoop[(*secondIndex + 2U) % secondLoop.size()];

    const auto incidentFaces = [this](EditableVertexId vertexId) {
        std::set<EditableFaceId> result;
        for (const auto& face : faces()) {
            const auto loop = faceVertices(face.id);
            if (std::find(loop.cbegin(), loop.cend(), vertexId) != loop.cend()) result.insert(face.id);
        }
        return result;
    };
    auto aFaces = incidentFaces(a);
    auto bFaces = incidentFaces(b);
    if (aFaces.size() != 3U || bFaces.size() != 3U) {
        if (error) *error = "Bevel baseline currently requires valence-3 edge endpoints";
        return std::nullopt;
    }
    aFaces.erase(firstFace); aFaces.erase(secondFace);
    bFaces.erase(firstFace); bFaces.erase(secondFace);
    if (aFaces.size() != 1U || bFaces.size() != 1U || *aFaces.begin() == *bFaces.begin()) {
        if (error) *error = "Bevel could not resolve unique endpoint cap faces";
        return std::nullopt;
    }
    const EditableFaceId aCapFace = *aFaces.begin();
    const EditableFaceId bCapFace = *bFaces.begin();
    const auto aCapLoop = faceVertices(aCapFace);
    const auto bCapLoop = faceVertices(bCapFace);

    const auto* aVertex = findVertex(a);
    const auto* bVertex = findVertex(b);
    const auto* aFirstNeighborVertex = findVertex(aNeighborFirst);
    const auto* aSecondNeighborVertex = findVertex(aNeighborSecond);
    const auto* bFirstNeighborVertex = findVertex(bNeighborFirst);
    const auto* bSecondNeighborVertex = findVertex(bNeighborSecond);
    if (!aVertex || !bVertex || !aFirstNeighborVertex || !aSecondNeighborVertex ||
        !bFirstNeighborVertex || !bSecondNeighborVertex) {
        if (error) *error = "Bevel references a missing adjacent vertex";
        return std::nullopt;
    }

    const auto offsetToward = [width, error](Vec3 origin, Vec3 target) -> std::optional<Vec3> {
        const float dx = target.x - origin.x;
        const float dy = target.y - origin.y;
        const float dz = target.z - origin.z;
        const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!std::isfinite(length) || length <= 1.0e-7F) {
            if (error) *error = "Bevel encountered a zero-length adjacent edge";
            return std::nullopt;
        }
        if (width >= length * 0.49F) {
            if (error) *error = "Bevel width is too large for the local edge neighborhood";
            return std::nullopt;
        }
        const float factor = width / length;
        return Vec3{origin.x + dx * factor, origin.y + dy * factor, origin.z + dz * factor};
    };
    const auto aFirstPosition = offsetToward(aVertex->position, aFirstNeighborVertex->position);
    const auto aSecondPosition = offsetToward(aVertex->position, aSecondNeighborVertex->position);
    const auto bFirstPosition = offsetToward(bVertex->position, bFirstNeighborVertex->position);
    const auto bSecondPosition = offsetToward(bVertex->position, bSecondNeighborVertex->position);
    if (!aFirstPosition || !aSecondPosition || !bFirstPosition || !bSecondPosition) return std::nullopt;

    EditableMesh working = *this;
    const EditableVertexId aFirst = working.addVertex(*aFirstPosition);
    const EditableVertexId aSecond = working.addVertex(*aSecondPosition);
    const EditableVertexId bFirst = working.addVertex(*bFirstPosition);
    const EditableVertexId bSecond = working.addVertex(*bSecondPosition);

    const auto replaceSimple = [](const std::vector<EditableVertexId>& loop,
                                  EditableVertexId firstOld, EditableVertexId firstNew,
                                  EditableVertexId secondOld, EditableVertexId secondNew) {
        auto result = loop;
        for (auto& vertex : result) {
            if (vertex == firstOld) vertex = firstNew;
            else if (vertex == secondOld) vertex = secondNew;
        }
        return result;
    };
    const auto expandEndpoint = [error](const std::vector<EditableVertexId>& loop,
                                        EditableVertexId endpoint,
                                        EditableVertexId neighborOne, EditableVertexId replacementOne,
                                        EditableVertexId neighborTwo, EditableVertexId replacementTwo)
        -> std::optional<std::vector<EditableVertexId>> {
        const auto found = std::find(loop.cbegin(), loop.cend(), endpoint);
        if (found == loop.cend()) {
            if (error) *error = "Bevel endpoint cap face does not contain its endpoint";
            return std::nullopt;
        }
        const std::size_t index = static_cast<std::size_t>(std::distance(loop.cbegin(), found));
        const EditableVertexId previous = loop[(index + loop.size() - 1U) % loop.size()];
        const EditableVertexId next = loop[(index + 1U) % loop.size()];
        const auto replacementFor = [&](EditableVertexId neighbor) -> EditableVertexId {
            if (neighbor == neighborOne) return replacementOne;
            if (neighbor == neighborTwo) return replacementTwo;
            return {};
        };
        const EditableVertexId firstReplacement = replacementFor(previous);
        const EditableVertexId secondReplacement = replacementFor(next);
        if (firstReplacement.isNull() || secondReplacement.isNull() ||
            firstReplacement == secondReplacement) {
            if (error) *error = "Bevel endpoint cap adjacency is not valence-3 manifold topology";
            return std::nullopt;
        }
        std::vector<EditableVertexId> result;
        result.reserve(loop.size() + 1U);
        for (std::size_t loopIndex = 0; loopIndex < loop.size(); ++loopIndex) {
            if (loopIndex != index) result.push_back(loop[loopIndex]);
            else {
                result.push_back(firstReplacement);
                result.push_back(secondReplacement);
            }
        }
        return result;
    };

    const auto rebuiltFirst = replaceSimple(firstLoop, a, aFirst, b, bFirst);
    const auto rebuiltSecond = replaceSimple(secondLoop, b, bSecond, a, aSecond);
    const auto rebuiltACap = expandEndpoint(aCapLoop, a,
                                             aNeighborFirst, aFirst,
                                             aNeighborSecond, aSecond);
    const auto rebuiltBCap = expandEndpoint(bCapLoop, b,
                                             bNeighborFirst, bFirst,
                                             bNeighborSecond, bSecond);
    if (!rebuiltACap || !rebuiltBCap) return std::nullopt;

    const std::array<EditableFaceId, 4> affectedFaces{
        firstFace, secondFace, aCapFace, bCapFace
    };
    for (const auto face : affectedFaces) {
        if (!working.removeFace(face, error)) return std::nullopt;
    }

    for (const auto originalVertex : std::array<EditableVertexId, 2>{a, b}) {
        for (const auto& halfEdge : working.halfEdges()) {
            if (halfEdge.origin == originalVertex || working.destination(halfEdge.id) == originalVertex) {
                if (error) *error = "Bevel endpoint remained referenced after removing its valence-3 fan";
                return std::nullopt;
            }
        }
        auto& slot = working.vertices_[static_cast<std::size_t>(originalVertex.value - 1U)];
        if (!slot) {
            if (error) *error = "Bevel endpoint vertex slot disappeared unexpectedly";
            return std::nullopt;
        }
        slot.reset();
        --working.vertexCount_;
    }

    const auto firstReplacementFace = working.addFace(rebuiltFirst, error);
    const auto secondReplacementFace = firstReplacementFace ? working.addFace(rebuiltSecond, error) : std::nullopt;
    const auto aCapReplacementFace = secondReplacementFace ? working.addFace(*rebuiltACap, error) : std::nullopt;
    const auto bCapReplacementFace = aCapReplacementFace ? working.addFace(*rebuiltBCap, error) : std::nullopt;
    const std::array<EditableVertexId, 4> bevelLoop{aFirst, aSecond, bSecond, bFirst};
    const auto bevelFace = bCapReplacementFace ? working.addFace(bevelLoop, error) : std::nullopt;
    if (!firstReplacementFace || !secondReplacementFace || !aCapReplacementFace ||
        !bCapReplacementFace || !bevelFace) return std::nullopt;
    if (!working.faceNormal(*bevelFace)) {
        if (error) *error = "Bevel generated a degenerate chamfer face";
        return std::nullopt;
    }
    if (!working.validate(error)) return std::nullopt;

    *this = std::move(working);
    if (error) error->clear();
    return EditableBevelResult{*bevelFace, {aFirst, aSecond, bSecond, bFirst}};
}
'''
    if anchor not in content: raise SystemExit('deleteFaces anchor missing for bevel insertion')
    write(path, content.replace(anchor, implementation + anchor, 1))

# Core tests on cube hard-surface edge and rejection paths.
path = 'tests/test_mesh_operations.cpp'
content = read(path)
if 'single edge bevel creates a valid chamfer on valence three manifold topology' not in content:
    content = content.rstrip() + r'''

TEST_CASE("single edge bevel creates a valid chamfer on valence three manifold topology") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto edge = mesh.edges().front().id;
    std::string error;
    const auto result = mesh.bevelEdge(edge, 0.2F, &error);
    REQUIRE(result.has_value());
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 10U);
    REQUIRE(mesh.edgeCount() == 15U);
    REQUIRE(mesh.halfEdgeCount() == 30U);
    REQUIRE(mesh.faceCount() == 7U);
    REQUIRE(mesh.findFace(result->bevelFace) != nullptr);
    REQUIRE(mesh.faceVertices(result->bevelFace).size() == 4U);
    for (const auto vertex : result->vertices) REQUIRE(mesh.findVertex(vertex) != nullptr);

    m3d::MeshResource render;
    render.id = m3d::ResourceId::generate();
    render.name = "Beveled Cube";
    render.authoring = mesh;
    REQUIRE(render.rebuildFromAuthoring(&error));
    REQUIRE(render.validate(&error));
}

TEST_CASE("single edge bevel rejects excessive width atomically") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto edge = mesh.edges().front().id;
    const auto before = mesh.snapshot();
    std::string error;
    REQUIRE(!mesh.bevelEdge(edge, 1.0F, &error).has_value());
    REQUIRE(!error.empty());
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}

TEST_CASE("single edge bevel rejects boundary topology until boundary bevel policy exists") {
    auto mesh = m3d::EditableMesh::makeCube();
    std::string error;
    REQUIRE(mesh.deleteFaces(std::array<m3d::EditableFaceId,1>{mesh.faces().front().id},&error));
    m3d::EditableEdgeId boundary{};
    for (const auto& candidate:mesh.edges()) {
        const auto* halfEdge=mesh.findHalfEdge(candidate.halfEdge);
        if (halfEdge && halfEdge->twin.isNull()) { boundary=candidate.id; break; }
    }
    REQUIRE(!boundary.isNull());
    const auto before=mesh.snapshot();
    REQUIRE(!mesh.bevelEdge(boundary,0.1F,&error).has_value());
    REQUIRE(!error.empty());
    REQUIRE(mesh.snapshot().vertices==before.vertices);
    REQUIRE(mesh.snapshot().halfEdges==before.halfEdges);
    REQUIRE(mesh.snapshot().edges==before.edges);
    REQUIRE(mesh.snapshot().faces==before.faces);
}
''' + '\n'
    write(path, content)

Path('tests/test_mesh_operations.cpp').write_text(Path('tests/test_mesh_operations.cpp').read_text().rstrip() + '\n')
print('single edge hard surface bevel core patch prepared')
