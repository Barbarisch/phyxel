#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <GLFW/glfw3.h>
#include <vector>
#include <set>
#include <map>
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
    // The file's own binding survives; actions the file never knew get their defaults
    // merged in (see LoadMergesActionsMissingFromTheFile), so the list is not just "Jump".
    ASSERT_NE(s2.findBinding("Jump"), nullptr);
    EXPECT_EQ(s2.findBinding("Jump")->key, 32);

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

// A settings.json written by an older build lists only the actions that existed then.
// An engine update that adds an action (ToggleCharacter, Ravenmere G-133) must not leave
// it unbound in every existing install: loading merges the defaults for missing actions
// and keeps the file's own bindings (including deliberate rebinds) untouched.
TEST(GameSettingsTest, LoadMergesActionsMissingFromTheFile) {
    const std::string path = "test_settings_merge_temp.json";
    GameSettings s;
    s.keybindings = {{"Jump", 32, 0}, {"Interact", 70 /* F, a deliberate rebind */, 0}};
    ASSERT_TRUE(s.saveToFile(path));

    GameSettings s2;
    ASSERT_TRUE(GameSettings::loadFromFile(path, s2));
    ASSERT_NE(s2.findBinding("ToggleCharacter"), nullptr) << "new default action not merged in";
    EXPECT_EQ(s2.findBinding("ToggleCharacter")->key, 67);   // C
    ASSERT_NE(s2.findBinding("Interact"), nullptr);
    EXPECT_EQ(s2.findBinding("Interact")->key, 70) << "the file's rebind must win over the default";
    for (const auto& d : GameSettings::defaultKeybindings())
        EXPECT_NE(s2.findBinding(d.action), nullptr) << d.action;
    std::filesystem::remove(path);
}


// ============================================================================
// DEFAULTS vs THE SETTINGS PANEL (2026-09-22).
//
// Two ways this pair rotted, both found by a user asking "is there an options panel
// for changing key bindings":
//   - PlaceCube and ToggleCharacter were BOTH bound to C. PlaceCube was read by
//     nothing anywhere in the tree; it existed only to collide.
//   - StrafeLeft/StrafeRight/ToggleAutorun/ToggleWalk were live (ControlScheme reads
//     them) but had no row in the panel, so they could only be changed by hand-editing
//     settings.json.
// ============================================================================

TEST(GameSettingsTest, NoTwoDefaultBindingsShareAKey) {
    const auto defaults = GameSettings::defaultKeybindings();
    ASSERT_FALSE(defaults.empty());

    // ONE overlap is deliberate and resolved at runtime: E is StrafeRight under the WoW
    // scheme and Interact under every other, and GameShell::applyMmoBindings moves
    // Interact to F when an MMO scheme is active - so only one of the pair is ever live.
    // Anything else is a real collision, which is how PlaceCube sat on ToggleCharacter's C.
    const std::set<std::string> kManaged{"Interact", "StrafeRight"};

    std::map<std::pair<int, int>, std::string> seen;   // (key, mods) -> first action
    std::vector<std::string> clashes;
    for (const auto& kb : defaults) {
        const auto combo = std::make_pair(kb.key, kb.modifiers);
        auto it = seen.find(combo);
        if (it == seen.end()) { seen[combo] = kb.action; continue; }
        if (kManaged.count(it->second) && kManaged.count(kb.action)) continue;
        clashes.push_back(it->second + " and " + kb.action + " share a key");
    }
    EXPECT_TRUE(clashes.empty())
        << clashes.size() << " colliding default binding(s): " << clashes.front();

    // The managed pair must still BE the managed pair - if one of them is rebound away
    // from E this whitelist is stale and should go.
    std::map<std::string, int> byAction;
    for (const auto& kb : defaults) byAction[kb.action] = kb.key;
    EXPECT_EQ(byAction["Interact"], GLFW_KEY_E);
    EXPECT_EQ(byAction["StrafeRight"], GLFW_KEY_E);
}

TEST(GameSettingsTest, EveryDefaultBindingIsEditableInTheSettingsPanel) {
    // A binding nobody can reach in the UI is a binding that does not really exist.
    std::ifstream f("resources/ui/settings_screen.json");
    ASSERT_TRUE(f.good()) << "run from the repo root";
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF)
        text.erase(0, 3);                                  // strip the UTF-8 BOM
    const auto screen = nlohmann::json::parse(text);

    std::set<std::string> listed;
    for (const auto& child : screen["panels"]["keybindings"]["children"])
        if (child.contains("action") && child["action"].value("type", "") == "rebind")
            listed.insert(child["action"].value("binding", ""));

    std::vector<std::string> missing;
    for (const auto& kb : GameSettings::defaultKeybindings())
        if (listed.find(kb.action) == listed.end()) missing.push_back(kb.action);

    EXPECT_TRUE(missing.empty())
        << missing.size() << " default binding(s) have no row in Settings > Keybindings"
        << " (first: " << missing.front() << ") - run: python tools/gen_keybind_panel.py";

    // And nothing in the panel that is not a real default, which is how a removed action
    // leaves a dead row behind.
    std::set<std::string> known;
    for (const auto& kb : GameSettings::defaultKeybindings()) known.insert(kb.action);
    for (const auto& a : listed)
        EXPECT_TRUE(known.count(a) > 0) << "panel offers '" << a << "', which is not a default";
}

TEST(GameSettingsTest, TheActionBarOccupiesTheWowRowAndIsRebindable) {
    std::map<std::string, int> byAction;
    for (const auto& kb : GameSettings::defaultKeybindings()) byAction[kb.action] = kb.key;

    // Slot 1 is key "1"; the off-by-one between slot index and key label lives in
    // Core::ActionBar and nowhere else.
    const int expected[12] = {GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
                              GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8,
                              GLFW_KEY_9, GLFW_KEY_0, GLFW_KEY_MINUS, GLFW_KEY_EQUAL};
    for (int i = 0; i < 12; ++i) {
        const std::string action = "ActionSlot" + std::to_string(i + 1);
        ASSERT_TRUE(byAction.count(action) > 0) << action << " is not a default binding";
        EXPECT_EQ(byAction[action], expected[i]) << action << " is on the wrong key";
    }
    ASSERT_TRUE(byAction.count("ToggleAbilities") > 0);
    EXPECT_EQ(byAction["ToggleAbilities"], GLFW_KEY_P);
}
