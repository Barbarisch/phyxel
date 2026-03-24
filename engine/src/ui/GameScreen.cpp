#include "ui/GameScreen.h"

namespace Phyxel {
namespace UI {

void GameScreen::setState(ScreenState newState) {
    if (newState == m_state) return;
    ScreenState old = m_state;
    m_state = newState;
    if (m_onStateChange) m_onStateChange(old, newState);
}

void GameScreen::startGame() {
    if (m_state == ScreenState::MainMenu) {
        setState(ScreenState::Playing);
    }
}

void GameScreen::togglePause() {
    if (m_state == ScreenState::Playing) {
        setState(ScreenState::Paused);
    } else if (m_state == ScreenState::Paused) {
        setState(ScreenState::Playing);
    }
}

void GameScreen::toggleInventory() {
    if (m_state == ScreenState::Playing) {
        setState(ScreenState::Inventory);
    } else if (m_state == ScreenState::Inventory) {
        setState(ScreenState::Playing);
    }
}

void GameScreen::toggleSettings() {
    if (m_state == ScreenState::Paused) {
        m_settingsReturnState = ScreenState::Paused;
        setState(ScreenState::Settings);
    } else if (m_state == ScreenState::MainMenu) {
        m_settingsReturnState = ScreenState::MainMenu;
        setState(ScreenState::Settings);
    } else if (m_state == ScreenState::Settings) {
        setState(m_settingsReturnState);
    }
}

void GameScreen::enterKeybindingRebind() {
    if (m_state == ScreenState::Settings) {
        setState(ScreenState::KeybindingRebind);
    }
}

void GameScreen::returnToMainMenu() {
    setState(ScreenState::MainMenu);
}

void GameScreen::resume() {
    if (m_state == ScreenState::Paused || m_state == ScreenState::Inventory) {
        setState(ScreenState::Playing);
    }
}

void GameScreen::goBack() {
    switch (m_state) {
        case ScreenState::KeybindingRebind:
            setState(ScreenState::Settings);
            break;
        case ScreenState::Settings:
            setState(m_settingsReturnState);
            break;
        case ScreenState::Inventory:
        case ScreenState::Paused:
            setState(ScreenState::Playing);
            break;
        default:
            break;
    }
}

} // namespace UI
} // namespace Phyxel
