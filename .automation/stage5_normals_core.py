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
'''    [[nodiscard]] bool deleteVertices(std::span<const EditableVertexId> vertices,\n                                      std::string* error = nullptr);\n''',
'''    [[nodiscard]] bool deleteVertices(std::span<const EditableVertexId> vertices,\n                                      std::string* error = nullptr);\n    [[nodiscard]] std::optional<std::vector<EditableFaceId>> flipFaceComponents(\n        std::span<const EditableFaceId> seedFaces, std::string* error = nullptr);\n    [[nodiscard]] std::optional<std::size_t> recalculateOutside(\n        std::string* error = nullptr);\n''')

path = 'src/core/src/editable_mesh_operations.cpp'
content = read(path)
closing = '\n} // namespace m3d\n'
append = r'''

std::optional<std::vector<EditableFaceId>> EditableMesh::flipFaceComponents(
    std::span<const EditableFaceId> seedFaces, std::string* error) {
    if (!validate(error)) return std::nullopt;
    std::set<EditableFaceId> seeds(seedFaces.begin(), seedFaces.end());
    if (seeds.empty()) {
        if (error) *error = "Flip Normals requires at least one seed face";
        return std::nullopt;
    }
    for (const auto face : seeds) {
        if (!findFace(face)) {
            if (error) *error = "Flip Normals selection contains a missing face";
            return std::nullopt;
        }
    }

    std::set<EditableFaceId> componentFaces;
    std::deque<EditableFaceId> pending(seeds.begin(), seeds.end());
    while (!pending.empty()) {
        const EditableFaceId faceId = pending.front();
        pending.pop_front();
        if (!componentFaces.insert(faceId).second) continue;
        const auto* face = findFace(faceId);
        if (!face) {
            if (error) *error = "Flip Normals encountered a missing component face";
            return std::nullopt;
        }
        EditableHalfEdgeId current = face->halfEdge;
        for (std::size_t guard = 0; guard <= halfEdgeCount_; ++guard) {
            const auto* halfEdge = findHalfEdge(current);
            if (!halfEdge || halfEdge->face != faceId) {
                if (error) *error = "Flip Normals encountered an invalid face loop";
                return std::nullopt;
            }
            if (!halfEdge->twin.isNull()) {
                const auto* twin = findHalfEdge(halfEdge->twin);
                if (!twin) {
                    if (error) *error = "Flip Normals encountered an invalid twin";
                    return std::nullopt;
                }
                if (!componentFaces.contains(twin->face)) pending.push_back(twin->face);
            }
            current = halfEdge->next;
            if (current == face->halfEdge) break;
            if (guard == halfEdgeCount_) {
                if (error) *error = "Flip Normals face traversal exceeded topology bounds";
                return std::nullopt;
            }
        }
    }

    std::vector<std::vector<EditableVertexId>> loops;
    loops.reserve(componentFaces.size());
    for (const auto face : componentFaces) {
        auto loop = faceVertices(face);
        if (loop.size() < 3U) {
            if (error) *error = "Flip Normals encountered a degenerate component face";
            return std::nullopt;
        }
        std::reverse(loop.begin(), loop.end());
        loops.push_back(std::move(loop));
    }

    EditableMesh working = *this;
    for (const auto face : componentFaces) {
        if (!working.removeFace(face, error)) return std::nullopt;
    }
    std::vector<EditableFaceId> flippedFaces;
    flippedFaces.reserve(loops.size());
    for (const auto& loop : loops) {
        const auto face = working.addFace(loop, error);
        if (!face) return std::nullopt;
        flippedFaces.push_back(*face);
    }
    if (!working.validate(error)) return std::nullopt;

    *this = std::move(working);
    if (error) error->clear();
    return flippedFaces;
}

std::optional<std::size_t> EditableMesh::recalculateOutside(std::string* error) {
    if (!validate(error)) return std::nullopt;

    struct ComponentInfo final {
        std::vector<EditableFaceId> faces;
        bool closed{true};
        double signedVolume{0.0};
    };

    std::set<EditableFaceId> visited;
    std::vector<ComponentInfo> components;
    for (const auto& rootFace : faces()) {
        if (visited.contains(rootFace.id)) continue;
        ComponentInfo component;
        std::deque<EditableFaceId> pending{rootFace.id};
        while (!pending.empty()) {
            const EditableFaceId faceId = pending.front();
            pending.pop_front();
            if (!visited.insert(faceId).second) continue;
            component.faces.push_back(faceId);
            const auto* face = findFace(faceId);
            if (!face) {
                if (error) *error = "Recalculate Outside encountered a missing face";
                return std::nullopt;
            }

            const auto loop = faceVertices(faceId);
            if (loop.size() < 3U) {
                if (error) *error = "Recalculate Outside encountered a degenerate face";
                return std::nullopt;
            }
            std::vector<Vec3> positions;
            positions.reserve(loop.size());
            for (const auto vertexId : loop) {
                const auto* vertex = findVertex(vertexId);
                if (!vertex) {
                    if (error) *error = "Recalculate Outside encountered a missing vertex";
                    return std::nullopt;
                }
                positions.push_back(vertex->position);
            }
            const auto cross = [](Vec3 left, Vec3 right) noexcept {
                return Vec3{
                    left.y * right.z - left.z * right.y,
                    left.z * right.x - left.x * right.z,
                    left.x * right.y - left.y * right.x,
                };
            };
            const auto dot = [](Vec3 left, Vec3 right) noexcept {
                return static_cast<double>(left.x) * right.x +
                       static_cast<double>(left.y) * right.y +
                       static_cast<double>(left.z) * right.z;
            };
            for (std::size_t index = 1U; index + 1U < positions.size(); ++index) {
                component.signedVolume += dot(positions[0], cross(positions[index], positions[index + 1U])) / 6.0;
            }

            EditableHalfEdgeId current = face->halfEdge;
            for (std::size_t guard = 0; guard <= halfEdgeCount_; ++guard) {
                const auto* halfEdge = findHalfEdge(current);
                if (!halfEdge) {
                    if (error) *error = "Recalculate Outside encountered an invalid half-edge";
                    return std::nullopt;
                }
                if (halfEdge->twin.isNull()) {
                    component.closed = false;
                } else {
                    const auto* twin = findHalfEdge(halfEdge->twin);
                    if (!twin) {
                        if (error) *error = "Recalculate Outside encountered an invalid twin";
                        return std::nullopt;
                    }
                    if (!visited.contains(twin->face)) pending.push_back(twin->face);
                }
                current = halfEdge->next;
                if (current == face->halfEdge) break;
                if (guard == halfEdgeCount_) {
                    if (error) *error = "Recalculate Outside traversal exceeded topology bounds";
                    return std::nullopt;
                }
            }
        }
        components.push_back(std::move(component));
    }

    std::vector<EditableFaceId> inwardSeeds;
    constexpr double kVolumeEpsilon = 1.0e-10;
    for (const auto& component : components) {
        if (!component.closed) continue;
        if (std::abs(component.signedVolume) <= kVolumeEpsilon) {
            if (error) *error = "Recalculate Outside found a closed component with ambiguous zero volume";
            return std::nullopt;
        }
        if (component.signedVolume < 0.0) inwardSeeds.push_back(component.faces.front());
    }

    EditableMesh working = *this;
    std::size_t flippedComponents = 0U;
    for (const auto seed : inwardSeeds) {
        const std::array<EditableFaceId, 1> seedArray{seed};
        if (!working.flipFaceComponents(seedArray, error)) return std::nullopt;
        ++flippedComponents;
    }
    if (flippedComponents > 0U) {
        if (!working.validate(error)) return std::nullopt;
        *this = std::move(working);
    }
    if (error) error->clear();
    return flippedComponents;
}
'''
if 'EditableMesh::flipFaceComponents' not in content:
    if not content.endswith(closing): raise SystemExit('editable mesh operations namespace closing not found')
    write(path, content[:-len(closing)] + append + closing)

