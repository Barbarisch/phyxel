#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>
#include <optional>
#include <imgui.h>

namespace Phyxel {
namespace Vulkan { class VulkanDevice; }
namespace UI {

/**
 * @brief Runtime game menu renderer driven by JSON layout definitions.
 *
 * Renders a full-screen interactive menu each ImGui frame using ImDrawList
 * (no ImGui windows — direct raw draw calls for maximum visual control).
 *
 * Supports:
 *  - Solid-color or image backgrounds
 *  - Elements: panel, label, button, image
 *  - Per-element animations: fade_in, slide_in_left, slide_in_right
 *  - Custom fonts loaded from TTF paths
 *  - Nested submenus via a panel stack
 *  - Action callbacks: transition_scene, quit_game, open_submenu, close_submenu
 *
 * ### JSON Layout Schema
 * @code{.json}
 * {
 *   "background_type": "solid",        // "solid" | "image"
 *   "background_color": [0.05, 0.05, 0.1, 1.0],
 *   "background_image": "resources/images/menu_bg.png",
 *   "music": "resources/sounds/menu_theme.wav",
 *   "fonts": [
 *     { "id": "title", "file": "resources/fonts/menu_title.ttf", "size": 64.0 },
 *     { "id": "body",  "file": "resources/fonts/menu_body.ttf",  "size": 24.0 }
 *   ],
 *   "panels": {
 *     "main": {
 *       "children": [
 *         { "type": "label",  "id": "game_title", "text": "MY GAME",
 *           "position": [640, 120], "size": [400, 80],
 *           "font": "title", "text_color": [1,0.9,0.5,1],
 *           "animation": "fade_in", "animation_delay": 0.0 },
 *         { "type": "button", "id": "btn_new",  "text": "New Game",
 *           "position": [540, 280], "size": [200, 50],
 *           "action": { "type": "transition_scene", "target": "game_world" },
 *           "animation": "slide_in_left", "animation_delay": 0.1 },
 *         { "type": "button", "id": "btn_opts", "text": "Options",
 *           "position": [540, 345], "size": [200, 50],
 *           "action": { "type": "open_submenu", "target": "options" },
 *           "animation": "slide_in_left", "animation_delay": 0.2 },
 *         { "type": "button", "id": "btn_quit", "text": "Quit",
 *           "position": [540, 410], "size": [200, 50],
 *           "action": { "type": "quit_game" },
 *           "animation": "slide_in_left", "animation_delay": 0.3 },
 *         { "type": "image",  "id": "logo",
 *           "image": "resources/images/logo.png",
 *           "position": [440, 30], "size": [400, 80],
 *           "animation": "fade_in", "animation_delay": 0.0 }
 *       ]
 *     },
 *     "options": {
 *       "children": [
 *         { "type": "label", "text": "Options", ... },
 *         { "type": "button", "text": "Back",
 *           "action": { "type": "close_submenu" } }
 *       ]
 *     }
 *   },
 *   "start_panel": "main"
 * }
 * @endcode
 *
 * Virtual canvas: 1280 x 720. All positions/sizes are in virtual pixels;
 * the renderer scales them to fit the actual window.
 */
class GameMenuRenderer {
public:
    GameMenuRenderer();
    ~GameMenuRenderer();

    // ── Layout loading ────────────────────────────────────────────────────────

    /// Load a menu layout JSON. Preloads textures and registers fonts with ImGui.
    /// IMPORTANT: call this before the first ImGui NewFrame() of the session
    /// (i.e., at scene load time, not inside a frame) so that fonts are built
    /// into the atlas before rendering begins.
    /// If vulkanDevice is null, image elements will be skipped.
    void load(const nlohmann::json& layout, Vulkan::VulkanDevice* vulkanDevice = nullptr);

    /// True if a layout is currently loaded.
    bool hasLayout() const { return !layout_.is_null(); }

    /// Clear the loaded layout and reset animation/submenu state.
    void unload();

    // ── Per-frame ─────────────────────────────────────────────────────────────

    /// Call once per frame after ImGui::NewFrame() and before ImGui::Render().
    /// Renders the full-screen menu and processes input.
    /// @param dt  Delta time in seconds (drives animations).
    /// @returns   true if the menu consumed a mouse click this frame.
    bool render(float dt);

    // ── Action callbacks ──────────────────────────────────────────────────────

    /// Called when a button with action "transition_scene" is clicked.
    std::function<void(const std::string& sceneId)> onTransitionScene;

