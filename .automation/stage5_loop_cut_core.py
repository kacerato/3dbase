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
'''struct EditableVertexWeldResult final {\n    std::vector<EditableVertexId> survivors;\n    std::size_t mergedCount{0};\n};\n''',
'''struct EditableVertexWeldResult final {\n    std::vector<EditableVertexId> survivors;\n    std::size_t mergedCount{0};\n};\n\nstruct EditableLoopCutResult final {\n    std::vector<EditableVertexId> vertices;\n    std::vector<EditableEdgeId> edges;\n    std::vector<EditableFaceId> faces;\n};\n''')
replace_once(path,
'''    [[nodiscard]] std::optional<std::vector<EditableFaceId>> bridgeBoundaryLoops(\n        std::span<const EditableEdgeId> edges, std::string* error = nullptr);\n''',
'''    [[nodiscard]] std::optional<std::vector<EditableFaceId>> bridgeBoundaryLoops(\n        std::span<const EditableEdgeId> edges, std::string* error = nullptr);\n    [[nodiscard]] std::optional<EditableLoopCutResult> loopCut(\n        EditableEdgeId edge, std::string* error = nullptr);\n''')

path = 'src/core/src/editable_mesh_operations.cpp'
replace_once(path,
'''#include <cmath>\n#include <limits>\n''',
'''#include <cmath>\n#include <deque>\n#include <limits>\n''')

