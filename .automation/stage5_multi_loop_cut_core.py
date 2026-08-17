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


# Overload preserves the one-cut API while adding evenly spaced multi-cut rings.
path = 'src/core/include/mobile3d/core/editable_mesh.hpp'
replace_once(path,
'''    [[nodiscard]] std::optional<EditableLoopCutResult> loopCut(\n        EditableEdgeId edge, std::string* error = nullptr);\n''',
'''    [[nodiscard]] std::optional<EditableLoopCutResult> loopCut(\n        EditableEdgeId edge, std::string* error = nullptr);\n    [[nodiscard]] std::optional<EditableLoopCutResult> loopCut(\n        EditableEdgeId edge, std::uint32_t cuts, std::string* error = nullptr);\n''')

path = 'src/core/src/editable_mesh_operations.cpp'
content = read(path)
anchor = '\n\nbool EditableMesh::deleteFaces('
if 'EditableMesh::loopCut(\n    EditableEdgeId startEdge, std::uint32_t cuts' not in content:
    implementation = r'''

std::optional<EditableLoopCutResult> EditableMesh::loopCut(
    EditableEdgeId startEdge, std::uint32_t cuts, std::string* error) {
    constexpr std::uint32_t kMaximumCuts = 32U;
    if (cuts == 0U || cuts > kMaximumCuts) {
        if (error) *error = "Loop Cut count must be between 1 and 32";
        return std::nullopt;
    }
    if (startEdge.isNull() || !findEdge(startEdge)) {
        if (error) *error = "Loop Cut start edge does not exist";
        return std::nullopt;
    }

    struct QuadData final {
        EditableFaceId face{};
        std::array<EditableVertexId, 4> vertices{};
        std::array<EditableEdgeId, 4> edges{};
    };
    struct SplitEdge final {
        EditableVertexId start{};
        EditableVertexId end{};
        std::vector<EditableVertexId> points;
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
        const std::array<EditableHalfEdgeId, 2> incident{representative->id, representative->twin};
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
                if (quad->edges[index] == currentEdge) { edgeIndex = index; break; }
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
        std::vector<std::size_t> indices;
        for (std::size_t index = 0; index < 4U; ++index) {
            if (ringEdges.contains(quad.edges[index])) indices.push_back(index);
        }
        if (indices.size() != 2U || (indices[0] + 2U) % 4U != indices[1]) {
            if (error) *error = "Loop Cut ring does not cross each quad through opposite edges";
            return std::nullopt;
        }
    }

    EditableMesh working = *this;
    std::map<EditableEdgeId, SplitEdge> splits;
    EditableLoopCutResult result;
    result.vertices.reserve(ringEdges.size() * static_cast<std::size_t>(cuts));
    for (const auto edgeId : ringEdges) {
        const auto* edge = findEdge(edgeId);
        const auto* halfEdge = edge ? findHalfEdge(edge->halfEdge) : nullptr;
        const auto* next = halfEdge ? findHalfEdge(halfEdge->next) : nullptr;
        const auto* first = halfEdge ? findVertex(halfEdge->origin) : nullptr;
        const auto* second = next ? findVertex(next->origin) : nullptr;
        if (!edge || !halfEdge || !next || !first || !second) {
            if (error) *error = "Loop Cut could not resolve a ring edge";
            return std::nullopt;
        }
        SplitEdge split;
        split.start = first->id;
        split.end = second->id;
        split.points.reserve(cuts);
        for (std::uint32_t cut = 1U; cut <= cuts; ++cut) {
            const float t = static_cast<float>(cut) / static_cast<float>(cuts + 1U);
            const Vec3 point{
                first->position.x + (second->position.x - first->position.x) * t,
                first->position.y + (second->position.y - first->position.y) * t,
                first->position.z + (second->position.z - first->position.z) * t,
            };
            const EditableVertexId vertex = working.addVertex(point);
            split.points.push_back(vertex);
            result.vertices.push_back(vertex);
        }
        splits.emplace(edgeId, std::move(split));
    }

    for (const auto& [faceId, _] : ringFaces) {
        if (!working.removeFace(faceId, error)) return std::nullopt;
    }

    const auto orientedPoints = [&splits, error](EditableEdgeId edgeId,
                                                 EditableVertexId start,
                                                 EditableVertexId end)
        -> std::optional<std::vector<EditableVertexId>> {
        const auto found = splits.find(edgeId);
        if (found == splits.end()) {
            if (error) *error = "Loop Cut lost split data for a ring edge";
            return std::nullopt;
        }
        if (found->second.start == start && found->second.end == end) return found->second.points;
        if (found->second.start == end && found->second.end == start) {
            auto reversed = found->second.points;
            std::reverse(reversed.begin(), reversed.end());
            return reversed;
        }
        if (error) *error = "Loop Cut edge orientation is inconsistent with its face";
        return std::nullopt;
    };

    std::set<EditableEdgeId> cutEdges;
    result.faces.reserve(ringFaces.size() * static_cast<std::size_t>(cuts + 1U));
    for (const auto& [_, quad] : ringFaces) {
        std::size_t firstRingIndex = 0U;
        bool foundRing = false;
        for (std::size_t index = 0; index < 4U; ++index) {
            if (ringEdges.contains(quad.edges[index])) {
                firstRingIndex = index;
                foundRing = true;
                break;
            }
        }
        if (!foundRing) {
            if (error) *error = "Loop Cut lost a ring edge during reconstruction";
            return std::nullopt;
        }
        const std::size_t oppositeIndex = (firstRingIndex + 2U) % 4U;
        const EditableVertexId v0 = quad.vertices[firstRingIndex];
        const EditableVertexId v1 = quad.vertices[(firstRingIndex + 1U) % 4U];
        const EditableVertexId v2 = quad.vertices[(firstRingIndex + 2U) % 4U];
        const EditableVertexId v3 = quad.vertices[(firstRingIndex + 3U) % 4U];
        const auto aPoints = orientedPoints(quad.edges[firstRingIndex], v0, v1);
        const auto bPoints = orientedPoints(quad.edges[oppositeIndex], v3, v2);
        if (!aPoints || !bPoints || aPoints->size() != cuts || bPoints->size() != cuts) return std::nullopt;

        std::vector<EditableVertexId> sideA;
        std::vector<EditableVertexId> sideB;
        sideA.reserve(static_cast<std::size_t>(cuts) + 2U);
        sideB.reserve(static_cast<std::size_t>(cuts) + 2U);
        sideA.push_back(v0);
        sideA.insert(sideA.end(), aPoints->begin(), aPoints->end());
        sideA.push_back(v1);
        sideB.push_back(v3);
        sideB.insert(sideB.end(), bPoints->begin(), bPoints->end());
        sideB.push_back(v2);

        for (std::size_t strip = 0; strip + 1U < sideA.size(); ++strip) {
            const std::array<EditableVertexId, 4> newQuad{
                sideA[strip], sideA[strip + 1U], sideB[strip + 1U], sideB[strip]
            };
            const auto face = working.addFace(newQuad, error);
            if (!face) return std::nullopt;
            result.faces.push_back(*face);
        }
        for (std::uint32_t cut = 1U; cut <= cuts; ++cut) {
            const EditableVertexId a = sideA[cut];
            const EditableVertexId b = sideB[cut];
            const auto direct = working.directedEdges_.find(DirectedEdgeKey{a.value, b.value});
            const auto reverse = working.directedEdges_.find(DirectedEdgeKey{b.value, a.value});
            const EditableHalfEdgeId cutHalfEdge = direct != working.directedEdges_.end()
                ? direct->second
                : (reverse != working.directedEdges_.end() ? reverse->second : EditableHalfEdgeId{});
            const auto* halfEdge = working.findHalfEdge(cutHalfEdge);
            if (!halfEdge) {
                if (error) *error = "Loop Cut failed to locate a generated cut edge";
                return std::nullopt;
            }
            cutEdges.insert(halfEdge->edge);
        }
    }

    if (!working.validate(error)) return std::nullopt;
    result.edges.assign(cutEdges.begin(), cutEdges.end());
    *this = std::move(working);
    if (error) error->clear();
    return result;
}
'''
    if anchor not in content: raise SystemExit('deleteFaces anchor missing after loopCut')
    write(path, content.replace(anchor, implementation + anchor, 1))

