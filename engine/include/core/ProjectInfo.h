#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// Information about a game project discovered on disk.
struct ProjectInfo {
    std::string name;               // Display name (from engine.json windowTitle or directory name)
    std::string path;               // Absolute path to project directory
    int64_t     lastOpened = 0;     // Unix timestamp of last open (0 = never opened via launcher)
    bool        hasGameDefinition = false;  // Whether game.json exists
    bool        hasCMakeLists     = false;  // Whether CMakeLists.txt exists (standalone game)
    bool        hasWorldDatabase  = false;  // Whether worlds/default.db exists

    nlohmann::json toJson() const;
    static ProjectInfo fromJson(const nlohmann::json& j);

    /// Check if path contains a valid project (has engine.json).
    static bool isValidProject(const std::string& projectPath);

    /// Scan a base directory for valid project subdirectories.
    /// Returns all discovered projects (without lastOpened timestamps).
    static std::vector<ProjectInfo> discoverProjects(const std::string& baseDir);

    /// Get the default base directory for projects (Documents/PhyxelProjects).
    static std::string getDefaultProjectsDir();

    /// Scaffold a new empty project directory with minimal engine.json.
    /// Returns the absolute path to the created project, or empty string on failure.
    static std::string scaffoldProject(const std::string& name, const std::string& baseDir);
};

} // namespace Core
} // namespace Phyxel