append = r'''

std::optional<EditableLoopCutResult> EditableMesh::loopCut(
    EditableEdgeId startEdge, std::string* error) {
    if (startEdge.isNull() || !findEdge(startEdge)) {
        if (error) *error = "Loop Cut start edge does not exist";
        return std::nullopt;
    }

    struct QuadData final {
        EditableFaceId face{};
        std::array<EditableVertexId, 4> vertices{};
        std::array<EditableEdgeId, 4> edges{};
    };

    const auto readQuad = [this, error](EditableFaceId faceId) -> std::optional<QuadData> {
        const auto* face = findFace(faceId);
        if (!face) {
            if (error) *error = "Loop Cut encountered a missing face";
            return std::nullopt;
        }
        QuadData data;
        data.face = faceId;
        EditableHalfEdgeId current = face->halfEdge;
        for (std::size_t index = 0; index < 4U; ++index) {
            const auto* halfEdge = findHalfEdge(current);
            if (!halfEdge || halfEdge->face != faceId) {
                if (error) *error = "Loop Cut encountered an invalid quad loop";
                return std::nullopt;
            }
            data.vertices[index] = halfEdge->origin;
            data.edges[index] = halfEdge->edge;
            current = halfEdge->next;
        }
        if (current != face->halfEdge) {
            if (error) *error = "Loop Cut currently supports quad faces only";
            return std::nullopt;
        }
        return data;
    };

    std::set<EditableEdgeId> ringEdges{startEdge};
    std::map<EditableFaceId, QuadData> ringFaces;
    std::deque<EditableEdgeId> pending{startEdge};
    while (!pending.empty()) {
        const EditableEdgeId currentEdge = pending.front();
        pending.pop_front();
        const auto* edge = findEdge(currentEdge);
        const auto* representative = edge ? findHalfEdge(edge->halfEdge) : nullptr;
        if (!edge || !representative) {
            if (error) *error = "Loop Cut ring contains an invalid edge";
            return std::nullopt;
        }

        std::array<EditableHalfEdgeId, 2> incident{representative->id, representative->twin};
        for (const auto halfEdgeId : incident) {
            if (halfEdgeId.isNull()) continue;
            const auto* halfEdge = findHalfEdge(halfEdgeId);
            if (!halfEdge) {
                if (error) *error = "Loop Cut ring contains an invalid half-edge";
                return std::nullopt;
            }
            if (ringFaces.contains(halfEdge->face)) continue;
            const auto quad = readQuad(halfEdge->face);
            if (!quad) return std::nullopt;

            std::optional<std::size_t> edgeIndex;
            for (std::size_t index = 0; index < 4U; ++index) {
                if (quad->edges[index] == currentEdge) {
                    edgeIndex = index;
                    break;
                }
            }
            if (!edgeIndex) {
                if (error) *error = "Loop Cut could not locate the traversed edge in its face";
                return std::nullopt;
            }
            const EditableEdgeId opposite = quad->edges[(*edgeIndex + 2U) % 4U];
            if (opposite.isNull() || opposite == currentEdge) {
                if (error) *error = "Loop Cut found an invalid opposite quad edge";
                return std::nullopt;
            }
            ringFaces.emplace(quad->face, *quad);
            if (ringEdges.insert(opposite).second) pending.push_back(opposite);
        }
    }
    if (ringFaces.empty() || ringEdges.size() < 2U) {
        if (error) *error = "Loop Cut did not discover a valid quad ring";
        return std::nullopt;
    }

    for (const auto& [_, quad] : ringFaces) {
        std::size_t ringCount = 0U;
        std::array<std::size_t, 2> indices{};
        for (std::size_t index = 0; index < 4U; ++index) {
            if (!ringEdges.contains(quad.edges[index])) continue;
            if (ringCount < indices.size()) indices[ringCount] = index;
            ++ringCount;
        }
        if (ringCount != 2U || (indices[0] + 2U) % 4U != indices[1]) {
            if (error) *error = "Loop Cut ring does not cross each quad through opposite edges";
            return std::nullopt;
        }
    }

    EditableMesh working = *this;
    std::map<EditableEdgeId, EditableVertexId> midpointByEdge;
    EditableLoopCutResult result;
    result.vertices.reserve(ringEdges.size());
    for (const auto edgeId : ringEdges) {
        const auto* edge = findEdge(edgeId);
        const auto* halfEdge = edge ? findHalfEdge(edge->halfEdge) : nullptr;
        const auto* next = halfEdge ? findHalfEdge(halfEdge->next) : nullptr;
        const auto* first = halfEdge ? findVertex(halfEdge->origin) : nullptr;
        const auto* second = next ? findVertex(next->origin) : nullptr;
        if (!edge || !halfEdge || !next || !first || !second) {
            if (error) *error = "Loop Cut could not resolve an edge midpoint";
            return std::nullopt;
        }
        const Vec3 midpoint{
            (first->position.x + second->position.x) * 0.5F,
            (first->position.y + second->position.y) * 0.5F,
            (first->position.z + second->position.z) * 0.5F,
        };
        const EditableVertexId vertex = working.addVertex(midpoint);
        midpointByEdge.emplace(edgeId, vertex);
        result.vertices.push_back(vertex);
    }

    for (const auto& [faceId, _] : ringFaces) {
        if (!working.removeFace(faceId, error)) return std::nullopt;
    }

    std::set<EditableEdgeId> cutEdges;
    result.faces.reserve(ringFaces.size() * 2U);
    for (const auto& [_, quad] : ringFaces) {
        std::size_t firstRingIndex = 0U;
        bool found = false;
        for (std::size_t index = 0; index < 4U; ++index) {
            if (!ringEdges.contains(quad.edges[index])) continue;
            firstRingIndex = index;
            found = true;
            break;
        }
        if (!found) {
            if (error) *error = "Loop Cut lost a quad ring edge during reconstruction";
            return std::nullopt;
        }
        const std::size_t oppositeIndex = (firstRingIndex + 2U) % 4U;
        const EditableVertexId v0 = quad.vertices[firstRingIndex];
        const EditableVertexId v1 = quad.vertices[(firstRingIndex + 1U) % 4U];
        const EditableVertexId v2 = quad.vertices[(firstRingIndex + 2U) % 4U];
        const EditableVertexId v3 = quad.vertices[(firstRingIndex + 3U) % 4U];
        const EditableVertexId m0 = midpointByEdge.at(quad.edges[firstRingIndex]);
        const EditableVertexId m1 = midpointByEdge.at(quad.edges[oppositeIndex]);
        const std::array<EditableVertexId, 4> firstQuad{v0, m0, m1, v3};
        const std::array<EditableVertexId, 4> secondQuad{m0, v1, v2, m1};
        const auto firstFace = working.addFace(firstQuad, error);
        const auto secondFace = firstFace ? working.addFace(secondQuad, error) : std::nullopt;
        if (!firstFace || !secondFace) return std::nullopt;
        result.faces.push_back(*firstFace);
        result.faces.push_back(*secondFace);

        const auto direct = working.directedEdges_.find(DirectedEdgeKey{m0.value, m1.value});
        const auto reverse = working.directedEdges_.find(DirectedEdgeKey{m1.value, m0.value});
        const EditableHalfEdgeId cutHalfEdge = direct != working.directedEdges_.end()
            ? direct->second
            : (reverse != working.directedEdges_.end() ? reverse->second : EditableHalfEdgeId{});
        const auto* halfEdge = working.findHalfEdge(cutHalfEdge);
        if (!halfEdge) {
            if (error) *error = "Loop Cut failed to locate the new cut edge";
            return std::nullopt;
        }
        cutEdges.insert(halfEdge->edge);
    }

    if (!working.validate(error)) return std::nullopt;
    result.edges.assign(cutEdges.begin(), cutEdges.end());
    *this = std::move(working);
    if (error) error->clear();
    return result;
}
'''
content = read(path)
closing = '\n} // namespace m3d\n'
if 'EditableMesh::loopCut' not in content:
    if not content.endswith(closing): raise SystemExit('editable mesh operations namespace closing not found')
    write(path, content[:-len(closing)] + append + closing)

