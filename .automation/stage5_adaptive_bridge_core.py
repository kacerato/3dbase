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
'''    [[nodiscard]] std::optional<std::vector<EditableFaceId>> bridgeBoundaryLoops(\n        std::span<const EditableEdgeId> edges, std::string* error = nullptr);\n''',
'''    [[nodiscard]] std::optional<std::vector<EditableFaceId>> bridgeBoundaryLoops(\n        std::span<const EditableEdgeId> edges, std::string* error = nullptr);\n    [[nodiscard]] std::optional<std::vector<EditableFaceId>> bridgeBoundaryLoopsAdaptive(\n        std::span<const EditableEdgeId> edges, std::string* error = nullptr);\n''')

path = 'src/core/src/editable_mesh_operations.cpp'
content = read(path)
anchor = '\n\nstd::optional<EditableLoopCutResult> EditableMesh::loopCut('
if 'EditableMesh::bridgeBoundaryLoopsAdaptive' not in content:
    implementation = r'''

std::optional<std::vector<EditableFaceId>> EditableMesh::bridgeBoundaryLoopsAdaptive(
    std::span<const EditableEdgeId> edges, std::string* error) {
    const auto loops = selectedBoundaryLoops(*this, edges, error);
    if (!loops) return std::nullopt;
    if (loops->size() != 2U) {
        if (error) *error = "Bridge requires exactly two closed boundary loops";
        return std::nullopt;
    }
    const auto& first = (*loops)[0];
    const auto& second = (*loops)[1];
    if (first.size() < 3U || second.size() < 3U) {
        if (error) *error = "Bridge loops must contain at least three vertices";
        return std::nullopt;
    }

    std::set<EditableVertexId> firstVertices(first.begin(), first.end());
    for (const auto vertex : second) {
        if (firstVertices.contains(vertex)) {
            if (error) *error = "Bridge boundary loops must be vertex-disjoint";
            return std::nullopt;
        }
    }

    std::vector<Vec3> firstPositions;
    std::vector<Vec3> secondPositions;
    firstPositions.reserve(first.size());
    secondPositions.reserve(second.size());
    for (const auto vertex : first) {
        const auto* value = findVertex(vertex);
        if (!value) {
            if (error) *error = "Bridge loop contains a missing vertex";
            return std::nullopt;
        }
        firstPositions.push_back(value->position);
    }
    for (const auto vertex : second) {
        const auto* value = findVertex(vertex);
        if (!value) {
            if (error) *error = "Bridge loop contains a missing vertex";
            return std::nullopt;
        }
        secondPositions.push_back(value->position);
    }

    const std::size_t firstCount = first.size();
    const std::size_t secondCount = second.size();
    std::size_t bestOffset = 0U;
    double bestCost = std::numeric_limits<double>::infinity();
    for (std::size_t offset = 0; offset < secondCount; ++offset) {
        double cost = 0.0;
        for (std::size_t index = 0; index < firstCount; ++index) {
            const std::size_t normalizedSecond =
                ((index * secondCount + firstCount / 2U) / firstCount) % secondCount;
            const std::size_t secondIndex =
                (offset + secondCount - normalizedSecond) % secondCount;
            cost += static_cast<double>(squaredDistance(firstPositions[index], secondPositions[secondIndex]));
        }
        for (std::size_t index = 0; index < secondCount; ++index) {
            const std::size_t normalizedFirst =
                ((index * firstCount + secondCount / 2U) / secondCount) % firstCount;
            const std::size_t secondIndex = (offset + secondCount - index) % secondCount;
            cost += static_cast<double>(squaredDistance(firstPositions[normalizedFirst], secondPositions[secondIndex]));
        }
        if (cost < bestCost) {
            bestCost = cost;
            bestOffset = offset;
        }
    }

    std::vector<EditableVertexId> alignedSecond;
    alignedSecond.reserve(secondCount);
    for (std::size_t index = 0; index < secondCount; ++index) {
        alignedSecond.push_back(second[(bestOffset + secondCount - index) % secondCount]);
    }

    EditableMesh working = *this;
    std::vector<EditableFaceId> created;
    created.reserve(firstCount + secondCount);
    std::size_t firstIndex = 0U;
    std::size_t secondIndex = 0U;
    constexpr double kProgressEpsilon = 1.0e-12;
    while (firstIndex < firstCount || secondIndex < secondCount) {
        const double nextFirst = firstIndex < firstCount
            ? static_cast<double>(firstIndex + 1U) / static_cast<double>(firstCount)
            : std::numeric_limits<double>::infinity();
        const double nextSecond = secondIndex < secondCount
            ? static_cast<double>(secondIndex + 1U) / static_cast<double>(secondCount)
            : std::numeric_limits<double>::infinity();

        const EditableVertexId a = first[firstIndex % firstCount];
        const EditableVertexId b = alignedSecond[secondIndex % secondCount];
        std::optional<EditableFaceId> face;
        if (std::abs(nextFirst - nextSecond) <= kProgressEpsilon) {
            const std::array<EditableVertexId, 4> quad{
                a,
                b,
                alignedSecond[(secondIndex + 1U) % secondCount],
                first[(firstIndex + 1U) % firstCount],
            };
            face = working.addFace(quad, error);
            ++firstIndex;
            ++secondIndex;
        } else if (nextFirst < nextSecond) {
            const std::array<EditableVertexId, 3> triangle{
                a,
                b,
                first[(firstIndex + 1U) % firstCount],
            };
            face = working.addFace(triangle, error);
            ++firstIndex;
        } else {
            const std::array<EditableVertexId, 3> triangle{
                a,
                b,
                alignedSecond[(secondIndex + 1U) % secondCount],
            };
            face = working.addFace(triangle, error);
            ++secondIndex;
        }
        if (!face) return std::nullopt;
        if (!working.faceNormal(*face)) {
            if (error) *error = "Bridge generated a degenerate side face";
            return std::nullopt;
        }
        created.push_back(*face);
    }

    if (created.empty() || !working.validate(error)) return std::nullopt;
    *this = std::move(working);
    if (error) error->clear();
    return created;
}
'''
    if anchor not in content: raise SystemExit('loopCut anchor missing after bridge')
    write(path, content.replace(anchor, implementation + anchor, 1))

