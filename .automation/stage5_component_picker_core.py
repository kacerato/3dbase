from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text()


def write(path: str, content: str) -> None:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content)


def replace_once(path: str, old: str, new: str) -> None:
    content = read(path)
    count = content.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected 1 match, found {count}: {old[:120]!r}")
    write(path, content.replace(old, new, 1))


write('src/editor/include/mobile3d/editor/mesh_screen_picker.hpp', r'''#pragma once

#include "mobile3d/editor/mesh_selection.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace m3d {

struct MeshScreenPoint final { float x{0.0F}; float y{0.0F}; float depth{1.0F}; };
struct MeshScreenVertex final { EditableVertexId id{}; MeshScreenPoint point{}; };
struct MeshScreenEdge final { EditableEdgeId id{}; MeshScreenPoint first{}; MeshScreenPoint second{}; };
struct MeshScreenFace final { EditableFaceId id{}; std::vector<MeshScreenPoint> vertices; };

struct MeshScreenPickRequest final {
    MeshSelectionMode mode{MeshSelectionMode::Vertex};
    float x{0.0F};
    float y{0.0F};
    float vertexRadius{14.0F};
    float edgeTolerance{12.0F};
    float occlusionDepthEpsilon{0.003F};
};

struct MeshScreenPickResult final {
    MeshSelectionMode mode{MeshSelectionMode::Vertex};
    std::uint32_t elementId{0};
    float screenDistance{0.0F};
    float depth{1.0F};
};

class MeshScreenPicker final {
public:
    [[nodiscard]] static std::optional<MeshScreenPickResult> pick(
        std::span<const MeshScreenVertex> vertices,
        std::span<const MeshScreenEdge> edges,
        std::span<const MeshScreenFace> faces,
        const MeshScreenPickRequest& request);
};

} // namespace m3d
''')

