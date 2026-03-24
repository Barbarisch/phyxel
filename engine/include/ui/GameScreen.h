#pragma once

#include <string>
#include <functional>

namespace Phyxel {
namespace UI {

/// Game screen states — controls which UI is shown and what input mode is active.
enum class ScreenState {
    MainMenu,   // Title screen — mouse free, game logic paused
    Playing,    // Normal gameplay — mouse captured, game running
    Paused,     // Pause overlay — mouse free, game logic paused
    Inventory   // Inventory screen — mouse free, game logic paused
};

/// Returns true if the game world simulation should tick in this state.
inline bool isGameRunning(ScreenState state) {
    return state == ScreenState::Playing;
}

/// Returns true if the mouse cursor should be free (not captured for camera look).
inline bool isMouseFree(ScreenState state) {
    return state != ScreenState::Playing;
}

/// Manages game screen state transitions.
class GameScreen {
public:
    using StateChangeCallback = std::function<void(ScreenState oldState, ScreenState newState)>;

    GameScreen() = default;

    ScreenState getState() const { return m_state; }

    /// Transition to a new state. Calls the state change callback if set.
    void setState(ScreenState newState);

    /// Start a new game (MainMenu → Playing).
    void startGame();

    /// Toggle pause (Playing ↔ Paused).
    void togglePause();

    /// Open/close inventory (Playing ↔ Inventory).
    void toggleInventory();

    /// Return to main menu from any state.
    void returnToMainMenu();

    /// Resume gameplay from Paused or Inventory.
    void resume();

    /// Set callback fired on every state change.
    void setOnStateChange(StateChangeCallback cb) { m_onStateChange = std::move(cb); }

private:
    ScreenState m_state = ScreenState::MainMenu;
    StateChangeCallback m_onStateChange;
};

} // namespace UI
} // namespace Phyxel
