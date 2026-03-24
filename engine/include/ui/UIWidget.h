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
    bool visible = true;
    bool enabled = true;
    bool hovered = false;
    bool focused = false;
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

    /// Add a child widget. Panel owns it.
    void addChild(std::unique_ptr<UIWidget> widget);

    /// Find a child by id (recursive).
    UIWidget* findChild(const std::string& childId);

    std::string title;
    Anchor anchor = Anchor::Center;
    glm::vec2 offset = {0, 0};
    bool showBackground = true;

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
    bool isTitle = false;  // uses titleColor + titleScale if true
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

} // namespace UI
} // namespace Phyxel
