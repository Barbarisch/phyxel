#include "ui/UIWidget.h"
#include "ui/UIRenderer.h"
#include "ui/BitmapFont.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Phyxel {
namespace UI {

// ════════════════════════════════════════════════════════════════
// Anchor resolution
// ════════════════════════════════════════════════════════════════

glm::vec2 resolveAnchor(Anchor anchor, glm::vec2 parentPos, glm::vec2 parentSize,
                         glm::vec2 size, glm::vec2 offset) {
    glm::vec2 pos;
    switch (anchor) {
        case Anchor::TopLeft:      pos = parentPos; break;
        case Anchor::TopCenter:    pos = {parentPos.x + (parentSize.x - size.x) * 0.5f, parentPos.y}; break;
        case Anchor::TopRight:     pos = {parentPos.x + parentSize.x - size.x, parentPos.y}; break;
        case Anchor::CenterLeft:   pos = {parentPos.x, parentPos.y + (parentSize.y - size.y) * 0.5f}; break;
        case Anchor::Center:       pos = parentPos + (parentSize - size) * 0.5f; break;
        case Anchor::CenterRight:  pos = {parentPos.x + parentSize.x - size.x, parentPos.y + (parentSize.y - size.y) * 0.5f}; break;
        case Anchor::BottomLeft:   pos = {parentPos.x, parentPos.y + parentSize.y - size.y}; break;
        case Anchor::BottomCenter: pos = {parentPos.x + (parentSize.x - size.x) * 0.5f, parentPos.y + parentSize.y - size.y}; break;
        case Anchor::BottomRight:  pos = parentPos + parentSize - size; break;
    }
    return pos + offset;
}

static bool hitTest(glm::vec2 mouse, glm::vec2 pos, glm::vec2 sz) {
    return mouse.x >= pos.x && mouse.x < pos.x + sz.x &&
           mouse.y >= pos.y && mouse.y < pos.y + sz.y;
}

// Compute Y offset where children start inside a panel (matches render layout)
static float panelContentStartY(const std::string& title, const BitmapFont* font, const UITheme& theme) {
    float y = theme.padding;
    if (!title.empty() && font) {
        y += font->lineHeight(theme.titleScale) + theme.itemSpacing * 2;
    }
    return y;
}

// ════════════════════════════════════════════════════════════════
// UIPanel
// ════════════════════════════════════════════════════════════════

void UIPanel::addChild(std::unique_ptr<UIWidget> widget) {
    children.push_back(std::move(widget));
}

UIWidget* UIPanel::findChild(const std::string& childId) {
    for (auto& child : children) {
        if (child->id == childId) return child.get();
        if (child->type() == WidgetType::Panel) {
            auto* found = static_cast<UIPanel*>(child.get())->findChild(childId);
            if (found) return found;
        }
    }
    return nullptr;
}

void UIPanel::render(UIRenderer* renderer, const BitmapFont* font,
                     const UITheme& theme, glm::vec2 pos) {
    if (!visible) return;

    if (showBackground) {
        renderer->drawRect(pos, size, theme.panelBorder);
        float bw = theme.borderWidth;
        renderer->drawRect(pos + glm::vec2(bw), size - glm::vec2(bw * 2), theme.panelBg);
    }

    float yOffset = theme.padding;

    if (!title.empty()) {
        float tw = font->measureText(title, theme.titleScale);
        float tx = pos.x + (size.x - tw) * 0.5f;
        font->drawText(renderer, title, {tx, pos.y + yOffset}, theme.titleColor, theme.titleScale);
        yOffset += font->lineHeight(theme.titleScale) + theme.itemSpacing * 2;
    }

    for (auto& child : children) {
        if (!child->visible) continue;
        float cx = pos.x + theme.padding;
        float cy = pos.y + yOffset;
        child->render(renderer, font, theme, {cx, cy});
        yOffset += child->size.y + theme.itemSpacing;
    }

    cachedFont_ = font;
}

bool UIPanel::handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) {
    if (!visible || !enabled) return false;
    if (!hitTest(mousePos, widgetPos, size)) return false;

    float yOffset = panelContentStartY(title, cachedFont_, theme);

    for (auto& child : children) {
        if (!child->visible) continue;
        float cx = widgetPos.x + theme.padding;
        float cy = widgetPos.y + yOffset;
        if (child->handleClick(mousePos, {cx, cy}, theme)) return true;
        yOffset += child->size.y + theme.itemSpacing;
    }
    return true;
}

