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
'''struct EditableMeshSnapshot final {\n    std::vector<EditableVertex> vertices;\n    std::vector<EditableHalfEdge> halfEdges;\n    std::vector<EditableEdge> edges;\n    std::vector<EditableFace> faces;\n};\n''',
'''struct EditableMeshSnapshot final {\n    std::vector<EditableVertex> vertices;\n    std::vector<EditableHalfEdge> halfEdges;\n    std::vector<EditableEdge> edges;\n    std::vector<EditableFace> faces;\n};\n\nstruct EditableVertexWeldResult final {\n    std::vector<EditableVertexId> survivors;\n    std::size_t mergedCount{0};\n};\n''')
replace_once(path,
'''    [[nodiscard]] std::optional<std::vector<EditableFaceId>> subdivideFace(\n        EditableFaceId face, std::string* error = nullptr);\n''',
'''    [[nodiscard]] std::optional<std::vector<EditableFaceId>> subdivideFace(\n        EditableFaceId face, std::string* error = nullptr);\n    [[nodiscard]] std::optional<EditableVertexId> mergeVertices(\n        std::span<const EditableVertexId> vertices, EditableVertexId target,\n        std::string* error = nullptr);\n    [[nodiscard]] std::optional<EditableVertexWeldResult> weldVertices(\n        std::span<const EditableVertexId> vertices, float distance,\n        std::optional<EditableVertexId> preferredTarget = std::nullopt,\n        std::string* error = nullptr);\n''')

path = 'src/core/src/editable_mesh_operations.cpp'
replace_once(path,
'''#include <cmath>\n#include <utility>\n''',
'''#include <cmath>\n#include <map>\n#include <set>\n#include <utility>\n''')

