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
'''    [[nodiscard]] std::optional<EditableFaceId> fillBoundaryLoop(\n        std::span<const EditableEdgeId> edges, std::string* error = nullptr);\n''',
'''    [[nodiscard]] std::optional<EditableFaceId> fillBoundaryLoop(\n        std::span<const EditableEdgeId> edges, std::string* error = nullptr);\n    [[nodiscard]] std::optional<std::vector<EditableFaceId>> gridFillBoundaryLoop(\n        std::span<const EditableEdgeId> edges, std::uint32_t span, std::uint32_t offset = 0U,\n        std::string* error = nullptr);\n''')

path = 'src/core/src/editable_mesh_operations.cpp'
content = read(path)
anchor = '\n\nstd::optional<std::vector<EditableFaceId>> EditableMesh::bridgeBoundaryLoops('
if 'EditableMesh::gridFillBoundaryLoop' not in content:
    implementation = r'''

std::optional<std::vector<EditableFaceId>> EditableMesh::gridFillBoundaryLoop(
    std::span<const EditableEdgeId> edges, std::uint32_t span, std::uint32_t offset,
    std::string* error) {
    const auto loops = selectedBoundaryLoops(*this, edges, error);
    if (!loops) return std::nullopt;
    if (loops->size() != 1U) {
        if (error) *error = "Grid Fill requires exactly one closed boundary loop";
        return std::nullopt;
    }
    const auto& sourceLoop = loops->front();
    const std::size_t perimeter = sourceLoop.size();
    if (perimeter < 4U || perimeter % 2U != 0U) {
        if (error) *error = "Grid Fill requires an even boundary with at least four edges";
        return std::nullopt;
    }

    const std::size_t halfPerimeter = perimeter / 2U;
    const std::size_t columns = static_cast<std::size_t>(span);
    if (columns == 0U || columns >= halfPerimeter) {
        if (error) *error = "Grid Fill span must be between 1 and half the boundary minus 1";
        return std::nullopt;
    }
    const std::size_t rows = halfPerimeter - columns;
    const std::size_t normalizedOffset = static_cast<std::size_t>(offset) % perimeter;

    std::vector<EditableVertexId> loop;
    loop.reserve(perimeter);
    for (std::size_t index = 0; index < perimeter; ++index) {
        loop.push_back(sourceLoop[(normalizedOffset + index) % perimeter]);
    }

    const std::size_t gridWidth = columns + 1U;
    const std::size_t gridHeight = rows + 1U;
    std::vector<EditableVertexId> grid(gridWidth * gridHeight);
    const auto gridIndex = [gridWidth](std::size_t row, std::size_t column) {
        return row * gridWidth + column;
    };

    for (std::size_t column = 0; column <= columns; ++column) {
        grid[gridIndex(0U, column)] = loop[column % perimeter];
    }
    for (std::size_t row = 0; row <= rows; ++row) {
        grid[gridIndex(row, columns)] = loop[(columns + row) % perimeter];
    }
    for (std::size_t column = 0; column <= columns; ++column) {
        grid[gridIndex(rows, columns - column)] = loop[(columns + rows + column) % perimeter];
    }
    for (std::size_t row = 0; row <= rows; ++row) {
        grid[gridIndex(rows - row, 0U)] = loop[(2U * columns + rows + row) % perimeter];
    }

    const auto positionOf = [this, error](EditableVertexId vertexId) -> std::optional<Vec3> {
        const auto* vertex = findVertex(vertexId);
        if (!vertex) {
            if (error) *error = "Grid Fill boundary contains a missing vertex";
            return std::nullopt;
        }
        return vertex->position;
    };
    const auto p00 = positionOf(grid[gridIndex(0U, 0U)]);
    const auto p10 = positionOf(grid[gridIndex(0U, columns)]);
    const auto p01 = positionOf(grid[gridIndex(rows, 0U)]);
    const auto p11 = positionOf(grid[gridIndex(rows, columns)]);
    if (!p00 || !p10 || !p01 || !p11) return std::nullopt;

    EditableMesh working = *this;
    const auto add = [](Vec3 left, Vec3 right) noexcept {
        return Vec3{left.x + right.x, left.y + right.y, left.z + right.z};
    };
    const auto scale = [](Vec3 value, float factor) noexcept {
        return Vec3{value.x * factor, value.y * factor, value.z * factor};
    };
    const auto subtract = [](Vec3 left, Vec3 right) noexcept {
        return Vec3{left.x - right.x, left.y - right.y, left.z - right.z};
    };

    for (std::size_t row = 1U; row < rows; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(rows);
        const auto left = positionOf(grid[gridIndex(row, 0U)]);
        const auto right = positionOf(grid[gridIndex(row, columns)]);
        if (!left || !right) return std::nullopt;
        for (std::size_t column = 1U; column < columns; ++column) {
            const float u = static_cast<float>(column) / static_cast<float>(columns);
            const auto top = positionOf(grid[gridIndex(0U, column)]);
            const auto bottom = positionOf(grid[gridIndex(rows, column)]);
            if (!top || !bottom) return std::nullopt;

            Vec3 blended = add(add(scale(*top, 1.0F - v), scale(*bottom, v)),
                               add(scale(*left, 1.0F - u), scale(*right, u)));
            Vec3 corners{};
            corners = add(corners, scale(*p00, (1.0F - u) * (1.0F - v)));
            corners = add(corners, scale(*p10, u * (1.0F - v)));
            corners = add(corners, scale(*p01, (1.0F - u) * v));
            corners = add(corners, scale(*p11, u * v));
            const EditableVertexId vertex = working.addVertex(subtract(blended, corners));
            grid[gridIndex(row, column)] = vertex;
        }
    }

    std::vector<EditableFaceId> created;
    created.reserve(rows * columns);
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            const std::array<EditableVertexId, 4> quad{
                grid[gridIndex(row, column)],
                grid[gridIndex(row + 1U, column)],
                grid[gridIndex(row + 1U, column + 1U)],
                grid[gridIndex(row, column + 1U)],
            };
            const auto face = working.addFace(quad, error);
            if (!face) return std::nullopt;
            if (!working.faceNormal(*face)) {
                if (error) *error = "Grid Fill generated a degenerate quad";
                return std::nullopt;
            }
            created.push_back(*face);
        }
    }
    if (created.empty() || !working.validate(error)) return std::nullopt;

    *this = std::move(working);
    if (error) error->clear();
    return created;
}
'''
    if anchor not in content: raise SystemExit('bridge anchor missing after fill')
    write(path, content.replace(anchor, implementation + anchor, 1))

