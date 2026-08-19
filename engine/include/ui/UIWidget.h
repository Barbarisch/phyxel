#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <functional>
#include <memory>

namespace Phyxel {
namespace UI {

class UIRenderer;
class BitmapFont;

// ════════════════════════════════════════════════════════════════
// Theme
// ════════════════════════════════════════════════════════════════

/// Color palette and sizing for the widget system.
struct UITheme {
    // Panel / background
    glm::vec4 panelBg       = {0.12f, 0.12f, 0.15f, 0.92f};
    glm::vec4 panelBorder   = {0.35f, 0.35f, 0.40f, 1.0f};

    // Text
    glm::vec4 textColor     = {0.95f, 0.92f, 0.85f, 1.0f};
    glm::vec4 titleColor    = {0.95f, 0.85f, 0.55f, 1.0f};
    glm::vec4 disabledColor = {0.5f, 0.5f, 0.5f, 1.0f};

    // Button
    glm::vec4 buttonBg      = {0.25f, 0.25f, 0.30f, 1.0f};
    glm::vec4 buttonHover   = {0.35f, 0.35f, 0.42f, 1.0f};
    glm::vec4 buttonActive  = {0.45f, 0.40f, 0.30f, 1.0f};
    glm::vec4 buttonText    = {0.95f, 0.92f, 0.85f, 1.0f};

    // Slider
    glm::vec4 sliderTrack   = {0.20f, 0.20f, 0.25f, 1.0f};
    glm::vec4 sliderFill    = {0.55f, 0.45f, 0.25f, 1.0f};
    glm::vec4 sliderKnob    = {0.80f, 0.70f, 0.45f, 1.0f};

    // Checkbox
    glm::vec4 checkboxBg    = {0.20f, 0.20f, 0.25f, 1.0f};
    glm::vec4 checkboxCheck = {0.55f, 0.80f, 0.40f, 1.0f};

    // Dropdown
    glm::vec4 dropdownBg    = {0.20f, 0.20f, 0.25f, 1.0f};
    glm::vec4 dropdownItem  = {0.30f, 0.30f, 0.35f, 1.0f};

    // Dimensions
    float textScale    = 2.0f;   // Scale for body text (8px * 2 = 16px)
    float titleScale   = 3.0f;   // Scale for titles
    float padding      = 8.0f;
    float itemSpacing  = 6.0f;
    float buttonHeight = 40.0f;
    float sliderHeight = 24.0f;
    float borderWidth  = 2.0f;

    // Runtime-only (NOT an authored theme value): seconds since the screen being
    // rendered was last shown. UISystem sets this per screen before rendering it;
    // widgets read it to drive appear animations. Defaults far past any animation
    // so hosts that never stamp it render the settled state.
    float screenElapsed = 1.0e9f;
};

// ════════════════════════════════════════════════════════════════
// Anchor / Layout
// ════════════════════════════════════════════════════════════════

/// How a widget anchors to its parent.
enum class Anchor {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
};

/// Convert anchor + size + offset → absolute position within parent rect.
glm::vec2 resolveAnchor(Anchor anchor, glm::vec2 parentPos, glm::vec2 parentSize,
                         glm::vec2 size, glm::vec2 offset = {0, 0});

// ════════════════════════════════════════════════════════════════
// Widget types
// ════════════════════════════════════════════════════════════════

enum class WidgetType {
    Panel,
    Label,
    Button,
    Slider,
    Checkbox,
    Dropdown,
    Image,
    ProgressBar,
    Repeater,
    TextInput,
};

// ════════════════════════════════════════════════════════════════
// UIWidget — base class
// ════════════════════════════════════════════════════════════════

class UIWidget {
public:
    virtual ~UIWidget() = default;

    virtual WidgetType type() const = 0;

    /// Render this widget. pos is the resolved screen position.
    virtual void render(UIRenderer* renderer, const BitmapFont* font,
                        const UITheme& theme, glm::vec2 pos) = 0;

    /// Handle a mouse click at the given screen coordinate. Returns true if consumed.
    virtual bool handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) { return false; }

    /// Handle key press for text input or keyboard navigation. Returns true if consumed.
    virtual bool handleKey(int glfwKey) { return false; }

    /// Handle mouse drag (for sliders).
    virtual bool handleDrag(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) { return false; }

    /// Handle mouse hover (for button highlights).
    virtual void handleHover(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) {}

    // Common properties
    std::string id;
    glm::vec2 size = {200, 40};
    glm::vec2 position = {0, 0};  // absolute offset within a free-layout parent
    bool visible = true;
    bool enabled = true;
    bool hovered = false;
    bool focused = false;

    /// Optional data-binding key. When set, a HudDataContext pulls the live value
    /// for this key into the widget each frame before render (see HudDataContext).
    std::string bind;

    /// Optional visibility-binding key. When set, the HudDataContext sets `visible`
    /// from the named float provider each frame (>0.5 → visible). Used to show a HUD
    /// element only in certain states (e.g. "combat.inCombat").
    std::string visibleWhen;

