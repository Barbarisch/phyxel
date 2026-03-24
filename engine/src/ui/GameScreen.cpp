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

void GameScreen::returnToMainMenu() {
    setState(ScreenState::MainMenu);
}

void GameScreen::resume() {
    if (m_state == ScreenState::Paused || m_state == ScreenState::Inventory) {
        setState(ScreenState::Playing);
    }
}

} // namespace UI
} // namespace Phyxel
