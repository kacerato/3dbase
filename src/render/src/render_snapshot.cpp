#include "mobile3d/render/render_snapshot.hpp"

#include "mobile3d/render/render_math.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>

namespace m3d {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
constexpr float kQuaternionEpsilon = 1.0e-8F;

void hashWord(std::uint64_t& hash, std::uint32_t word) noexcept {
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        hash ^= static_cast<std::uint8_t>((word >> shift) & 0xFFU);
        hash *= kFnvPrime;
    }
}

std::uint64_t meshContentHash(const MeshResource& mesh) noexcept {
    std::uint64_t hash = kFnvOffset;
    hashWord(hash, static_cast<std::uint32_t>(mesh.vertices.size()));
    hashWord(hash, static_cast<std::uint32_t>(mesh.indices.size()));
    for (const auto& vertex : mesh.vertices) {
        hashWord(hash, std::bit_cast<std::uint32_t>(vertex.position.x));
        hashWord(hash, std::bit_cast<std::uint32_t>(vertex.position.y));
        hashWord(hash, std::bit_cast<std::uint32_t>(vertex.position.z));
        hashWord(hash, std::bit_cast<std::uint32_t>(vertex.normal.x));
        hashWord(hash, std::bit_cast<std::uint32_t>(vertex.normal.y));
        hashWord(hash, std::bit_cast<std::uint32_t>(vertex.normal.z));
    }
    for (const auto index : mesh.indices) hashWord(hash, index);
    return hash;
}

Quat normalizedQuaternion(Quat value) noexcept {
    const float magnitudeSquared = value.x * value.x + value.y * value.y +
                                   value.z * value.z + value.w * value.w;
    if (magnitudeSquared <= kQuaternionEpsilon) return {};
    const float inverseMagnitude = 1.0F / std::sqrt(magnitudeSquared);
    return {
        value.x * inverseMagnitude,
        value.y * inverseMagnitude,
        value.z * inverseMagnitude,
        value.w * inverseMagnitude,
    };
}

Quat multiplyQuaternion(Quat left, Quat right) noexcept {
    return normalizedQuaternion({
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
    });
}

} // namespace

RenderSceneSnapshot::RenderSceneSnapshot(std::uint64_t sceneRevision,
                                         std::uint64_t selectionRevision,
                                         std::vector<RenderObjectSnapshot> objects,
                                         std::vector<RenderMeshSnapshot> meshes)
    : sceneRevision_(sceneRevision), selectionRevision_(selectionRevision),
      objects_(std::move(objects)), meshes_(std::move(meshes)) {}

const RenderObjectSnapshot* RenderSceneSnapshot::find(ObjectId id) const noexcept {
    const auto found = std::find_if(objects_.cbegin(), objects_.cend(),
                                    [id](const RenderObjectSnapshot& object) { return object.id == id; });
    return found == objects_.cend() ? nullptr : &*found;
}

const RenderObjectSnapshot* RenderSceneSnapshot::findPickId(PickId pickId) const noexcept {
    if (pickId == backgroundPickId) return nullptr;
    const auto found = std::find_if(objects_.cbegin(), objects_.cend(),
                                    [pickId](const RenderObjectSnapshot& object) {
                                        return object.pickId == pickId;
                                    });
    return found == objects_.cend() ? nullptr : &*found;
}

const RenderMeshSnapshot* RenderSceneSnapshot::findMesh(ResourceId id) const noexcept {
    const auto found = std::find_if(meshes_.cbegin(), meshes_.cend(),
                                    [id](const RenderMeshSnapshot& mesh) { return mesh.id == id; });
    return found == meshes_.cend() ? nullptr : &*found;
}

RenderSceneSnapshot RenderSnapshotBuilder::build(const Scene& scene,
                                                 const SelectionModel& selection,
                                                 std::uint64_t sceneRevision,
                                                 std::uint64_t selectionRevision) {
    auto authoredObjects = scene.objects();
    std::sort(authoredObjects.begin(), authoredObjects.end(),
              [](const SceneObject& left, const SceneObject& right) {
                  if (left.id.high() != right.id.high()) return left.id.high() < right.id.high();
                  return left.id.low() < right.id.low();
              });

    auto authoredMeshes = scene.meshResources();
    std::sort(authoredMeshes.begin(), authoredMeshes.end(),
              [](const MeshResource& left, const MeshResource& right) {
                  if (left.id.high() != right.id.high()) return left.id.high() < right.id.high();
                  return left.id.low() < right.id.low();
              });

    std::vector<RenderMeshSnapshot> meshSnapshots;
    meshSnapshots.reserve(authoredMeshes.size());
    for (const auto& mesh : authoredMeshes) {
        meshSnapshots.push_back(RenderMeshSnapshot{
            .id = mesh.id,
            .vertices = mesh.vertices,
            .indices = mesh.indices,
            .bounds = mesh.bounds(),
            .contentHash = meshContentHash(mesh),
        });
    }

    std::unordered_map<ObjectId, Mat4, ObjectIdHash> worldCache;
    std::function<Mat4(ObjectId)> resolveWorld = [&](ObjectId id) -> Mat4 {
        if (const auto found = worldCache.find(id); found != worldCache.end()) return found->second;
        const auto* object = scene.find(id);
        if (!object) return Mat4::identity();
        Mat4 world = transformMatrix(object->localTransform);
        if (object->parent) world = multiply(resolveWorld(*object->parent), world);
        worldCache.emplace(id, world);
        return world;
    };

    std::unordered_map<ObjectId, Quat, ObjectIdHash> rotationCache;
    std::function<Quat(ObjectId)> resolveWorldRotation = [&](ObjectId id) -> Quat {
        if (const auto found = rotationCache.find(id); found != rotationCache.end()) return found->second;
        const auto* object = scene.find(id);
        if (!object) return {};
        Quat rotation = normalizedQuaternion(object->localTransform.rotation);
        if (object->parent) rotation = multiplyQuaternion(resolveWorldRotation(*object->parent), rotation);
        rotationCache.emplace(id, rotation);
        return rotation;
    };

    const auto active = selection.active();
    std::vector<RenderObjectSnapshot> snapshots;
    snapshots.reserve(authoredObjects.size());
    std::uint64_t nextPickId = 1;
    for (const auto& object : authoredObjects) {
        const PickId pickId = nextPickId <= std::numeric_limits<PickId>::max()
            ? static_cast<PickId>(nextPickId++)
            : backgroundPickId;
        snapshots.push_back(RenderObjectSnapshot{
            .id = object.id,
            .pickId = pickId,
            .type = object.type,
            .localTransform = object.localTransform,
            .worldTransform = resolveWorld(object.id),
            .worldRotation = resolveWorldRotation(object.id),
            .parent = object.parent,
            .meshResource = object.meshResource,
            .visible = object.visible,
            .selected = selection.contains(object.id),
            .active = active && *active == object.id,
        });
    }

    return RenderSceneSnapshot(sceneRevision, selectionRevision,
                               std::move(snapshots), std::move(meshSnapshots));
}

} // namespace m3d
