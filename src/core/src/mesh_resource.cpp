#include "mobile3d/core/mesh_resource.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace m3d {
namespace {

bool finite(const Vec3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

bool MeshResource::validate(std::string* error) const {
    if (id.isNull()) {
        if (error) *error = "Mesh resource id is null";
        return false;
    }
    if (name.empty()) {
        if (error) *error = "Mesh resource name is empty";
        return false;
    }
    if (vertices.empty()) {
        if (error) *error = "Mesh resource has no vertices";
        return false;
    }
    if (indices.empty() || indices.size() % 3U != 0U) {
        if (error) *error = "Mesh indices must contain complete triangles";
        return false;
    }
    for (const auto& vertex : vertices) {
        if (!finite(vertex.position) || !finite(vertex.normal)) {
            if (error) *error = "Mesh resource contains non-finite vertex data";
            return false;
        }
    }
    for (const auto index : indices) {
        if (index >= vertices.size()) {
            if (error) *error = "Mesh resource index is outside the vertex buffer";
            return false;
        }
    }
    if (error) error->clear();
    return true;
}

std::optional<Bounds3> MeshResource::bounds() const noexcept {
    if (vertices.empty()) {
        return std::nullopt;
    }
    Bounds3 result{vertices.front().position, vertices.front().position};
    for (const auto& vertex : vertices) {
        result.min.x = std::min(result.min.x, vertex.position.x);
        result.min.y = std::min(result.min.y, vertex.position.y);
        result.min.z = std::min(result.min.z, vertex.position.z);
        result.max.x = std::max(result.max.x, vertex.position.x);
        result.max.y = std::max(result.max.y, vertex.position.y);
        result.max.z = std::max(result.max.z, vertex.position.z);
    }
    return result;
}

MeshResource MeshResource::makeCube(std::string name, float size) {
    MeshResource mesh;
    mesh.id = ResourceId::generate();
    mesh.name = std::move(name);
    const float h = std::max(size, 0.0001F) * 0.5F;

    const auto addFace = [&mesh](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({a, normal});
        mesh.vertices.push_back({b, normal});
        mesh.vertices.push_back({c, normal});
        mesh.vertices.push_back({d, normal});
        mesh.indices.insert(mesh.indices.end(), {base, base + 1U, base + 2U,
                                                  base, base + 2U, base + 3U});
    };

    addFace({-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0.0F, 0.0F, 1.0F});
    addFace({h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0.0F, 0.0F, -1.0F});
    addFace({-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-1.0F, 0.0F, 0.0F});
    addFace({h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, {1.0F, 0.0F, 0.0F});
    addFace({-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, {0.0F, 1.0F, 0.0F});
    addFace({-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, {0.0F, -1.0F, 0.0F});
    return mesh;
}

} // namespace m3d