bool UIPanel::handleDrag(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) {
    if (!visible || !enabled) return false;

    float yOffset = panelContentStartY(title, cachedFont_, theme);

    for (auto& child : children) {
        if (!child->visible) continue;
        float cx = widgetPos.x + theme.padding;
        float cy = widgetPos.y + yOffset;
        if (child->handleDrag(mousePos, {cx, cy}, theme)) return true;
        yOffset += child->size.y + theme.itemSpacing;
    }
    return false;
}

void UIPanel::handleHover(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) {
    if (!visible) return;

    float yOffset = panelContentStartY(title, cachedFont_, theme);

    for (auto& child : children) {
        if (!child->visible) continue;
        float cx = widgetPos.x + theme.padding;
        float cy = widgetPos.y + yOffset;
        child->handleHover(mousePos, {cx, cy}, theme);
        yOffset += child->size.y + theme.itemSpacing;
    }
}

// ════════════════════════════════════════════════════════════════
// UILabel
// ════════════════════════════════════════════════════════════════

void UILabel::render(UIRenderer* renderer, const BitmapFont* font,
                     const UITheme& theme, glm::vec2 pos) {
    if (!visible) return;
    float scale = isTitle ? theme.titleScale : theme.textScale;
    glm::vec4 color = enabled ? (isTitle ? theme.titleColor : theme.textColor) : theme.disabledColor;
    font->drawText(renderer, text, pos, color, scale);
    size.x = font->measureText(text, scale);
    size.y = font->lineHeight(scale);
}

// ════════════════════════════════════════════════════════════════
// UIButton
// ════════════════════════════════════════════════════════════════

void UIButton::render(UIRenderer* renderer, const BitmapFont* font,
                      const UITheme& theme, glm::vec2 pos) {
    if (!visible) return;

    glm::vec4 bg = hovered ? theme.buttonHover : theme.buttonBg;
    if (!enabled) bg = theme.panelBg;

    renderer->drawRect(pos, size, bg);

    float textW = font->measureText(text, theme.textScale);
    float textH = font->lineHeight(theme.textScale);
    glm::vec2 textPos = {
        pos.x + (size.x - textW) * 0.5f,
        pos.y + (size.y - textH) * 0.5f
    };
    glm::vec4 textColor = enabled ? theme.buttonText : theme.disabledColor;
    font->drawText(renderer, text, textPos, textColor, theme.textScale);
}

bool UIButton::handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& /*theme*/) {
    if (!visible || !enabled) return false;
    if (hitTest(mousePos, widgetPos, size)) {
        if (onClick) onClick();
        return true;
    }
    return false;
}

void UIButton::handleHover(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& /*theme*/) {
    hovered = visible && enabled && hitTest(mousePos, widgetPos, size);
}

// ════════════════════════════════════════════════════════════════
// UISlider
// ════════════════════════════════════════════════════════════════

void UISlider::render(UIRenderer* renderer, const BitmapFont* font,
                      const UITheme& theme, glm::vec2 pos) {
    if (!visible) return;

    float labelW = 0;
    if (!label.empty()) {
        font->drawText(renderer, label, pos, theme.textColor, theme.textScale);
        labelW = font->measureText(label, theme.textScale) + theme.padding;
    }

    char valBuf[32];
    snprintf(valBuf, sizeof(valBuf), "%.2f", value);
    float valW = font->measureText(valBuf, theme.textScale) + theme.padding;

    float trackX = pos.x + labelW;
    float trackW = size.x - labelW - valW;
    float trackY = pos.y + (size.y - theme.sliderHeight) * 0.5f;
    renderer->drawRect({trackX, trackY}, {trackW, theme.sliderHeight}, theme.sliderTrack);

    float t = (maxVal > minVal) ? (value - minVal) / (maxVal - minVal) : 0.0f;
    float fillW = trackW * t;
    renderer->drawRect({trackX, trackY}, {fillW, theme.sliderHeight}, theme.sliderFill);

    float knobW = 8.0f;
    float knobX = trackX + fillW - knobW * 0.5f;
    renderer->drawRect({knobX, trackY - 2}, {knobW, theme.sliderHeight + 4}, theme.sliderKnob);

    font->drawText(renderer, valBuf, {trackX + trackW + theme.padding, pos.y}, theme.textColor, theme.textScale);
}

bool UISlider::handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) {
    if (!visible || !enabled) return false;
    if (!hitTest(mousePos, widgetPos, size)) return false;

    float labelW = label.empty() ? 0.0f : (static_cast<float>(label.size()) * BitmapFont::GLYPH_W * theme.textScale + theme.padding);
    char valBuf[32];
    snprintf(valBuf, sizeof(valBuf), "%.2f", value);
    float valW = static_cast<float>(strlen(valBuf)) * BitmapFont::GLYPH_W * theme.textScale + theme.padding;

    float trackX = widgetPos.x + labelW;
    float trackW = size.x - labelW - valW;
    if (trackW > 0) {
        float newT = std::clamp((mousePos.x - trackX) / trackW, 0.0f, 1.0f);
        value = minVal + newT * (maxVal - minVal);
        if (onChange) onChange(value);
    }
    return true;
}

