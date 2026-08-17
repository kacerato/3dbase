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
'''    [[nodiscard]] std::optional<EditableVertexWeldResult> weldVertices(\n        std::span<const EditableVertexId> vertices, float distance,\n        std::optional<EditableVertexId> preferredTarget = std::nullopt,\n        std::string* error = nullptr);\n''',
'''    [[nodiscard]] std::optional<EditableVertexWeldResult> weldVertices(\n        std::span<const EditableVertexId> vertices, float distance,\n        std::optional<EditableVertexId> preferredTarget = std::nullopt,\n        std::string* error = nullptr);\n    [[nodiscard]] std::optional<EditableFaceId> fillBoundaryLoop(\n        std::span<const EditableEdgeId> edges, std::string* error = nullptr);\n    [[nodiscard]] std::optional<std::vector<EditableFaceId>> bridgeBoundaryLoops(\n        std::span<const EditableEdgeId> edges, std::string* error = nullptr);\n''')

path = 'src/core/src/editable_mesh_operations.cpp'
replace_once(path,
'''#include <map>\n#include <set>\n#include <utility>\n''',
'''#include <limits>\n#include <map>\n#include <set>\n#include <utility>\n''')

helper_anchor = '''[[nodiscard]] Vec3 addScaled(Vec3 value, Vec3 direction, float scale) noexcept {\n    value.x += direction.x * scale;\n    value.y += direction.y * scale;\n    value.z += direction.z * scale;\n    return value;\n}\n\n} // namespace\n'''
helper_new = r'''[[nodiscard]] Vec3 addScaled(Vec3 value, Vec3 direction, float scale) noexcept {
    value.x += direction.x * scale;
    value.y += direction.y * scale;
    value.z += direction.z * scale;
    return value;
}

struct BoundaryArc final {
    EditableEdgeId edge{};
    EditableVertexId destination{};
};

[[nodiscard]] std::optional<std::vector<std::vector<EditableVertexId>>> selectedBoundaryLoops(
    const EditableMesh& mesh, std::span<const EditableEdgeId> selectedEdges,
    std::string* error) {
    std::set<EditableEdgeId> unique(selectedEdges.begin(), selectedEdges.end());
    if (unique.size() < 3U) {
        if (error) *error = "Boundary operation requires at least three unique edges";
        return std::nullopt;
    }

    std::map<EditableVertexId, BoundaryArc> outgoing;
    std::set<EditableVertexId> incoming;
    for (const auto edgeId : unique) {
        const auto* edge = mesh.findEdge(edgeId);
        const auto* halfEdge = edge ? mesh.findHalfEdge(edge->halfEdge) : nullptr;
        const auto* next = halfEdge ? mesh.findHalfEdge(halfEdge->next) : nullptr;
        if (!edge || !halfEdge || !next) {
            if (error) *error = "Selected boundary edge is invalid";
            return std::nullopt;
        }
        if (!halfEdge->twin.isNull()) {
            if (error) *error = "Selected edge is not on an open boundary";
            return std::nullopt;
        }
        const auto origin = halfEdge->origin;
        const auto destination = next->origin;
        if (origin.isNull() || destination.isNull() || origin == destination ||
            outgoing.contains(origin) || incoming.contains(destination)) {
            if (error) *error = "Selected boundary edges branch or repeat a vertex";
            return std::nullopt;
        }
        outgoing.emplace(origin, BoundaryArc{edgeId, destination});
        incoming.insert(destination);
    }

    std::set<EditableEdgeId> visited;
    std::vector<std::vector<EditableVertexId>> loops;
    for (const auto& [origin, arc] : outgoing) {
        if (visited.contains(arc.edge)) continue;
        std::vector<EditableVertexId> loop;
        const EditableVertexId start = origin;
        EditableVertexId current = start;
        bool closed = false;
        for (std::size_t step = 0; step <= unique.size(); ++step) {
            const auto found = outgoing.find(current);
            if (found == outgoing.end()) break;
            if (visited.contains(found->second.edge)) {
                closed = current == start;
                break;
            }
            loop.push_back(current);
            visited.insert(found->second.edge);
            current = found->second.destination;
            if (current == start) {
                closed = true;
                break;
            }
        }
        if (!closed || loop.size() < 3U) {
            if (error) *error = "Selected boundary edges do not form closed loops";
            return std::nullopt;
        }
        loops.push_back(std::move(loop));
    }

    if (visited.size() != unique.size()) {
        if (error) *error = "Selected boundary traversal did not consume every edge";
        return std::nullopt;
    }
    if (error) error->clear();
    return loops;
}

[[nodiscard]] float squaredDistance(Vec3 left, Vec3 right) noexcept {
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return x * x + y * y + z * z;
}

} // namespace
'''
replace_once(path, helper_anchor, helper_new)