write('src/editor/src/mesh_screen_picker.cpp', r'''#include "mobile3d/editor/mesh_screen_picker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace m3d {
namespace {

[[nodiscard]] bool finitePoint(const MeshScreenPoint& point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.depth);
}
[[nodiscard]] float squaredDistance(float ax, float ay, float bx, float by) noexcept {
    const float dx = ax - bx; const float dy = ay - by; return dx * dx + dy * dy;
}
[[nodiscard]] std::optional<float> triangleDepthAt(float x, float y,
                                                   const MeshScreenPoint& a,
                                                   const MeshScreenPoint& b,
                                                   const MeshScreenPoint& c) noexcept {
    if (!finitePoint(a) || !finitePoint(b) || !finitePoint(c)) return std::nullopt;
    const float denominator = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (std::abs(denominator) <= 1.0e-8F) return std::nullopt;
    const float wa = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / denominator;
    const float wb = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / denominator;
    const float wc = 1.0F - wa - wb;
    constexpr float epsilon = -1.0e-5F;
    if (wa < epsilon || wb < epsilon || wc < epsilon) return std::nullopt;
    return wa * a.depth + wb * b.depth + wc * c.depth;
}
struct FaceDepthHit final { EditableFaceId face{}; float depth{1.0F}; };
[[nodiscard]] std::optional<FaceDepthHit> frontFaceAt(std::span<const MeshScreenFace> faces,
                                                       float x, float y) {
    std::optional<FaceDepthHit> best;
    for (const auto& face : faces) {
        if (face.id.isNull() || face.vertices.size() < 3U) continue;
        const auto& origin = face.vertices.front();
        for (std::size_t index = 1U; index + 1U < face.vertices.size(); ++index) {
            const auto depth = triangleDepthAt(x, y, origin, face.vertices[index], face.vertices[index + 1U]);
            if (!depth || *depth < 0.0F || *depth > 1.0F) continue;
            if (!best || *depth < best->depth) best = FaceDepthHit{face.id, *depth};
        }
    }
    return best;
}
[[nodiscard]] bool occluded(float depth, const std::optional<FaceDepthHit>& front, float epsilon) noexcept {
    return front && depth > front->depth + std::max(epsilon, 0.0F);
}
struct EdgeDistance final { float distance{std::numeric_limits<float>::infinity()}; float depth{1.0F}; };
[[nodiscard]] EdgeDistance distanceToEdge(float x, float y,
                                          const MeshScreenPoint& first,
                                          const MeshScreenPoint& second) noexcept {
    if (!finitePoint(first) || !finitePoint(second)) return {};
    const float dx = second.x - first.x; const float dy = second.y - first.y;
    const float lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 1.0e-8F) return {std::sqrt(squaredDistance(x, y, first.x, first.y)), first.depth};
    const float t = std::clamp(((x - first.x) * dx + (y - first.y) * dy) / lengthSquared, 0.0F, 1.0F);
    const float cx = first.x + dx * t; const float cy = first.y + dy * t;
    return {std::sqrt(squaredDistance(x, y, cx, cy)), first.depth + (second.depth - first.depth) * t};
}
[[nodiscard]] bool better(float distance, float depth,
                          const std::optional<MeshScreenPickResult>& best) noexcept {
    if (!best) return true;
    constexpr float tie = 0.25F;
    return distance + tie < best->screenDistance ||
           (std::abs(distance - best->screenDistance) <= tie && depth < best->depth);
}

} // namespace

std::optional<MeshScreenPickResult> MeshScreenPicker::pick(
    std::span<const MeshScreenVertex> vertices,
    std::span<const MeshScreenEdge> edges,
    std::span<const MeshScreenFace> faces,
    const MeshScreenPickRequest& request) {
    if (!std::isfinite(request.x) || !std::isfinite(request.y) ||
        !std::isfinite(request.vertexRadius) || !std::isfinite(request.edgeTolerance) ||
        request.vertexRadius < 0.0F || request.edgeTolerance < 0.0F) return std::nullopt;
    const auto front = frontFaceAt(faces, request.x, request.y);
    if (request.mode == MeshSelectionMode::Face) {
        if (!front) return std::nullopt;
        return MeshScreenPickResult{MeshSelectionMode::Face, front->face.value, 0.0F, front->depth};
    }
    std::optional<MeshScreenPickResult> best;
    if (request.mode == MeshSelectionMode::Vertex) {
        for (const auto& vertex : vertices) {
            if (vertex.id.isNull() || !finitePoint(vertex.point) || vertex.point.depth < 0.0F || vertex.point.depth > 1.0F) continue;
            const float distance = std::sqrt(squaredDistance(request.x, request.y, vertex.point.x, vertex.point.y));
            if (distance > request.vertexRadius || occluded(vertex.point.depth, front, request.occlusionDepthEpsilon)) continue;
            if (better(distance, vertex.point.depth, best))
                best = MeshScreenPickResult{MeshSelectionMode::Vertex, vertex.id.value, distance, vertex.point.depth};
        }
        return best;
    }
    for (const auto& edge : edges) {
        if (edge.id.isNull()) continue;
        const auto candidate = distanceToEdge(request.x, request.y, edge.first, edge.second);
        if (!std::isfinite(candidate.distance) || candidate.distance > request.edgeTolerance ||
            candidate.depth < 0.0F || candidate.depth > 1.0F ||
            occluded(candidate.depth, front, request.occlusionDepthEpsilon)) continue;
        if (better(candidate.distance, candidate.depth, best))
            best = MeshScreenPickResult{MeshSelectionMode::Edge, edge.id.value, candidate.distance, candidate.depth};
    }
    return best;
}

} // namespace m3d
''')

replace_once('src/editor/CMakeLists.txt', '    src/mesh_edit_snapshot.cpp\n',
             '    src/mesh_edit_snapshot.cpp\n    src/mesh_screen_picker.cpp\n')
replace_once('src/editor/include/mobile3d/editor/editor_session.hpp',
'''    [[nodiscard]] bool selectMeshFace(EditableFaceId face,\n                                      MeshSelectionAction action = MeshSelectionAction::Replace);\n''',
'''    [[nodiscard]] bool selectMeshFace(EditableFaceId face,\n                                      MeshSelectionAction action = MeshSelectionAction::Replace);\n    [[nodiscard]] bool clearMeshSelection() noexcept;\n''')
replace_once('src/editor/src/editor_mesh_edit.cpp',
'''bool EditorSession::selectMeshFace(EditableFaceId face, MeshSelectionAction action) {\n    if (!meshEditTransaction_) return false;\n    if (!meshEditTransaction_->selection.select(meshEditTransaction_->working, face, action)) return false;\n    ++selectionRevision_;\n    ++uiRevision_;\n    return true;\n}\n''',
'''bool EditorSession::selectMeshFace(EditableFaceId face, MeshSelectionAction action) {\n    if (!meshEditTransaction_) return false;\n    if (!meshEditTransaction_->selection.select(meshEditTransaction_->working, face, action)) return false;\n    ++selectionRevision_;\n    ++uiRevision_;\n    return true;\n}\n\nbool EditorSession::clearMeshSelection() noexcept {\n    if (!meshEditTransaction_) return false;\n    if (meshEditTransaction_->selection.empty()) return true;\n    meshEditTransaction_->selection.clear();\n    ++selectionRevision_;\n    ++uiRevision_;\n    return true;\n}\n''')

