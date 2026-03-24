#include <gtest/gtest.h>
#include "ui/GameScreen.h"

using namespace Phyxel::UI;

TEST(GameScreenTest, DefaultState) {
    GameScreen screen;
    EXPECT_EQ(screen.getState(), ScreenState::MainMenu);
}

TEST(GameScreenTest, StartGame) {
    GameScreen screen;
    screen.startGame();
    EXPECT_EQ(screen.getState(), ScreenState::Playing);
}

TEST(GameScreenTest, StartGameOnlyFromMainMenu) {
    GameScreen screen;
    screen.startGame(); // MainMenu → Playing
    screen.startGame(); // Playing → no change
    EXPECT_EQ(screen.getState(), ScreenState::Playing);
}

TEST(GameScreenTest, TogglePause) {
    GameScreen screen;
    screen.startGame();
    screen.togglePause();
    EXPECT_EQ(screen.getState(), ScreenState::Paused);
    screen.togglePause();
    EXPECT_EQ(screen.getState(), ScreenState::Playing);
}

TEST(GameScreenTest, TogglePauseIgnoredInMainMenu) {
    GameScreen screen;
    screen.togglePause();
    EXPECT_EQ(screen.getState(), ScreenState::MainMenu);
}

TEST(GameScreenTest, ToggleInventory) {
    GameScreen screen;
    screen.startGame();
    screen.toggleInventory();
    EXPECT_EQ(screen.getState(), ScreenState::Inventory);
    screen.toggleInventory();
    EXPECT_EQ(screen.getState(), ScreenState::Playing);
}

TEST(GameScreenTest, ToggleInventoryIgnoredInMainMenu) {
    GameScreen screen;
    screen.toggleInventory();
    EXPECT_EQ(screen.getState(), ScreenState::MainMenu);
}

TEST(GameScreenTest, Resume) {
    GameScreen screen;
    screen.startGame();
    screen.togglePause();
    screen.resume();
    EXPECT_EQ(screen.getState(), ScreenState::Playing);
}

TEST(GameScreenTest, ResumeFromInventory) {
    GameScreen screen;
    screen.startGame();
    screen.toggleInventory();
    screen.resume();
    EXPECT_EQ(screen.getState(), ScreenState::Playing);
}

TEST(GameScreenTest, ResumeIgnoredInMainMenu) {
    GameScreen screen;
    screen.resume();
    EXPECT_EQ(screen.getState(), ScreenState::MainMenu);
}

TEST(GameScreenTest, ReturnToMainMenu) {
    GameScreen screen;
    screen.startGame();
    screen.returnToMainMenu();
    EXPECT_EQ(screen.getState(), ScreenState::MainMenu);
}

TEST(GameScreenTest, ReturnToMainMenuFromPaused) {
    GameScreen screen;
    screen.startGame();
    screen.togglePause();
    screen.returnToMainMenu();
    EXPECT_EQ(screen.getState(), ScreenState::MainMenu);
}

TEST(GameScreenTest, IsGameRunning) {
    EXPECT_TRUE(isGameRunning(ScreenState::Playing));
    EXPECT_FALSE(isGameRunning(ScreenState::MainMenu));
    EXPECT_FALSE(isGameRunning(ScreenState::Paused));
    EXPECT_FALSE(isGameRunning(ScreenState::Inventory));
}

TEST(GameScreenTest, IsMouseFree) {
    EXPECT_FALSE(isMouseFree(ScreenState::Playing));
    EXPECT_TRUE(isMouseFree(ScreenState::MainMenu));
    EXPECT_TRUE(isMouseFree(ScreenState::Paused));
    EXPECT_TRUE(isMouseFree(ScreenState::Inventory));
}

TEST(GameScreenTest, StateChangeCallback) {
    GameScreen screen;
    ScreenState capturedOld = ScreenState::Playing;
    ScreenState capturedNew = ScreenState::Playing;
    int callCount = 0;

    screen.setOnStateChange([&](ScreenState oldS, ScreenState newS) {
        capturedOld = oldS;
        capturedNew = newS;
        callCount++;
    });

    screen.startGame();
    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(capturedOld, ScreenState::MainMenu);
    EXPECT_EQ(capturedNew, ScreenState::Playing);
}

