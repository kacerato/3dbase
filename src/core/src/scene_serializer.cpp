#include "mobile3d/core/scene_serializer.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace m3d {
namespace {

bool writeAtomic(const std::filesystem::path& path, const std::string& content, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error) *error = "Could not create scene directory: " + ec.message();
        return false;
    }
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            if (error) *error = "Could not open temporary scene file";
            return false;
        }
        output << content;
        output.flush();
        if (!output) {
            if (error) *error = "Could not flush temporary scene file";
            return false;
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
    }
    if (ec) {
        std::filesystem::remove(temporary);
        if (error) *error = "Could not replace scene atomically: " + ec.message();
        return false;
    }
    return true;
}

bool readEditableSnapshot(std::istream& input, EditableMeshSnapshot& snapshot, ResourceId& resourceId,
                          std::string* error) {
    std::string record;
    std::string resourceText;
    std::size_t vertexCount = 0;
    std::size_t halfEdgeCount = 0;
    std::size_t edgeCount = 0;
    std::size_t faceCount = 0;
    if (!(input >> record >> std::quoted(resourceText) >> vertexCount >> halfEdgeCount >> edgeCount >> faceCount)
        || record != "authoring") {
        if (error) *error = "Malformed editable mesh authoring header";
        return false;
    }
    const auto parsedResource = ResourceId::fromString(resourceText);
    if (!parsedResource || parsedResource->isNull()) {
        if (error) *error = "Invalid editable mesh resource id";
        return false;
    }
    resourceId = *parsedResource;

    snapshot.vertices.reserve(vertexCount);
    snapshot.halfEdges.reserve(halfEdgeCount);
    snapshot.edges.reserve(edgeCount);
    snapshot.faces.reserve(faceCount);

    for (std::size_t index = 0; index < vertexCount; ++index) {
        std::string key;
        std::uint32_t id = 0;
        std::uint32_t outgoing = 0;
        EditableVertex vertex;
        if (!(input >> key >> id >> vertex.position.x >> vertex.position.y >> vertex.position.z >> outgoing)
            || key != "a_vertex") {
            if (error) *error = "Malformed editable vertex record";
            return false;
        }
        vertex.id = EditableVertexId{id};
        vertex.outgoing = EditableHalfEdgeId{outgoing};
        snapshot.vertices.push_back(vertex);
    }

    for (std::size_t index = 0; index < halfEdgeCount; ++index) {
        std::string key;
        std::uint32_t id = 0;
        std::uint32_t origin = 0;
        std::uint32_t next = 0;
        std::uint32_t twin = 0;
        std::uint32_t edge = 0;
        std::uint32_t face = 0;
        if (!(input >> key >> id >> origin >> next >> twin >> edge >> face) || key != "a_halfedge") {
            if (error) *error = "Malformed editable half-edge record";
            return false;
        }
        snapshot.halfEdges.push_back(EditableHalfEdge{
            EditableHalfEdgeId{id}, EditableVertexId{origin}, EditableHalfEdgeId{next},
            EditableHalfEdgeId{twin}, EditableEdgeId{edge}, EditableFaceId{face}});
    }

    for (std::size_t index = 0; index < edgeCount; ++index) {
        std::string key;
        std::uint32_t id = 0;
        std::uint32_t halfEdge = 0;
        if (!(input >> key >> id >> halfEdge) || key != "a_edge") {
            if (error) *error = "Malformed editable edge record";
            return false;
        }
        snapshot.edges.push_back(EditableEdge{EditableEdgeId{id}, EditableHalfEdgeId{halfEdge}});
    }

    for (std::size_t index = 0; index < faceCount; ++index) {
        std::string key;
        std::uint32_t id = 0;
        std::uint32_t halfEdge = 0;
        if (!(input >> key >> id >> halfEdge) || key != "a_face") {
            if (error) *error = "Malformed editable face record";
            return false;
        }
        snapshot.faces.push_back(EditableFace{EditableFaceId{id}, EditableHalfEdgeId{halfEdge}});
    }
    return true;
}

} // namespace

