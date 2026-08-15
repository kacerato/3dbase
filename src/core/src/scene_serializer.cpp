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

} // namespace

bool SceneSerializer::write(const std::filesystem::path& path, const Scene& scene, std::string* error) {
    std::ostringstream output;
    output << "M3DSCENE " << currentFormatVersion << '\n';
    output << "mesh_count " << scene.meshResourceCount() << '\n';
    for (const auto& mesh : scene.meshResources()) {
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
    return scene;
}

} // namespace m3d
