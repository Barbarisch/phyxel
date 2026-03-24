#include "ProjectLauncher.h"
#include "utils/Logger.h"
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <ctime>
#include <cstring>

namespace Phyxel {

// Reuse the same color palette as GameMenus.cpp
static const ImVec4 kColorTitle     = ImVec4(0.95f, 0.85f, 0.55f, 1.0f);  // warm gold
static const ImVec4 kColorButton    = ImVec4(0.25f, 0.55f, 0.35f, 1.0f);  // forest green
static const ImVec4 kColorButtonHov = ImVec4(0.30f, 0.65f, 0.40f, 1.0f);
static const ImVec4 kColorButtonAct = ImVec4(0.20f, 0.45f, 0.30f, 1.0f);
static const ImVec4 kColorSelected  = ImVec4(0.25f, 0.45f, 0.65f, 0.80f); // blue highlight
static const ImVec4 kColorHover     = ImVec4(0.20f, 0.35f, 0.50f, 0.40f);
static const ImVec4 kColorSubtext   = ImVec4(0.6f, 0.6f, 0.6f, 0.8f);
static const ImVec4 kColorError     = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);

static bool styledButton(const char* label, float width = 0.0f, float height = 0.0f) {
    ImGui::PushStyleColor(ImGuiCol_Button, kColorButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColorButtonHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kColorButtonAct);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    bool clicked = ImGui::Button(label, ImVec2(width, height));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return clicked;
}

static std::string formatTimestamp(int64_t timestamp) {
    if (timestamp <= 0) return "";
    std::time_t t = static_cast<std::time_t>(timestamp);
    std::tm tm_buf;
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_buf);
    return std::string(buf);
}

ProjectLauncher::ProjectLauncher() {
    std::memset(newProjectName_, 0, sizeof(newProjectName_));
    std::memset(browsePath_, 0, sizeof(browsePath_));
}

void ProjectLauncher::initialize(const std::string& baseDir) {
    baseDir_ = baseDir;
    state_.load(baseDir_);
    refresh();
}

void ProjectLauncher::refresh() {
    projects_ = state_.getMergedProjects(baseDir_);
    selectedIndex_ = -1;
}

