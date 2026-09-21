#include "ui/UIWidget.h"
#include "ui/UIRenderer.h"
#include "ui/BitmapFont.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>

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

// Default hover: a plain box test, so any widget can be a tooltip target (G-148). The
// types that want more (buttons lighten, sliders track a drag) override this.
void UIWidget::handleHover(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& /*theme*/) {
    hovered = visible && hitTest(mousePos, widgetPos, size);
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
// UIWidget — appear animation
// ════════════════════════════════════════════════════════════════

bool UIWidget::computeAppear(float elapsed, float& alphaOut, glm::vec2& offsetOut) const {
    if (appearAnim == AppearAnim::None) return false;
    const float dur = appearDuration > 0.0f ? appearDuration : 0.0001f;
    float t = (elapsed - appearDelay) / dur;
    if (t >= 1.0f) return false;                                  // settled — no push needed
    t = std::max(0.0f, t);
    const float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);  // ease-out cubic (GameMenuRenderer)
    alphaOut = e;
    const float rem = (1.0f - e) * 80.0f;                         // slide distance, matches old renderer
    switch (appearAnim) {
        case AppearAnim::SlideInLeft:  offsetOut = {-rem, 0.0f}; break;
        case AppearAnim::SlideInRight: offsetOut = { rem, 0.0f}; break;
        case AppearAnim::SlideInUp:    offsetOut = {0.0f, -rem}; break;  // enters from above
        default:                       offsetOut = {0.0f, 0.0f}; break;  // fade_in
    }
    return true;
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

    // Contain the children: content can never draw outside the panel's box
    // (long labels, overflowing rows). Nested panels intersect their clips.
    // Panels sized 0 (auto/fullscreen overlays) don't clip; "clip": false in
    // JSON opts a panel out for intentional overhang.
    const bool doClip = clipChildren && size.x > 0.0f && size.y > 0.0f;
    if (doClip) renderer->pushClip(pos, size);

    // Scrolling: clamp against last frame's measured content, shift children.
    if (scrollable) {
        const float maxScroll = std::max(0.0f, contentHeight - size.y);
        scrollOffset = std::clamp(scrollOffset, 0.0f, maxScroll);
    }
    const glm::vec2 scrollVec = scrollable ? glm::vec2(0.0f, scrollOffset) : glm::vec2(0.0f);

    if (freeLayout) {
        float maxExtent = 0.0f;
        for (auto& child : children) {
            if (!child->visible) continue;
            float animAlpha; glm::vec2 animOff;
            const bool animating = child->computeAppear(theme.screenElapsed, animAlpha, animOff);
            if (animating) renderer->pushAnim(animAlpha, animOff);
            child->render(renderer, font, theme, pos + child->position - scrollVec);
            if (animating) renderer->popAnim();
            maxExtent = std::max(maxExtent, child->position.y + child->size.y);
        }
        contentHeight = maxExtent;
        cachedFont_ = font;
        drawScrollbar(renderer, theme, pos);
        if (doClip) renderer->popClip();
        return;
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
        float cy = pos.y + yOffset - scrollVec.y;
        float animAlpha; glm::vec2 animOff;
        const bool animating = child->computeAppear(theme.screenElapsed, animAlpha, animOff);
        if (animating) renderer->pushAnim(animAlpha, animOff);
        child->render(renderer, font, theme, {cx, cy});
        if (animating) renderer->popAnim();
        yOffset += child->size.y + theme.itemSpacing;
    }
    contentHeight = yOffset;

    cachedFont_ = font;
    drawScrollbar(renderer, theme, pos);
    if (doClip) renderer->popClip();
}

// Slim right-edge scrollbar, drawn only when scrollable content overflows.
void UIPanel::drawScrollbar(UIRenderer* renderer, const UITheme& theme, glm::vec2 pos) {
    if (!scrollable || contentHeight <= size.y || size.y <= 8.0f) return;
    const float trackX = pos.x + size.x - 6.0f;
    const float trackY = pos.y + 2.0f;
    const float trackH = size.y - 4.0f;
    // Fixed translucent colors, not theme greys — the bar must read against the
    // panel it sits on regardless of the theme's panel color.
    renderer->drawRect({trackX, trackY}, {4.0f, trackH}, {0.0f, 0.0f, 0.0f, 0.35f});
    const float frac   = size.y / contentHeight;
    const float thumbH = std::max(12.0f, trackH * frac);
    const float maxScroll = contentHeight - size.y;
    const float t = maxScroll > 0.0f ? scrollOffset / maxScroll : 0.0f;
    renderer->drawRect({trackX, trackY + (trackH - thumbH) * t}, {4.0f, thumbH},
                       {1.0f, 1.0f, 1.0f, 0.55f});
}