# Core tests: complete cube ring + non-quad atomic rejection.
path = 'tests/test_mesh_operations.cpp'
content = read(path)
if 'centered loop cut propagates through an entire quad ring' not in content:
    content = content.rstrip() + r'''

TEST_CASE("centered loop cut propagates through an entire quad ring") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto originalFaces = mesh.faces();
    std::map<m3d::EditableFaceId, std::vector<m3d::EditableVertexId>> originalLoops;
    for (const auto& face : originalFaces) originalLoops.emplace(face.id, mesh.faceVertices(face.id));
    const auto startEdge = mesh.edges().front().id;
    std::string error;

    const auto result = mesh.loopCut(startEdge, &error);
    REQUIRE(result.has_value());
    REQUIRE(error.empty());
    REQUIRE(result->vertices.size() == 4U);
    REQUIRE(result->edges.size() == 4U);
    REQUIRE(result->faces.size() == 8U);
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 12U);
    REQUIRE(mesh.edgeCount() == 20U);
    REQUIRE(mesh.halfEdgeCount() == 40U);
    REQUIRE(mesh.faceCount() == 10U);

    std::size_t untouched = 0U;
    for (const auto& [faceId, loop] : originalLoops) {
        if (!mesh.findFace(faceId)) continue;
        ++untouched;
        REQUIRE(mesh.faceVertices(faceId) == loop);
    }
    REQUIRE(untouched == 2U);

    m3d::MeshResource render;
    render.id = m3d::ResourceId::generate();
    render.authoring = mesh;
    REQUIRE(render.rebuildFromAuthoring(&error));
    REQUIRE(render.validate(&error));
}

TEST_CASE("loop cut rejects a non quad ring without changing topology") {
    m3d::EditableMesh mesh;
    const auto a = mesh.addVertex({0.0F,0.0F,0.0F});
    const auto b = mesh.addVertex({1.0F,0.0F,0.0F});
    const auto c = mesh.addVertex({0.0F,1.0F,0.0F});
    const std::array<m3d::EditableVertexId,3> triangle{a,b,c};
    std::string error;
    REQUIRE(mesh.addFace(triangle,&error).has_value());
    const auto before = mesh.snapshot();
    REQUIRE(!mesh.loopCut(mesh.edges().front().id,&error).has_value());
    REQUIRE(!error.empty());
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}
''' + '\n'
    write(path, content)

Path('tests/test_mesh_operations.cpp').write_text(Path('tests/test_mesh_operations.cpp').read_text().rstrip() + '\n')
print('quad ring loop cut core patch prepared')
