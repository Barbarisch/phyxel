#include <gtest/gtest.h>
#include "core/GameSettings.h"
#include <fstream>
#include <filesystem>

using namespace Phyxel::Core;

// ============================================================================
// Keybinding JSON round-trip
// ============================================================================

TEST(KeybindingTest, ToJsonAndBack) {
    Keybinding kb;
    kb.action = "MoveForward";
    kb.key = 87; // W
    kb.modifiers = 0;

    auto j = kb.toJson();
    auto kb2 = Keybinding::fromJson(j);

    EXPECT_EQ(kb2.action, "MoveForward");
    EXPECT_EQ(kb2.key, 87);
    EXPECT_EQ(kb2.modifiers, 0);
}

TEST(KeybindingTest, WithModifiers) {
    Keybinding kb;
    kb.action = "Sprint";
    kb.key = 340; // Left Shift
    kb.modifiers = 1; // SHIFT

    auto j = kb.toJson();
    auto kb2 = Keybinding::fromJson(j);

    EXPECT_EQ(kb2.action, "Sprint");
    EXPECT_EQ(kb2.key, 340);
    EXPECT_EQ(kb2.modifiers, 1);
}

// ============================================================================
// GameSettings JSON round-trip
// ============================================================================

TEST(GameSettingsTest, DefaultValues) {
    GameSettings s;
    EXPECT_EQ(s.resolutionWidth, 1600);
    EXPECT_EQ(s.resolutionHeight, 900);
    EXPECT_FALSE(s.fullscreen);
    EXPECT_EQ(s.vsync, VSyncMode::Off);
    EXPECT_FLOAT_EQ(s.fov, 45.0f);
    EXPECT_FLOAT_EQ(s.masterVolume, 1.0f);
    EXPECT_FLOAT_EQ(s.mouseSensitivity, 0.1f);
    EXPECT_FALSE(s.invertY);
}

TEST(GameSettingsTest, ToJsonAndBack) {
    GameSettings s;
    s.resolutionWidth = 1920;
    s.resolutionHeight = 1080;
    s.fullscreen = true;
    s.vsync = VSyncMode::Adaptive;
    s.fov = 90.0f;
    s.masterVolume = 0.5f;
    s.mouseSensitivity = 0.3f;
    s.invertY = true;
    s.keybindings = GameSettings::defaultKeybindings();

    auto j = s.toJson();
    GameSettings s2;
    GameSettings::fromJson(j, s2);

    EXPECT_EQ(s2.resolutionWidth, 1920);
    EXPECT_EQ(s2.resolutionHeight, 1080);
    EXPECT_TRUE(s2.fullscreen);
    EXPECT_EQ(s2.vsync, VSyncMode::Adaptive);
    EXPECT_FLOAT_EQ(s2.fov, 90.0f);
    EXPECT_FLOAT_EQ(s2.masterVolume, 0.5f);
    EXPECT_FLOAT_EQ(s2.mouseSensitivity, 0.3f);
    EXPECT_TRUE(s2.invertY);
    EXPECT_FALSE(s2.keybindings.empty());
}

// ============================================================================
// File save/load
// ============================================================================

TEST(GameSettingsTest, SaveAndLoad) {
    const std::string path = "test_settings_temp.json";

    GameSettings s;
    s.resolutionWidth = 2560;
    s.resolutionHeight = 1440;
    s.fov = 100.0f;
    s.vsync = VSyncMode::On;
    s.keybindings.push_back({"Jump", 32, 0}); // Space

    ASSERT_TRUE(s.saveToFile(path));

    GameSettings s2;
    ASSERT_TRUE(GameSettings::loadFromFile(path, s2));

    EXPECT_EQ(s2.resolutionWidth, 2560);
    EXPECT_EQ(s2.resolutionHeight, 1440);
    EXPECT_FLOAT_EQ(s2.fov, 100.0f);
    EXPECT_EQ(s2.vsync, VSyncMode::On);
    ASSERT_EQ(s2.keybindings.size(), 1u);
    EXPECT_EQ(s2.keybindings[0].action, "Jump");

    std::filesystem::remove(path);
}

