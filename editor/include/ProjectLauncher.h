#pragma once

#include "core/ProjectInfo.h"
#include "core/LauncherState.h"
#include <string>
#include <vector>

namespace Phyxel {

/// Result of the project launcher interaction.
struct LauncherResult {
    enum class Action { None, Open, Create, Cancel };

    Action      action          = Action::None;
    std::string projectPath;        // For Open: selected project path
    std::string newProjectName;     // For Create: user-entered name
};

/// Full-screen ImGui project launcher/picker.
/// Shown when editor starts without --project flag.
class ProjectLauncher {
public:
    ProjectLauncher();

    /// Initialize with project base directory. Loads state and discovers projects.
    void initialize(const std::string& baseDir);

    /// Render one frame of the launcher UI. Returns true when the user has made a selection.
    bool render(LauncherResult& result);

    /// Refresh the project list (re-scan disk).
    void refresh();

    /// Get the base directory being used.
    const std::string& getBaseDir() const { return baseDir_; }

    /// Get the launcher state (for persisting after selection).
    Core::LauncherState& getState() { return state_; }

private:
    std::string baseDir_;
    Core::LauncherState state_;
    std::vector<Core::ProjectInfo> projects_;

    // UI state
    int  selectedIndex_ = -1;
    bool showNewProject_ = false;
    char newProjectName_[256] = {};
    char browsePath_[512] = {};
    bool showBrowse_ = false;
    std::string errorMessage_;
};

} // namespace Phyxel
