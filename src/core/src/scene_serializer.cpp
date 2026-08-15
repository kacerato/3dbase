#include "mobile3d/core/scene_serializer.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
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
               << (object.locked ? 1 : 0) << '\n';
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
        decoded.push_back(std::move(object));
    }

    Scene scene;
    if (!scene.restoreObjects(decoded)) {
        if (error) *error = "Scene hierarchy is invalid or cyclic";
        return std::nullopt;
    }
    return scene;
}

} // namespace m3d