bool UISlider::handleDrag(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) {
    return handleClick(mousePos, widgetPos, theme);
}

// ════════════════════════════════════════════════════════════════
// UICheckbox
// ════════════════════════════════════════════════════════════════

void UICheckbox::render(UIRenderer* renderer, const BitmapFont* font,
                        const UITheme& theme, glm::vec2 pos) {
    if (!visible) return;

    float boxSize = size.y - 4;
    float boxY = pos.y + 2;

    renderer->drawRect({pos.x, boxY}, {boxSize, boxSize}, theme.checkboxBg);
    if (checked) {
        float inset = 3.0f;
        renderer->drawRect({pos.x + inset, boxY + inset},
                          {boxSize - inset * 2, boxSize - inset * 2}, theme.checkboxCheck);
    }

    if (!label.empty()) {
        float textY = pos.y + (size.y - font->lineHeight(theme.textScale)) * 0.5f;
        font->drawText(renderer, label, {pos.x + boxSize + theme.padding, textY},
                       enabled ? theme.textColor : theme.disabledColor, theme.textScale);
    }
}

bool UICheckbox::handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& /*theme*/) {
    if (!visible || !enabled) return false;
    if (hitTest(mousePos, widgetPos, size)) {
        checked = !checked;
        if (onChange) onChange(checked);
        return true;
    }
    return false;
}

// ════════════════════════════════════════════════════════════════
// UIDropdown
// ════════════════════════════════════════════════════════════════

void UIDropdown::render(UIRenderer* renderer, const BitmapFont* font,
                        const UITheme& theme, glm::vec2 pos) {
    if (!visible) return;

    float labelW = 0;
    if (!label.empty()) {
        font->drawText(renderer, label, pos, theme.textColor, theme.textScale);
        labelW = font->measureText(label, theme.textScale) + theme.padding;
    }

    float boxX = pos.x + labelW;
    float boxW = size.x - labelW;
    renderer->drawRect({boxX, pos.y}, {boxW, size.y}, theme.dropdownBg);

    if (selectedIndex >= 0 && selectedIndex < (int)options.size()) {
        float textY = pos.y + (size.y - font->lineHeight(theme.textScale)) * 0.5f;
        font->drawText(renderer, options[selectedIndex],
                       {boxX + theme.padding, textY}, theme.textColor, theme.textScale);
    }

    const char* arrow = open ? "^" : "v";
    float arrowW = font->measureText(arrow, theme.textScale);
    font->drawText(renderer, arrow,
                   {boxX + boxW - arrowW - theme.padding,
                    pos.y + (size.y - font->lineHeight(theme.textScale)) * 0.5f},
                   theme.textColor, theme.textScale);

    if (open) {
        float itemH = size.y;
        for (int i = 0; i < (int)options.size(); ++i) {
            float iy = pos.y + size.y + i * itemH;
            glm::vec4 bg = (i == selectedIndex) ? theme.sliderFill : theme.dropdownItem;
            renderer->drawRect({boxX, iy}, {boxW, itemH}, bg);
            float textY = iy + (itemH - font->lineHeight(theme.textScale)) * 0.5f;
            font->drawText(renderer, options[i], {boxX + theme.padding, textY},
                           theme.textColor, theme.textScale);
        }
    }
}

bool UIDropdown::handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) {
    if (!visible || !enabled) return false;

    float labelW = label.empty() ? 0.0f : (static_cast<float>(label.size()) * BitmapFont::GLYPH_W * theme.textScale + theme.padding);
    float boxX = widgetPos.x + labelW;
    float boxW = size.x - labelW;

    if (open) {
        float itemH = size.y;
        for (int i = 0; i < (int)options.size(); ++i) {
            float iy = widgetPos.y + size.y + i * itemH;
            if (hitTest(mousePos, {boxX, iy}, {boxW, itemH})) {
                selectedIndex = i;
                open = false;
                if (onChange) onChange(selectedIndex);
                return true;
            }
        }
        open = false;
        return true;
    }

    if (hitTest(mousePos, {boxX, widgetPos.y}, {boxW, size.y})) {
        open = !open;
        return true;
    }
    return false;
}

} // namespace UI
} // namespace Phyxel