# Build a rectangular open prism with a 3x2 segmented top boundary and fill it with a quad grid.
path = 'tests/test_mesh_operations.cpp'
content = read(path)
if 'grid fill creates a structured quad patch across a segmented rectangular boundary' not in content:
    content = content.rstrip() + r'''

TEST_CASE("grid fill creates a structured quad patch across a segmented rectangular boundary") {
    m3d::EditableMesh mesh;
    const std::array<m3d::Vec3,10> ringPositions{
        m3d::Vec3{-1.5F,-1.0F,1.0F}, m3d::Vec3{-0.5F,-1.0F,1.0F},
        m3d::Vec3{0.5F,-1.0F,1.0F}, m3d::Vec3{1.5F,-1.0F,1.0F},
        m3d::Vec3{1.5F,0.0F,1.0F}, m3d::Vec3{1.5F,1.0F,1.0F},
        m3d::Vec3{0.5F,1.0F,1.0F}, m3d::Vec3{-0.5F,1.0F,1.0F},
        m3d::Vec3{-1.5F,1.0F,1.0F}, m3d::Vec3{-1.5F,0.0F,1.0F}
    };
    std::array<m3d::EditableVertexId,10> top{};
    std::array<m3d::EditableVertexId,10> bottom{};
    for (std::size_t index=0; index<ringPositions.size(); ++index) {
        top[index] = mesh.addVertex(ringPositions[index]);
        auto position = ringPositions[index];
        position.z = -1.0F;
        bottom[index] = mesh.addVertex(position);
    }
    std::string error;
    REQUIRE(mesh.addFace(bottom,&error).has_value());
    for (std::size_t index=0; index<top.size(); ++index) {
        const std::size_t next=(index+1U)%top.size();
        const std::array<m3d::EditableVertexId,4> side{top[index],top[next],bottom[next],bottom[index]};
        REQUIRE(mesh.addFace(side,&error).has_value());
    }
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount()==20U);
    REQUIRE(mesh.edgeCount()==30U);
    REQUIRE(mesh.faceCount()==11U);

    std::vector<m3d::EditableEdgeId> boundary;
    for (const auto& edge:mesh.edges()) {
        const auto* halfEdge=mesh.findHalfEdge(edge.halfEdge);
        if (halfEdge && halfEdge->twin.isNull()) boundary.push_back(edge.id);
    }
    REQUIRE(boundary.size()==10U);
    const auto filled=mesh.gridFillBoundaryLoop(boundary,3U,0U,&error);
    REQUIRE(filled.has_value());
    REQUIRE(filled->size()==6U);
    REQUIRE(error.empty());
    REQUIRE(mesh.validate(&error));
    REQUIRE(mesh.vertexCount()==22U);
    REQUIRE(mesh.edgeCount()==37U);
    REQUIRE(mesh.halfEdgeCount()==74U);
    REQUIRE(mesh.faceCount()==17U);
    for (const auto& edge:mesh.edges()) {
        const auto* halfEdge=mesh.findHalfEdge(edge.halfEdge);
        REQUIRE(halfEdge != nullptr);
        REQUIRE(!halfEdge->twin.isNull());
    }
}

TEST_CASE("grid fill span and offset validation is atomic") {
    auto mesh=m3d::EditableMesh::makeCube();
    std::string error;
    REQUIRE(mesh.deleteFaces(std::array<m3d::EditableFaceId,1>{mesh.faces().front().id},&error));
    std::vector<m3d::EditableEdgeId> boundary;
    for(const auto& edge:mesh.edges()) {
        const auto* halfEdge=mesh.findHalfEdge(edge.halfEdge);
        if(halfEdge && halfEdge->twin.isNull()) boundary.push_back(edge.id);
    }
    const auto before=mesh.snapshot();
    REQUIRE(!mesh.gridFillBoundaryLoop(boundary,0U,0U,&error).has_value());
    REQUIRE(!mesh.gridFillBoundaryLoop(boundary,2U,0U,&error).has_value());
    REQUIRE(mesh.snapshot().vertices==before.vertices);
    REQUIRE(mesh.snapshot().halfEdges==before.halfEdges);
    REQUIRE(mesh.snapshot().edges==before.edges);
    REQUIRE(mesh.snapshot().faces==before.faces);
}
''' + '\n'
    write(path, content)

Path('tests/test_mesh_operations.cpp').write_text(Path('tests/test_mesh_operations.cpp').read_text().rstrip() + '\n')
print('grid fill core patch prepared')
