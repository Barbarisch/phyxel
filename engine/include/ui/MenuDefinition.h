#pragma once

#include "ui/UIWidget.h"
#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <optional>

namespace Phyxel {
namespace UI {

/**
 * @brief Builds UIPanel widget trees from JSON definitions.
 *
 * JSON format:
 * {
 *   "id": "settings_menu",
 *   "title": "Settings",
 *   "anchor": "Center",
 *   "size": [400, 500],
 *   "offset": [0, 0],
 *   "children": [
 *     { "type": "label", "id": "title", "text": "Graphics", "isTitle": true },
 *     { "type": "slider", "id": "fov", "label": "FOV ", "value": 70, "min": 30, "max": 120, "size": [380, 32] },
 *     { "type": "checkbox", "id": "fullscreen", "label": "Fullscreen", "checked": false, "size": [380, 32] },
 *     { "type": "dropdown", "id": "quality", "label": "Quality ", "options": ["Low","Medium","High"], "selected": 1, "size": [380, 40] },
 *     { "type": "button", "id": "back", "text": "Back", "size": [380, 40] }
 *   ]
 * }
 */
class MenuDefinition {
public:
    /// Callback registry: maps widget IDs to actions.
    /// Set callbacks before or after building — they're wired during build
    /// or can be wired after via the panel's findChild().
    using CallbackMap = std::unordered_map<std::string, std::function<void()>>;
    using SliderCallbackMap = std::unordered_map<std::string, std::function<void(float)>>;
    using CheckboxCallbackMap = std::unordered_map<std::string, std::function<void(bool)>>;
    using DropdownCallbackMap = std::unordered_map<std::string, std::function<void(int)>>;

    /// Build a UIPanel from a JSON definition string.
    static std::unique_ptr<UIPanel> buildFromJson(const std::string& jsonStr);

    /// Build from a parsed JSON object.
    static std::unique_ptr<UIPanel> buildFromJson(const nlohmann::json& j);

    /// Build and wire callbacks.
    static std::unique_ptr<UIPanel> buildFromJson(
        const nlohmann::json& j,
        const CallbackMap& buttonCallbacks,
        const SliderCallbackMap& sliderCallbacks = {},
        const CheckboxCallbackMap& checkboxCallbacks = {},
        const DropdownCallbackMap& dropdownCallbacks = {}
    );

    /// Serialize a UIPanel to JSON.
    static nlohmann::json toJson(const UIPanel& panel);

    /// Build a single widget from its JSON definition (also used to expand Repeater
    /// item templates).
    static std::unique_ptr<UIWidget> buildWidget(const nlohmann::json& j);

private:
    static Anchor parseAnchor(const std::string& str);
};

class HudDataContext;  // forward decl

/// Apply a HudDataContext to a HUD widget tree: scalar value binds, `visibleWhen`
/// visibility, and Repeater list-expansion. Call once per frame before rendering.
/// Lives here (not in HudDataContext) because repeater expansion needs JSON building.
void applyHudBindings(UIWidget* root, const HudDataContext& ctx);

class UISystem;  // forward decl

/// Host actions a menu button can invoke (wired by the loader).
struct MenuActions {
    std::function<void(const std::string& sceneId)> onTransitionScene;
    std::function<void()> onQuit;
    std::function<void()> onLoadGame;
    /// Pause/screen-overlay actions (button action types "resume" /
    /// "open_settings" / "main_menu" / "show_credits"). Left null on menu-scene
    /// loads; wired by the loadPauseMenuInto / loadGameScreenInto host.
    std::function<void()> onResume;
    std::function<void()> onSettings;
    std::function<void()> onMainMenu;
    std::function<void()> onShowCredits;
    std::function<void()> onBack;   // settings/sub-screen "Back" (action "back")
    /// Settings widgets (slider/checkbox/dropdown carrying a "setting" key):
    /// read the current value to initialize the widget (onGetSetting) and apply a
    /// change (onSetSetting). Floats throughout — checkbox is 0/1, dropdown is the
    /// selected index. Wired by the settings screen's host.
    std::function<float(const std::string& key)> onGetSetting;
    std::function<void(const std::string& key, float value)> onSetSetting;
    /// Resolve a {{token}} in label/button text to a display string (e.g.
    /// "playtime", "story.<var>"). Return nullopt to leave the token literal.
    /// Applied once at menu load (static).
    std::function<std::optional<std::string>(const std::string& token)> onResolveVariable;
};

/// Convert a GameMenuRenderer-style menu layout (position-based 1280x720 canvas,
/// panels/submenus, solid/image background, label/button/image elements, button
/// actions) into UISystem fullscreen screens named "menu:<panelKey>", and show the
/// start panel. Replaces the ImGui GameMenuRenderer for menu scenes (no ImGui).
/// Not yet ported: per-element animations, fonts, {{token}} interpolation.
void loadMenuInto(UISystem& ui, const nlohmann::json& layout, const MenuActions& actions);

/// Load the data-driven pause overlay (resources/ui/pause_menu.json) into UISystem
/// screens named "pause:<panelKey>" and show the start panel. Replaces the ImGui
/// renderPauseMenu (docs/HudSystem.md §11a). Buttons use action types "resume" /
/// "open_settings" / "main_menu" (wired via MenuActions) plus the shared
/// "quit_game". Idempotent. Call unloadPauseMenuFrom on resume.
void loadPauseMenuInto(UISystem& ui, const MenuActions& actions);

/// Remove the pause overlay screens ("pause:*") added by loadPauseMenuInto.
void unloadPauseMenuFrom(UISystem& ui);

/// Load a full-screen data-driven game screen (intro / victory / credits — the
/// ScreenState screens migrated off ImGui, docs/HudSystem.md §11a) from
/// resources/ui/<name>_screen.json into UISystem screens named "<name>:<panel>".
/// Opaque background (covers the view). Dynamic text ({{title}}/{{tagline}}/…) is
/// resolved via actions.onResolveVariable; buttons use action types
/// "main_menu" / "show_credits" / "quit_game". Call unloadGameScreenFrom to remove.
void loadGameScreenInto(UISystem& ui, const std::string& name, const MenuActions& actions);

/// Remove the "<name>:*" screens added by loadGameScreenInto.
void unloadGameScreenFrom(UISystem& ui, const std::string& name);

/// Remove all "menu:*" screens previously added by loadMenuInto.
void unloadMenuFrom(UISystem& ui);

/// Load HUD panels into the UISystem as shown screens. Uses `gameHud` if non-null
/// (a game.json "hud" object or array of panels), else the engine default HUD
/// (resources/ui/default_hud.json). Shared by the editor and standalone hosts —
/// the host registers data providers separately on the RenderCoordinator hudData().
void loadHudInto(UISystem& ui, const nlohmann::json* gameHud);

} // namespace UI
} // namespace Phyxel