TEST(GameScreenTest, CallbackNotFiredOnSameState) {
    GameScreen screen;
    int callCount = 0;
    screen.setOnStateChange([&](ScreenState, ScreenState) { callCount++; });
    screen.setState(ScreenState::MainMenu); // already MainMenu
    EXPECT_EQ(callCount, 0);
}

TEST(GameScreenTest, SetStateDirectly) {
    GameScreen screen;
    screen.setState(ScreenState::Playing);
    EXPECT_EQ(screen.getState(), ScreenState::Playing);
    screen.setState(ScreenState::Inventory);
    EXPECT_EQ(screen.getState(), ScreenState::Inventory);
}

TEST(GameScreenTest, FullCycleWorkflow) {
    GameScreen screen;
    // MainMenu → Playing → Paused → Playing → Inventory → Playing → MainMenu
    screen.startGame();
    EXPECT_EQ(screen.getState(), ScreenState::Playing);
    screen.togglePause();
    EXPECT_EQ(screen.getState(), ScreenState::Paused);
    screen.resume();
    EXPECT_EQ(screen.getState(), ScreenState::Playing);
    screen.toggleInventory();
    EXPECT_EQ(screen.getState(), ScreenState::Inventory);
    screen.toggleInventory();
    EXPECT_EQ(screen.getState(), ScreenState::Playing);
    screen.returnToMainMenu();
    EXPECT_EQ(screen.getState(), ScreenState::MainMenu);
}

// ======== Settings & Keybinding Rebind state tests ========

TEST(GameScreenTest, ToggleSettingsFromPaused) {
    GameScreen screen;
    screen.startGame();
    screen.togglePause();
    EXPECT_EQ(screen.getState(), ScreenState::Paused);
    screen.toggleSettings();
    EXPECT_EQ(screen.getState(), ScreenState::Settings);
}

TEST(GameScreenTest, ToggleSettingsFromMainMenu) {
    GameScreen screen;
    screen.toggleSettings();
    EXPECT_EQ(screen.getState(), ScreenState::Settings);
}

TEST(GameScreenTest, GoBackFromSettingsToPaused) {
    GameScreen screen;
    screen.startGame();
    screen.togglePause();
    screen.toggleSettings();
    EXPECT_EQ(screen.getState(), ScreenState::Settings);
    screen.goBack();
    EXPECT_EQ(screen.getState(), ScreenState::Paused);
}

TEST(GameScreenTest, GoBackFromSettingsToMainMenu) {
    GameScreen screen;
    screen.toggleSettings();
    EXPECT_EQ(screen.getState(), ScreenState::Settings);
    screen.goBack();
    EXPECT_EQ(screen.getState(), ScreenState::MainMenu);
}

TEST(GameScreenTest, EnterKeybindingRebind) {
    GameScreen screen;
    screen.startGame();
    screen.togglePause();
    screen.toggleSettings();
    screen.enterKeybindingRebind();
    EXPECT_EQ(screen.getState(), ScreenState::KeybindingRebind);
}

TEST(GameScreenTest, GoBackFromKeybindingToSettings) {
    GameScreen screen;
    screen.startGame();
    screen.togglePause();
    screen.toggleSettings();
    screen.enterKeybindingRebind();
    EXPECT_EQ(screen.getState(), ScreenState::KeybindingRebind);
    screen.goBack();
    EXPECT_EQ(screen.getState(), ScreenState::Settings);
}

TEST(GameScreenTest, FullSettingsCycleFromPaused) {
    GameScreen screen;
    screen.startGame();
    screen.togglePause();
    screen.toggleSettings();
    screen.enterKeybindingRebind();
    screen.goBack();  // Keybinding → Settings
    EXPECT_EQ(screen.getState(), ScreenState::Settings);
    screen.goBack();  // Settings → Paused
    EXPECT_EQ(screen.getState(), ScreenState::Paused);
}

TEST(GameScreenTest, IsGameRunningSettings) {
    EXPECT_FALSE(isGameRunning(ScreenState::Settings));
    EXPECT_FALSE(isGameRunning(ScreenState::KeybindingRebind));
}

TEST(GameScreenTest, IsMouseFreeSettings) {
    EXPECT_TRUE(isMouseFree(ScreenState::Settings));
    EXPECT_TRUE(isMouseFree(ScreenState::KeybindingRebind));
}