# Core tests.
path = 'tests/test_mesh_operations.cpp'
content = read(path)
if 'flip normals reverses an entire connected component consistently' not in content:
    content = content.rstrip() + r'''

TEST_CASE("flip normals reverses an entire connected component consistently") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const auto seed = mesh.faces().front().id;
    const std::array<m3d::EditableFaceId,1> seeds{seed};
    std::string error;
    const auto flipped = mesh.flipFaceComponents(seeds, &error);
    REQUIRE(flipped.has_value());
    REQUIRE(flipped->size() == 6U);
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));

    float orientationScore = 0.0F;
    for (const auto& face : mesh.faces()) {
        const auto normal = mesh.faceNormal(face.id);
        const auto loop = mesh.faceVertices(face.id);
        REQUIRE(normal.has_value());
        REQUIRE(!loop.empty());
        m3d::Vec3 center{};
        for (const auto vertexId : loop) {
            const auto* vertex = mesh.findVertex(vertexId);
            REQUIRE(vertex != nullptr);
            center.x += vertex->position.x;
            center.y += vertex->position.y;
            center.z += vertex->position.z;
        }
        const float inverse = 1.0F / static_cast<float>(loop.size());
        center.x *= inverse; center.y *= inverse; center.z *= inverse;
        orientationScore += normal->x * center.x + normal->y * center.y + normal->z * center.z;
    }
    REQUIRE(orientationScore < 0.0F);
}

TEST_CASE("recalculate outside restores outward orientation for a closed component") {
    auto mesh = m3d::EditableMesh::makeCube(2.0F);
    const std::array<m3d::EditableFaceId,1> seeds{mesh.faces().front().id};
    std::string error;
    REQUIRE(mesh.flipFaceComponents(seeds, &error).has_value());
    const auto flippedComponents = mesh.recalculateOutside(&error);
    REQUIRE(flippedComponents.has_value());
    REQUIRE(*flippedComponents == 1U);
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));

    m3d::MeshResource render;
    render.id = m3d::ResourceId::generate();
    render.name = "Normals Cube";
    render.authoring = mesh;
    REQUIRE(render.rebuildFromAuthoring(&error));
    for (const auto& vertex : render.vertices) {
        const float outward = vertex.normal.x * vertex.position.x +
                              vertex.normal.y * vertex.position.y +
                              vertex.normal.z * vertex.position.z;
        REQUIRE(outward > 0.0F);
    }
}

TEST_CASE("recalculate outside leaves open components unchanged") {
    auto mesh = m3d::EditableMesh::makeCube();
    std::string error;
    REQUIRE(mesh.deleteFaces(std::array<m3d::EditableFaceId,1>{mesh.faces().front().id}, &error));
    const auto before = mesh.snapshot();
    const auto result = mesh.recalculateOutside(&error);
    REQUIRE(result.has_value());
    REQUIRE(*result == 0U);
    REQUIRE(mesh.snapshot().vertices == before.vertices);
    REQUIRE(mesh.snapshot().halfEdges == before.halfEdges);
    REQUIRE(mesh.snapshot().edges == before.edges);
    REQUIRE(mesh.snapshot().faces == before.faces);
}
''' + '\n'
    write(path, content)

Path('tests/test_mesh_operations.cpp').write_text(Path('tests/test_mesh_operations.cpp').read_text().rstrip() + '\n')
print('normal orientation tools core patch prepared')
