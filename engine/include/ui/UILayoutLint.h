#pragma once
// HUD layout LINT (2026-09-11, docs/game-production/CombatUiBg3.md increment 2).
// A HUD authored as hand-placed rectangles fails silently: labels run past their
// panel's bottom edge and get clipped, panels land on top of each other. The lint
// measures the widget tree the way the renderer lays it out and REPORTS every
// child that would be cut off and every pair of visible panels that overlap, so the
// build (and the playtest harness) can refuse the layout instead of a tester
// noticing "the text doesn't fit". Pure over widget trees - no GPU, unit-testable.
#include <algorithm>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ui/BitmapFont.h"
#include "ui/UIWidget.h"

namespace Phyxel {
namespace UI {

struct LayoutDefect {
    std::string kind;     ///< "child_overflow" | "panel_overlap"
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
            const float right = child->position.x + child->size.x;
            const float over = std::max(bottom - panel.size.y, right - panel.size.x);
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
    }
    lintPanelOverlaps(placed, out, screenSize);
    return out;
}

}  // namespace UI
}  // namespace Phyxel