    // ── Appear animation (menu polish) ──────────────────────────
    // Same schema as the retired ImGui GameMenuRenderer so existing authored
    // menus (menu_demo.json, MenuEditorPanel) keep working: JSON "animation"
    // (fade_in / slide_in_left / slide_in_right / slide_in_up),
    // "animation_delay", "animation_duration". All types fade alpha in; slides
    // additionally offset 80px along their axis, ease-out cubic. Driven by
    // UITheme::screenElapsed — replays every time the screen is (re)shown.
    enum class AppearAnim { None, FadeIn, SlideInLeft, SlideInRight, SlideInUp };
    AppearAnim appearAnim = AppearAnim::None;
    float appearDelay    = 0.0f;
    float appearDuration = 0.4f;

    /// Evaluate the appear animation at `elapsed` seconds since screen show.
    /// Returns false when no animation applies (none authored, or settled) —
    /// callers skip the renderer anim push entirely in that case.
    bool computeAppear(float elapsed, float& alphaOut, glm::vec2& offsetOut) const;
};

// ════════════════════════════════════════════════════════════════
// Panel — container with optional title and auto-layout children
// ════════════════════════════════════════════════════════════════

class UIPanel : public UIWidget {
public:
    WidgetType type() const override { return WidgetType::Panel; }
    void render(UIRenderer* renderer, const BitmapFont* font,
                const UITheme& theme, glm::vec2 pos) override;
    bool handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) override;
    bool handleDrag(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) override;
    void handleHover(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) override;

    /// Contain children to the panel's box (default). JSON "clip": false opts
    /// out for intentional overhang. Panels with zero size never clip.
    bool clipChildren = true;

    /// Scrollable content (JSON "scrollable": true): the mouse wheel over the
    /// panel shifts children vertically, content clips to the box, and a slim
    /// scrollbar renders when content overflows. Quest logs, long lists,
    /// dialogue histories. Both free and flow layouts.
    bool scrollable = false;
    float scrollOffset = 0.0f;    ///< current scroll in px (0 = top), clamped in render
    float contentHeight = 0.0f;   ///< measured during render

    /// Wheel input. Returns true when consumed (hit a scrollable panel whose
    /// content overflows). Recurses into children first so nested scrollables
    /// win over their parents.
    bool handleScroll(glm::vec2 mousePos, glm::vec2 widgetPos, float delta, const UITheme& theme);

    /// Add a child widget. Panel owns it.
    void addChild(std::unique_ptr<UIWidget> widget);

private:
    void drawScrollbar(UIRenderer* renderer, const UITheme& theme, glm::vec2 pos);

public:

    /// Find a child by id (recursive).
    UIWidget* findChild(const std::string& childId);

    std::string title;
    Anchor anchor = Anchor::Center;
    glm::vec2 offset = {0, 0};
    bool showBackground = true;
    /// When true, children are placed at their absolute `position` (relative to the
    /// panel) instead of auto-stacked vertically. Used for menus/free-form layouts.
    bool freeLayout = false;

    std::vector<std::unique_ptr<UIWidget>> children;

private:
    const BitmapFont* cachedFont_ = nullptr; // set during render for input layout
};

// ════════════════════════════════════════════════════════════════
// Label — static text
// ════════════════════════════════════════════════════════════════

class UILabel : public UIWidget {
public:
    WidgetType type() const override { return WidgetType::Label; }
    void render(UIRenderer* renderer, const BitmapFont* font,
                const UITheme& theme, glm::vec2 pos) override;

    std::string text;
    bool isTitle = false;     // uses titleColor + titleScale if true
    float wrapWidth = 0.0f;   // >0 = word-wrap to this pixel width (multi-line)
    /// Horizontal alignment relative to `position.x`:
    ///   Left   (default) — text STARTS at position.x (historical behavior)
    ///   Center           — text is centered ON position.x (what every shipped
    ///                      screen JSON authored: position 640 = screen center)
    ///   Right            — text ENDS at position.x
    enum class HAlign { Left, Center, Right };
    HAlign align = HAlign::Left;

    // Per-element overrides (JSON "color" / "scale"). alpha 0 / scale 0 = unset
    // → theme colors and scales apply. Scale is absolute font scale (theme body
    // text is 2.0, titles 3.0).
    glm::vec4 customColor = {0, 0, 0, 0};
    float customScale = 0.0f;
};

// ════════════════════════════════════════════════════════════════
// Button — clickable
// ════════════════════════════════════════════════════════════════

class UIButton : public UIWidget {
public:
    WidgetType type() const override { return WidgetType::Button; }
    void render(UIRenderer* renderer, const BitmapFont* font,
                const UITheme& theme, glm::vec2 pos) override;
    bool handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) override;
    void handleHover(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) override;

    std::string text;
    std::function<void()> onClick;

    // Per-element overrides (JSON "color" = text, "bg" = background,
    // "bgHover" = hover background; unset hover = bg lightened 25%).
    // alpha 0 = unset → theme colors apply.
    glm::vec4 customColor   = {0, 0, 0, 0};
    glm::vec4 customBg      = {0, 0, 0, 0};
    glm::vec4 customBgHover = {0, 0, 0, 0};
};

