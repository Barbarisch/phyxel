#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <optional>
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
    Compass,
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

    /// Handle mouse hover (for button highlights and tooltips). The default sets
    /// `hovered` from a plain box test, so ANY widget can carry a tooltip — not only
    /// the types that wanted a hover highlight.
    virtual void handleHover(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme);

    // Common properties
    std::string id;
    glm::vec2 size = {200, 40};
    glm::vec2 position = {0, 0};  // absolute offset within a free-layout parent
    /// JSON "actionBind": the HudDataContext action a click on this (button) invokes,
    /// with the repeater row's record. Empty = a plain button.
    std::string actionBind;
    /// LAYOUT PASS (2026-09-11): the height this widget needs when given `availWidth`,
    /// measured BEFORE it renders. Labels wrap and count lines, repeaters sum their
    /// rows, panels stack their children. Default: the authored size. Auto-sized
    /// panels use it so content is never cut off by a hand-tuned rectangle.
    virtual float measureHeight(const BitmapFont* font, const UITheme& theme, float availWidth) const {
        (void)font; (void)theme; (void)availWidth; return size.y;
    }
    /// The width this widget needs. Default: the authored width. A horizontal repeater
    /// sums its rows, so a panel can hug a data-driven row of items (G-148 - the action
    /// bar's icon slots left the authored 1000 px plate almost entirely empty).
    virtual float measureWidth(const BitmapFont* font, const UITheme& theme) const {
        (void)font; (void)theme; return size.x;
    }
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

    // ── DRAG AND DROP (Ravenmere G-150) ─────────────────────────
    // A widget with a non-empty payload can be PICKED UP: press and hold on it and the
    // pointer carries `dragPayload` until release. JSON "drag" is a fixed payload;
    // "dragBind" pulls one per repeater row ("item.payload").
    //
    // A widget with `dropBind` is a DROP TARGET: releasing a drag over it invokes that
    // HudDataContext action with THIS widget's row plus the reserved keys "_drag" (the
    // payload) and "_drop" ("target"). Releasing over NOTHING invokes the SOURCE's
    // handler instead with "_drop" = "none" - which is how "drag a slot off the bar to
    // clear it" works without any extra machinery.
    //
    // A draggable widget's CLICK moves from press to release (and is suppressed entirely
    // once a drag passes the movement threshold), because otherwise picking up a spell to
    // rearrange it would also cast it. Widgets with no payload keep click-on-press.
    std::string dragPayload;
    std::string dragBind;
    std::string dropBind;
    /// Wired by the binding pass from `dropBind`, exactly as onClick is from actionBind.
    /// UISystem COPIES this by value when a drag starts, so a repeater rebuilding its
    /// rows mid-drag cannot leave the drag holding a dangling widget.
    std::function<void(const std::string& payload, bool onTarget)> onDrop;

    /// HOVER TEXT (Ravenmere G-148). JSON "tooltip" for a fixed string, or "tooltipBind"
    /// for a repeater row's "item.<field>". An action bar of icons is unreadable without
    /// it: the icon says WHICH thing, the tooltip says what it does — and why it is greyed
    /// out. Drawn by UISystem at the pointer, after every screen, so it is never clipped
    /// by the panel it belongs to. Newlines start a new line.
    std::string tooltip;
    std::string tooltipBind;

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
    /// AUTO-SIZE (JSON "autoSize": true): before anchoring/rendering, the panel's height
    /// becomes its measured content height; with "maxSize" [w,h] the height caps there and
    /// the panel turns scrollable. The Objectives panel used to clip its own text, the
    /// Initiative list its last rows (Ravenmere manual test 2026-09-11).
    bool autoHeight = false;
    /// AUTO-WIDTH (JSON "autoWidth": true): the panel's width becomes its measured content
    /// width, capped by "maxSize"[0]. Applied BEFORE the height, since the inner width is
    /// what the height measurement wraps text against.
    bool autoWidth = false;
    glm::vec2 maxSize = {0, 0};   ///< 0 = uncapped
    float measureContentHeight(const BitmapFont* font, const UITheme& theme) const;
    float measureContentWidth(const BitmapFont* font, const UITheme& theme) const;
    void applyAutoSize(const BitmapFont* font, const UITheme& theme);
    float measureHeight(const BitmapFont* font, const UITheme& theme, float availWidth) const override;

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
    float measureHeight(const BitmapFont* font, const UITheme& theme, float availWidth) const override;
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
    /// Grow the width so the label fits (text + 2*padding); `size.x` is the minimum.
    /// For data-driven labels (action bar rows) whose length isn't known when authored.
    bool fitText = false;

    // ── ICON (Ravenmere G-148) ──────────────────────────────────
    // A button may show a PNG instead of its label: JSON "icon" for a fixed path, or
    // "iconBind" for a repeater row's "item.<field>". When an icon draws, the label is
    // NOT drawn — it becomes the tooltip's business. This is why the action bar can be a
    // row of squares rather than a row of sentences.
    std::string iconPath;
    std::string iconBind;
    int  loadedIcon = -1;    ///< UIRenderer texture index (-1 untried, -2 failed)
    /// Inset of the icon inside the button box, in px per side.
    float iconPadding = 4.0f;
    /// JSON "frame": draw a border around the box (theme.panelBorder) with the fill inset
    /// inside it, the way a panel does. An action-bar slot needs it: an EMPTY slot renders
    /// the disabled background, which is near-identical to the bar's own plate, so without
    /// a frame the grid of slots is invisible and there is nothing to aim a drag at.
    bool drawFrame = false;
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