bool SceneSerializer::write(const std::filesystem::path& path, const Scene& scene, std::string* error) {
    std::ostringstream output;
    output << "M3DSCENE " << currentFormatVersion << '\n';

    auto meshes = scene.meshResources();
    output << "mesh_count " << meshes.size() << '\n';
    for (auto& mesh : meshes) {
        if (mesh.authoring && !mesh.rebuildFromAuthoring(error)) return false;
        output << "mesh " << std::quoted(mesh.id.toString()) << ' ' << std::quoted(mesh.name) << ' '
               << mesh.vertices.size() << ' ' << mesh.indices.size() << '\n';
        for (const auto& vertex : mesh.vertices) {
            output << "vertex " << vertex.position.x << ' ' << vertex.position.y << ' ' << vertex.position.z << ' '
                   << vertex.normal.x << ' ' << vertex.normal.y << ' ' << vertex.normal.z << '\n';
        }
        for (std::size_t index = 0; index < mesh.indices.size(); index += 3U) {
            output << "triangle " << mesh.indices[index] << ' ' << mesh.indices[index + 1U] << ' '
                   << mesh.indices[index + 2U] << '\n';
        }
    }

    std::size_t authoringCount = 0;
    for (const auto& mesh : meshes) if (mesh.authoring) ++authoringCount;
    output << "authoring_count " << authoringCount << '\n';
    for (const auto& mesh : meshes) {
        if (!mesh.authoring) continue;
        const auto snapshot = mesh.authoring->snapshot();
        output << "authoring " << std::quoted(mesh.id.toString()) << ' '
               << snapshot.vertices.size() << ' ' << snapshot.halfEdges.size() << ' '
               << snapshot.edges.size() << ' ' << snapshot.faces.size() << '\n';
        for (const auto& vertex : snapshot.vertices) {
            output << "a_vertex " << vertex.id.value << ' '
                   << vertex.position.x << ' ' << vertex.position.y << ' ' << vertex.position.z << ' '
                   << vertex.outgoing.value << '\n';
        }
        for (const auto& halfEdge : snapshot.halfEdges) {
            output << "a_halfedge " << halfEdge.id.value << ' ' << halfEdge.origin.value << ' '
                   << halfEdge.next.value << ' ' << halfEdge.twin.value << ' ' << halfEdge.edge.value << ' '
                   << halfEdge.face.value << '\n';
        }
        for (const auto& edge : snapshot.edges) {
            output << "a_edge " << edge.id.value << ' ' << edge.halfEdge.value << '\n';
        }
        for (const auto& face : snapshot.faces) {
            output << "a_face " << face.id.value << ' ' << face.halfEdge.value << '\n';
        }
    }

    output << "collection_count " << scene.collectionCount() << '\n';
    for (const auto& collection : scene.collections()) {
        output << "collection " << std::quoted(collection.id.toString()) << ' '
               << std::quoted(collection.name) << ' '
               << (collection.visible ? 1 : 0) << ' '
               << (collection.locked ? 1 : 0) << ' '
               << collection.objects.size();
        for (const auto object : collection.objects) output << ' ' << std::quoted(object.toString());
        output << '\n';
    }

    output << "layer_count " << scene.layerCount() << '\n';
    for (const auto& layer : scene.layers()) {
        output << "layer " << std::quoted(layer.id.toString()) << ' '
               << std::quoted(layer.name) << ' '
               << (layer.enabled ? 1 : 0) << ' '
               << layer.collections.size();
        for (const auto collection : layer.collections) output << ' ' << std::quoted(collection.toString());
        output << '\n';
    }

    output << "count " << scene.size() << '\n';
    for (const auto& object : scene.objects()) {
        output << "object "
               << std::quoted(object.id.toString()) << ' '
               << static_cast<int>(object.type) << ' '
               << std::quoted(object.parent ? object.parent->toString() : std::string("-")) << ' '
               << std::quoted(object.name) << ' '
               << object.localTransform.position.x << ' '
               << object.localTransform.position.y << ' '
               << object.localTransform.position.z << ' '
               << object.localTransform.rotation.x << ' '
               << object.localTransform.rotation.y << ' '
               << object.localTransform.rotation.z << ' '
               << object.localTransform.rotation.w << ' '
               << object.localTransform.scale.x << ' '
               << object.localTransform.scale.y << ' '
               << object.localTransform.scale.z << ' '
               << (object.visible ? 1 : 0) << ' '
               << (object.locked ? 1 : 0) << ' '
               << std::quoted(object.meshResource ? object.meshResource->toString() : std::string("-")) << '\n';
    }
    return writeAtomic(path, output.str(), error);
}