// ════════════════════════════════════════════════════════════════
// Slider — float value
// ════════════════════════════════════════════════════════════════

class UISlider : public UIWidget {
public:
    WidgetType type() const override { return WidgetType::Slider; }
    void render(UIRenderer* renderer, const BitmapFont* font,
                const UITheme& theme, glm::vec2 pos) override;
    bool handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) override;
    bool handleDrag(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) override;

    std::string label;
    float value = 0.5f;
    float minVal = 0.0f;
    float maxVal = 1.0f;
    std::function<void(float)> onChange;
};

// ════════════════════════════════════════════════════════════════
// Checkbox — boolean toggle
// ════════════════════════════════════════════════════════════════

class UICheckbox : public UIWidget {
public:
    WidgetType type() const override { return WidgetType::Checkbox; }
    void render(UIRenderer* renderer, const BitmapFont* font,
                const UITheme& theme, glm::vec2 pos) override;
    bool handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) override;

    std::string label;
    bool checked = false;
    std::function<void(bool)> onChange;
};

// ════════════════════════════════════════════════════════════════
// TextInput — single-line editable text field (keyboard capture)
// ════════════════════════════════════════════════════════════════

class UITextInput : public UIWidget {
public:
    WidgetType type() const override { return WidgetType::TextInput; }
    void render(UIRenderer* renderer, const BitmapFont* font,
                const UITheme& theme, glm::vec2 pos) override;
    bool handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) override;

    std::string text;            ///< Current buffer contents.
    std::string placeholder;     ///< Shown (dimmed) when empty + unfocused.
    bool focused = false;        ///< Receives typed chars while true.
    size_t maxLength = 255;
    float caretTimer = 0.0f;     ///< Blink accumulator (advanced in render).
    std::function<void(const std::string&)> onSubmit;  ///< Enter pressed.
    std::function<void(const std::string&)> onChange;  ///< Text edited.
};

// ════════════════════════════════════════════════════════════════
// Dropdown — select from a list
// ════════════════════════════════════════════════════════════════

class UIDropdown : public UIWidget {
public:
    WidgetType type() const override { return WidgetType::Dropdown; }
    void render(UIRenderer* renderer, const BitmapFont* font,
                const UITheme& theme, glm::vec2 pos) override;
    bool handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) override;

    std::string label;
    std::vector<std::string> options;
    int selectedIndex = 0;
    bool open = false;
    std::function<void(int)> onChange;
};

// ════════════════════════════════════════════════════════════════
// UIImage — textured rectangle (PNG file, or colored placeholder)
// ════════════════════════════════════════════════════════════════

class UIImage : public UIWidget {
public:
    WidgetType type() const override { return WidgetType::Image; }
    void render(UIRenderer* renderer, const BitmapFont* font,
                const UITheme& theme, glm::vec2 pos) override;

    std::string imagePath;  ///< Relative or absolute path to a PNG file
    glm::vec4 tintColor = {1.0f, 1.0f, 1.0f, 1.0f};
    void* textureHandle = nullptr; ///< Platform-specific loaded texture (ImTextureID, editor path)
    int loadedTexture = -1; ///< Cached UIRenderer texture index (-1 not attempted, -2 failed)
};

// ════════════════════════════════════════════════════════════════
// UIProgressBar — horizontal fill bar (health/resource), drawRect-based
// ════════════════════════════════════════════════════════════════

class UIProgressBar : public UIWidget {
public:
    WidgetType type() const override { return WidgetType::ProgressBar; }
    void render(UIRenderer* renderer, const BitmapFont* font,
                const UITheme& theme, glm::vec2 pos) override;

    std::string label;             ///< Optional prefix shown in the value text
    float value  = 1.0f;           ///< Current value (clamped to [minVal, maxVal])
    float minVal = 0.0f;
    float maxVal = 1.0f;
    bool  showValueText = true;    ///< Draw "cur/max" centered over the bar

    glm::vec4 fillColor   = {0.75f, 0.20f, 0.20f, 1.0f}; ///< default: health red
    glm::vec4 trackColor  = {0.12f, 0.12f, 0.15f, 0.85f};
    glm::vec4 borderColor = {0.0f, 0.0f, 0.0f, 0.9f};
};

// ════════════════════════════════════════════════════════════════
// UIRepeater — one cloned child per record from a list data-binding
// ════════════════════════════════════════════════════════════════

class UIRepeater : public UIWidget {
public:
    WidgetType type() const override { return WidgetType::Repeater; }
    void render(UIRenderer* renderer, const BitmapFont* font,
                const UITheme& theme, glm::vec2 pos) override;

    /// `bind` (inherited) names a list provider. The HUD binding pass rebuilds
    /// `generated` (one item per record from itemTemplateJson) and binds each item's
    /// "item.<field>" widgets from the record. Items stack vertically.
    std::string itemTemplateJson;   ///< Serialized JSON of one item's widget def
    float itemSpacing = 4.0f;
    bool  horizontal = false;       ///< Lay items left-to-right instead of top-down
    std::vector<std::unique_ptr<UIWidget>> generated; ///< managed by the binding pass
};

} // namespace UI
} // namespace Phyxel
