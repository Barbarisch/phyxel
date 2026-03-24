#include "core/ProjectInfo.h"
#include "core/EngineConfig.h"
#include "utils/Logger.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace Phyxel {
namespace Core {

namespace fs = std::filesystem;

nlohmann::json ProjectInfo::toJson() const {
    return {
        {"name",              name},
        {"path",              path},
        {"lastOpened",        lastOpened},
        {"hasGameDefinition", hasGameDefinition},
        {"hasCMakeLists",     hasCMakeLists},
        {"hasWorldDatabase",  hasWorldDatabase}
    };
}

ProjectInfo ProjectInfo::fromJson(const nlohmann::json& j) {
    ProjectInfo info;
    if (j.contains("name"))              info.name              = j["name"].get<std::string>();
    if (j.contains("path"))              info.path              = j["path"].get<std::string>();
    if (j.contains("lastOpened"))        info.lastOpened        = j["lastOpened"].get<int64_t>();
    if (j.contains("hasGameDefinition")) info.hasGameDefinition = j["hasGameDefinition"].get<bool>();
    if (j.contains("hasCMakeLists"))     info.hasCMakeLists     = j["hasCMakeLists"].get<bool>();
    if (j.contains("hasWorldDatabase"))  info.hasWorldDatabase  = j["hasWorldDatabase"].get<bool>();
    return info;
}

bool ProjectInfo::isValidProject(const std::string& projectPath) {
    // A valid project must have engine.json
    return fs::exists(fs::path(projectPath) / "engine.json");
}

std::vector<ProjectInfo> ProjectInfo::discoverProjects(const std::string& baseDir) {
    std::vector<ProjectInfo> projects;

    if (!fs::is_directory(baseDir)) {
        return projects;
    }

    for (const auto& entry : fs::directory_iterator(baseDir)) {
        if (!entry.is_directory()) continue;

        auto dirPath = entry.path();
        if (!isValidProject(dirPath.string())) continue;

        ProjectInfo info;
        info.path = dirPath.string();
        info.name = dirPath.filename().string();

        // Try to read display name from engine.json
        auto engineJsonPath = dirPath / "engine.json";
        try {
            std::ifstream fin(engineJsonPath);
            if (fin.is_open()) {
                nlohmann::json j = nlohmann::json::parse(fin);
                if (j.contains("window") && j["window"].contains("title")) {
                    info.name = j["window"]["title"].get<std::string>();
                }
            }
        } catch (...) {
            // Keep directory name as fallback
        }

        info.hasGameDefinition = fs::exists(dirPath / "game.json");
        info.hasCMakeLists     = fs::exists(dirPath / "CMakeLists.txt");
        info.hasWorldDatabase  = fs::exists(dirPath / "worlds" / "default.db");

        projects.push_back(std::move(info));
    }

    return projects;
}

std::string ProjectInfo::getDefaultProjectsDir() {
    // Check environment variable override first
    const char* envDir = std::getenv("PHYXEL_PROJECTS_DIR");
    if (envDir && fs::is_directory(envDir)) {
        return std::string(envDir);
    }

#ifdef _WIN32
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile) {
        return (fs::path(userProfile) / "Documents" / "PhyxelProjects").string();
    }
#endif
    const char* home = std::getenv("HOME");
    if (home) {
        return (fs::path(home) / "Documents" / "PhyxelProjects").string();
    }

    return "PhyxelProjects";
}

std::string ProjectInfo::scaffoldProject(const std::string& name, const std::string& baseDir) {
    if (name.empty()) return "";

    fs::path projectPath = fs::path(baseDir) / name;

    // Don't overwrite existing project
    if (fs::exists(projectPath)) {
        LOG_WARN("ProjectInfo", "Project directory already exists: {}", projectPath.string());
        return projectPath.string();
    }

    try {
        // Create directory structure
        fs::create_directories(projectPath / "worlds");
        fs::create_directories(projectPath / "resources" / "templates");
        fs::create_directories(projectPath / "resources" / "textures");
        fs::create_directories(projectPath / "resources" / "sounds");
        fs::create_directories(projectPath / "resources" / "dialogues");
        fs::create_directories(projectPath / "resources" / "animated_characters");

        // Write minimal engine.json
        nlohmann::json engineJson = {
            {"window", {
                {"title", name},
                {"width", 1600},
                {"height", 900}
            }}
        };

        std::ofstream fout(projectPath / "engine.json");
        if (fout.is_open()) {
            fout << engineJson.dump(2);
            fout.close();
        }

        LOG_INFO("ProjectInfo", "Scaffolded new project: {}", projectPath.string());
        return projectPath.string();

    } catch (const std::exception& e) {
        LOG_ERROR("ProjectInfo", "Failed to scaffold project '{}': {}", name, e.what());
        return "";
    }
}

} // namespace Core
} // namespace Phyxel