path = 'tests/test_mesh_operations.cpp'
content = read(path)
if 'adaptive bridge closes triangle to quad boundary loops' not in content:
    content = content.rstrip() + r'''

TEST_CASE("adaptive bridge closes triangle to quad boundary loops") {
    m3d::EditableMesh mesh;
    const std::array<m3d::EditableVertexId,3> triangle{
        mesh.addVertex({0.0F,-1.2F,0.0F}), mesh.addVertex({1.1F,0.8F,0.0F}),
        mesh.addVertex({-1.1F,0.8F,0.0F})
    };
    const std::array<m3d::EditableVertexId,4> quad{
        mesh.addVertex({-1.2F,-1.2F,2.0F}), mesh.addVertex({1.2F,-1.2F,2.0F}),
        mesh.addVertex({1.2F,1.2F,2.0F}), mesh.addVertex({-1.2F,1.2F,2.0F})
    };
    const std::array<m3d::EditableVertexId,3> triangleWinding{triangle[0],triangle[2],triangle[1]};
    std::string error;
    REQUIRE(mesh.addFace(triangleWinding,&error).has_value());
    REQUIRE(mesh.addFace(quad,&error).has_value());
    std::vector<m3d::EditableEdgeId> boundaries;
    for (const auto& edge : mesh.edges()) boundaries.push_back(edge.id);
    REQUIRE(boundaries.size() == 7U);

    const auto bridge = mesh.bridgeBoundaryLoopsAdaptive(boundaries,&error);
    REQUIRE(bridge.has_value());
    REQUIRE(bridge->size() == 6U);
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 7U);
    REQUIRE(mesh.edgeCount() == 13U);
    REQUIRE(mesh.halfEdgeCount() == 26U);
    REQUIRE(mesh.faceCount() == 8U);
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        REQUIRE(halfEdge != nullptr);
        REQUIRE(!halfEdge->twin.isNull());
    }
}

TEST_CASE("adaptive bridge retains quad band for equal loop counts") {
    m3d::EditableMesh mesh;
    const std::array<m3d::EditableVertexId,4> bottom{
        mesh.addVertex({-1,-1,0}),mesh.addVertex({1,-1,0}),mesh.addVertex({1,1,0}),mesh.addVertex({-1,1,0})};
    const std::array<m3d::EditableVertexId,4> top{
        mesh.addVertex({-1,-1,2}),mesh.addVertex({1,-1,2}),mesh.addVertex({1,1,2}),mesh.addVertex({-1,1,2})};
    const std::array<m3d::EditableVertexId,4> bottomWinding{bottom[0],bottom[3],bottom[2],bottom[1]};
    std::string error;
    REQUIRE(mesh.addFace(bottomWinding,&error).has_value());
    REQUIRE(mesh.addFace(top,&error).has_value());
    std::vector<m3d::EditableEdgeId> boundaries;
    for(const auto& edge:mesh.edges()) boundaries.push_back(edge.id);
    const auto bridge=mesh.bridgeBoundaryLoopsAdaptive(boundaries,&error);
    REQUIRE(bridge.has_value());
    REQUIRE(bridge->size()==4U);
    for(const auto faceId:*bridge) REQUIRE(mesh.faceVertices(faceId).size()==4U);
    REQUIRE(mesh.validate(&error));
}
''' + '\n'
    write(path, content)

Path('tests/test_mesh_operations.cpp').write_text(Path('tests/test_mesh_operations.cpp').read_text().rstrip() + '\n')
print('adaptive unequal bridge core patch prepared')
