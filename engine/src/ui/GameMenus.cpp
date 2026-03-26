#include "ui/GameMenus.h"
#include "core/Inventory.h"
#include "core/HealthComponent.h"
#include "core/GameSettings.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>

namespace Phyxel {
namespace UI {

// ============================================================================
// Color palette — earthy voxel game feel
// ============================================================================
static const ImVec4 kColorTitle     = ImVec4(0.95f, 0.85f, 0.55f, 1.0f);  // warm gold
static const ImVec4 kColorButton    = ImVec4(0.25f, 0.55f, 0.35f, 1.0f);  // forest green
static const ImVec4 kColorButtonHov = ImVec4(0.30f, 0.65f, 0.40f, 1.0f);
static const ImVec4 kColorButtonAct = ImVec4(0.20f, 0.45f, 0.30f, 1.0f);
static const ImVec4 kColorHealthBg  = ImVec4(0.15f, 0.15f, 0.15f, 0.80f);
static const ImVec4 kColorHealthBar = ImVec4(0.85f, 0.20f, 0.20f, 1.0f);
static const ImVec4 kColorHealthOk  = ImVec4(0.20f, 0.75f, 0.25f, 1.0f);
static const ImVec4 kColorHotbarBg  = ImVec4(0.12f, 0.12f, 0.12f, 0.85f);
static const ImVec4 kColorSlotEmpty = ImVec4(0.25f, 0.25f, 0.25f, 0.60f);
static const ImVec4 kColorSlotSel   = ImVec4(0.95f, 0.85f, 0.55f, 0.90f);
static const ImVec4 kColorOverlayBg = ImVec4(0.0f, 0.0f, 0.0f, 0.60f);

// Material name → display color (approximate tint from CLAUDE.md material table)
static ImVec4 getMaterialColor(const std::string& mat) {
    if (mat == "Wood")    return ImVec4(0.80f, 0.60f, 0.30f, 1.0f);
    if (mat == "Metal")   return ImVec4(0.70f, 0.70f, 0.80f, 1.0f);
    if (mat == "Glass")   return ImVec4(0.90f, 0.95f, 1.00f, 1.0f);
    if (mat == "Rubber")  return ImVec4(0.30f, 0.30f, 0.30f, 1.0f);
    if (mat == "Stone")   return ImVec4(0.60f, 0.50f, 0.40f, 1.0f);
    if (mat == "Ice")     return ImVec4(0.80f, 0.90f, 1.00f, 1.0f);
    if (mat == "Cork")    return ImVec4(0.80f, 0.70f, 0.50f, 1.0f);
    if (mat == "glow")    return ImVec4(1.00f, 1.00f, 0.80f, 1.0f);
    return ImVec4(0.90f, 0.90f, 0.90f, 1.0f); // Default
}

// Helper: styled centered button
static bool centeredButton(const char* label, float width, float height) {
    ImVec2 windowSize = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPosX((windowSize.x - width) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Button, kColorButton);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColorButtonHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kColorButtonAct);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    bool clicked = ImGui::Button(label, ImVec2(width, height));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    return clicked;
}

// ============================================================================
// MAIN MENU
// ============================================================================

void renderMainMenu(const std::string& title, const MainMenuActions& actions) {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // Full-screen darkened background
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("##MainMenuBg", nullptr, bgFlags)) {
        // Title — centered, large
        float titleY = displaySize.y * 0.20f;
        ImGui::SetCursorPos(ImVec2(0, titleY));

        // Scale font for title (use default font scaled 2x)
        ImGui::PushStyleColor(ImGuiCol_Text, kColorTitle);
        float titleWidth = ImGui::CalcTextSize(title.c_str()).x * 2.0f;
        ImGui::SetCursorPosX((displaySize.x - titleWidth) * 0.5f);
        ImGui::SetWindowFontScale(2.0f);
        ImGui::Text("%s", title.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        // Subtitle
        const char* subtitle = "A Voxel Game Engine";
        float subtitleWidth = ImGui::CalcTextSize(subtitle).x;
        ImGui::SetCursorPosX((displaySize.x - subtitleWidth) * 0.5f);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 0.8f));
        ImGui::Text("%s", subtitle);
        ImGui::PopStyleColor();

