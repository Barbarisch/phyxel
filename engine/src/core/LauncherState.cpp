#include "core/LauncherState.h"
#include "utils/Logger.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <unordered_set>

namespace Phyxel {
namespace Core {

namespace fs = std::filesystem;

std::string LauncherState::stateFilePath(const std::string& baseDir) {
    return (fs::path(baseDir) / ".launcher_state.json").string();
}

bool LauncherState::load(const std::string& baseDir) {
    auto path = stateFilePath(baseDir);
    if (!fs::exists(path)) {
        return true; // Not an error — first run
    }

    try {
        std::ifstream fin(path);
        if (!fin.is_open()) return true;

        nlohmann::json j = nlohmann::json::parse(fin);
        recentProjects_.clear();

        if (j.contains("recentProjects") && j["recentProjects"].is_array()) {
            for (const auto& entry : j["recentProjects"]) {
                recentProjects_.push_back(ProjectInfo::fromJson(entry));
            }
        }

        return true;
    } catch (const std::exception& e) {
        LOG_WARN("LauncherState", "Failed to parse launcher state: {}", e.what());
        return false;
    }
}

bool LauncherState::save(const std::string& baseDir) const {
    // Ensure base directory exists
    fs::create_directories(baseDir);

    auto path = stateFilePath(baseDir);
    try {
        nlohmann::json j;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& proj : recentProjects_) {
            arr.push_back(proj.toJson());
        }
        j["recentProjects"] = arr;

        std::ofstream fout(path);
        if (!fout.is_open()) return false;
        fout << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        LOG_WARN("LauncherState", "Failed to save launcher state: {}", e.what());
        return false;
    }
}

void LauncherState::addRecentProject(const ProjectInfo& project) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();

    // Remove existing entry with same path (case-insensitive on Windows)
    recentProjects_.erase(
        std::remove_if(recentProjects_.begin(), recentProjects_.end(),
            [&](const ProjectInfo& p) {
                return fs::path(p.path) == fs::path(project.path);
            }),
        recentProjects_.end()
    );

    // Insert at front with updated timestamp
    ProjectInfo entry = project;
    entry.lastOpened = timestamp;
    recentProjects_.insert(recentProjects_.begin(), entry);

    // Cap at 20 recent entries
    if (recentProjects_.size() > 20) {
        recentProjects_.resize(20);
    }
}

std::vector<ProjectInfo> LauncherState::getMergedProjects(const std::string& baseDir) const {
    // Discover all projects on disk
    auto discovered = ProjectInfo::discoverProjects(baseDir);

    // Build a set of discovered paths for quick lookup
    std::unordered_set<std::string> discoveredPaths;
    for (const auto& p : discovered) {
        discoveredPaths.insert(fs::path(p.path).string());
    }

    // Start with recent projects (preserving order), but only if they still exist
    std::vector<ProjectInfo> merged;
    std::unordered_set<std::string> addedPaths;

    for (const auto& recent : recentProjects_) {
        if (!fs::is_directory(recent.path)) continue;
        if (!ProjectInfo::isValidProject(recent.path)) continue;

        // Find matching discovered project to get latest disk info
        ProjectInfo entry = recent;
        for (const auto& disc : discovered) {
            if (fs::path(disc.path) == fs::path(recent.path)) {
                entry.hasGameDefinition = disc.hasGameDefinition;
                entry.hasCMakeLists     = disc.hasCMakeLists;
                entry.hasWorldDatabase  = disc.hasWorldDatabase;
                if (entry.name == fs::path(entry.path).filename().string()) {
                    entry.name = disc.name; // Prefer engine.json title
                }
                break;
            }
        }

        merged.push_back(entry);
        addedPaths.insert(fs::path(entry.path).string());
    }

    // Add any discovered projects not in the recent list
    for (const auto& disc : discovered) {
        auto canonical = fs::path(disc.path).string();
        if (addedPaths.count(canonical) == 0) {
            merged.push_back(disc);
            addedPaths.insert(canonical);
        }
    }

    return merged;
}

} // namespace Core
} // namespace Phyxel
