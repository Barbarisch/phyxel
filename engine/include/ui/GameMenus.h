#pragma once

#include <string>
#include <functional>

namespace Phyxel {
namespace Core {
    class Inventory;
    class HealthComponent;
}
namespace UI {

/// Rendering callbacks for main menu buttons.
struct MainMenuActions {
    std::function<void()> onStartGame;
    std::function<void()> onQuit;
};

/// Rendering callbacks for pause menu buttons.
struct PauseMenuActions {
    std::function<void()> onResume;
    std::function<void()> onMainMenu;
    std::function<void()> onQuit;
};

/// Render the main menu / title screen (centered, full-screen overlay).
/// @param title     Game title displayed at the top.
/// @param actions   Button callbacks.
void renderMainMenu(const std::string& title, const MainMenuActions& actions);

/// Render the pause menu (centered overlay with darkened background).
/// @param actions   Button callbacks.
void renderPauseMenu(const PauseMenuActions& actions);

/// Render the in-game HUD:
///   - Health bar (top-left)
///   - Crosshair (screen center)
///   - Hotbar (bottom-center, 9 slots)
/// @param health    Player health component (can be null to skip health bar).
/// @param inventory Player inventory (can be null to skip hotbar).
void renderGameHUD(const Core::HealthComponent* health,
                   const Core::Inventory* inventory);

/// Render the inventory screen (grid of 36 slots + hotbar highlighted).
/// @param inventory Player inventory.
/// @param onClose   Called when the user presses the close button or ESC.
void renderInventoryScreen(Core::Inventory* inventory,
                           std::function<void()> onClose = nullptr);

} // namespace UI
} // namespace Phyxel
