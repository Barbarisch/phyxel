#include "core/GameSettings.h"
#include "utils/Logger.h"
#include <GLFW/glfw3.h>
#include <fstream>

namespace Phyxel {
namespace Core {

// ============================================================================
// Keybinding JSON
// ============================================================================

nlohmann::json Keybinding::toJson() const {
    nlohmann::json j;
    j["action"] = action;
    j["key"] = keyToString(key);
    if (modifiers != 0)
        j["modifiers"] = modifiersToString(modifiers);
    return j;
}

Keybinding Keybinding::fromJson(const nlohmann::json& j) {
    Keybinding b;
    b.action = j.value("action", "");
    b.key = stringToKey(j.value("key", ""));
    std::string modStr = j.value("modifiers", "");
    b.modifiers = 0;
    if (modStr.find("Shift") != std::string::npos)   b.modifiers |= GLFW_MOD_SHIFT;
    if (modStr.find("Ctrl") != std::string::npos)    b.modifiers |= GLFW_MOD_CONTROL;
    if (modStr.find("Alt") != std::string::npos)     b.modifiers |= GLFW_MOD_ALT;
    if (modStr.find("Super") != std::string::npos)   b.modifiers |= GLFW_MOD_SUPER;
    return b;
}

// ============================================================================
// GameSettings JSON
// ============================================================================

static std::string vsyncToString(VSyncMode m) {
    switch (m) {
        case VSyncMode::Off:      return "off";
        case VSyncMode::On:       return "on";
        case VSyncMode::Adaptive: return "adaptive";
    }
    return "off";
}

static VSyncMode stringToVSync(const std::string& s) {
    if (s == "on")       return VSyncMode::On;
    if (s == "adaptive") return VSyncMode::Adaptive;
    return VSyncMode::Off;
}

nlohmann::json GameSettings::toJson() const {
    nlohmann::json j;

    // Display
    j["display"]["resolution_width"]  = resolutionWidth;
    j["display"]["resolution_height"] = resolutionHeight;
    j["display"]["fullscreen"]        = fullscreen;
    j["display"]["vsync"]             = vsyncToString(vsync);
    j["display"]["fov"]               = fov;
    j["display"]["brightness"]        = brightness;

    // Audio
    j["audio"]["master_volume"] = masterVolume;
    j["audio"]["music_volume"]  = musicVolume;
    j["audio"]["sfx_volume"]    = sfxVolume;

    // Controls
    j["controls"]["mouse_sensitivity"] = mouseSensitivity;
    j["controls"]["invert_y"]          = invertY;

    // Keybindings
    nlohmann::json bindings = nlohmann::json::array();
    for (const auto& kb : keybindings) {
        bindings.push_back(kb.toJson());
    }
    j["controls"]["keybindings"] = bindings;

    // Rendering
    j["rendering"]["render_distance"] = renderDistance;

    return j;
}

void GameSettings::fromJson(const nlohmann::json& j, GameSettings& s) {
    // Display
    if (j.contains("display")) {
        auto& d = j["display"];
        s.resolutionWidth  = d.value("resolution_width",  s.resolutionWidth);
        s.resolutionHeight = d.value("resolution_height", s.resolutionHeight);
        s.fullscreen       = d.value("fullscreen",        s.fullscreen);
        s.vsync            = stringToVSync(d.value("vsync", "off"));
        s.fov              = d.value("fov",               s.fov);
        s.brightness       = d.value("brightness",        s.brightness);
    }

    // Audio
    if (j.contains("audio")) {
        auto& a = j["audio"];
        s.masterVolume = a.value("master_volume", s.masterVolume);
        s.musicVolume  = a.value("music_volume",  s.musicVolume);
        s.sfxVolume    = a.value("sfx_volume",    s.sfxVolume);
    }

    // Controls
    if (j.contains("controls")) {
        auto& c = j["controls"];
        s.mouseSensitivity = c.value("mouse_sensitivity", s.mouseSensitivity);
        s.invertY          = c.value("invert_y",          s.invertY);

        if (c.contains("keybindings") && c["keybindings"].is_array()) {
            s.keybindings.clear();
            for (const auto& kb : c["keybindings"]) {
                s.keybindings.push_back(Keybinding::fromJson(kb));
            }
        }
    }

    // Rendering
    if (j.contains("rendering")) {
        s.renderDistance = j["rendering"].value("render_distance", s.renderDistance);
    }
}

bool GameSettings::loadFromFile(const std::string& path, GameSettings& out) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_INFO("GameSettings", "No settings file found at '{}', using defaults", path);
        return true; // not an error — just use defaults
    }
    try {
        nlohmann::json j = nlohmann::json::parse(file);
        fromJson(j, out);
        LOG_INFO("GameSettings", "Loaded settings from '{}'", path);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("GameSettings", "Failed to parse settings '{}': {}", path, e.what());
        return false;
    }
}