/// Wayfinding strip (Ravenmere G-117 / G-51: "directions to go east mean nothing", and
/// the user wants no quest markers): a horizontal band of cardinal letters that slides
/// with the camera heading, plus named points of interest (scene exits, authored
/// locations) as labelled ticks while they are inside the visible span. `bind` is the
/// heading float key (degrees, 0 = north = +z, 90 = east = +x); `poiBind` the list key
/// whose records carry texts["label"] + floats["bearing"] (degrees, same convention).
class UICompass : public UIWidget {
public:
    WidgetType type() const override { return WidgetType::Compass; }
    void render(UIRenderer* renderer, const BitmapFont* font,
                const UITheme& theme, glm::vec2 pos) override;

    struct Poi { std::string label; float bearing = 0.0f; };
    std::string poiBind;
    float heading = 0.0f;           ///< degrees, set by the binding pass
    float spanDeg = 180.0f;         ///< degrees visible across the strip (±span/2)
    std::vector<Poi> pois;          ///< set by the binding pass

    /// Where a bearing lands on a strip of `width` px centred on `heading`: the x offset
    /// from the centre, or std::nullopt when outside the span. Pure; unit-tested.
    static std::optional<float> offsetFor(float bearing, float heading, float spanDeg, float width);
    /// Signed shortest difference a - b in (-180, 180].
    static float wrapDeg(float a);
};

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
    float measureHeight(const BitmapFont* font, const UITheme& theme, float availWidth) const override;
    float measureWidth(const BitmapFont* font, const UITheme& theme) const override;
    /// Clicks/hover route to the generated items at the positions render() lays them
    /// out (G-106: without this the action bar swallowed every click).
    bool handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) override;
    void handleHover(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) override;
    std::vector<std::unique_ptr<UIWidget>> generated; ///< managed by the binding pass
};

// ════════════════════════════════════════════════════════════════
// Tooltip lookup (Ravenmere G-148)
// ════════════════════════════════════════════════════════════════
// Run AFTER handleHover has walked the tree for this frame: these only read the
// `hovered` flags that pass set, so they need no layout of their own and stay pure.

/// The deepest hovered widget carrying a tooltip, or nullptr. Deepest wins, so a
/// tooltip on a row beats one on the panel behind it.
const UIWidget* hoveredTooltipWidget(const UIWidget* root);

/// The deepest hovered widget that can be PICKED UP, or nullptr (G-150).
UIWidget* hoveredDragSource(UIWidget* root);
/// The deepest hovered widget that ACCEPTS a drop, or nullptr (G-150).
UIWidget* hoveredDropTarget(UIWidget* root);

/// Convenience: that widget's tooltip, or "" when nothing under the pointer has one.
std::string hoveredTooltip(const UIWidget* root);

} // namespace UI
} // namespace Phyxel