append = r'''

std::optional<EditableVertexId> EditableMesh::mergeVertices(
    std::span<const EditableVertexId> vertices, EditableVertexId target,
    std::string* error) {
    std::set<EditableVertexId> unique(vertices.begin(), vertices.end());
    if (unique.size() < 2U) {
        if (error) *error = "Merge requires at least two unique vertices";
        return std::nullopt;
    }
    if (target.isNull() || !unique.contains(target)) {
        if (error) *error = "Merge target must be one of the selected vertices";
        return std::nullopt;
    }
    for (const auto vertex : unique) {
        if (!findVertex(vertex)) {
            if (error) *error = "Merge selection contains a missing vertex";
            return std::nullopt;
        }
    }

    std::set<EditableVertexId> sources = unique;
    sources.erase(target);
    EditableMesh working = *this;

    struct RebuiltFace final {
        EditableFaceId original{};
        std::vector<EditableVertexId> vertices;
    };
    std::vector<RebuiltFace> affected;
    for (const auto& face : working.faces()) {
        const auto loop = working.faceVertices(face.id);
        const bool touchesSource = std::any_of(loop.cbegin(), loop.cend(),
                                               [&sources](EditableVertexId vertex) {
                                                   return sources.contains(vertex);
                                               });
        if (!touchesSource) continue;

        std::vector<EditableVertexId> replaced;
        replaced.reserve(loop.size());
        for (const auto vertex : loop) replaced.push_back(sources.contains(vertex) ? target : vertex);

        std::vector<EditableVertexId> compact;
        compact.reserve(replaced.size());
        for (const auto vertex : replaced) {
            if (compact.empty() || compact.back() != vertex) compact.push_back(vertex);
        }
        if (compact.size() > 1U && compact.front() == compact.back()) compact.pop_back();

        std::set<EditableVertexId> distinct(compact.begin(), compact.end());
        if (compact.size() >= 3U && distinct.size() != compact.size()) {
            if (error) *error = "Merge would create a self-touching face";
            return std::nullopt;
        }
        if (distinct.size() < 3U) compact.clear();
        affected.push_back(RebuiltFace{face.id, std::move(compact)});
    }
    if (affected.empty()) {
        if (error) *error = "Merge vertices are not referenced by any editable face";
        return std::nullopt;
    }

    for (const auto& face : affected) {
        if (!working.removeFace(face.original, error)) return std::nullopt;
    }

    for (const auto source : sources) {
        for (const auto& halfEdge : working.halfEdges()) {
            if (halfEdge.origin == source || working.destination(halfEdge.id) == source) {
                if (error) *error = "Merge source is still referenced after removing incident faces";
                return std::nullopt;
            }
        }
        if (source.isNull() || static_cast<std::size_t>(source.value) > working.vertices_.size()) {
            if (error) *error = "Merge source vertex slot is invalid";
            return std::nullopt;
        }
        auto& slot = working.vertices_[static_cast<std::size_t>(source.value - 1U)];
        if (!slot) {
            if (error) *error = "Merge source vertex disappeared unexpectedly";
            return std::nullopt;
        }
        slot.reset();
        --working.vertexCount_;
    }

    for (const auto& face : affected) {
        if (face.vertices.empty()) continue;
        if (!working.addFace(face.vertices, error)) return std::nullopt;
    }

    if (!working.validate(error)) return std::nullopt;
    *this = std::move(working);
    if (error) error->clear();
    return target;
}

std::optional<EditableVertexWeldResult> EditableMesh::weldVertices(
    std::span<const EditableVertexId> vertices, float distance,
    std::optional<EditableVertexId> preferredTarget, std::string* error) {
    if (!std::isfinite(distance) || distance <= 0.0F) {
        if (error) *error = "Weld distance must be finite and positive";
        return std::nullopt;
    }

    std::set<EditableVertexId> uniqueSet(vertices.begin(), vertices.end());
    if (uniqueSet.size() < 2U) {
        if (error) *error = "Weld requires at least two unique vertices";
        return std::nullopt;
    }
    if (preferredTarget && !uniqueSet.contains(*preferredTarget)) {
        if (error) *error = "Preferred weld target must be selected";
        return std::nullopt;
    }

    std::vector<EditableVertexId> unique(uniqueSet.begin(), uniqueSet.end());
    std::vector<Vec3> positions;
    positions.reserve(unique.size());
    for (const auto id : unique) {
        const auto* vertex = findVertex(id);
        if (!vertex) {
            if (error) *error = "Weld selection contains a missing vertex";
            return std::nullopt;
        }
        positions.push_back(vertex->position);
    }

    std::vector<std::size_t> parent(unique.size());
    for (std::size_t index = 0; index < parent.size(); ++index) parent[index] = index;
    const auto findRoot = [&parent](std::size_t index) {
        std::size_t root = index;
        while (parent[root] != root) root = parent[root];
        while (parent[index] != index) {
            const std::size_t next = parent[index];
            parent[index] = root;
            index = next;
        }
        return root;
    };
    const float distanceSquared = distance * distance;
    for (std::size_t left = 0; left < unique.size(); ++left) {
        for (std::size_t right = left + 1U; right < unique.size(); ++right) {
            const float dx = positions[left].x - positions[right].x;
            const float dy = positions[left].y - positions[right].y;
            const float dz = positions[left].z - positions[right].z;
            if (dx * dx + dy * dy + dz * dz > distanceSquared) continue;
            const std::size_t leftRoot = findRoot(left);
            const std::size_t rightRoot = findRoot(right);
            if (leftRoot != rightRoot) parent[rightRoot] = leftRoot;
        }
    }

    std::map<std::size_t, std::vector<EditableVertexId>> groups;
    for (std::size_t index = 0; index < unique.size(); ++index) groups[findRoot(index)].push_back(unique[index]);

    EditableMesh working = *this;
    EditableVertexWeldResult result;
    result.survivors.reserve(groups.size());
    for (auto& [_, group] : groups) {
        std::sort(group.begin(), group.end());
        EditableVertexId representative = group.front();
        if (preferredTarget && std::find(group.cbegin(), group.cend(), *preferredTarget) != group.cend()) {
            representative = *preferredTarget;
        }
        if (group.size() > 1U) {
            if (!working.mergeVertices(group, representative, error)) return std::nullopt;
            result.mergedCount += group.size() - 1U;
        }
        result.survivors.push_back(representative);
    }

    std::sort(result.survivors.begin(), result.survivors.end());
    if (result.mergedCount > 0U) {
        if (!working.validate(error)) return std::nullopt;
        *this = std::move(working);
    }
    if (error) error->clear();
    return result;
}
'''

content = read(path)
closing = '\n} // namespace m3d\n'
if 'EditableMesh::mergeVertices' not in content:
    if not content.endswith(closing):
        raise SystemExit('editable_mesh_operations.cpp closing namespace not found')
    write(path, content[:-len(closing)] + append + closing)