bool UIPanel::handleScroll(glm::vec2 mousePos, glm::vec2 widgetPos, float delta, const UITheme& theme) {
    if (!visible || !enabled) return false;
    if (!hitTest(mousePos, widgetPos, size)) return false;

    const glm::vec2 scrollVec = scrollable ? glm::vec2(0.0f, scrollOffset) : glm::vec2(0.0f);

    // Nested scrollables win over their parents.
    if (freeLayout) {
        for (auto& child : children) {
            if (!child->visible) continue;
            if (auto* p = dynamic_cast<UIPanel*>(child.get()))
                if (p->handleScroll(mousePos, widgetPos + child->position - scrollVec, delta, theme))
                    return true;
        }
    } else {
        float yOffset = panelContentStartY(title, cachedFont_, theme);
        for (auto& child : children) {
            if (!child->visible) continue;
            if (auto* p = dynamic_cast<UIPanel*>(child.get()))
                if (p->handleScroll(mousePos, {widgetPos.x + theme.padding,
                                               widgetPos.y + yOffset - scrollVec.y}, delta, theme))
                    return true;
            yOffset += child->size.y + theme.itemSpacing;
        }
    }

    if (!scrollable || contentHeight <= size.y) return false;
    const float step = 48.0f;                 // px per wheel notch
    scrollOffset = std::clamp(scrollOffset - delta * step,
                              0.0f, contentHeight - size.y);
    return true;
}

bool UIPanel::handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) {
    if (!visible || !enabled) return false;
    if (!hitTest(mousePos, widgetPos, size)) return false;

    // Hit-testing mirrors the render offset for scrollable panels — a button
    // scrolled 100px up must be clicked where it DRAWS, not where it started.
    const glm::vec2 scrollVec = scrollable ? glm::vec2(0.0f, scrollOffset) : glm::vec2(0.0f);

    if (freeLayout) {
        for (auto& child : children) {
            if (!child->visible) continue;
            if (child->handleClick(mousePos, widgetPos + child->position - scrollVec, theme)) return true;
        }
        return true;
    }

    float yOffset = panelContentStartY(title, cachedFont_, theme);

    for (auto& child : children) {
        if (!child->visible) continue;
        float cx = widgetPos.x + theme.padding;
        float cy = widgetPos.y + yOffset - scrollVec.y;
        if (child->handleClick(mousePos, {cx, cy}, theme)) return true;
        yOffset += child->size.y + theme.itemSpacing;
    }
    return true;
}

bool UIPanel::handleDrag(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) {
    if (!visible || !enabled) return false;

    const glm::vec2 scrollVec = scrollable ? glm::vec2(0.0f, scrollOffset) : glm::vec2(0.0f);

    if (freeLayout) {
        for (auto& child : children) {
            if (!child->visible) continue;
            if (child->handleDrag(mousePos, widgetPos + child->position - scrollVec, theme)) return true;
        }
        return false;
    }

    float yOffset = panelContentStartY(title, cachedFont_, theme);

    for (auto& child : children) {
        if (!child->visible) continue;
        float cx = widgetPos.x + theme.padding;
        float cy = widgetPos.y + yOffset - scrollVec.y;
        if (child->handleDrag(mousePos, {cx, cy}, theme)) return true;
        yOffset += child->size.y + theme.itemSpacing;
    }
    return false;
}

