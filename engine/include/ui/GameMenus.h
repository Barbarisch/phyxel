#pragma once

#include <string>
#include <functional>
#include <vector>

namespace Phyxel {
namespace Core {
    class Inventory;
    class HealthComponent;
    struct GameSettings;
    struct Keybinding;
}
namespace UI {

/// Rendering callbacks for main menu buttons.
struct MainMenuActions {
    std::function<void()> onStartGame;
    std::function<void()> onSettings;
    std::function<void()> onQuit;
};

/// Rendering callbacks for pause menu buttons.
struct PauseMenuActions {
    std::function<void()> onResume;
    std::function<void()> onSettings;
    std::function<void()> onMainMenu;
    std::function<void()> onQuit;
};

/// Callbacks for when settings are changed in the UI.
struct SettingsCallbacks {
    std::function<void(int w, int h)>  onResolutionChanged;
    std::function<void(bool)>          onFullscreenChanged;
    std::function<void(int)>           onVSyncChanged;    // 0=Off, 1=On, 2=Adaptive
    std::function<void(float)>         onFovChanged;
    std::function<void(float)>         onMouseSensChanged;
    std::function<void(float)>         onMasterVolumeChanged;
    std::function<void(float)>         onMusicVolumeChanged;
    std::function<void(float)>         onSfxVolumeChanged;
    std::function<void()>              onKeybindingsPressed;
    std::function<void()>              onBack;
    std::function<void()>              onSave;
    std::function<void(const std::string& provider,
                       const std::string& model,
                       const std::string& apiKey)> onAISettingsChanged;
};

/// State for the keybinding rebind screen.
struct KeybindRebindState {
    int  selectedIndex = -1;  // which binding is being rebound (-1 = none)
    bool waitingForKey = false;
};

/// Render the main menu / title screen (centered, full-screen overlay).
void renderMainMenu(const std::string& title, const MainMenuActions& actions);

/// Render the pause menu (centered overlay with darkened background).
void renderPauseMenu(const PauseMenuActions& actions);

/// Render the in-game HUD (health bar, crosshair, hotbar).
void renderGameHUD(const Core::HealthComponent* health,
                   const Core::Inventory* inventory);

/// Render the inventory screen (grid of 36 slots + hotbar highlighted).
void renderInventoryScreen(Core::Inventory* inventory,
                           std::function<void()> onClose = nullptr);

/// Render the settings screen (graphics, audio, controls).
void renderSettingsScreen(Core::GameSettings& settings,
                          const SettingsCallbacks& callbacks);

/// Render the keybinding rebind screen.
/// Returns the GLFW key code captured when waitingForKey is true, or -1 if none.
int renderKeybindingScreen(std::vector<Core::Keybinding>& bindings,
                            KeybindRebindState& state,
                            std::function<void()> onBack = nullptr);

} // namespace UI
} // namespace Phyxel