write('tests/test_mesh_screen_picker.cpp', r'''#include "test_harness.hpp"
#include "mobile3d/editor/mesh_screen_picker.hpp"
#include <vector>

TEST_CASE("mesh screen picker chooses nearest visible vertex in pixel space") {
    const std::vector<m3d::MeshScreenVertex> vertices{{{1U}, {100,100,0.60F}}, {{2U}, {105,100,0.30F}}, {{3U}, {150,150,0.10F}}};
    m3d::MeshScreenPickRequest request; request.mode=m3d::MeshSelectionMode::Vertex; request.x=103; request.y=100; request.vertexRadius=12;
    const auto hit=m3d::MeshScreenPicker::pick(vertices,{}, {},request); REQUIRE(hit.has_value()); REQUIRE(hit->elementId==2U);
}
TEST_CASE("mesh screen picker resolves edge distance and depth ties deterministically") {
    const std::vector<m3d::MeshScreenEdge> edges{{{4U},{50,50,0.70F},{150,50,0.70F}},{{5U},{50,54,0.25F},{150,54,0.25F}}};
    m3d::MeshScreenPickRequest request; request.mode=m3d::MeshSelectionMode::Edge; request.x=100; request.y=52; request.edgeTolerance=8;
    const auto hit=m3d::MeshScreenPicker::pick({},edges,{},request); REQUIRE(hit.has_value()); REQUIRE(hit->elementId==5U);
}
TEST_CASE("mesh screen picker chooses frontmost overlapping face") {
    const std::vector<m3d::MeshScreenFace> faces{{{10U},{{0,0,0.70F},{100,0,0.70F},{100,100,0.70F},{0,100,0.70F}}},{{11U},{{0,0,0.20F},{100,0,0.20F},{100,100,0.20F},{0,100,0.20F}}}};
    m3d::MeshScreenPickRequest request; request.mode=m3d::MeshSelectionMode::Face; request.x=40; request.y=60;
    const auto hit=m3d::MeshScreenPicker::pick({}, {},faces,request); REQUIRE(hit.has_value()); REQUIRE(hit->elementId==11U);
}
TEST_CASE("mesh screen picker rejects component hidden behind front surface") {
    const std::vector<m3d::MeshScreenVertex> vertices{{{20U},{50,50,0.80F}}};
    const std::vector<m3d::MeshScreenFace> faces{{{21U},{{0,0,0.20F},{100,0,0.20F},{100,100,0.20F},{0,100,0.20F}}}};
    m3d::MeshScreenPickRequest request; request.mode=m3d::MeshSelectionMode::Vertex; request.x=50; request.y=50; request.vertexRadius=12;
    REQUIRE(!m3d::MeshScreenPicker::pick(vertices,{},faces,request).has_value());
}
''')
replace_once('tests/CMakeLists.txt', '    test_mesh_edit_snapshot.cpp\n',
             '    test_mesh_edit_snapshot.cpp\n    test_mesh_screen_picker.cpp\n')

path='tests/test_mesh_edit_transaction.cpp'; content=read(path)
if 'mesh component selection can clear without leaving edit mode' not in content:
    content=content.rstrip()+r'''

TEST_CASE("mesh component selection can clear without leaving edit mode") {
    const auto path = meshEditProjectPath(); MeshEditCleanup cleanup(path); m3d::EditorSession session; std::string error;
    REQUIRE(session.createProject(path,"Mesh Selection Clear",&error)); const auto object=session.createObject(m3d::ObjectType::Mesh,"Cube"); REQUIRE(object.has_value());
    REQUIRE(session.beginMeshEdit(*object,&error)); const auto vertex=session.editableMesh()->vertices().front().id; REQUIRE(session.selectMeshVertex(vertex));
    REQUIRE(!session.meshSelection()->empty()); const auto revision=session.selectionRevision(); REQUIRE(session.clearMeshSelection());
    REQUIRE(session.hasMeshEditTransaction()); REQUIRE(session.meshSelection()->empty()); REQUIRE(session.selectionRevision()>revision); REQUIRE(session.cancelMeshEdit());
}
'''+ '\n'; write(path,content)
print('component picker core prepared')