void UIPanel::handleHover(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) {
    if (!visible) return;
    // The panel itself is a hover target too (G-148), so a tooltip may sit on a container
    // and act as the fallback when the pointer is over the box but not over a child.
    hovered = hitTest(mousePos, widgetPos, size);

    const glm::vec2 scrollVec = scrollable ? glm::vec2(0.0f, scrollOffset) : glm::vec2(0.0f);

    if (freeLayout) {
        for (auto& child : children) {
            if (!child->visible) continue;
            child->handleHover(mousePos, widgetPos + child->position - scrollVec, theme);
        }
        return;
    }

    float yOffset = panelContentStartY(title, cachedFont_, theme);

    for (auto& child : children) {
        if (!child->visible) continue;
        float cx = widgetPos.x + theme.padding;
        float cy = widgetPos.y + yOffset - scrollVec.y;
        child->handleHover(mousePos, {cx, cy}, theme);
        yOffset += child->size.y + theme.itemSpacing;
    }
}

// ════════════════════════════════════════════════════════════════
// UILabel
// ════════════════════════════════════════════════════════════════

// Greedy word-wrap to a max pixel width (honors existing '\n'). Long words that
// exceed the width are left on their own line (overflow) rather than split.
static std::string wrapText(const BitmapFont* font, const std::string& text,
                            float scale, float maxWidth) {
    std::string out, line, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && font->measureText(trial, scale) > maxWidth) {
            out += line; out += '\n'; line = word;
        } else {
            line = trial;
        }
        word.clear();
    };
    for (char c : text) {
        if (c == '\n') { flushWord(); out += line; out += '\n'; line.clear(); }
        else if (c == ' ') { flushWord(); }
        else word += c;
    }
    flushWord();
    out += line;
    return out;
}

// ---- Layout pass: measure before render (auto-sized panels) ------------------------------
float UILabel::measureHeight(const BitmapFont* font, const UITheme& theme, float availWidth) const {
    const float scale = customScale > 0.0f ? customScale : (isTitle ? theme.titleScale : theme.textScale);
    const float width = wrapWidth > 0.0f ? wrapWidth : availWidth;
    if (!font) return size.y;
    if (width <= 0.0f || text.empty()) return font->lineHeight(scale);
    const std::string wrapped = wrapText(font, text, scale, width);
    int lines = 1;
    for (char c : wrapped) if (c == '\n') ++lines;
    return font->lineHeight(scale) * static_cast<float>(lines);
}

float UIRepeater::measureHeight(const BitmapFont* font, const UITheme& theme, float availWidth) const {
    if (generated.empty() || horizontal) return size.y;
    float h = 0.0f;
    int n = 0;
    for (const auto& child : generated) {
        if (!child->visible) continue;
        h += child->measureHeight(font, theme, availWidth) + itemSpacing;
        ++n;
    }
    return n > 0 ? h - itemSpacing : 0.0f;
}

float UIPanel::measureContentHeight(const BitmapFont* font, const UITheme& theme) const {
    const float inner = size.x - theme.padding * 2.0f;
    if (freeLayout) {
        float maxExtent = 0.0f;
        for (const auto& child : children) {
            if (!child->visible) continue;
            maxExtent = std::max(maxExtent, child->position.y + child->measureHeight(font, theme, inner));
        }
        return maxExtent + theme.padding;
    }
    float h = theme.padding;
    if (!title.empty() && font) h += font->lineHeight(theme.titleScale) + theme.itemSpacing * 2.0f;
    int n = 0;
    for (const auto& child : children) {
        if (!child->visible) continue;
        h += child->measureHeight(font, theme, inner) + theme.itemSpacing;
        ++n;
    }
    if (n > 0) h -= theme.itemSpacing;
    return h + theme.padding;
}

float UIPanel::measureHeight(const BitmapFont* font, const UITheme& theme, float availWidth) const {
    (void)availWidth;
    if (!autoHeight) return size.y;
    const float h = measureContentHeight(font, theme);
    return (maxSize.y > 0.0f) ? std::min(h, maxSize.y) : h;
}

float UIPanel::measureContentWidth(const BitmapFont* font, const UITheme& theme) const {
    float w = 0.0f;
    for (const auto& child : children) {
        if (!child->visible) continue;
        const float cw = child->measureWidth(font, theme)
                       + (freeLayout ? child->position.x : 0.0f);
        w = std::max(w, cw);
    }
    return w + theme.padding * 2.0f;
}

