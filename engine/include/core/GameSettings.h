#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// VSync mode matching Vulkan present modes.
enum class VSyncMode {
    Off,            // VK_PRESENT_MODE_IMMEDIATE_KHR  — uncapped FPS
    On,             // VK_PRESENT_MODE_FIFO_KHR       — standard vsync
    Adaptive        // VK_PRESENT_MODE_MAILBOX_KHR    — triple-buffered
};

/// A single keybinding: action name → key + modifiers.
struct Keybinding {
    std::string action;     // e.g. "MoveForward", "TogglePause"
    int key = 0;            // GLFW key code
    int modifiers = 0;      // GLFW modifier flags (SHIFT, CTRL, ALT)

    nlohmann::json toJson() const;
    static Keybinding fromJson(const nlohmann::json& j);
};

/// All user-configurable game settings, serialized to settings.json.
struct GameSettings {

    // -- Display ----------------------------------------------------------
    int         resolutionWidth   = 1600;
    int         resolutionHeight  = 900;
    bool        fullscreen        = false;
    VSyncMode   vsync             = VSyncMode::Off;
    float       fov               = 45.0f;  // degrees, 30–120
    float       brightness        = 1.0f;   // 0.5–2.0

    // -- Audio ------------------------------------------------------------
    float       masterVolume      = 1.0f;   // 0.0–1.0
    float       musicVolume       = 0.8f;
    float       sfxVolume         = 1.0f;

    // -- Controls ---------------------------------------------------------
    float       mouseSensitivity  = 0.1f;   // degrees per pixel, 0.01–1.0
    bool        invertY           = false;
    std::vector<Keybinding> keybindings;

    // -- Rendering --------------------------------------------------------
    float       renderDistance    = 256.0f;

    // =====================================================================
    // Load / Save
    // =====================================================================

    /// Load settings from a JSON file. Missing keys keep defaults.
    static bool loadFromFile(const std::string& path, GameSettings& out);

    /// Save settings to a JSON file.
    bool saveToFile(const std::string& path) const;

    nlohmann::json toJson() const;
    static void fromJson(const nlohmann::json& j, GameSettings& s);

    // =====================================================================
    // Keybinding helpers
    // =====================================================================

    /// Find the keybinding for an action name. Returns nullptr if not found.
    const Keybinding* findBinding(const std::string& action) const;

    /// Set or add a keybinding for the given action.
    void setBinding(const std::string& action, int key, int modifiers = 0);

    /// Remove a keybinding by action name.
    void removeBinding(const std::string& action);

    /// Get default keybindings for a standard game.
    static std::vector<Keybinding> defaultKeybindings();
};

/// Convert GLFW key code to a human-readable name.
std::string keyToString(int glfwKey);

/// Convert a human-readable key name back to a GLFW key code. Returns -1 if unknown.
int stringToKey(const std::string& name);

/// Convert GLFW modifier flags to a display string (e.g. "Ctrl+Shift").
std::string modifiersToString(int mods);

} // namespace Core
} // namespace Phyxel
