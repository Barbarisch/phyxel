#pragma once
// HUD layout LINT (2026-09-11, docs/game-production/CombatUiBg3.md increment 2).
// A HUD authored as hand-placed rectangles fails silently: labels run past their
// panel's bottom edge and get clipped, panels land on top of each other. The lint
// measures the widget tree the way the renderer lays it out and REPORTS every
// child that would be cut off and every pair of visible panels that overlap, so the
// build (and the playtest harness) can refuse the layout instead of a tester
// noticing "the text doesn't fit". Pure over widget trees - no GPU, unit-testable.
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <glm/glm.hpp>

#include "ui/BitmapFont.h"
#include "ui/UIWidget.h"

namespace Phyxel {
namespace UI {

struct LayoutDefect {
    std::string kind;     ///< "child_overflow" | "panel_overlap" | "text_overflow" | "content_overflow" | "text_overlap"
    std::string panel;    ///< panel id
    std::string other;    ///< child id (overflow) or the other panel id (overlap)
    float amount = 0.0f;  ///< px cut off / px of overlap along the smaller axis
    std::string message;
};

struct PlacedPanel {
    const UIPanel* panel = nullptr;
    glm::vec2 pos{0.0f};   ///< resolved screen position
};

/// Children that the panel would cut off (clipping on, not scrollable): in flow
/// layout the running stack passes the panel's bottom; in free layout a child's
/// box leaves the panel's box.
inline void lintPanelChildren(const UIPanel& panel, const BitmapFont* font, const UITheme& theme,
                              std::vector<LayoutDefect>& out) {
    if (!panel.clipChildren || panel.scrollable || panel.size.x <= 0.0f || panel.size.y <= 0.0f) return;
    const float inner = panel.size.x - theme.padding * 2.0f;
    if (panel.freeLayout) {
        for (const auto& child : panel.children) {
            if (!child->visible) continue;
            const float bottom = child->position.y + child->measureHeight(font, theme, inner);
            // A label's horizontal extent follows its alignment: "center" puts the TEXT centred
            // ON position.x and "right" ends it there (UILabel::HAlign) - the box is not
            // position..position+width. Reading it that way flagged every centred title as
            // "leaves the screen" (G-183: 240 px on the keybindings hint, 60-160 px on each
            // data screen) and one fix moved real titles off-centre chasing it.
            float left = child->position.x, right = child->position.x + child->size.x;
            if (const auto* lbl = dynamic_cast<const UILabel*>(child.get())) {
                if (lbl->align == UILabel::HAlign::Center) { left = child->position.x - child->size.x * 0.5f; right = child->position.x + child->size.x * 0.5f; }
                else if (lbl->align == UILabel::HAlign::Right) { left = child->position.x - child->size.x; right = child->position.x; }
            }
            const float over = std::max({bottom - panel.size.y, right - panel.size.x, -left});
            if (over > 0.5f)
                out.push_back({"child_overflow", panel.id, child->id, over,
                               "'" + child->id + "' leaves '" + panel.id + "' by " + std::to_string(int(over + 0.5f)) + " px"});
        }
        return;
    }
    float y = theme.padding;
    if (!panel.title.empty() && font) y += font->lineHeight(theme.titleScale) + theme.itemSpacing * 2.0f;
    for (const auto& child : panel.children) {
        if (!child->visible) continue;
        const float h = child->measureHeight(font, theme, inner);
        const float bottom = y + h;
        if (bottom > panel.size.y + 0.5f)
            out.push_back({"child_overflow", panel.id, child->id, bottom - panel.size.y,
                           "'" + child->id + "' is cut off by '" + panel.id + "' (" +
                           std::to_string(int(bottom - panel.size.y + 0.5f)) + " px past the bottom)"});
        y += h + theme.itemSpacing;
    }
}

/// Pairwise overlap of placed panels (only panels that are visible at the same time
/// should be passed in - the caller knows the visibility state).
inline bool isFullscreenOverlay(const UIPanel& p, glm::vec2 screenSize) {
    return screenSize.x > 0.0f && screenSize.y > 0.0f &&
           p.size.x >= screenSize.x * 0.9f && p.size.y >= screenSize.y * 0.9f;
}

inline void lintPanelOverlaps(const std::vector<PlacedPanel>& placed, std::vector<LayoutDefect>& out,
                              glm::vec2 screenSize = {0.0f, 0.0f}) {
    for (size_t a = 0; a < placed.size(); ++a)
        for (size_t b = a + 1; b < placed.size(); ++b) {
            const auto& A = placed[a]; const auto& B = placed[b];
            if (!A.panel || !B.panel) continue;
            // A fullscreen overlay (fade / loading / dialogue backdrop) covers everything by
            // design - the shipped shell logged "'hud_hotbar' overlaps '' by 624x100" at every
            // scene transition (2026-09-11).
            if (isFullscreenOverlay(*A.panel, screenSize) || isFullscreenOverlay(*B.panel, screenSize)) continue;
            const float ox = std::min(A.pos.x + A.panel->size.x, B.pos.x + B.panel->size.x) - std::max(A.pos.x, B.pos.x);
            const float oy = std::min(A.pos.y + A.panel->size.y, B.pos.y + B.panel->size.y) - std::max(A.pos.y, B.pos.y);
            if (ox > 0.5f && oy > 0.5f)
                out.push_back({"panel_overlap", A.panel->id, B.panel->id, std::min(ox, oy),
                               "'" + A.panel->id + "' overlaps '" + B.panel->id + "' by " +
                               std::to_string(int(ox + 0.5f)) + "x" + std::to_string(int(oy + 0.5f)) + " px"});
        }
}

// ---- Text fit (G-184, user: "UI strings overlap ui elements or dont fit") ----------------
// Judged with the renderer's own metrics (UILabel::render / UIButton::render):
//   text_overflow     a label's widest DRAWN line is wider than its box (labels wrap only when
//                     wrapWidth > 0 - an authored size becomes the wrapWidth at load); a
//                     button's text is wider than its frame, unless an icon draws in its place;
//   content_overflow  a fixed-height nested panel (a repeater row) is shorter than what is
//                     drawn inside it - the stack spaces rows by size.y, so the rows draw over
//                     each other;
//   text_overlap      in a free-layout panel a string lands on a sibling string, or runs into a
//                     sibling button / panel frame.
// Recurses into nested panels and a repeater's generated rows. Needs the font.
inline std::string textName(const UIWidget& w, const std::string& text) {
    if (!w.id.empty()) return w.id;
    return "\"" + text.substr(0, 28) + (text.size() > 28 ? "..." : "") + "\"";
}

/// True when the button draws its icon instead of its label (UIButton::render): a loaded icon,
/// or - headless, never rendered - one that will load because the file exists. A missing PNG
/// falls back to the label, which then has to fit.
inline bool buttonDrawsIcon(const UIButton& b) {
    if (b.iconPath.empty() || b.loadedIcon == -2) return false;
    if (b.loadedIcon >= 0) return true;
    std::error_code ec;
    return std::filesystem::exists(b.iconPath, ec);
}

struct TextRect { const UIWidget* w; std::string text; float x0, x1, y0, y1; };

/// Where a label's text is drawn. Single-line text is aligned by its own width, wrapped text by
/// its wrap box (UILabel::render), so "center" / "right" resolve differently for the two.
inline TextRect labelTextRect(const UILabel& l, const BitmapFont* font, const UITheme& theme) {
    const float scale = l.drawScale(theme);
    const std::string wrapped = l.wrapWidth > 0.0f
        ? UILabel::tailLines(UILabel::wrapLines(font, l.text, scale, l.wrapWidth), l.maxLines) : l.text;
    const bool multi = wrapped.find('\n') != std::string::npos;
    const float tw = l.measureTextWidth(font, theme);
    const float ref = multi ? l.wrapWidth : tw;
    float x0 = l.position.x;
    if (l.align == UILabel::HAlign::Center)     x0 = l.position.x - ref * 0.5f;
    else if (l.align == UILabel::HAlign::Right) x0 = l.position.x - ref;
    int lines = 1;
    for (char c : wrapped) if (c == '\n') ++lines;
    const float th = font->lineHeight(scale) * static_cast<float>(lines);
    return {&l, l.text, x0, x0 + tw, l.position.y, l.position.y + th};
}

inline bool textRectOf(const UIWidget& w, const BitmapFont* font, const UITheme& theme, TextRect& out) {
    if (!font || !w.visible) return false;
    if (const auto* lbl = dynamic_cast<const UILabel*>(&w)) {
        if (lbl->text.empty()) return false;
        out = labelTextRect(*lbl, font, theme);
        return true;
    }
    if (const auto* btn = dynamic_cast<const UIButton*>(&w)) {
        if (btn->text.empty() || buttonDrawsIcon(*btn)) return false;
        const float tw = font->measureText(btn->text, theme.textScale);
        const float th = font->lineHeight(theme.textScale);
        const float cx = w.position.x + w.size.x * 0.5f, cy = w.position.y + w.size.y * 0.5f;
        out = {&w, btn->text, cx - tw * 0.5f, cx + tw * 0.5f, cy - th * 0.5f, cy + th * 0.5f};
        return true;
    }
    return false;
}

inline void lintTextFit(const UIWidget& w, const BitmapFont* font, const UITheme& theme,
                        std::vector<LayoutDefect>& out, int depth = 0) {
    if (!font || !w.visible) return;
    if (const auto* lbl = dynamic_cast<const UILabel*>(&w)) {
        const float box = lbl->wrapWidth > 0.0f ? lbl->wrapWidth : lbl->size.x;
        if (!lbl->text.empty() && box > 0.0f) {
            const float tw = lbl->measureTextWidth(font, theme);
            if (tw > box + 0.5f) {
                const std::string id = textName(w, lbl->text);
                out.push_back({"text_overflow", id, "", tw - box,
                               "'" + id + "' text is " + std::to_string(int(tw - box + 0.5f)) + " px wider than its " +
                               std::to_string(int(box + 0.5f)) + " px box (\"" + lbl->text.substr(0, 40) + "\")"});
            }
        }
    } else if (const auto* btn = dynamic_cast<const UIButton*>(&w)) {
        const float inner = w.size.x - theme.padding * 2.0f;
        if (!btn->text.empty() && inner > 0.0f && !buttonDrawsIcon(*btn)) {
            const float tw = font->measureText(btn->text, theme.textScale);
            if (tw > inner + 0.5f) {
                const std::string id = textName(w, btn->text);
                out.push_back({"text_overflow", id, "", tw - inner,
                               "'" + id + "' button text is " + std::to_string(int(tw - inner + 0.5f)) +
                               " px wider than its frame (\"" + btn->text.substr(0, 40) + "\")"});
            }
        }
    }
    const std::vector<std::unique_ptr<UIWidget>>* kids = nullptr;
    bool freeLayout = false;
    if (const auto* p = dynamic_cast<const UIPanel*>(&w)) {
        kids = &p->children; freeLayout = p->freeLayout;
        // A nested panel (a repeater row) must hold what is drawn in it. The drawn extent is
        // the content height less the bottom padding, which nothing draws in. An autoSize row
        // is NOT exempt: lintLayout's applyAutoSize pass has sized it (as the renderer does
        // every frame), so it passes only if that pass really made it tall enough.
        if (depth > 0 && !p->scrollable && p->size.y > 0.0f) {
            const float drawn = p->measureContentHeight(font, theme) - theme.padding;
            if (drawn > p->size.y + 0.5f) {
                const std::string id = w.id.empty() ? std::string("(row)") : w.id;
                out.push_back({"content_overflow", id, "", drawn - p->size.y,
                               "'" + id + "' draws " + std::to_string(int(drawn + 0.5f)) + " px of content in a " +
                               std::to_string(int(p->size.y + 0.5f)) + " px row"});
            }
        }
    } else if (const auto* r = dynamic_cast<const UIRepeater*>(&w)) {
        kids = &r->generated;
    }
    if (!kids) return;
    for (const auto& c : *kids) lintTextFit(*c, font, theme, out, depth + 1);
    if (!freeLayout) return;
    auto overlap = [](float ax0, float ax1, float ay0, float ay1,
                      float bx0, float bx1, float by0, float by1, float& ox, float& oy) {
        ox = std::min(ax1, bx1) - std::max(ax0, bx0);
        oy = std::min(ay1, by1) - std::max(ay0, by0);
        return ox > 0.5f && oy > 0.5f;
    };
    std::vector<TextRect> rects;
    for (const auto& c : *kids) { TextRect r; if (textRectOf(*c, font, theme, r)) rects.push_back(r); }
    for (size_t i = 0; i < rects.size(); ++i) {
        const auto& a = rects[i];
        const std::string ia = textName(*a.w, a.text);
        // string vs string
        for (size_t j = i + 1; j < rects.size(); ++j) {
            const auto& b = rects[j];
            float ox, oy;
            if (overlap(a.x0, a.x1, a.y0, a.y1, b.x0, b.x1, b.y0, b.y1, ox, oy)) {
                const std::string ib = textName(*b.w, b.text);
                out.push_back({"text_overlap", ia, ib, std::min(ox, oy),
                               "'" + ia + "' text overlaps '" + ib + "' text by " +
                               std::to_string(int(ox + 0.5f)) + "x" + std::to_string(int(oy + 0.5f)) + " px"});
            }
        }
        // string vs a sibling's frame (a button, a nested panel) - not the backdrop
        for (const auto& c : *kids) {
            if (c.get() == a.w || !c->visible) continue;
            if (!dynamic_cast<const UIButton*>(c.get()) && !dynamic_cast<const UIPanel*>(c.get())) continue;
            if (w.size.x > 0.0f && w.size.y > 0.0f && c->size.x >= w.size.x * 0.9f && c->size.y >= w.size.y * 0.9f) continue;
            float ox, oy;
            if (overlap(a.x0, a.x1, a.y0, a.y1, c->position.x, c->position.x + c->size.x,
                        c->position.y, c->position.y + c->size.y, ox, oy)) {
                const std::string ib = c->id.empty() ? std::string("(unnamed)") : c->id;
                out.push_back({"text_overlap", ia, ib, std::min(ox, oy),
                               "'" + ia + "' text runs into '" + ib + "' by " +
                               std::to_string(int(ox + 0.5f)) + "x" + std::to_string(int(oy + 0.5f)) + " px"});
            }
        }
    }
}

/// Whole-screen lint: auto-size every panel first (as the renderer does), then check
/// children and overlaps.
inline std::vector<LayoutDefect> lintLayout(std::vector<UIPanel*> panels, glm::vec2 screenSize,
                                            const BitmapFont* font, const UITheme& theme) {
    std::vector<LayoutDefect> out;
    std::vector<PlacedPanel> placed;
    for (UIPanel* p : panels) {
        if (!p || !p->visible) continue;
        p->applyAutoSize(font, theme);
        placed.push_back({p, resolveAnchor(p->anchor, {0, 0}, screenSize, p->size, p->offset)});
        lintPanelChildren(*p, font, theme, out);
        lintTextFit(*p, font, theme, out);
    }
    lintPanelOverlaps(placed, out, screenSize);
    return out;
}

}  // namespace UI
}  // namespace Phyxel