    /// Called when a button with action "quit_game" is clicked.
    std::function<void()> onQuit;

    /// Called when a button with action "load_game" is clicked.
    std::function<void()> onLoadGame;

    // ── MCP / programmatic control ────────────────────────────────────────────

    /// Override the visibility of a named element (any panel, any depth).
    void setElementVisible(const std::string& id, bool visible);

    /// Override the text of a label or button element.
    void setElementText(const std::string& id, const std::string& text);

    /// Override the background color of an element (RGBA 0..1).
    void setElementColor(const std::string& id, float r, float g, float b, float a);

    /// Get the JSON definition of a named element (or null if not found).
    nlohmann::json getElementDef(const std::string& id) const;

    /// Add a new element to a named panel (or the root children if panelId is empty).
    void addElement(const nlohmann::json& elementDef, const std::string& panelId = "");

    /// Remove a named element from whichever panel it belongs to.
    void removeElement(const std::string& id);

    /// Programmatically push a submenu onto the stack (same as clicking open_submenu).
    void openSubmenu(const std::string& panelId);

    /// Pop the current submenu (go back one level).
    void closeSubmenu();

    // ── Virtual canvas ────────────────────────────────────────────────────────
    static constexpr float kVirtualW = 1280.0f;
    static constexpr float kVirtualH = 720.0f;

private:
    // ── Internal state ────────────────────────────────────────────────────────

    nlohmann::json layout_;
    Vulkan::VulkanDevice* device_ = nullptr;

    /// Font registry: "id" → ImFont* (loaded via Fonts->AddFontFromFileTTF).
    std::unordered_map<std::string, ImFont*> fontRegistry_;

    /// Background texture handle (ImTextureID) for background_image mode.
    void* bgTextureId_ = nullptr;

    /// Per-image-element texture cache: element id → ImTextureID.
    std::unordered_map<std::string, void*> imageTextureCache_;

    /// Submenu navigation stack. Each entry is a panel key in layout_["panels"].
    /// Empty → show start_panel.
    std::vector<std::string> submenuStack_;

    /// Accumulated time since last load() call (drives animations).
    float elapsed_ = 0.0f;

    /// Animation start times: element id → time when animation began.
    std::unordered_map<std::string, float> animStartTimes_;

    // Per-element runtime overrides (set via MCP tools / programmatic control)
    struct ElementOverride {
        std::optional<bool>        visible;
        std::optional<std::string> text;
        std::optional<ImVec4>      color;
    };
    std::unordered_map<std::string, ElementOverride> overrides_;

    // ── Rendering ─────────────────────────────────────────────────────────────

    bool renderInternal(ImDrawList* dl, ImVec2 screenSize, float scaleX, float scaleY, float dt);

    void renderElement(ImDrawList* dl, const nlohmann::json& elem,
                       ImVec2 origin, float scaleX, float scaleY, float dt,
                       bool& anyClick);

    bool renderButton(ImDrawList* dl, ImVec2 pos, ImVec2 size,
                      const std::string& text, ImVec4 bgColor, ImVec4 hoverColor,
                      ImVec4 textColor, ImFont* font, float fontSize, float alpha);

    // ── Animation helpers ──────────────────────────────────────────────────────

    /// Returns the alpha multiplier for the element's animation (0..1).
    float getAnimAlpha(const nlohmann::json& elem, float dt);

    /// Returns the XY slide offset in virtual pixels for the element's animation.
    ImVec2 getAnimOffset(const nlohmann::json& elem, float dt);

    // ── Layout helpers ────────────────────────────────────────────────────────

    /// Return the key of the panel currently on top of the stack (or start_panel).
    std::string currentPanelKey() const;

    /// Find a JSON element by id, searching recursively in children arrays.
    nlohmann::json* findElement(const std::string& id);
    const nlohmann::json* findElementConst(const std::string& id) const;

    /// Find the panel JSON object that directly contains element id.
    nlohmann::json* findOwnerPanel(const std::string& id);

    // ── Utility ───────────────────────────────────────────────────────────────

    ImVec4 parseColor(const nlohmann::json& j, ImVec4 def = {1,1,1,1}) const;
    ImFont* getFont(const std::string& fontId) const;
    void    handleAction(const nlohmann::json& action);
    void*   getImageTexture(const std::string& elemId, const std::string& imagePath);
};

} // namespace UI
} // namespace Phyxel