bool GameSettings::saveToFile(const std::string& path) const {
    try {
        std::ofstream file(path);
        if (!file.is_open()) {
            LOG_ERROR("GameSettings", "Cannot open '{}' for writing", path);
            return false;
        }
        file << toJson().dump(2);
        LOG_INFO("GameSettings", "Saved settings to '{}'", path);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("GameSettings", "Failed to save settings '{}': {}", path, e.what());
        return false;
    }
}

// ============================================================================
// Keybinding helpers
// ============================================================================

const Keybinding* GameSettings::findBinding(const std::string& action) const {
    for (const auto& kb : keybindings) {
        if (kb.action == action) return &kb;
    }
    return nullptr;
}

void GameSettings::setBinding(const std::string& action, int key, int modifiers) {
    for (auto& kb : keybindings) {
        if (kb.action == action) {
            kb.key = key;
            kb.modifiers = modifiers;
            return;
        }
    }
    keybindings.push_back({action, key, modifiers});
}

void GameSettings::removeBinding(const std::string& action) {
    keybindings.erase(
        std::remove_if(keybindings.begin(), keybindings.end(),
                        [&](const Keybinding& kb) { return kb.action == action; }),
        keybindings.end());
}

std::vector<Keybinding> GameSettings::defaultKeybindings() {
    return {
        {"MoveForward",    GLFW_KEY_W,      0},
        {"MoveBackward",   GLFW_KEY_S,      0},
        {"MoveLeft",       GLFW_KEY_A,      0},
        {"MoveRight",      GLFW_KEY_D,      0},
        {"Jump",           GLFW_KEY_SPACE,   0},
        {"Sprint",         GLFW_KEY_LEFT_SHIFT, 0},
        {"Crouch",         GLFW_KEY_LEFT_CONTROL, 0},
        {"TogglePause",    GLFW_KEY_ESCAPE,  0},
        {"ToggleInventory",GLFW_KEY_TAB,     0},
        {"PlaceCube",      GLFW_KEY_C,       0},
        {"Attack",         GLFW_KEY_F,       0},
        {"Interact",       GLFW_KEY_E,       0},
    };
}

// ============================================================================
// Key name conversion
// ============================================================================

std::string keyToString(int glfwKey) {
    // Letters
    if (glfwKey >= GLFW_KEY_A && glfwKey <= GLFW_KEY_Z) {
        return std::string(1, static_cast<char>('A' + (glfwKey - GLFW_KEY_A)));
    }
    // Digits
    if (glfwKey >= GLFW_KEY_0 && glfwKey <= GLFW_KEY_9) {
        return std::string(1, static_cast<char>('0' + (glfwKey - GLFW_KEY_0)));
    }
    // F-keys
    if (glfwKey >= GLFW_KEY_F1 && glfwKey <= GLFW_KEY_F12) {
        return "F" + std::to_string(glfwKey - GLFW_KEY_F1 + 1);
    }
    // Common keys
    switch (glfwKey) {
        case GLFW_KEY_SPACE:         return "Space";
        case GLFW_KEY_ESCAPE:        return "Escape";
        case GLFW_KEY_ENTER:         return "Enter";
        case GLFW_KEY_TAB:           return "Tab";
        case GLFW_KEY_BACKSPACE:     return "Backspace";
        case GLFW_KEY_LEFT_SHIFT:    return "LShift";
        case GLFW_KEY_RIGHT_SHIFT:   return "RShift";
        case GLFW_KEY_LEFT_CONTROL:  return "LCtrl";
        case GLFW_KEY_RIGHT_CONTROL: return "RCtrl";
        case GLFW_KEY_LEFT_ALT:      return "LAlt";
        case GLFW_KEY_RIGHT_ALT:     return "RAlt";
        case GLFW_KEY_UP:            return "Up";
        case GLFW_KEY_DOWN:          return "Down";
        case GLFW_KEY_LEFT:          return "Left";
        case GLFW_KEY_RIGHT:         return "Right";
        case GLFW_KEY_DELETE:        return "Delete";
        case GLFW_KEY_INSERT:        return "Insert";
        case GLFW_KEY_HOME:          return "Home";
        case GLFW_KEY_END:           return "End";
        case GLFW_KEY_PAGE_UP:       return "PageUp";
        case GLFW_KEY_PAGE_DOWN:     return "PageDown";
        case GLFW_KEY_GRAVE_ACCENT:  return "Grave";
        case GLFW_KEY_MINUS:         return "Minus";
        case GLFW_KEY_EQUAL:         return "Equal";
        case GLFW_KEY_LEFT_BRACKET:  return "LBracket";
        case GLFW_KEY_RIGHT_BRACKET: return "RBracket";
        case GLFW_KEY_SEMICOLON:     return "Semicolon";
        case GLFW_KEY_APOSTROPHE:    return "Apostrophe";
        case GLFW_KEY_COMMA:         return "Comma";
        case GLFW_KEY_PERIOD:        return "Period";
        case GLFW_KEY_SLASH:         return "Slash";
        case GLFW_KEY_BACKSLASH:     return "Backslash";
        default: return "Key" + std::to_string(glfwKey);
    }
}