bool ProjectLauncher::render(LauncherResult& result) {
    result.action = LauncherResult::Action::None;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // Full-screen darkened background
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(40.0f, 30.0f));
    if (!ImGui::Begin("##ProjectLauncher", nullptr, bgFlags)) {
        ImGui::End();
        ImGui::PopStyleVar();
        return false;
    }

    // ===== Title =====
    ImGui::PushStyleColor(ImGuiCol_Text, kColorTitle);
    ImGui::SetWindowFontScale(2.2f);
    ImGui::Text("Phyxel");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, kColorSubtext);
    ImGui::Text("Game Development Environment");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ===== Layout: left panel (project list) + right panel (actions) =====
    float panelWidth = displaySize.x - 80.0f; // account for window padding
    float listWidth  = panelWidth * 0.65f;
    float actionWidth = panelWidth * 0.30f;

    // ----- Project list -----
    ImGui::BeginChild("##ProjectList", ImVec2(listWidth, displaySize.y - 180.0f), true);

    ImGui::SetWindowFontScale(1.2f);
    ImGui::PushStyleColor(ImGuiCol_Text, kColorTitle);
    ImGui::Text("Projects");
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();

    if (projects_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kColorSubtext);
        ImGui::TextWrapped("No projects found in %s", baseDir_.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("Click \"New Project\" to create your first game project.");
        ImGui::PopStyleColor();
    } else {
        for (int i = 0; i < static_cast<int>(projects_.size()); i++) {
            const auto& proj = projects_[i];
            bool isSelected = (selectedIndex_ == i);

            ImGui::PushID(i);

            // Row background
            ImVec2 rowStart = ImGui::GetCursorScreenPos();
            float rowHeight = 58.0f;
            ImVec2 rowEnd(rowStart.x + listWidth - 20.0f, rowStart.y + rowHeight);

            bool hovered = ImGui::IsMouseHoveringRect(rowStart, rowEnd);

            if (isSelected) {
                ImGui::GetWindowDrawList()->AddRectFilled(rowStart, rowEnd,
                    ImGui::ColorConvertFloat4ToU32(kColorSelected), 4.0f);
            } else if (hovered) {
                ImGui::GetWindowDrawList()->AddRectFilled(rowStart, rowEnd,
                    ImGui::ColorConvertFloat4ToU32(kColorHover), 4.0f);
            }

            // Clickable area
            ImGui::InvisibleButton("##row", ImVec2(listWidth - 20.0f, rowHeight));
            if (ImGui::IsItemClicked()) {
                selectedIndex_ = i;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                // Double-click to open
                result.action = LauncherResult::Action::Open;
                result.projectPath = proj.path;
                ImGui::PopID();
                ImGui::EndChild();
                ImGui::End();
                ImGui::PopStyleVar();
                return true;
            }

            // Draw project info over the invisible button using DrawList
            // (avoids SetCursorScreenPos which triggers ImGui assertions)
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImFont* font = ImGui::GetFont();
            float baseFontSize = ImGui::GetFontSize();

            // Project name (slightly larger)
            dl->AddText(font, baseFontSize * 1.1f,
                ImVec2(rowStart.x + 12.0f, rowStart.y + 6.0f),
                ImGui::GetColorU32(ImGuiCol_Text), proj.name.c_str());

            // Path (subtext color)
            dl->AddText(font, baseFontSize,
                ImVec2(rowStart.x + 12.0f, rowStart.y + 30.0f),
                ImGui::ColorConvertFloat4ToU32(kColorSubtext), proj.path.c_str());

            // Badges + timestamp on right side
            std::string badges;
            if (proj.hasWorldDatabase)  badges += "[World] ";
            if (proj.hasGameDefinition) badges += "[Game Def] ";
            if (proj.hasCMakeLists)     badges += "[Standalone] ";

            std::string timestamp = formatTimestamp(proj.lastOpened);
            if (!timestamp.empty()) {
                badges += "  " + timestamp;
            }

            if (!badges.empty()) {
                float badgeWidth = ImGui::CalcTextSize(badges.c_str()).x;
                dl->AddText(font, baseFontSize,
                    ImVec2(rowEnd.x - badgeWidth - 10.0f, rowStart.y + 30.0f),
                    ImGui::ColorConvertFloat4ToU32(kColorSubtext), badges.c_str());
            }

            ImGui::PopID();
        }
    }

    ImGui::EndChild();

    // ----- Action panel (same line, to the right) -----
    ImGui::SameLine(listWidth + 50.0f);
    ImGui::BeginChild("##Actions", ImVec2(actionWidth, displaySize.y - 180.0f), false);

    ImGui::Spacing();
    ImGui::Spacing();

    float btnWidth = actionWidth - 20.0f;
    float btnHeight = 40.0f;

    // Open selected project
    bool hasSelection = (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(projects_.size()));
    if (!hasSelection) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
    }
    if (styledButton("Open Project", btnWidth, btnHeight) && hasSelection) {
        result.action = LauncherResult::Action::Open;
        result.projectPath = projects_[selectedIndex_].path;
        if (!hasSelection) ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::End();
        ImGui::PopStyleVar();
        return true;
    }
    if (!hasSelection) {
        ImGui::PopStyleVar();
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();

    // New project section
    if (!showNewProject_) {
        if (styledButton("New Project", btnWidth, btnHeight)) {
            showNewProject_ = true;
            showBrowse_ = false;
            errorMessage_.clear();
            std::memset(newProjectName_, 0, sizeof(newProjectName_));
        }
    } else {
        ImGui::SetWindowFontScale(1.1f);
        ImGui::Text("New Project");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Spacing();

        ImGui::Text("Project Name:");
        ImGui::SetNextItemWidth(btnWidth);
        bool enterPressed = ImGui::InputText("##NewName", newProjectName_, sizeof(newProjectName_),
                                              ImGuiInputTextFlags_EnterReturnsTrue);

        if (!errorMessage_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kColorError);
            ImGui::TextWrapped("%s", errorMessage_.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();

        if ((styledButton("Create", btnWidth * 0.48f, 35.0f) || enterPressed) && newProjectName_[0] != '\0') {
            // Validate name: alphanumeric, spaces, hyphens, underscores
            std::string name(newProjectName_);
            bool valid = !name.empty();
            for (char c : name) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != ' ' && c != '-' && c != '_') {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                errorMessage_ = "Name can only contain letters, numbers, spaces, hyphens, underscores.";
            } else if (std::filesystem::exists(std::filesystem::path(baseDir_) / name)) {
                errorMessage_ = "A project with this name already exists.";
            } else {
                result.action = LauncherResult::Action::Create;
                result.newProjectName = name;
                ImGui::EndChild();
                ImGui::End();
                ImGui::PopStyleVar();
                return true;
            }
        }

        ImGui::SameLine();
        if (styledButton("Cancel##new", btnWidth * 0.48f, 35.0f)) {
            showNewProject_ = false;
            errorMessage_.clear();
        }
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Spacing();

    // Browse for a project directory
    if (!showBrowse_) {
        if (styledButton("Browse...", btnWidth, btnHeight)) {
            showBrowse_ = true;
            showNewProject_ = false;
            errorMessage_.clear();
            std::memset(browsePath_, 0, sizeof(browsePath_));
        }
    } else {
        ImGui::Text("Project Path:");
        ImGui::SetNextItemWidth(btnWidth);
        bool enterPressed = ImGui::InputText("##BrowsePath", browsePath_, sizeof(browsePath_),
                                              ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();

        if ((styledButton("Open##browse", btnWidth * 0.48f, 35.0f) || enterPressed) && browsePath_[0] != '\0') {
            std::string path(browsePath_);
            if (!std::filesystem::is_directory(path)) {
                errorMessage_ = "Directory does not exist.";
            } else if (!Core::ProjectInfo::isValidProject(path)) {
                errorMessage_ = "Not a valid project (missing engine.json).";
            } else {
                result.action = LauncherResult::Action::Open;
                result.projectPath = path;
                ImGui::EndChild();
                ImGui::End();
                ImGui::PopStyleVar();
                return true;
            }
        }

        ImGui::SameLine();
        if (styledButton("Cancel##browse", btnWidth * 0.48f, 35.0f)) {
            showBrowse_ = false;
            errorMessage_.clear();
        }

        if (!errorMessage_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, kColorError);
            ImGui::TextWrapped("%s", errorMessage_.c_str());
            ImGui::PopStyleColor();
        }
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Refresh button
    if (styledButton("Refresh", btnWidth, 30.0f)) {
        refresh();
    }

    ImGui::EndChild();

    // ===== Footer (drawn via DrawList to avoid SetCursorPos assertion) =====
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();
        std::string footerText = "Projects directory: " + baseDir_;
        dl->AddText(font, fontSize,
            ImVec2(winPos.x + 40.0f, winPos.y + displaySize.y - 45.0f),
            ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.4f, 0.6f)),
            footerText.c_str());
    }

    ImGui::End();
    ImGui::PopStyleVar();
    return false;
}

} // namespace Phyxel