append = r'''

std::optional<EditableFaceId> EditableMesh::fillBoundaryLoop(
    std::span<const EditableEdgeId> edges, std::string* error) {
    const auto loops = selectedBoundaryLoops(*this, edges, error);
    if (!loops) return std::nullopt;
    if (loops->size() != 1U) {
        if (error) *error = "Fill requires exactly one closed boundary loop";
        return std::nullopt;
    }

    EditableMesh working = *this;
    std::vector<EditableVertexId> faceVertices = loops->front();
    std::reverse(faceVertices.begin(), faceVertices.end());
    const auto face = working.addFace(faceVertices, error);
    if (!face || !working.validate(error)) return std::nullopt;

    *this = std::move(working);
    if (error) error->clear();
    return face;
}

std::optional<std::vector<EditableFaceId>> EditableMesh::bridgeBoundaryLoops(
    std::span<const EditableEdgeId> edges, std::string* error) {
    const auto loops = selectedBoundaryLoops(*this, edges, error);
    if (!loops) return std::nullopt;
    if (loops->size() != 2U) {
        if (error) *error = "Bridge currently requires exactly two closed boundary loops";
        return std::nullopt;
    }
    const auto& first = (*loops)[0];
    const auto& second = (*loops)[1];
    if (first.size() != second.size()) {
        if (error) *error = "Bridge currently requires boundary loops with the same vertex count";
        return std::nullopt;
    }
    if (first.size() < 3U) {
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

    const std::size_t count = first.size();
    std::size_t bestOffset = 0U;
    float bestCost = std::numeric_limits<float>::infinity();
    for (std::size_t offset = 0; offset < count; ++offset) {
        float cost = 0.0F;
        for (std::size_t index = 0; index < count; ++index) {
            const std::size_t secondIndex = (offset + count - index) % count;
            cost += squaredDistance(firstPositions[index], secondPositions[secondIndex]);
        }
        if (cost < bestCost) {
            bestCost = cost;
            bestOffset = offset;
        }
    }

    EditableMesh working = *this;
    std::vector<EditableFaceId> created;
    created.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t nextIndex = (index + 1U) % count;
        const std::size_t secondIndex = (bestOffset + count - index) % count;
        const std::size_t secondNext = (bestOffset + count - nextIndex) % count;
        const std::array<EditableVertexId, 4> quad{
            first[index], second[secondIndex], second[secondNext], first[nextIndex]
        };
        const auto face = working.addFace(quad, error);
        if (!face) return std::nullopt;
        created.push_back(*face);
    }
    if (!working.validate(error)) return std::nullopt;

    *this = std::move(working);
    if (error) error->clear();
    return created;
}
'''
content = read(path)
closing = '\n} // namespace m3d\n'
if 'EditableMesh::fillBoundaryLoop' not in content:
    if not content.endswith(closing): raise SystemExit('editable mesh operations namespace closing not found')
    write(path, content[:-len(closing)] + append + closing)

