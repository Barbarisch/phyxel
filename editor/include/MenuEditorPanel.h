#pragma once

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>
#include <imgui.h>

namespace Phyxel::Editor {

/// Types of UI elements that can appear in a menu scene layout.
enum class MenuElementType { Panel, Button, Label };

/// An action associated with a button element.
struct MenuButtonAction {
    std::string type;   ///< "transition_scene", "quit_game", "open_submenu"
    std::string target; ///< Scene ID, submenu ID, etc.
};

/// A single element in a menu layout.
struct MenuElementDef {
    std::string       id;
    MenuElementType   type   = MenuElementType::Button;
    std::string       text;
    float             x      = 0.0f;   ///< Position in 1280x720 virtual canvas (pixels)
    float             y      = 0.0f;
    float             width  = 200.0f;
    float             height = 48.0f;
    ImVec4            color  = {0.25f, 0.25f, 0.30f, 1.0f};
    ImVec4            textColor = {0.95f, 0.92f, 0.85f, 1.0f};
    MenuButtonAction  action;           ///< Only used for Button type
};

/// Background type for the menu canvas.
enum class MenuBackgroundType { SolidColor, WorldScene };

/**
 * @brief Dockable ImGui panel for editing menu-scene layouts.
 *
 * Shown by Application when the active scene is of type SceneType::Menu.
 * Provides an element list, a WYSIWYG 2D canvas preview, and a properties
 * sub-panel for the selected element. The menu layout is serialized to/from
 * nlohmann::json compatible with MenuDefinition::buildFromJson().
 */
class MenuEditorPanel {
public:
    MenuEditorPanel();

    // ── Wiring ───────────────────────────────────────────────────────────────

    /// Called when the user presses "Save" — passes the serialized JSON.
    std::function<void(const nlohmann::json&)> onSave;

    /// Name of the menu scene being edited (shown in the title bar).
    void setSceneName(const std::string& name) { m_sceneName = name; }

    /// Load a layout JSON into the editor (replaces current state).
    void loadFromJson(const nlohmann::json& j);

    /// Serialize the current state to MenuDefinition-compatible JSON.
    nlohmann::json toJson() const;

    // ── Rendering ────────────────────────────────────────────────────────────

    /// Render the full panel as a dockable ImGui window.
    void render(bool* open = nullptr);

private:
    // Internal helpers
    void renderToolbar();
    void renderElementList();
    void renderCanvas();
    void renderElementProperties();

    std::string                  m_sceneName;
    std::vector<MenuElementDef>  m_elements;
    int                          m_selectedIdx = -1;
    bool                         m_dragging    = false;
    ImVec2                       m_dragOffset  = {0, 0};

    // Background
    MenuBackgroundType m_backgroundType = MenuBackgroundType::SolidColor;
    ImVec4             m_backgroundColor = {0.06f, 0.06f, 0.10f, 1.0f};

    // Canvas virtual resolution
    static constexpr float kVirtualW = 1280.0f;
    static constexpr float kVirtualH = 720.0f;

    // Element ID counter
    int m_nextId = 1;

    // Auto-generate a unique element ID string
    std::string nextElementId(const char* prefix);

    // Convert MenuDefinition JSON type string to/from enum
    static MenuElementType typeFromString(const std::string& s);
    static const char*     typeToString(MenuElementType t);

    // Char buffers for ImGui::InputText fields (avoid re-alloc each frame)
    char m_editId[64]     = {};
    char m_editText[256]  = {};
    char m_editTarget[128] = {};
    bool m_editDirty      = false;   ///< Buffers need repopulating on selection change
};

} // namespace Phyxel::Editor
