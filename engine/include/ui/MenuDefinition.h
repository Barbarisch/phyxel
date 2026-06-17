#pragma once

#include "ui/UIWidget.h"
#include <nlohmann/json.hpp>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

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
};

/// Convert a GameMenuRenderer-style menu layout (position-based 1280x720 canvas,
/// panels/submenus, solid/image background, label/button/image elements, button
/// actions) into UISystem fullscreen screens named "menu:<panelKey>", and show the
/// start panel. Replaces the ImGui GameMenuRenderer for menu scenes (no ImGui).
/// Not yet ported: per-element animations, fonts, {{token}} interpolation.
void loadMenuInto(UISystem& ui, const nlohmann::json& layout, const MenuActions& actions);

/// Remove all "menu:*" screens previously added by loadMenuInto.
void unloadMenuFrom(UISystem& ui);

} // namespace UI
} // namespace Phyxel
