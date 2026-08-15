#include "mobile3d/core/project_manifest.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace m3d {
namespace {

bool atomicReplace(const std::filesystem::path& path, const std::string& content, std::string* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error) *error = "Could not create manifest directory: " + ec.message();
        return false;
    }

    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            if (error) *error = "Could not open temporary manifest file";
            return false;
        }
        output << content;
        output.flush();
        if (!output) {
            if (error) *error = "Could not flush temporary manifest file";
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
        if (error) *error = "Could not replace manifest atomically: " + ec.message();
        return false;
    }
    return true;
}

} // namespace

bool ProjectManifestCodec::write(const std::filesystem::path& path,
                                 const ProjectManifest& manifest,
                                 std::string* error) {
    std::ostringstream output;
    output << "M3DPROJECT " << manifest.formatVersion << '\n';
    output << "name " << std::quoted(manifest.name) << '\n';
    output << "main_scene " << std::quoted(manifest.mainScene.generic_string()) << '\n';
    return atomicReplace(path, output.str(), error);
}

std::optional<ProjectManifest> ProjectManifestCodec::read(const std::filesystem::path& path,
                                                          std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error) *error = "Could not open project manifest";
        return std::nullopt;
    }

    std::string magic;
    ProjectManifest manifest;
    if (!(input >> magic >> manifest.formatVersion) || magic != "M3DPROJECT") {
        if (error) *error = "Invalid project manifest header";
        return std::nullopt;
    }
    if (manifest.formatVersion > ProjectManifest::currentFormatVersion || manifest.formatVersion < 1) {
        if (error) *error = "Unsupported project manifest version";
        return std::nullopt;
    }

    std::string key;
    std::string value;
    if (!(input >> key >> std::quoted(value)) || key != "name") {
        if (error) *error = "Missing project name";
        return std::nullopt;
    }
    manifest.name = value;

    if (!(input >> key >> std::quoted(value)) || key != "main_scene") {
        if (error) *error = "Missing main scene path";
        return std::nullopt;
    }
    manifest.mainScene = value;
    return manifest;
}

} // namespace m3d
