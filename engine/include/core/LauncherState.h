#pragma once

#include "core/ProjectInfo.h"
#include <string>
#include <vector>

namespace Phyxel {
namespace Core {

/// Persistent state for the project launcher (recent projects, preferences).
/// Stored in the PhyxelProjects base directory as .launcher_state.json.
class LauncherState {
public:
    /// Load state from disk. Returns false only on parse error (missing file = OK).
    bool load(const std::string& baseDir);

    /// Save state to disk.
    bool save(const std::string& baseDir) const;

    /// Record a project as recently opened (upserts with current timestamp).
    void addRecentProject(const ProjectInfo& project);

    /// Get recent projects sorted by lastOpened descending.
    /// Merges with discovered projects and prunes entries whose paths no longer exist.
    std::vector<ProjectInfo> getMergedProjects(const std::string& baseDir) const;

    /// Get just the raw recent entries (for serialization).
    const std::vector<ProjectInfo>& getRecentEntries() const { return recentProjects_; }

private:
    std::vector<ProjectInfo> recentProjects_;

    static std::string stateFilePath(const std::string& baseDir);
};

} // namespace Core
} // namespace Phyxel