# Core operation tests.
path = 'tests/test_mesh_operations.cpp'
content = read(path)
if 'fill boundary loop closes a removed cube face' not in content:
    content = content.rstrip() + r'''

TEST_CASE("fill boundary loop closes a removed cube face and reuses boundary edge ids") {
    auto mesh = m3d::EditableMesh::makeCube(1.0F);
    const auto removed = mesh.faces().front().id;
    const auto removedVertices = mesh.faceVertices(removed);
    std::string error;
    REQUIRE(mesh.removeFace(removed, &error));
    REQUIRE(mesh.faceCount() == 5U);

    std::vector<m3d::EditableEdgeId> boundary;
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        if (halfEdge && halfEdge->twin.isNull()) boundary.push_back(edge.id);
    }
    REQUIRE(boundary.size() == 4U);
    const auto boundaryIds = boundary;
    const auto filled = mesh.fillBoundaryLoop(boundary, &error);
    REQUIRE(filled.has_value());
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 8U);
    REQUIRE(mesh.edgeCount() == 12U);
    REQUIRE(mesh.halfEdgeCount() == 24U);
    REQUIRE(mesh.faceCount() == 6U);
    REQUIRE(mesh.faceVertices(*filled).size() == removedVertices.size());
    for (const auto edgeId : boundaryIds) {
        const auto* edge = mesh.findEdge(edgeId);
        const auto* halfEdge = edge ? mesh.findHalfEdge(edge->halfEdge) : nullptr;
        REQUIRE(edge != nullptr);
        REQUIRE(halfEdge != nullptr);
        REQUIRE(!halfEdge->twin.isNull());
    }
}

TEST_CASE("bridge equal boundary loops creates a closed quad band") {
    m3d::EditableMesh mesh;
    const std::array<m3d::EditableVertexId, 4> bottom{
        mesh.addVertex({-1.0F, -1.0F, 0.0F}), mesh.addVertex({1.0F, -1.0F, 0.0F}),
        mesh.addVertex({1.0F, 1.0F, 0.0F}), mesh.addVertex({-1.0F, 1.0F, 0.0F})
    };
    const std::array<m3d::EditableVertexId, 4> top{
        mesh.addVertex({-1.0F, -1.0F, 2.0F}), mesh.addVertex({1.0F, -1.0F, 2.0F}),
        mesh.addVertex({1.0F, 1.0F, 2.0F}), mesh.addVertex({-1.0F, 1.0F, 2.0F})
    };
    const std::array<m3d::EditableVertexId, 4> bottomWinding{bottom[0], bottom[3], bottom[2], bottom[1]};
    const std::array<m3d::EditableVertexId, 4> topWinding{top[0], top[1], top[2], top[3]};
    std::string error;
    REQUIRE(mesh.addFace(bottomWinding, &error).has_value());
    REQUIRE(mesh.addFace(topWinding, &error).has_value());
    REQUIRE(mesh.validate(&error));

    std::vector<m3d::EditableEdgeId> boundaries;
    for (const auto& edge : mesh.edges()) boundaries.push_back(edge.id);
    REQUIRE(boundaries.size() == 8U);
    const auto bridge = mesh.bridgeBoundaryLoops(boundaries, &error);
    REQUIRE(bridge.has_value());
    REQUIRE(bridge->size() == 4U);
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 8U);
    REQUIRE(mesh.edgeCount() == 12U);
    REQUIRE(mesh.halfEdgeCount() == 24U);
    REQUIRE(mesh.faceCount() == 6U);
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        REQUIRE(halfEdge != nullptr);
        REQUIRE(!halfEdge->twin.isNull());
    }
}

TEST_CASE("fill rejects an incomplete boundary selection atomically") {
    auto mesh = m3d::EditableMesh::makeCube();
    std::string error;
    REQUIRE(mesh.removeFace(mesh.faces().front().id, &error));
    std::vector<m3d::EditableEdgeId> boundary;
    for (const auto& edge : mesh.edges()) {
        const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
        if (halfEdge && halfEdge->twin.isNull()) boundary.push_back(edge.id);
    }
    REQUIRE(boundary.size() == 4U);
    boundary.pop_back();
    const auto before = mesh.snapshot();
    REQUIRE(!mesh.fillBoundaryLoop(boundary, &error).has_value());
    REQUIRE(!error.empty());
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}
''' + '\n'
    write(path, content)

Path('tests/test_mesh_operations.cpp').write_text(Path('tests/test_mesh_operations.cpp').read_text().rstrip() + '\n')
print('fill and bridge core patch prepared')