std::optional<Scene> SceneSerializer::read(const std::filesystem::path& path, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error) *error = "Could not open scene file";
        return std::nullopt;
    }

    std::string magic;
    int version = 0;
    if (!(input >> magic >> version) || magic != "M3DSCENE") {
        if (error) *error = "Invalid scene header";
        return std::nullopt;
    }
    if (version > currentFormatVersion || version < 1) {
        if (error) *error = "Unsupported scene version";
        return std::nullopt;
    }

    Scene scene;
    if (version >= 2) {
        std::string meshCountKey;
        std::size_t meshCount = 0;
        if (!(input >> meshCountKey >> meshCount) || meshCountKey != "mesh_count") {
            if (error) *error = "Invalid scene mesh resource count";
            return std::nullopt;
        }
        for (std::size_t meshIndex = 0; meshIndex < meshCount; ++meshIndex) {
            std::string record;
            std::string idText;
            MeshResource mesh;
            std::size_t vertexCount = 0;
            std::size_t indexCount = 0;
            if (!(input >> record >> std::quoted(idText) >> std::quoted(mesh.name) >> vertexCount >> indexCount)
                || record != "mesh" || indexCount % 3U != 0U) {
                if (error) *error = "Malformed mesh resource record";
                return std::nullopt;
            }
            const auto parsedId = ResourceId::fromString(idText);
            if (!parsedId || parsedId->isNull()) {
                if (error) *error = "Invalid mesh resource id";
                return std::nullopt;
            }
            mesh.id = *parsedId;
            mesh.vertices.reserve(vertexCount);
            mesh.indices.reserve(indexCount);
            for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
                std::string vertexRecord;
                MeshVertex vertex;
                if (!(input >> vertexRecord
                      >> vertex.position.x >> vertex.position.y >> vertex.position.z
                      >> vertex.normal.x >> vertex.normal.y >> vertex.normal.z)
                    || vertexRecord != "vertex") {
                    if (error) *error = "Malformed mesh vertex record";
                    return std::nullopt;
                }
                mesh.vertices.push_back(vertex);
            }
            for (std::size_t triangle = 0; triangle < indexCount / 3U; ++triangle) {
                std::string triangleRecord;
                std::uint32_t a = 0;
                std::uint32_t b = 0;
                std::uint32_t c = 0;
                if (!(input >> triangleRecord >> a >> b >> c) || triangleRecord != "triangle") {
                    if (error) *error = "Malformed mesh triangle record";
                    return std::nullopt;
                }
                mesh.indices.insert(mesh.indices.end(), {a, b, c});
            }
            if (!scene.insertMeshResource(std::move(mesh))) {
                if (error) *error = "Invalid or duplicate mesh resource";
                return std::nullopt;
            }
        }
    }

    if (version >= 4) {
        std::string authoringCountKey;
        std::size_t authoringCount = 0;
        if (!(input >> authoringCountKey >> authoringCount) || authoringCountKey != "authoring_count") {
            if (error) *error = "Invalid editable mesh authoring count";
            return std::nullopt;
        }
        for (std::size_t index = 0; index < authoringCount; ++index) {
            EditableMeshSnapshot snapshot;
            ResourceId resourceId{};
            if (!readEditableSnapshot(input, snapshot, resourceId, error)) return std::nullopt;
            auto* resource = scene.findMeshResource(resourceId);
            if (!resource || resource->authoring) {
                if (error) *error = "Editable mesh authoring references a missing or duplicate resource";
                return std::nullopt;
            }
            const auto editable = EditableMesh::fromSnapshot(snapshot, error);
            if (!editable) return std::nullopt;
            resource->authoring = *editable;
            if (!resource->rebuildFromAuthoring(error)) return std::nullopt;
        }
    }

    std::vector<SceneCollection> decodedCollections;
    std::vector<SceneLayer> decodedLayers;
    if (version >= 3) {
        std::string collectionCountKey;
        std::size_t collectionCount = 0;
        if (!(input >> collectionCountKey >> collectionCount) || collectionCountKey != "collection_count") {
            if (error) *error = "Invalid scene collection count";
            return std::nullopt;
        }
        decodedCollections.reserve(collectionCount);
        for (std::size_t collectionIndex = 0; collectionIndex < collectionCount; ++collectionIndex) {
            std::string record;
            std::string idText;
            int visible = 1;
            int locked = 0;
            std::size_t objectCount = 0;
            SceneCollection collection;
            if (!(input >> record >> std::quoted(idText) >> std::quoted(collection.name)
                  >> visible >> locked >> objectCount) || record != "collection") {
                if (error) *error = "Malformed collection record";
                return std::nullopt;
            }
            const auto id = CollectionId::fromString(idText);
            if (!id || id->isNull()) { if (error) *error = "Invalid collection id"; return std::nullopt; }
            collection.id = *id;
            collection.visible = visible != 0;
            collection.locked = locked != 0;
            collection.objects.reserve(objectCount);
            for (std::size_t member = 0; member < objectCount; ++member) {
                std::string objectText;
                if (!(input >> std::quoted(objectText))) { if (error) *error = "Malformed collection membership"; return std::nullopt; }
                const auto objectId = ObjectId::fromString(objectText);
                if (!objectId || objectId->isNull()) { if (error) *error = "Invalid collection object id"; return std::nullopt; }
                collection.objects.push_back(*objectId);
            }
            decodedCollections.push_back(std::move(collection));
        }

        std::string layerCountKey;
        std::size_t layerCount = 0;
        if (!(input >> layerCountKey >> layerCount) || layerCountKey != "layer_count") {
            if (error) *error = "Invalid scene layer count";
            return std::nullopt;
        }
        decodedLayers.reserve(layerCount);
        for (std::size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
            std::string record;
            std::string idText;
            int enabled = 1;
            std::size_t layerCollectionCount = 0;
            SceneLayer layer;
            if (!(input >> record >> std::quoted(idText) >> std::quoted(layer.name)
                  >> enabled >> layerCollectionCount) || record != "layer") {
                if (error) *error = "Malformed layer record";
                return std::nullopt;
            }
            const auto id = LayerId::fromString(idText);
            if (!id || id->isNull()) { if (error) *error = "Invalid layer id"; return std::nullopt; }
            layer.id = *id;
            layer.enabled = enabled != 0;
            layer.collections.reserve(layerCollectionCount);
            for (std::size_t member = 0; member < layerCollectionCount; ++member) {
                std::string collectionText;
                if (!(input >> std::quoted(collectionText))) { if (error) *error = "Malformed layer membership"; return std::nullopt; }
                const auto collectionId = CollectionId::fromString(collectionText);
                if (!collectionId || collectionId->isNull()) { if (error) *error = "Invalid layer collection id"; return std::nullopt; }
                layer.collections.push_back(*collectionId);
            }
            decodedLayers.push_back(std::move(layer));
        }
    }

    std::string countKey;
    std::size_t count = 0;
    if (!(input >> countKey >> count) || countKey != "count") {
        if (error) *error = "Invalid scene object count";
        return std::nullopt;
    }

    std::vector<SceneObject> decoded;
    decoded.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        std::string record;
        std::string idText;
        std::string parentText;
        std::string meshText{"-"};
        SceneObject object;
        int type = 0;
        int visible = 1;
        int locked = 0;

        if (!(input >> record >> std::quoted(idText) >> type >> std::quoted(parentText)
              >> std::quoted(object.name)
              >> object.localTransform.position.x
              >> object.localTransform.position.y
              >> object.localTransform.position.z
              >> object.localTransform.rotation.x
              >> object.localTransform.rotation.y
              >> object.localTransform.rotation.z
              >> object.localTransform.rotation.w
              >> object.localTransform.scale.x
              >> object.localTransform.scale.y
              >> object.localTransform.scale.z
              >> visible >> locked) || record != "object") {
            if (error) *error = "Malformed scene object record";
            return std::nullopt;
        }
        if (version >= 2 && !(input >> std::quoted(meshText))) {
            if (error) *error = "Malformed mesh reference in scene object";
            return std::nullopt;
        }

        const auto parsedId = ObjectId::fromString(idText);
        if (!parsedId || parsedId->isNull()) {
            if (error) *error = "Invalid object id in scene";
            return std::nullopt;
        }
        if (type < static_cast<int>(ObjectType::Mesh) || type > static_cast<int>(ObjectType::Collection)) {
            if (error) *error = "Invalid object type in scene";
            return std::nullopt;
        }

        object.id = *parsedId;
        object.type = static_cast<ObjectType>(type);
        object.visible = visible != 0;
        object.locked = locked != 0;

        if (parentText != "-") {
            const auto parent = ObjectId::fromString(parentText);
            if (!parent || parent->isNull()) {
                if (error) *error = "Invalid parent id in scene";
                return std::nullopt;
            }
            object.parent = *parent;
        }
        if (meshText != "-") {
            const auto mesh = ResourceId::fromString(meshText);
            if (!mesh || mesh->isNull()) {
                if (error) *error = "Invalid mesh resource reference in scene";
                return std::nullopt;
            }
            object.meshResource = *mesh;
        }
        decoded.push_back(std::move(object));
    }

    if (!scene.restoreObjects(decoded)) {
        if (error) *error = "Scene hierarchy or resource references are invalid";
        return std::nullopt;
    }
    for (auto& collection : decodedCollections) {
        if (!scene.insertCollection(std::move(collection))) {
            if (error) *error = "Invalid collection or object membership";
            return std::nullopt;
        }
    }
    for (auto& layer : decodedLayers) {
        if (!scene.insertLayer(std::move(layer))) {
            if (error) *error = "Invalid layer or collection membership";
            return std::nullopt;
        }
    }
    return scene;
}

} // namespace m3d