void UIPanel::applyAutoSize(const BitmapFont* font, const UITheme& theme) {
    // Nested auto panels first (their heights feed this panel's content height).
    for (auto& child : children)
        if (auto* p = dynamic_cast<UIPanel*>(child.get())) p->applyAutoSize(font, theme);
    // Width BEFORE height: the inner width is what the height measurement wraps against.
    if (autoWidth) {
        const float w = measureContentWidth(font, theme);
        size.x = (maxSize.x > 0.0f) ? std::min(w, maxSize.x) : w;
    }
    if (!autoHeight) return;
    const float h = measureContentHeight(font, theme);
    if (maxSize.y > 0.0f && h > maxSize.y) { size.y = maxSize.y; scrollable = true; }
    else size.y = h;
    if (maxSize.x > 0.0f && size.x > maxSize.x) size.x = maxSize.x;
}

void UILabel::render(UIRenderer* renderer, const BitmapFont* font,
                     const UITheme& theme, glm::vec2 pos) {
    if (!visible) return;
    float scale = customScale > 0.0f ? customScale
                                     : (isTitle ? theme.titleScale : theme.textScale);
    glm::vec4 color = enabled ? (isTitle ? theme.titleColor : theme.textColor) : theme.disabledColor;
    if (enabled && customColor.a > 0.0f) color = customColor;

    std::string wrapped;
    bool multiline = false;
    if (wrapWidth > 0.0f) {
        wrapped = wrapText(font, text, scale, wrapWidth);
        multiline = wrapped.find('\n') != std::string::npos;
    }
    if (multiline) {
        // Multi-line: align the wrap BOX relative to position.x.
        glm::vec2 drawPos = pos;
        if (align == HAlign::Center)      drawPos.x = pos.x - wrapWidth * 0.5f;
        else if (align == HAlign::Right)  drawPos.x = pos.x - wrapWidth;
        font->drawText(renderer, wrapped, drawPos, color, scale);
        int lines = 1;
        for (char c : wrapped) if (c == '\n') ++lines;
        size.x = wrapWidth;
        size.y = font->lineHeight(scale) * static_cast<float>(lines);
    } else {
        const float textW = font->measureText(text, scale);
        glm::vec2 drawPos = pos;
        if (align == HAlign::Center)      drawPos.x = pos.x - textW * 0.5f;
        else if (align == HAlign::Right)  drawPos.x = pos.x - textW;
        font->drawText(renderer, text, drawPos, color, scale);
        size.x = textW;
        size.y = font->lineHeight(scale);
    }
}

// ════════════════════════════════════════════════════════════════
// UIButton
// ════════════════════════════════════════════════════════════════