int stringToKey(const std::string& name) {
    if (name.empty()) return GLFW_KEY_UNKNOWN;

    // Single letter
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') {
        return GLFW_KEY_A + (name[0] - 'A');
    }
    // Single digit
    if (name.size() == 1 && name[0] >= '0' && name[0] <= '9') {
        return GLFW_KEY_0 + (name[0] - '0');
    }
    // F-keys
    if (name[0] == 'F' && name.size() >= 2 && name.size() <= 3) {
        int n = std::stoi(name.substr(1));
        if (n >= 1 && n <= 12) return GLFW_KEY_F1 + n - 1;
    }
    // Named keys
    if (name == "Space")      return GLFW_KEY_SPACE;
    if (name == "Escape")     return GLFW_KEY_ESCAPE;
    if (name == "Enter")      return GLFW_KEY_ENTER;
    if (name == "Tab")        return GLFW_KEY_TAB;
    if (name == "Backspace")  return GLFW_KEY_BACKSPACE;
    if (name == "LShift")     return GLFW_KEY_LEFT_SHIFT;
    if (name == "RShift")     return GLFW_KEY_RIGHT_SHIFT;
    if (name == "LCtrl")      return GLFW_KEY_LEFT_CONTROL;
    if (name == "RCtrl")      return GLFW_KEY_RIGHT_CONTROL;
    if (name == "LAlt")       return GLFW_KEY_LEFT_ALT;
    if (name == "RAlt")       return GLFW_KEY_RIGHT_ALT;
    if (name == "Up")         return GLFW_KEY_UP;
    if (name == "Down")       return GLFW_KEY_DOWN;
    if (name == "Left")       return GLFW_KEY_LEFT;
    if (name == "Right")      return GLFW_KEY_RIGHT;
    if (name == "Delete")     return GLFW_KEY_DELETE;
    if (name == "Insert")     return GLFW_KEY_INSERT;
    if (name == "Home")       return GLFW_KEY_HOME;
    if (name == "End")        return GLFW_KEY_END;
    if (name == "PageUp")     return GLFW_KEY_PAGE_UP;
    if (name == "PageDown")   return GLFW_KEY_PAGE_DOWN;
    if (name == "Grave")      return GLFW_KEY_GRAVE_ACCENT;
    if (name == "Minus")      return GLFW_KEY_MINUS;
    if (name == "Equal")      return GLFW_KEY_EQUAL;
    if (name == "LBracket")   return GLFW_KEY_LEFT_BRACKET;
    if (name == "RBracket")   return GLFW_KEY_RIGHT_BRACKET;
    if (name == "Semicolon")  return GLFW_KEY_SEMICOLON;
    if (name == "Apostrophe") return GLFW_KEY_APOSTROPHE;
    if (name == "Comma")      return GLFW_KEY_COMMA;
    if (name == "Period")     return GLFW_KEY_PERIOD;
    if (name == "Slash")      return GLFW_KEY_SLASH;
    if (name == "Backslash")  return GLFW_KEY_BACKSLASH;

    // Fallback: try "Key###" format
    if (name.substr(0, 3) == "Key") {
        try { return std::stoi(name.substr(3)); }
        catch (...) {}
    }
    return GLFW_KEY_UNKNOWN;
}

std::string modifiersToString(int mods) {
    std::string result;
    if (mods & GLFW_MOD_CONTROL) result += "Ctrl+";
    if (mods & GLFW_MOD_SHIFT)   result += "Shift+";
    if (mods & GLFW_MOD_ALT)     result += "Alt+";
    if (mods & GLFW_MOD_SUPER)   result += "Super+";
    if (!result.empty()) result.pop_back(); // remove trailing +
    return result;
}

} // namespace Core
} // namespace Phyxel