# Core topology tests.
path = 'tests/test_mesh_operations.cpp'
content = read(path)
if 'vertex merge to active preserves untouched topology identities' not in content:
    content = content.rstrip() + r'''

TEST_CASE("vertex merge to active preserves untouched topology identities") {
    auto mesh = m3d::EditableMesh::makeCube(1.0F);
    const auto edge = mesh.edges().front();
    const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
    REQUIRE(halfEdge != nullptr);
    const auto* next = mesh.findHalfEdge(halfEdge->next);
    REQUIRE(next != nullptr);
    const auto target = halfEdge->origin;
    const auto source = next->origin;

    std::optional<m3d::EditableFaceId> untouchedFace;
    std::vector<m3d::EditableVertexId> untouchedLoop;
    for (const auto& face : mesh.faces()) {
        const auto loop = mesh.faceVertices(face.id);
        if (std::find(loop.cbegin(), loop.cend(), source) == loop.cend() &&
            std::find(loop.cbegin(), loop.cend(), target) == loop.cend()) {
            untouchedFace = face.id;
            untouchedLoop = loop;
            break;
        }
    }
    REQUIRE(untouchedFace.has_value());

    const std::array<m3d::EditableVertexId, 2> selected{target, source};
    std::string error;
    const auto merged = mesh.mergeVertices(selected, target, &error);
    REQUIRE(merged == target);
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.findVertex(target) != nullptr);
    REQUIRE(mesh.findVertex(source) == nullptr);
    REQUIRE(mesh.vertexCount() == 7U);
    REQUIRE(mesh.edgeCount() == 11U);
    REQUIRE(mesh.halfEdgeCount() == 22U);
    REQUIRE(mesh.faceCount() == 6U);
    REQUIRE(mesh.findFace(*untouchedFace) != nullptr);
    REQUIRE(mesh.faceVertices(*untouchedFace) == untouchedLoop);
}

TEST_CASE("weld by distance prefers active target and reports merged count") {
    auto mesh = m3d::EditableMesh::makeCube(1.0F);
    const auto edge = mesh.edges().front();
    const auto* halfEdge = mesh.findHalfEdge(edge.halfEdge);
    const auto* next = halfEdge ? mesh.findHalfEdge(halfEdge->next) : nullptr;
    REQUIRE(halfEdge != nullptr);
    REQUIRE(next != nullptr);
    const auto active = halfEdge->origin;
    const auto other = next->origin;
    const std::array<m3d::EditableVertexId, 2> selected{active, other};
    std::string error;

    const auto result = mesh.weldVertices(selected, 1.01F, active, &error);
    REQUIRE(result.has_value());
    REQUIRE(error.empty());
    REQUIRE(result->mergedCount == 1U);
    REQUIRE(result->survivors.size() == 1U);
    REQUIRE(result->survivors.front() == active);
    REQUIRE(mesh.findVertex(other) == nullptr);
    REQUIRE(mesh.vertexCount() == 7U);
    REQUIRE(mesh.validate(&error));
}

TEST_CASE("weld with no vertices inside threshold is an exact no-op") {
    auto mesh = m3d::EditableMesh::makeCube(1.0F);
    const auto vertices = mesh.vertices();
    REQUIRE(vertices.size() >= 2U);
    const std::array<m3d::EditableVertexId, 2> selected{vertices[0].id, vertices[6].id};
    const auto before = mesh.snapshot();
    std::string error;
    const auto result = mesh.weldVertices(selected, 0.01F, vertices[0].id, &error);
    REQUIRE(result.has_value());
    REQUIRE(result->mergedCount == 0U);
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}

TEST_CASE("invalid merge target leaves topology unchanged") {
    auto mesh = m3d::EditableMesh::makeCube();
    const auto vertices = mesh.vertices();
    const std::array<m3d::EditableVertexId, 2> selected{vertices[0].id, vertices[1].id};
    const auto before = mesh.snapshot();
    std::string error;
    REQUIRE(!mesh.mergeVertices(selected, vertices[2].id, &error).has_value());
    REQUIRE(!error.empty());
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}
''' + '\n'
    write(path, content)

print('merge/weld core patch prepared')