void UIButton::render(UIRenderer* renderer, const BitmapFont* font,
                      const UITheme& theme, glm::vec2 pos) {
    if (!visible) return;

    glm::vec4 bg = hovered ? theme.buttonHover : theme.buttonBg;
    if (customBg.a > 0.0f) {
        // Per-element background override; hover state lightens it 25% unless an
        // explicit hover color was authored.
        bg = hovered ? (customBgHover.a > 0.0f
                            ? customBgHover
                            : glm::vec4(glm::min(glm::vec3(customBg) * 1.25f, glm::vec3(1.0f)),
                                        customBg.a))
                     : customBg;
    }
    if (!enabled) bg = theme.panelBg;

    float textW = font->measureText(text, theme.textScale);
    if (fitText) size.x = std::max(size.x, textW + 2.0f * theme.padding);
    renderer->drawRect(pos, size, bg);

    // ICON (G-148): a button showing a PNG draws the image instead of its label — the
    // label survives as tooltip text. Square, centred, and letterboxed into the box so a
    // non-square slot never stretches the art.
    if (!iconPath.empty()) {
        if (loadedIcon == -1) {
            const int idx = renderer->loadTexture(iconPath);
            loadedIcon = (idx >= 0) ? idx : -2;
        }
        if (loadedIcon >= 0) {
            const float side = std::max(1.0f, std::min(size.x, size.y) - 2.0f * iconPadding);
            const glm::vec2 ip{pos.x + (size.x - side) * 0.5f, pos.y + (size.y - side) * 0.5f};
            // Greyed rows dim rather than vanish, so you can still see WHAT is unavailable.
            const float b = enabled ? 1.0f : 0.45f;
            renderer->drawImage(ip, {side, side}, loadedIcon, {b, b, b, 1.0f});
            return;
        }
        // Load failed: fall through and draw the label, so a missing PNG is a visible
        // word rather than an empty square you cannot identify.
    }

    float textH = font->lineHeight(theme.textScale);
    glm::vec2 textPos = {
        pos.x + (size.x - textW) * 0.5f,
        pos.y + (size.y - textH) * 0.5f
    };
    glm::vec4 textColor = enabled ? theme.buttonText : theme.disabledColor;
    if (enabled && customColor.a > 0.0f) textColor = customColor;
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
    // NOT gated on `enabled` (G-148): a greyed-out row is exactly the one you point at to
    // find out WHY it is greyed, and the tooltip reads this flag. The disabled look is
    // unaffected — render() forces the disabled background after the hover colour.
    hovered = visible && hitTest(mousePos, widgetPos, size);
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

// ════════════════════════════════════════════════════════════════
// UITextInput
// ════════════════════════════════════════════════════════════════

void UITextInput::render(UIRenderer* renderer, const BitmapFont* font,
                         const UITheme& theme, glm::vec2 pos) {
    if (!visible) return;

    // Field background + focus border.
    renderer->drawRect(pos, size, theme.checkboxBg);
    if (focused) {
        float bw = theme.borderWidth;
        glm::vec4 b = theme.titleColor;
        renderer->drawRect(pos, {size.x, bw}, b);
        renderer->drawRect({pos.x, pos.y + size.y - bw}, {size.x, bw}, b);
        renderer->drawRect(pos, {bw, size.y}, b);
        renderer->drawRect({pos.x + size.x - bw, pos.y}, {bw, size.y}, b);
    }

    const float pad = theme.padding;
    const bool showPlaceholder = text.empty() && !focused;
    const std::string shown = showPlaceholder ? placeholder : text;
    const glm::vec4 col = showPlaceholder ? theme.disabledColor : theme.textColor;
    const float lineH = font->lineHeight(theme.textScale);
    const float textY = pos.y + (size.y - lineH) * 0.5f;
    if (!shown.empty())
        font->drawText(renderer, shown, {pos.x + pad, textY}, col, theme.textScale);

    // Blinking caret at the end of the text while focused.
    if (focused) {
        caretTimer += 0.016f;
        if (std::fmod(caretTimer, 1.0f) < 0.5f) {
            float tw = font->measureText(text, theme.textScale);
            renderer->drawRect({pos.x + pad + tw + 1.0f, textY}, {2.0f, lineH}, theme.textColor);
        }
    }
}

bool UITextInput::handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& /*theme*/) {
    if (!visible || !enabled) return false;
    // Focus when clicked; the UISystem clears focus on other inputs. (Editing —
    // typed chars / backspace / Enter — is driven by UISystem::handleInput.)
    if (hitTest(mousePos, widgetPos, size)) {
        focused = true;
        return true;
    }
    return false;
}

// ════════════════════════════════════════════════════════════════
// UIImage
// ════════════════════════════════════════════════════════════════

void UIImage::render(UIRenderer* renderer, const BitmapFont* /*font*/,
                     const UITheme& /*theme*/, glm::vec2 pos) {
    if (!visible) return;
    // Lazy-load the PNG into a Vulkan texture on first render (cached by the renderer).
    if (loadedTexture == -1 && !imagePath.empty()) {
        int idx = renderer->loadTexture(imagePath);
        loadedTexture = (idx >= 0) ? idx : -2;  // -2 = tried & failed, don't retry
    }
    if (loadedTexture >= 0) {
        renderer->drawImage(pos, size, loadedTexture, tintColor);
    } else {
        // No image (or load failed): tinted placeholder rect so layout stays visible.
        renderer->drawRect(pos, size, tintColor);
    }
}

// ════════════════════════════════════════════════════════════════
// UIProgressBar
// ════════════════════════════════════════════════════════════════