# Multi-cut tests and bounds.
path = 'tests/test_mesh_operations.cpp'
content = read(path)
if 'multi loop cut creates evenly spaced shared rings' not in content:
    content = content.rstrip() + r'''

TEST_CASE("multi loop cut creates evenly spaced shared rings") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto startEdge = mesh.edges().front().id;
    std::string error;
    const auto result = mesh.loopCut(startEdge, 3U, &error);
    REQUIRE(result.has_value());
    REQUIRE(error.empty());
    REQUIRE(result->vertices.size() == 12U);
    REQUIRE(result->edges.size() == 12U);
    REQUIRE(result->faces.size() == 16U);
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount() == 20U);
    REQUIRE(mesh.edgeCount() == 36U);
    REQUIRE(mesh.halfEdgeCount() == 72U);
    REQUIRE(mesh.faceCount() == 18U);

    std::set<float> xCoordinates;
    std::set<float> yCoordinates;
    std::set<float> zCoordinates;
    for (const auto vertexId : result->vertices) {
        const auto* vertex = mesh.findVertex(vertexId);
        REQUIRE(vertex != nullptr);
        if (std::abs(vertex->position.x) < 0.999F) xCoordinates.insert(vertex->position.x);
        if (std::abs(vertex->position.y) < 0.999F) yCoordinates.insert(vertex->position.y);
        if (std::abs(vertex->position.z) < 0.999F) zCoordinates.insert(vertex->position.z);
    }
    REQUIRE(xCoordinates.size() == 3U || yCoordinates.size() == 3U || zCoordinates.size() == 3U);
}

TEST_CASE("multi loop cut rejects zero and excessive cut counts atomically") {
    auto mesh = m3d::EditableMesh::makeCube();
    const auto edge = mesh.edges().front().id;
    const auto before = mesh.snapshot();
    std::string error;
    REQUIRE(!mesh.loopCut(edge, 0U, &error).has_value());
    REQUIRE(!error.empty());
    REQUIRE(!mesh.loopCut(edge, 33U, &error).has_value());
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}
''' + '\n'
    write(path, content)

Path('tests/test_mesh_operations.cpp').write_text(Path('tests/test_mesh_operations.cpp').read_text().rstrip() + '\n')
print('multi ring loop cut core patch prepared')