TEST(GameSettingsTest, LoadNonExistentFileUsesDefaults) {
    GameSettings s;
    // Missing file is not an error — returns true with default values
    EXPECT_TRUE(GameSettings::loadFromFile("nonexistent_settings_12345.json", s));
    EXPECT_EQ(s.resolutionWidth, 1600);  // still defaults
}

// ============================================================================
// Keybinding helpers
// ============================================================================

TEST(GameSettingsTest, FindBinding) {
    GameSettings s;
    s.keybindings = GameSettings::defaultKeybindings();

    auto* kb = s.findBinding("MoveForward");
    ASSERT_NE(kb, nullptr);
    EXPECT_EQ(kb->action, "MoveForward");
}

TEST(GameSettingsTest, FindBindingNotFound) {
    GameSettings s;
    EXPECT_EQ(s.findBinding("NonExistent"), nullptr);
}

TEST(GameSettingsTest, SetBindingNew) {
    GameSettings s;
    s.setBinding("CustomAction", 65, 0); // A key
    auto* kb = s.findBinding("CustomAction");
    ASSERT_NE(kb, nullptr);
    EXPECT_EQ(kb->key, 65);
}

TEST(GameSettingsTest, SetBindingOverwrite) {
    GameSettings s;
    s.setBinding("Jump", 32, 0);  // Space
    s.setBinding("Jump", 257, 0); // Enter
    auto* kb = s.findBinding("Jump");
    ASSERT_NE(kb, nullptr);
    EXPECT_EQ(kb->key, 257);
    // Should not duplicate
    int count = 0;
    for (auto& b : s.keybindings) {
        if (b.action == "Jump") count++;
    }
    EXPECT_EQ(count, 1);
}

TEST(GameSettingsTest, RemoveBinding) {
    GameSettings s;
    s.setBinding("Jump", 32, 0);
    s.removeBinding("Jump");
    EXPECT_EQ(s.findBinding("Jump"), nullptr);
}

TEST(GameSettingsTest, DefaultKeybindingsNotEmpty) {
    auto bindings = GameSettings::defaultKeybindings();
    EXPECT_GE(bindings.size(), 5u);
}

// ============================================================================
// Key name conversion
// ============================================================================

TEST(GameSettingsTest, KeyToString) {
    EXPECT_EQ(keyToString(87), "W");
    EXPECT_EQ(keyToString(32), "Space");
    EXPECT_EQ(keyToString(256), "Escape");
    EXPECT_EQ(keyToString(290), "F1");
}

TEST(GameSettingsTest, StringToKey) {
    EXPECT_EQ(stringToKey("W"), 87);
    EXPECT_EQ(stringToKey("Space"), 32);
    EXPECT_EQ(stringToKey("Escape"), 256);
    EXPECT_EQ(stringToKey("F1"), 290);
}

TEST(GameSettingsTest, StringToKeyUnknown) {
    EXPECT_EQ(stringToKey("UnknownKey123"), -1);
}

TEST(GameSettingsTest, ModifiersToString) {
    EXPECT_EQ(modifiersToString(0), "");
    // GLFW_MOD_SHIFT = 0x0001
    EXPECT_NE(modifiersToString(1).find("Shift"), std::string::npos);
}

// ============================================================================
// AI settings
// ============================================================================

TEST(GameSettingsTest, AIDefaultValues) {
    GameSettings s;
    EXPECT_EQ(s.aiProvider, "anthropic");
    EXPECT_EQ(s.aiModel, "");
    EXPECT_EQ(s.aiApiKey, "");
}

TEST(GameSettingsTest, AIJsonRoundTrip) {
    GameSettings s;
    s.aiProvider = "openai";
    s.aiModel = "gpt-4o";

    auto j = s.toJson();
    GameSettings s2;
    GameSettings::fromJson(j, s2);

    EXPECT_EQ(s2.aiProvider, "openai");
    EXPECT_EQ(s2.aiModel, "gpt-4o");
}

TEST(GameSettingsTest, AIApiKeyNotSerialized) {
    GameSettings s;
    s.aiApiKey = "sk-secret-key-12345";

    auto j = s.toJson();
    // API key should NOT be in the JSON output (security)
    EXPECT_FALSE(j.contains("ai") && j["ai"].contains("api_key"));
}