void UIProgressBar::render(UIRenderer* renderer, const BitmapFont* font,
                           const UITheme& theme, glm::vec2 pos) {
    if (!visible) return;

    // Border, then inset track.
    renderer->drawRect(pos, size, borderColor);
    float bw = theme.borderWidth;
    glm::vec2 inPos  = pos + glm::vec2(bw);
    glm::vec2 inSize = size - glm::vec2(bw * 2.0f);
    renderer->drawRect(inPos, inSize, trackColor);

    // Fill proportional to value.
    float t = (maxVal > minVal) ? (value - minVal) / (maxVal - minVal) : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    if (t > 0.0f) {
        renderer->drawRect(inPos, {inSize.x * t, inSize.y}, fillColor);
    }

    // Centered value text.
    if (showValueText && font) {
        char buf[48];
        if (!label.empty())
            snprintf(buf, sizeof(buf), "%s %d/%d", label.c_str(),
                     (int)(value + 0.5f), (int)(maxVal + 0.5f));
        else
            snprintf(buf, sizeof(buf), "%d/%d", (int)(value + 0.5f), (int)(maxVal + 0.5f));
        float tw = font->measureText(buf, theme.textScale);
        float th = font->lineHeight(theme.textScale);
        glm::vec2 tp = {pos.x + (size.x - tw) * 0.5f, pos.y + (size.y - th) * 0.5f};
        font->drawText(renderer, buf, tp, theme.textColor, theme.textScale);
    }
}

// ════════════════════════════════════════════════════════════════
// UICompass
// ════════════════════════════════════════════════════════════════

float UICompass::wrapDeg(float a) {
    a = std::fmod(a + 180.0f, 360.0f);
    if (a < 0.0f) a += 360.0f;
    return a - 180.0f;
}

std::optional<float> UICompass::offsetFor(float bearing, float heading, float spanDeg, float width) {
    const float d = wrapDeg(bearing - heading);
    const float half = spanDeg * 0.5f;
    if (half <= 0.0f || std::abs(d) > half) return std::nullopt;
    return d / half * (width * 0.5f);
}

void UICompass::render(UIRenderer* renderer, const BitmapFont* font,
                       const UITheme& theme, glm::vec2 pos) {
    if (!visible || !renderer) return;
    const float cx = pos.x + size.x * 0.5f;
    renderer->drawRect(pos, size, {0.04f, 0.04f, 0.06f, 0.55f});
    // Centre needle
    renderer->drawRect({cx - 1.0f, pos.y}, {2.0f, size.y}, {1.0f, 0.85f, 0.45f, 0.95f});
    if (!font) return;
    const float cardSc = theme.textScale * 0.9f;
    const float tickSc = theme.textScale * 0.65f;
    const float textH  = font->lineHeight(cardSc);
    // Cardinal letters + 45-degree ticks
    static const char* kCard[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    for (int i = 0; i < 8; ++i) {
        const auto off = offsetFor(static_cast<float>(i) * 45.0f, heading, spanDeg, size.x);
        if (!off) continue;
        const bool major = (i % 2 == 0);
        const float sc = major ? cardSc : tickSc;
        const float tw = font->measureText(kCard[i], sc);
        const float x = cx + *off;
        // Two rows INSIDE the strip (anything drawn outside the panel rect is clipped):
        // points of interest on the top row, the cardinal letters on the bottom row.
        renderer->drawRect({x - 0.5f, pos.y + size.y - 6.0f}, {1.0f, 6.0f}, {0.85f, 0.85f, 0.85f, 0.7f});
        font->drawText(renderer, kCard[i], {x - tw * 0.5f, pos.y + size.y - textH - 6.0f},
                       major ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) : glm::vec4(0.8f, 0.8f, 0.8f, 0.9f), sc);
    }
    // Points of interest: a tick from the top edge with the label beside it on the top row
    const float poiSc = theme.textScale * 0.62f;
    for (const auto& p : pois) {
        const auto off = offsetFor(p.bearing, heading, spanDeg, size.x);
        if (!off) continue;
        const float x = cx + *off;
        renderer->drawRect({x - 1.5f, pos.y + 1.0f}, {3.0f, 9.0f}, {0.98f, 0.90f, 0.62f, 1.0f});
        const float tw = font->measureText(p.label, poiSc);
        const float lh = font->lineHeight(poiSc);
        float lx = x + 5.0f;                                   // label to the right of the tick...
        if (lx + tw > pos.x + size.x - 2.0f) lx = x - 5.0f - tw; // ...or to the left near the edge
        renderer->drawRect({lx - 2.0f, pos.y + 1.0f}, {tw + 4.0f, lh}, {0.04f, 0.04f, 0.06f, 0.75f});
        font->drawText(renderer, p.label, {lx, pos.y + 1.0f}, {0.98f, 0.90f, 0.62f, 1.0f}, poiSc);
    }
}

