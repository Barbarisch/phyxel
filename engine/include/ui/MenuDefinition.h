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

private:
    static Anchor parseAnchor(const std::string& str);
    static std::unique_ptr<UIWidget> buildWidget(const nlohmann::json& j);
};

} // namespace UI
} // namespace Phyxel
