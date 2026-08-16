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
    if (authoring && !authoring->validate(error)) return false;
    if (error) error->clear();
    return true;
}

bool MeshResource::rebuildFromAuthoring(std::string* error) {
    if (!authoring) {
        if (error) *error = "Mesh resource has no authoring topology";
        return false;
    }
    if (!authoring->writeRenderMesh(*this, error)) return false;
    return validate(error);
}

bool MeshResource::ensureAuthoring(float weldEpsilon, std::string* error) {
    if (authoring) return authoring->validate(error);
    const auto editable = EditableMesh::fromMeshResource(*this, weldEpsilon, error);
    if (!editable) return false;
    authoring = *editable;
    if (error) error->clear();
    return true;
}

std::optional<Bounds3> MeshResource::bounds() const noexcept {
    if (vertices.empty()) return std::nullopt;
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

MeshResource MeshResource::makeCube(std::string nameValue, float size) {
    MeshResource mesh;
    mesh.id = ResourceId::generate();
    mesh.name = std::move(nameValue);
    mesh.authoring = EditableMesh::makeCube(size);
    std::string error;
    if (!mesh.rebuildFromAuthoring(&error)) {
        mesh.vertices.clear();
        mesh.indices.clear();
    }
    return mesh;
}

} // namespace m3d