// ════════════════════════════════════════════════════════════════
// UIRepeater
// ════════════════════════════════════════════════════════════════

void UIRepeater::render(UIRenderer* renderer, const BitmapFont* font,
                        const UITheme& theme, glm::vec2 pos) {
    if (!visible) return;
    float adv = 0.0f;
    for (auto& child : generated) {
        if (!child || !child->visible) continue;
        glm::vec2 cp = horizontal ? glm::vec2{pos.x + adv, pos.y}
                                  : glm::vec2{pos.x, pos.y + adv};
        child->render(renderer, font, theme, cp);
        adv += (horizontal ? child->size.x : child->size.y) + itemSpacing;
    }
    if (horizontal) size.x = adv; else size.y = adv;  // report extent for parent layout
}

float UIRepeater::measureWidth(const BitmapFont* font, const UITheme& theme) const {
    if (!horizontal) return size.x;
    // Same walk render() does, so the box a parent sizes to is the box the rows occupy.
    float adv = 0.0f;
    for (const auto& child : generated) {
        if (!child || !child->visible) continue;
        adv += child->measureWidth(font, theme) + itemSpacing;
    }
    return adv > 0.0f ? adv : size.x;
}

bool UIRepeater::handleClick(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) {
    if (!visible || !enabled) return false;
    float adv = 0.0f;
    for (auto& child : generated) {
        if (!child || !child->visible) continue;
        glm::vec2 cp = horizontal ? glm::vec2{widgetPos.x + adv, widgetPos.y}
                                  : glm::vec2{widgetPos.x, widgetPos.y + adv};
        if (child->handleClick(mousePos, cp, theme)) return true;
        adv += (horizontal ? child->size.x : child->size.y) + itemSpacing;
    }
    return false;
}

void UIRepeater::handleHover(glm::vec2 mousePos, glm::vec2 widgetPos, const UITheme& theme) {
    if (!visible) return;
    hovered = hitTest(mousePos, widgetPos, size);   // G-148: see UIPanel::handleHover
    float adv = 0.0f;
    for (auto& child : generated) {
        if (!child || !child->visible) continue;
        glm::vec2 cp = horizontal ? glm::vec2{widgetPos.x + adv, widgetPos.y}
                                  : glm::vec2{widgetPos.x, widgetPos.y + adv};
        child->handleHover(mousePos, cp, theme);
        adv += (horizontal ? child->size.x : child->size.y) + itemSpacing;
    }
}

// ════════════════════════════════════════════════════════════════
// Tooltip lookup (G-148)
// ════════════════════════════════════════════════════════════════

const UIWidget* hoveredTooltipWidget(const UIWidget* root) {
    if (!root || !root->visible) return nullptr;

    // Children first, so the deepest hovered widget wins: a tooltip on an action-bar row
    // must beat one on the bar behind it.
    if (root->type() == WidgetType::Panel) {
        for (const auto& c : static_cast<const UIPanel*>(root)->children)
            if (const UIWidget* hit = hoveredTooltipWidget(c.get())) return hit;
    } else if (root->type() == WidgetType::Repeater) {
        for (const auto& c : static_cast<const UIRepeater*>(root)->generated)
            if (const UIWidget* hit = hoveredTooltipWidget(c.get())) return hit;
    }
    return (root->hovered && !root->tooltip.empty()) ? root : nullptr;
}

std::string hoveredTooltip(const UIWidget* root) {
    const UIWidget* w = hoveredTooltipWidget(root);
    return w ? w->tooltip : std::string();
}

} // namespace UI
} // namespace Phyxel