        // Buttons
        float btnWidth = 220.0f;
        float btnHeight = 45.0f;
        float btnStartY = displaySize.y * 0.48f;

        ImGui::SetCursorPos(ImVec2(0, btnStartY));
        if (centeredButton("Start Game", btnWidth, btnHeight)) {
            if (actions.onStartGame) actions.onStartGame();
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0f);
        if (centeredButton("Settings", btnWidth, btnHeight)) {
            if (actions.onSettings) actions.onSettings();
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0f);
        if (centeredButton("Quit", btnWidth, btnHeight)) {
            if (actions.onQuit) actions.onQuit();
        }

        // Version / footer
        const char* version = "Phyxel Engine v0.1";
        float versionWidth = ImGui::CalcTextSize(version).x;
        ImGui::SetCursorPos(ImVec2((displaySize.x - versionWidth) * 0.5f, displaySize.y - 40.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 0.6f));
        ImGui::Text("%s", version);
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ============================================================================
// PAUSE MENU
// ============================================================================

void renderPauseMenu(const PauseMenuActions& actions) {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // Semi-transparent full-screen overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);
    ImGui::SetNextWindowBgAlpha(0.50f);
    ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("##PauseMenuBg", nullptr, bgFlags)) {
        // "PAUSED" header
        float headerY = displaySize.y * 0.28f;
        ImGui::SetCursorPos(ImVec2(0, headerY));
        ImGui::PushStyleColor(ImGuiCol_Text, kColorTitle);
        ImGui::SetWindowFontScale(1.8f);
        const char* pauseText = "PAUSED";
        float textW = ImGui::CalcTextSize(pauseText).x;
        ImGui::SetCursorPosX((displaySize.x - textW) * 0.5f);
        ImGui::Text("%s", pauseText);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        // Buttons
        float btnWidth = 200.0f;
        float btnHeight = 40.0f;
        float btnStartY = displaySize.y * 0.42f;

        ImGui::SetCursorPos(ImVec2(0, btnStartY));
        if (centeredButton("Resume", btnWidth, btnHeight)) {
            if (actions.onResume) actions.onResume();
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
        if (centeredButton("Settings", btnWidth, btnHeight)) {
            if (actions.onSettings) actions.onSettings();
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
        if (centeredButton("Main Menu", btnWidth, btnHeight)) {
            if (actions.onMainMenu) actions.onMainMenu();
        }

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
        if (centeredButton("Quit", btnWidth, btnHeight)) {
            if (actions.onQuit) actions.onQuit();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ============================================================================
// GAME HUD
// ============================================================================

void renderGameHUD(const Core::HealthComponent* health,
                   const Core::Inventory* inventory) {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGuiWindowFlags hudFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground |
                                 ImGuiWindowFlags_NoInputs;

    // --- Crosshair (screen center) ---
    {
        float cx = displaySize.x * 0.5f;
        float cy = displaySize.y * 0.5f;
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        const float size = 10.0f;
        const float thickness = 2.0f;
        ImU32 color = IM_COL32(255, 255, 255, 180);
        drawList->AddLine(ImVec2(cx - size, cy), ImVec2(cx + size, cy), color, thickness);
        drawList->AddLine(ImVec2(cx, cy - size), ImVec2(cx, cy + size), color, thickness);
    }

    // --- Health bar (top-left) ---
    if (health) {
        float barWidth = 200.0f;
        float barHeight = 18.0f;
        float padding = 12.0f;
        float x = padding;
        float y = padding;

        ImGui::SetNextWindowPos(ImVec2(x, y));
        ImGui::SetNextWindowSize(ImVec2(barWidth + 20.0f, barHeight + 30.0f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        if (ImGui::Begin("##HealthHUD", nullptr, hudFlags)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 wp = ImGui::GetWindowPos();

            // Background
            dl->AddRectFilled(ImVec2(wp.x, wp.y),
                              ImVec2(wp.x + barWidth, wp.y + barHeight),
                              ImGui::ColorConvertFloat4ToU32(kColorHealthBg), 3.0f);

            // Health fill
            float pct = health->getHealthPercent();
            ImVec4 barColor = pct > 0.5f ? kColorHealthOk : kColorHealthBar;
            if (pct > 0.25f && pct <= 0.5f) {
                barColor = ImVec4(0.9f, 0.7f, 0.1f, 1.0f); // yellow warning
            }
            dl->AddRectFilled(ImVec2(wp.x + 1, wp.y + 1),
                              ImVec2(wp.x + 1 + (barWidth - 2) * pct, wp.y + barHeight - 1),
                              ImGui::ColorConvertFloat4ToU32(barColor), 2.0f);

            // Text overlay
            char hpText[32];
            snprintf(hpText, sizeof(hpText), "%.0f / %.0f",
                     health->getHealth(), health->getMaxHealth());
            ImVec2 textSize = ImGui::CalcTextSize(hpText);
            dl->AddText(ImVec2(wp.x + (barWidth - textSize.x) * 0.5f,
                                wp.y + (barHeight - textSize.y) * 0.5f),
                        IM_COL32(255, 255, 255, 220), hpText);
        }
        ImGui::End();
    }

    // --- Hotbar (bottom-center, 9 slots) ---
    if (inventory) {
        float slotSize = 48.0f;
        float slotPadding = 4.0f;
        float totalWidth = Core::Inventory::HOTBAR_SIZE * (slotSize + slotPadding) - slotPadding;
        float barX = (displaySize.x - totalWidth) * 0.5f;
        float barY = displaySize.y - slotSize - 20.0f;

        ImGui::SetNextWindowPos(ImVec2(barX - 8.0f, barY - 8.0f));
        ImGui::SetNextWindowSize(ImVec2(totalWidth + 16.0f, slotSize + 30.0f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        if (ImGui::Begin("##HotbarHUD", nullptr, hudFlags)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            int selected = inventory->getSelectedSlot();

            for (int i = 0; i < Core::Inventory::HOTBAR_SIZE; ++i) {
                float sx = barX + i * (slotSize + slotPadding);
                float sy = barY;
                ImVec2 p0(sx, sy);
                ImVec2 p1(sx + slotSize, sy + slotSize);

                // Slot background
                dl->AddRectFilled(p0, p1,
                    ImGui::ColorConvertFloat4ToU32(kColorHotbarBg), 4.0f);

                // Selection highlight
                if (i == selected) {
                    dl->AddRect(p0, p1,
                        ImGui::ColorConvertFloat4ToU32(kColorSlotSel), 4.0f, 0, 2.5f);
                }

                // Slot content
                auto slot = inventory->getSlot(i);
                if (slot.has_value()) {
                    // Material color swatch
                    ImVec4 matColor = getMaterialColor(slot->material);
                    float inset = 6.0f;
                    dl->AddRectFilled(
                        ImVec2(p0.x + inset, p0.y + inset),
                        ImVec2(p1.x - inset, p1.y - inset),
                        ImGui::ColorConvertFloat4ToU32(matColor), 3.0f);

                    // Material name (abbreviated)
                    const char* name = slot->material.c_str();
                    char abbr[5];
                    snprintf(abbr, sizeof(abbr), "%.3s", name);
                    ImVec2 ts = ImGui::CalcTextSize(abbr);
                    dl->AddText(ImVec2(sx + (slotSize - ts.x) * 0.5f,
                                       sy + 4.0f),
                                IM_COL32(255, 255, 255, 200), abbr);

                    // Count
                    if (slot->count > 1) {
                        char countStr[8];
                        snprintf(countStr, sizeof(countStr), "%d", slot->count);
                        ImVec2 cs = ImGui::CalcTextSize(countStr);
                        dl->AddText(ImVec2(p1.x - cs.x - 3.0f, p1.y - cs.y - 2.0f),
                                    IM_COL32(255, 255, 255, 230), countStr);
                    }
                } else {
                    // Empty slot marker
                    dl->AddRectFilled(
                        ImVec2(p0.x + 12.0f, p0.y + 12.0f),
                        ImVec2(p1.x - 12.0f, p1.y - 12.0f),
                        ImGui::ColorConvertFloat4ToU32(kColorSlotEmpty), 2.0f);
                }

                // Slot number (1-9)
                char numStr[4];
                snprintf(numStr, sizeof(numStr), "%d", i + 1);
                dl->AddText(ImVec2(sx + 3.0f, sy + slotSize - 14.0f),
                            IM_COL32(200, 200, 200, 120), numStr);
            }
        }
        ImGui::End();
    }
}

// ============================================================================
// INVENTORY SCREEN
// ============================================================================

void renderInventoryScreen(Core::Inventory* inventory,
                           std::function<void()> onClose) {
    if (!inventory) return;

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // Darkened background overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);
    ImGui::SetNextWindowBgAlpha(0.45f);
    ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoInputs;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##InvOverlay", nullptr, bgFlags);
    ImGui::End();
    ImGui::PopStyleVar();

    // Inventory window (centered)
    float slotSize = 52.0f;
    float slotPad = 4.0f;
    int cols = 9;
    int rows = 4;  // 36 slots / 9
    float gridWidth = cols * (slotSize + slotPad) - slotPad;
    float gridHeight = rows * (slotSize + slotPad) - slotPad;
    float windowWidth = gridWidth + 40.0f;
    float windowHeight = gridHeight + 80.0f;  // extra for title + close btn

    ImGui::SetNextWindowPos(ImVec2((displaySize.x - windowWidth) * 0.5f,
                                    (displaySize.y - windowHeight) * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGuiWindowFlags invFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    if (ImGui::Begin("Inventory", nullptr, invFlags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetCursorScreenPos();
        int selected = inventory->getSelectedSlot();

        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                int idx = row * cols + col;
                float sx = wp.x + col * (slotSize + slotPad);
                float sy = wp.y + row * (slotSize + slotPad);
                ImVec2 p0(sx, sy);
                ImVec2 p1(sx + slotSize, sy + slotSize);

                // Background
                bool isHotbar = (idx < Core::Inventory::HOTBAR_SIZE);
                ImVec4 bg = isHotbar ? ImVec4(0.18f, 0.22f, 0.18f, 0.90f)
                                     : ImVec4(0.15f, 0.15f, 0.15f, 0.85f);
                dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(bg), 4.0f);

                // Selection border for current hotbar slot
                if (idx == selected) {
                    dl->AddRect(p0, p1,
                        ImGui::ColorConvertFloat4ToU32(kColorSlotSel), 4.0f, 0, 2.5f);
                }

                // Slot content
                auto slot = inventory->getSlot(idx);
                if (slot.has_value()) {
                    ImVec4 matColor = getMaterialColor(slot->material);
                    float inset = 6.0f;
                    dl->AddRectFilled(
                        ImVec2(p0.x + inset, p0.y + inset),
                        ImVec2(p1.x - inset, p1.y - inset),
                        ImGui::ColorConvertFloat4ToU32(matColor), 3.0f);

                    // Material name
                    const char* name = slot->material.c_str();
                    ImVec2 ts = ImGui::CalcTextSize(name);
                    if (ts.x > slotSize - 4.0f) {
                        char abbr[6];
                        snprintf(abbr, sizeof(abbr), "%.4s", name);
                        ts = ImGui::CalcTextSize(abbr);
                        dl->AddText(ImVec2(sx + (slotSize - ts.x) * 0.5f, sy + 4.0f),
                                    IM_COL32(255, 255, 255, 200), abbr);
                    } else {
                        dl->AddText(ImVec2(sx + (slotSize - ts.x) * 0.5f, sy + 4.0f),
                                    IM_COL32(255, 255, 255, 200), name);
                    }

                    // Count
                    if (slot->count > 1) {
                        char countStr[8];
                        snprintf(countStr, sizeof(countStr), "%d", slot->count);
                        ImVec2 cs = ImGui::CalcTextSize(countStr);
                        dl->AddText(ImVec2(p1.x - cs.x - 3.0f, p1.y - cs.y - 2.0f),
                                    IM_COL32(255, 255, 255, 230), countStr);
                    }
                } else {
                    dl->AddRectFilled(
                        ImVec2(p0.x + 14.0f, p0.y + 14.0f),
                        ImVec2(p1.x - 14.0f, p1.y - 14.0f),
                        ImGui::ColorConvertFloat4ToU32(kColorSlotEmpty), 2.0f);
                }
            }
        }

        // Move cursor past the grid
        ImGui::Dummy(ImVec2(gridWidth, gridHeight + 8.0f));

        // Close button
        if (centeredButton("Close [Tab]", 120.0f, 30.0f)) {
            if (onClose) onClose();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ============================================================================
// SETTINGS SCREEN
// ============================================================================

// Common resolutions for the dropdown
static const int kResolutions[][2] = {
    {1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080},
    {2560, 1440}, {3840, 2160}
};
static const int kResCount = sizeof(kResolutions) / sizeof(kResolutions[0]);

void renderSettingsScreen(Core::GameSettings& settings,
                          const SettingsCallbacks& callbacks) {
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // Full-screen darkened overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##SettingsOverlay", nullptr, bgFlags);
    ImGui::End();
    ImGui::PopStyleVar();

    // Settings panel (centered, scrollable)
    float panelW = 520.0f;
    float panelH = displaySize.y * 0.75f;
    ImGui::SetNextWindowPos(ImVec2((displaySize.x - panelW) * 0.5f,
                                    (displaySize.y - panelH) * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    if (ImGui::Begin("Settings", nullptr, panelFlags)) {

        // === DISPLAY ===
        ImGui::PushStyleColor(ImGuiCol_Text, kColorTitle);
        ImGui::SetWindowFontScale(1.2f);
        ImGui::Text("Display");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        // Resolution combo
        {
            char currentRes[32];
            snprintf(currentRes, sizeof(currentRes), "%dx%d",
                     settings.resolutionWidth, settings.resolutionHeight);
            if (ImGui::BeginCombo("Resolution", currentRes)) {
                for (int i = 0; i < kResCount; ++i) {
                    char label[32];
                    snprintf(label, sizeof(label), "%dx%d",
                             kResolutions[i][0], kResolutions[i][1]);
                    bool selected = (settings.resolutionWidth == kResolutions[i][0] &&
                                     settings.resolutionHeight == kResolutions[i][1]);
                    if (ImGui::Selectable(label, selected)) {
                        settings.resolutionWidth = kResolutions[i][0];
                        settings.resolutionHeight = kResolutions[i][1];
                        if (callbacks.onResolutionChanged)
                            callbacks.onResolutionChanged(kResolutions[i][0], kResolutions[i][1]);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }

        // Fullscreen
        if (ImGui::Checkbox("Fullscreen", &settings.fullscreen)) {
            if (callbacks.onFullscreenChanged)
                callbacks.onFullscreenChanged(settings.fullscreen);
        }

        // VSync
        {
            const char* vsyncLabels[] = { "Off", "On", "Adaptive" };
            int vsyncIdx = static_cast<int>(settings.vsync);
            if (ImGui::Combo("VSync", &vsyncIdx, vsyncLabels, 3)) {
                settings.vsync = static_cast<Core::VSyncMode>(vsyncIdx);
                if (callbacks.onVSyncChanged)
                    callbacks.onVSyncChanged(vsyncIdx);
            }
        }

        // FOV
        if (ImGui::SliderFloat("Field of View", &settings.fov, 30.0f, 120.0f, "%.0f")) {
            if (callbacks.onFovChanged)
                callbacks.onFovChanged(settings.fov);
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // === AUDIO ===
        ImGui::PushStyleColor(ImGuiCol_Text, kColorTitle);
        ImGui::SetWindowFontScale(1.2f);
        ImGui::Text("Audio");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::SliderFloat("Master Volume", &settings.masterVolume, 0.0f, 1.0f, "%.0f%%")) {
            if (callbacks.onMasterVolumeChanged)
                callbacks.onMasterVolumeChanged(settings.masterVolume);
        }
        if (ImGui::SliderFloat("Music Volume", &settings.musicVolume, 0.0f, 1.0f, "%.0f%%")) {
            if (callbacks.onMusicVolumeChanged)
                callbacks.onMusicVolumeChanged(settings.musicVolume);
        }
        if (ImGui::SliderFloat("SFX Volume", &settings.sfxVolume, 0.0f, 1.0f, "%.0f%%")) {
            if (callbacks.onSfxVolumeChanged)
                callbacks.onSfxVolumeChanged(settings.sfxVolume);
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // === CONTROLS ===
        ImGui::PushStyleColor(ImGuiCol_Text, kColorTitle);
        ImGui::SetWindowFontScale(1.2f);
        ImGui::Text("Controls");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::SliderFloat("Mouse Sensitivity", &settings.mouseSensitivity, 0.01f, 1.0f, "%.2f")) {
            if (callbacks.onMouseSensChanged)
                callbacks.onMouseSensChanged(settings.mouseSensitivity);
        }
        ImGui::Checkbox("Invert Y Axis", &settings.invertY);

        ImGui::Spacing();
        if (centeredButton("Keybindings...", 200.0f, 35.0f)) {
            if (callbacks.onKeybindingsPressed)
                callbacks.onKeybindingsPressed();
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // === AI ===
        ImGui::PushStyleColor(ImGuiCol_Text, kColorTitle);
        ImGui::SetWindowFontScale(1.2f);
        ImGui::Text("AI");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        // Provider dropdown
        {
            const char* providers[] = { "anthropic", "openai", "ollama" };
            int providerIdx = 0;
            for (int i = 0; i < 3; ++i) {
                if (settings.aiProvider == providers[i]) { providerIdx = i; break; }
            }
            if (ImGui::Combo("AI Provider", &providerIdx, providers, 3)) {
                settings.aiProvider = providers[providerIdx];
                if (callbacks.onAISettingsChanged)
                    callbacks.onAISettingsChanged(settings.aiProvider, settings.aiModel, settings.aiApiKey);
            }
        }

        // Model name
        {
            static char modelBuf[256] = {};
            static bool modelInited = false;
            if (!modelInited) {
                snprintf(modelBuf, sizeof(modelBuf), "%s", settings.aiModel.c_str());
                modelInited = true;
            }
            if (ImGui::InputText("AI Model", modelBuf, sizeof(modelBuf))) {
                settings.aiModel = modelBuf;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (callbacks.onAISettingsChanged)
                    callbacks.onAISettingsChanged(settings.aiProvider, settings.aiModel, settings.aiApiKey);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Leave empty for provider default.\nExamples: claude-sonnet-4-20250514, gpt-4o, llama3.2");

        // API Key (masked input)
        {
            static char keyBuf[256] = {};
            static bool keyInited = false;
            if (!keyInited) {
                snprintf(keyBuf, sizeof(keyBuf), "%s", settings.aiApiKey.c_str());
                keyInited = true;
            }
            if (ImGui::InputText("API Key", keyBuf, sizeof(keyBuf), ImGuiInputTextFlags_Password)) {
                settings.aiApiKey = keyBuf;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (callbacks.onAISettingsChanged)
                    callbacks.onAISettingsChanged(settings.aiProvider, settings.aiModel, settings.aiApiKey);
            }
        }

        // Status indicator
        {
            bool hasKey = !settings.aiApiKey.empty();
            ImVec4 statusColor = hasKey ? ImVec4(0.3f, 0.9f, 0.3f, 1.0f) : ImVec4(0.9f, 0.5f, 0.2f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, statusColor);
            ImGui::Text(hasKey ? "API key configured" : "No API key (set PHYXEL_AI_API_KEY env var or enter above)");
            ImGui::PopStyleColor();
        }

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Bottom buttons
        float bw = 120.0f;
        float totalBtnW = bw * 2 + 20.0f;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - totalBtnW) * 0.5f);

        ImGui::PushStyleColor(ImGuiCol_Button, kColorButton);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColorButtonHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, kColorButtonAct);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

        if (ImGui::Button("Save", ImVec2(bw, 35.0f))) {
            if (callbacks.onSave) callbacks.onSave();
        }
        ImGui::SameLine(0, 20.0f);
        if (ImGui::Button("Back", ImVec2(bw, 35.0f))) {
            if (callbacks.onBack) callbacks.onBack();
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ============================================================================
// KEYBINDING REBIND SCREEN
// ============================================================================

int renderKeybindingScreen(std::vector<Core::Keybinding>& bindings,
                            KeybindRebindState& state,
                            std::function<void()> onBack) {
    int capturedKey = -1;
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;

    // Overlay
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(displaySize);
    ImGui::SetNextWindowBgAlpha(0.75f);
    ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoCollapse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##KBOverlay", nullptr, bgFlags);
    ImGui::End();
    ImGui::PopStyleVar();

    // Keybinding panel
    float panelW = 480.0f;
    float panelH = displaySize.y * 0.70f;
    ImGui::SetNextWindowPos(ImVec2((displaySize.x - panelW) * 0.5f,
                                    (displaySize.y - panelH) * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGuiWindowFlags panelFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    if (ImGui::Begin("Keybindings", nullptr, panelFlags)) {

        ImGui::PushStyleColor(ImGuiCol_Text, kColorTitle);
        ImGui::Text("Click a binding to rebind it, then press any key.");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();

        // Scrollable binding list
        ImGui::BeginChild("##BindingList", ImVec2(0, -45), true);
        for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
            auto& kb = bindings[i];
            ImGui::PushID(i);

            // Action name (left column)
            ImGui::Text("%s", kb.action.c_str());
            ImGui::SameLine(250.0f);

            // Key display (right column — clickable button)
            std::string keyLabel;
            if (state.waitingForKey && state.selectedIndex == i) {
                keyLabel = "[Press a key...]";
            } else {
                if (kb.modifiers != 0)
                    keyLabel = Core::modifiersToString(kb.modifiers) + "+" + Core::keyToString(kb.key);
                else
                    keyLabel = Core::keyToString(kb.key);
            }

            bool isSelected = (state.selectedIndex == i && state.waitingForKey);
            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.5f, 0.1f, 0.8f));
            }

            if (ImGui::Button(keyLabel.c_str(), ImVec2(180.0f, 0))) {
                state.selectedIndex = i;
                state.waitingForKey = true;
            }

            if (isSelected) {
                ImGui::PopStyleColor();
            }

            ImGui::PopID();
        }
        ImGui::EndChild();

        // If waiting for key, check for key press
        if (state.waitingForKey && state.selectedIndex >= 0) {
            for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST; ++k) {
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(k), false)) {
                    capturedKey = k;
                    bindings[state.selectedIndex].key = k;
                    bindings[state.selectedIndex].modifiers = 0; // reset mods on rebind
                    state.waitingForKey = false;
                    state.selectedIndex = -1;
                    break;
                }
            }
        }

        // Back button
        ImGui::Spacing();
        if (centeredButton("Back", 120.0f, 30.0f)) {
            state.waitingForKey = false;
            state.selectedIndex = -1;
            if (onBack) onBack();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    return capturedKey;
}

} // namespace UI
} // namespace Phyxel
