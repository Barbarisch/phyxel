#include "ui/UISystem.h"
#include "ui/UILayoutLint.h"
#include <algorithm>
#include <vector>
#include "input/InputManager.h"
#include "utils/Logger.h"
#include <GLFW/glfw3.h>

namespace Phyxel {
namespace UI {

UISystem::UISystem(Vulkan::VulkanDevice* device, uint32_t width, uint32_t height)
    : renderer_(device, width, height)
    , screenWidth_(width)
    , screenHeight_(height) {}

UISystem::~UISystem() {
    cleanup();
}

bool UISystem::initialize(VkRenderPass renderPass) {
    if (!renderer_.initialize(renderPass)) return false;
    // Prefer a crisp TrueType font; fall back to the embedded bitmap font.
    const std::string kFont = "resources/fonts/JetBrainsMonoNerdFontMono-Regular.ttf";
    if (!font_.initializeTTF(&renderer_, kFont, 48.0f)) {
        if (!font_.initialize(&renderer_)) return false;
    }
    initialized_ = true;
    return true;
}

void UISystem::cleanup() {
    screens_.clear();
    renderer_.cleanup();
    initialized_ = false;
}

void UISystem::resize(uint32_t width, uint32_t height) {
    screenWidth_ = width;
    screenHeight_ = height;
    renderer_.resize(width, height);
}

void UISystem::setWindowSize(uint32_t winW, uint32_t winH) {
    placement_ = placementFor(winW, winH, screenWidth_, screenHeight_);
    renderer_.setViewportRect(placement_.x, placement_.y,
                              screenWidth_ * placement_.scale, screenHeight_ * placement_.scale);
}

// ── Screen management ───────────────────────────────────────

void UISystem::addScreen(const std::string& name, std::unique_ptr<UIPanel> panel) {
    screens_[name] = {std::move(panel), false};
}

void UISystem::removeScreen(const std::string& name) {
    screens_.erase(name);
}

void UISystem::showScreen(const std::string& name) {
    auto it = screens_.find(name);
    if (it == screens_.end()) return;
    if (!it->second.visible)   // hidden→visible edge: restart appear animations
        it->second.shownAt = std::chrono::steady_clock::now();
    it->second.visible = true;
}

void UISystem::hideScreen(const std::string& name) {
    auto it = screens_.find(name);
    if (it != screens_.end()) {
        it->second.visible = false;
    }
}

void UISystem::toggleScreen(const std::string& name) {
    auto it = screens_.find(name);
    if (it != screens_.end()) it->second.visible = !it->second.visible;
}

bool UISystem::isScreenVisible(const std::string& name) const {
    auto it = screens_.find(name);
    return it != screens_.end() && it->second.visible;
}

void UISystem::hideAllScreens() {
    for (auto& [name, entry] : screens_) entry.visible = false;
}

UIPanel* UISystem::getScreen(const std::string& name) {
    auto it = screens_.find(name);
    return it != screens_.end() ? it->second.panel.get() : nullptr;
}

bool UISystem::hasVisibleScreens() const {
    for (auto& [name, entry] : screens_) {
        if (entry.visible) return true;
    }
    return false;
}

std::vector<std::pair<std::string, bool>> UISystem::getScreenList() const {
    std::vector<std::pair<std::string, bool>> result;
    result.reserve(screens_.size());
    for (auto& [name, entry] : screens_) {
        result.emplace_back(name, entry.visible);
    }
    return result;
}

// ── Input routing ───────────────────────────────────────────

std::vector<UISystem::ScreenEntry*> UISystem::visibleScreenSnapshot() {
    std::vector<ScreenEntry*> active;
    active.reserve(screens_.size());
    for (auto& [name, entry] : screens_)
        if (entry.visible && entry.panel) active.push_back(&entry);
    return active;
}

// Collect visible UITextInput widgets from a (possibly nested) widget tree.
static void collectTextInputs(UIWidget* w, std::vector<UITextInput*>& out) {
    if (!w || !w->visible) return;
    if (w->type() == WidgetType::TextInput) { out.push_back(static_cast<UITextInput*>(w)); return; }
    if (w->type() == WidgetType::Panel) {
        auto* p = static_cast<UIPanel*>(w);
        for (auto& c : p->children) collectTextInputs(c.get(), out);
    }
}

// The ghost the pointer carries: whatever picture the source was already showing.
static std::string dragIconOf(const UIWidget* w) {
    if (!w) return {};
    if (w->type() == WidgetType::Button) return static_cast<const UIButton*>(w)->iconPath;
    if (w->type() == WidgetType::Image)  return static_cast<const UIImage*>(w)->imagePath;
    return {};
}

// How far the pointer must move before a press becomes a drag rather than a click.
// Windows' own system drag threshold (SM_CXDRAG) is 4 px at 96 DPI; 6 px in the logical
// canvas is that, with margin for a shaky hand on a 52 px slot.
static constexpr float kDragThresholdPx = 6.0f;

void UISystem::resetDrag() {
    dragPending_ = false;
    dragActive_  = false;
    dragPayload_.clear();
    dragIconPath_.clear();
    dragIconTex_ = -1;
    dragSourceDrop_ = {};
}

bool UISystem::handleInput(Input::InputManager* input) {
    // G-148: with nothing on screen there is nothing to point at, so a tooltip left over
    // from the frame the HUD was up must not keep drawing.
    if (!initialized_ || !hasVisibleScreens()) { hoverTooltip_.clear(); resetDrag(); return false; }

    // ── Key capture (rebind) ─────────────────────────────────────────────────
    // Takes priority over and consumes all other input. Arms only after a
    // keys-released frame so the click/Enter that opened capture isn't grabbed.
    if (keyCaptureActive_) {
        const int k = input->scanPressedKey();
        if (!keyCaptureArmed_) {
            if (k == GLFW_KEY_UNKNOWN) keyCaptureArmed_ = true;
            return true;
        }
        if (k == GLFW_KEY_ESCAPE) {
            auto cancel = keyCaptureCancelCb_;
            cancelKeyCapture();
            if (cancel) cancel();
            return true;
        }
        if (k != GLFW_KEY_UNKNOWN) {
            auto cb = keyCaptureCb_;
            const int mods = input->currentModifiers();
            // Single-key bindings: the captured key IS the binding, so a modifier
            // key (Shift for Sprint) reports mods=0, not "Shift+Shift".
            const bool keyIsModifier =
                (k == GLFW_KEY_LEFT_SHIFT || k == GLFW_KEY_RIGHT_SHIFT ||
                 k == GLFW_KEY_LEFT_CONTROL || k == GLFW_KEY_RIGHT_CONTROL ||
                 k == GLFW_KEY_LEFT_ALT || k == GLFW_KEY_RIGHT_ALT);
            cancelKeyCapture();
            if (cb) cb(k, keyIsModifier ? 0 : mods);
            return true;
        }
        return true;  // armed, nothing pressed yet — keep consuming
    }

    double mx, my;
    input->getCurrentMousePosition(mx, my);
    glm::vec2 mousePos = toLogical({static_cast<float>(mx), static_cast<float>(my)});
    lastMousePos_ = mousePos;        // G-148: where a tooltip would be drawn
    hoverTooltip_.clear();

    bool mousePressed = input->isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
    bool mouseJustClicked = mousePressed && !wasMousePressed_;
    bool mouseDragging = mousePressed && wasMousePressed_;
    // G-150: a drop happens on RELEASE, which nothing needed until now.
    bool mouseJustReleased = !mousePressed && wasMousePressed_;
    wasMousePressed_ = mousePressed;

    glm::vec2 screenSize(static_cast<float>(screenWidth_), static_cast<float>(screenHeight_));
    bool consumed = false;

    // Snapshot the screens visible at the START of this pass. A button's onClick
    // can reveal another screen mid-loop (e.g. close_submenu → menuShowOnly shows
    // menu:main); without this snapshot the newly-revealed screen receives the
    // SAME click later in the iteration and instantly re-fires whatever button
    // sits under the cursor — the menu Back soft-lock (Credits→Back bounces
    // straight back to Credits, depending on screens_ hash order).
    // (game-dev feedback round 5 — UIShowcase.)
    auto activeScreens = visibleScreenSnapshot();

    // Pass 1: layout + hover for every visible screen, remembering where each landed so
    // the click/drag decision below does not redo the anchor maths.
    std::vector<std::pair<UIPanel*, glm::vec2>> laid;
    laid.reserve(activeScreens.size());
    for (auto* entry : activeScreens) {
        auto* panel = entry->panel.get();
        panel->applyAutoSize(&font_, theme_);   // layout pass: content decides the height
        glm::vec2 panelPos = resolveAnchor(panel->anchor, {0, 0}, screenSize,
                                            panel->size, panel->offset);
        panel->handleHover(mousePos, panelPos, theme_);
        // G-148: the LAST visible screen with a hit wins, matching the draw order - the
        // screen drawn on top is the one the pointer is really over.
        if (std::string t = hoveredTooltip(panel); !t.empty()) hoverTooltip_ = std::move(t);
        laid.emplace_back(panel, panelPos);
    }

    // ── DRAG AND DROP (G-150) ────────────────────────────────────────────────
    // Resolved BEFORE clicks: pressing a bar slot to pick it up must not also cast it,
    // so a press on a draggable widget withholds the click until release.
    if (mouseJustClicked) {
        for (auto& [panel, panelPos] : laid) {
            if (UIWidget* src = hoveredDragSource(panel)) {
                dragPending_    = true;
                dragPressPos_   = mousePos;
                dragPayload_    = src->dragPayload;
                dragIconPath_   = dragIconOf(src);
                dragIconTex_    = -1;
                dragSourceDrop_ = src->onDrop;   // by value - the row may be rebuilt
            }
        }
    }
    if (dragPending_ && !dragActive_ && mouseDragging) {
        const glm::vec2 d = mousePos - dragPressPos_;
        if (d.x * d.x + d.y * d.y > kDragThresholdPx * kDragThresholdPx) dragActive_ = true;
    }
    if (dragActive_) hoverTooltip_.clear();   // one thing at the pointer at a time

    if (mouseJustReleased && (dragPending_ || dragActive_)) {
        if (dragActive_) {
            UIWidget* target = nullptr;
            for (auto& [panel, panelPos] : laid)
                if (UIWidget* t = hoveredDropTarget(panel)) target = t;
            // Released on nothing still reaches the SOURCE, which is how dragging a slot
            // off the bar clears it.
            if (target && target->onDrop) target->onDrop(dragPayload_, true);
            else if (dragSourceDrop_)     dragSourceDrop_(dragPayload_, false);
        } else {
            // Never passed the threshold, so it was a click after all. Dispatch it now,
            // at the press position, since it was withheld on press.
            for (auto& [panel, panelPos] : laid)
                if (panel->handleClick(dragPressPos_, panelPos, theme_)) consumed = true;
        }
        consumed = true;
        resetDrag();
    }

    // Pass 2: ordinary clicks and slider drags, for everything the drag machinery did
    // not claim this frame.
    if (!dragPending_ && !dragActive_) {
        for (auto& [panel, panelPos] : laid) {
            if (mouseJustClicked) {
                if (panel->handleClick(mousePos, panelPos, theme_)) consumed = true;
            } else if (mouseDragging) {
                if (panel->handleDrag(mousePos, panelPos, theme_)) consumed = true;
            }
        }
    }

    // ── Text fields (e.g. the AI conversation box) ───────────────────────────
    // Deliver typed characters + Backspace/Enter to the focused UITextInput. A
    // click this frame may have focused one (handleClick); otherwise auto-focus the
    // first (the dialogue box is the only field on screen). Keeps a single focus.
    std::vector<UITextInput*> inputs;
    for (auto* entry : activeScreens) collectTextInputs(entry->panel.get(), inputs);
    if (!inputs.empty()) {
        UITextInput* focused = nullptr;
        for (auto* ti : inputs) if (ti->focused) { focused = ti; break; }
        if (!focused) { focused = inputs.front(); focused->focused = true; }
        for (auto* ti : inputs) if (ti != focused) ti->focused = false;

        // Printable characters (ASCII range — the bitmap font's glyph set).
        for (unsigned int cp : input->getTypedChars()) {
            if (cp >= 32 && cp < 127 && focused->text.size() < focused->maxLength) {
                focused->text.push_back(static_cast<char>(cp));
                if (focused->onChange) focused->onChange(focused->text);
            }
        }
        // Backspace + Enter, edge-triggered (isKeyPressed is held-state).
        const bool bs = input->isKeyPressed(GLFW_KEY_BACKSPACE);
        if (bs && !prevBackspace_ && !focused->text.empty()) {
            focused->text.pop_back();
            if (focused->onChange) focused->onChange(focused->text);
        }
        prevBackspace_ = bs;
        const bool ent = input->isKeyPressed(GLFW_KEY_ENTER);
        if (ent && !prevEnter_ && focused->onSubmit) {
            focused->onSubmit(focused->text);
        }
        prevEnter_ = ent;
        consumed = true;
    }

    return consumed;
}

bool UISystem::injectClick(glm::vec2 pos) {
    if (!initialized_) return false;
    pos = toLogical(pos);   // window px -> the logical canvas

    glm::vec2 screenSize(static_cast<float>(screenWidth_), static_cast<float>(screenHeight_));
    bool consumed = false;

    // Snapshot before dispatch — same soft-lock guard as handleInput (an injected
    // click via POST /api/ui/click must not be re-delivered to a screen revealed
    // by this very click). (game-dev feedback round 5 — UIShowcase.)
    auto activeScreens = visibleScreenSnapshot();

    for (auto* entry : activeScreens) {
        auto* panel = entry->panel.get();

        panel->applyAutoSize(&font_, theme_);   // layout pass: content decides the height
        glm::vec2 panelPos = resolveAnchor(panel->anchor, {0, 0}, screenSize,
                                            panel->size, panel->offset);
        // Update hover so button visuals match, then click.
        panel->handleHover(pos, panelPos, theme_);
        if (panel->handleClick(pos, panelPos, theme_)) consumed = true;
    }

    return consumed;
}

std::string UISystem::injectHover(glm::vec2 pos) {
    if (!initialized_) return {};
    pos = toLogical(pos);   // window px -> the logical canvas
    lastMousePos_ = pos;
    hoverTooltip_.clear();

    glm::vec2 screenSize(static_cast<float>(screenWidth_), static_cast<float>(screenHeight_));
    for (auto* entry : visibleScreenSnapshot()) {
        auto* panel = entry->panel.get();
        panel->applyAutoSize(&font_, theme_);   // layout pass: content decides the height
        glm::vec2 panelPos = resolveAnchor(panel->anchor, {0, 0}, screenSize,
                                            panel->size, panel->offset);
        panel->handleHover(pos, panelPos, theme_);
        if (std::string t = hoveredTooltip(panel); !t.empty()) hoverTooltip_ = std::move(t);
    }
    return hoverTooltip_;
}

UISystem::DragResult UISystem::injectDrag(glm::vec2 from, glm::vec2 to) {
    DragResult out;
    if (!initialized_) return out;
    const glm::vec2 a = toLogical(from), b = toLogical(to);
    glm::vec2 screenSize(static_cast<float>(screenWidth_), static_cast<float>(screenHeight_));

    auto layAndHover = [&](glm::vec2 at, std::vector<std::pair<UIPanel*, glm::vec2>>& laid) {
        laid.clear();
        for (auto* entry : visibleScreenSnapshot()) {
            auto* panel = entry->panel.get();
            panel->applyAutoSize(&font_, theme_);
            glm::vec2 panelPos = resolveAnchor(panel->anchor, {0, 0}, screenSize,
                                                panel->size, panel->offset);
            panel->handleHover(at, panelPos, theme_);
            laid.emplace_back(panel, panelPos);
        }
    };

    std::vector<std::pair<UIPanel*, glm::vec2>> laid;
    layAndHover(a, laid);
    std::function<void(const std::string&, bool)> sourceDrop;
    for (auto& [panel, panelPos] : laid)
        if (UIWidget* src = hoveredDragSource(panel)) {
            out.picked = true;
            out.payload = src->dragPayload;
            sourceDrop = src->onDrop;
        }
    if (!out.picked) return out;

    layAndHover(b, laid);
    lastMousePos_ = b;
    UIWidget* target = nullptr;
    for (auto& [panel, panelPos] : laid)
        if (UIWidget* t = hoveredDropTarget(panel)) target = t;
    if (target && target->onDrop) { target->onDrop(out.payload, true); out.dropped = true; }
    else if (sourceDrop)          { sourceDrop(out.payload, false); }
    return out;
}

bool UISystem::handleScroll(glm::vec2 pos, float delta) {
    if (!initialized_ || delta == 0.0f || !hasVisibleScreens()) return false;
    pos = toLogical(pos);

    glm::vec2 screenSize(static_cast<float>(screenWidth_), static_cast<float>(screenHeight_));
    auto activeScreens = visibleScreenSnapshot();

    for (auto* entry : activeScreens) {
        auto* panel = entry->panel.get();
        panel->applyAutoSize(&font_, theme_);   // layout pass: content decides the height
        glm::vec2 panelPos = resolveAnchor(panel->anchor, {0, 0}, screenSize,
                                            panel->size, panel->offset);
        if (panel->handleScroll(pos, panelPos, delta, theme_)) return true;
    }
    return false;
}

// ── Key capture (rebind) ────────────────────────────────────

void UISystem::beginKeyCapture(std::function<void(int, int)> onCaptured,
                               std::function<void()> onCancelled) {
    keyCaptureCb_ = std::move(onCaptured);
    keyCaptureCancelCb_ = std::move(onCancelled);
    keyCaptureActive_ = true;
    keyCaptureArmed_ = false;  // wait for a keys-released frame before grabbing
}

void UISystem::cancelKeyCapture() {
    keyCaptureActive_ = false;
    keyCaptureArmed_ = false;
    keyCaptureCb_ = {};
    keyCaptureCancelCb_ = {};
}

// ── Rendering ───────────────────────────────────────────────

bool UISystem::worldToScreen(const glm::vec3& worldPos, const glm::mat4& view,
                             const glm::mat4& proj, float screenW, float screenH,
                             glm::vec2& outScreen) {
    glm::vec4 clip = proj * view * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.0f) return false;  // behind the camera
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    outScreen.x = (ndc.x * 0.5f + 0.5f) * screenW;
    outScreen.y = (ndc.y * 0.5f + 0.5f) * screenH;  // Vulkan proj already Y-flipped
    return true;
}

void UISystem::addWorldMarker(glm::vec2 windowPx, float sizePx, glm::vec4 color) {
    worldMarkers_.push_back({toLogical(windowPx), sizePx, color});
}

void UISystem::addWorldLabel(glm::vec2 screenPos, const std::string& text,
                             glm::vec4 textColor, float bgAlpha) {
    if (text.empty()) return;
    worldLabels_.push_back({toLogical(screenPos), text, textColor, bgAlpha});
}

void UISystem::addNameplate(const Nameplate& plate) {
    if (plate.name.empty()) return;
    Nameplate p = plate; p.screenPos = toLogical(plate.screenPos); nameplates_.push_back(p);
}

void UISystem::render(VkCommandBuffer cmd) {
    // Draw whenever there are visible screens OR queued world labels (a speech
    // bubble can show with no HUD panel visible).
    // Nameplates count as content too — without them in this guard, a scene
    // with no visible HUD screens would silently drop every queued plate.
    if (!initialized_ ||
        (!hasVisibleScreens() && worldLabels_.empty() && nameplates_.empty())) {
        worldMarkers_.clear();
        worldLabels_.clear();
        nameplates_.clear();
        return;
    }

    renderer_.beginFrame();

    glm::vec2 screenSize(static_cast<float>(screenWidth_), static_cast<float>(screenHeight_));

    const auto now = std::chrono::steady_clock::now();
    for (auto& [name, entry] : screens_) {
        if (!entry.visible || !entry.panel) continue;
        auto* panel = entry.panel.get();

        panel->applyAutoSize(&font_, theme_);   // layout pass: content decides the height
        glm::vec2 panelPos = resolveAnchor(panel->anchor, {0, 0}, screenSize,
                                            panel->size, panel->offset);

        // Per-screen appear-animation clock (see UITheme::screenElapsed).
        theme_.screenElapsed = std::chrono::duration<float>(now - entry.shownAt).count();
        panel->render(&renderer_, &font_, theme_, panelPos);
    }
    theme_.screenElapsed = 1.0e9f;   // world labels & any later draws render settled

    // World markers (the target ring's dots, G-105): plain rects in logical space,
    // under the labels and plates so text stays readable over them.
    for (const auto& m : worldMarkers_)
        renderer_.drawRect(m.pos - glm::vec2(m.size * 0.5f), {m.size, m.size}, m.color);
    worldMarkers_.clear();

    // World-anchored overlay labels (speech bubbles / interaction prompts), drawn
    // last so they sit over the HUD. Centered horizontally, box sits ABOVE the
    // anchor point. Cleared after drawing (re-queued each frame by the host).
    const float padX = 10.0f, padY = 6.0f;
    const float lineH = font_.lineHeight(theme_.textScale);
    for (const auto& wl : worldLabels_) {
        float textW = font_.measureText(wl.text, theme_.textScale);
        glm::vec2 boxSize(textW + padX * 2.0f, lineH + padY * 2.0f);
        glm::vec2 boxPos(wl.screenPos.x - boxSize.x * 0.5f, wl.screenPos.y - boxSize.y);
        if (wl.bgAlpha > 0.0f)
            renderer_.drawRect(boxPos, boxSize, {0.05f, 0.05f, 0.08f, wl.bgAlpha});
        font_.drawText(&renderer_, wl.text, {boxPos.x + padX, boxPos.y + padY},
                       wl.textColor, theme_.textScale);
    }
    worldLabels_.clear();

    // Combat NAMEPLATES: name + health bar (+ targeting subtitle + selection
    // bracket) above each character. Drawn after the labels so a selected
    // target reads clearly over everything else.
    for (const auto& np : nameplates_) {
        const float s      = np.scale;
        const float barW   = 84.0f * s;
        const float barH   = 7.0f  * s;
        const float nameSc = theme_.textScale * 0.75f * s;
        const float subSc  = theme_.textScale * 0.6f  * s;
        const float nameH  = font_.lineHeight(nameSc);
        const float subH   = np.subtitle.empty() ? 0.0f : font_.lineHeight(subSc);

        // Layout, anchored at the head: the BAR sits on the anchor line, the
        // NAME stacks above it, and the targeting subtitle hangs BELOW — so
        // the wide readout never crowds the name or the bracket.
        const float barTop = np.screenPos.y - barH;

        // Health bar: green -> amber -> red as it drains, on a dark backing.
        const float f = glm::clamp(np.hpFrac, 0.0f, 1.0f);
        const glm::vec4 fill = (f > 0.6f) ? glm::vec4(0.35f, 0.75f, 0.35f, 0.95f)
                             : (f > 0.3f) ? glm::vec4(0.85f, 0.70f, 0.25f, 0.95f)
                                          : glm::vec4(0.80f, 0.25f, 0.22f, 0.95f);
        const glm::vec2 barPos(np.screenPos.x - barW * 0.5f, barTop);
        renderer_.drawRect(barPos - glm::vec2(1.0f), {barW + 2.0f, barH + 2.0f},
                           {0.03f, 0.03f, 0.05f, 0.85f});
        renderer_.drawRect(barPos, {barW * f, barH}, fill);

        // Name, directly above the bar.
        const float nameW  = font_.measureText(np.name, nameSc);
        const float nameY  = barTop - 2.0f * s - nameH;
        const glm::vec4 nameCol = np.selected ? glm::vec4(1.0f, 0.85f, 0.45f, 1.0f)
                                : np.hostile  ? glm::vec4(0.95f, 0.62f, 0.55f, 1.0f)
                                              : glm::vec4(0.62f, 0.90f, 0.72f, 1.0f);
        renderer_.drawRect({np.screenPos.x - nameW * 0.5f - 4.0f, nameY},
                           {nameW + 8.0f, nameH},
                           {0.04f, 0.04f, 0.06f, np.selected ? 0.80f : 0.55f});
        font_.drawText(&renderer_, np.name, {np.screenPos.x - nameW * 0.5f, nameY},
                       nameCol, nameSc);

        // Targeting readout ABOVE the name (G-122): the column is readout / name / bar,
        // all above the head anchor, so nothing hangs over the character's face and
        // the bracket has one box to frame. (It used to hang below the bar, where it
        // collided with the head, the bracket and any interact prompt.)
        float subW = 0.0f, subY = nameY;
        if (!np.subtitle.empty()) {
            subW = font_.measureText(np.subtitle, subSc);
            subY = nameY - 2.0f * s - subH;
            renderer_.drawRect({np.screenPos.x - subW * 0.5f - 4.0f, subY},
                               {subW + 8.0f, subH}, {0.04f, 0.04f, 0.06f, 0.78f});
            font_.drawText(&renderer_, np.subtitle,
                           {np.screenPos.x - subW * 0.5f, subY}, {0.98f, 0.90f, 0.62f, 1.0f}, subSc);
        }

        // (The yellow corner bracket that used to frame the selected column was retired
        // 2026-09-16: the animated ground ring is the selection marker now - "a more
        // obvious visual marker, something like a bright animated circle on the ground".)
    }
    nameplates_.clear();

    // The DRAGGED GHOST (G-150): what the pointer is carrying, drawn over everything so
    // it is never clipped by the panel it came from or the one it is heading for.
    if (dragActive_ && !dragIconPath_.empty()) {
        if (dragIconTex_ == -1) {
            const int idx = renderer_.loadTexture(dragIconPath_);
            dragIconTex_ = (idx >= 0) ? idx : -2;
        }
        if (dragIconTex_ >= 0) {
            const float side = 48.0f;
            renderer_.drawImage(lastMousePos_ - glm::vec2(side * 0.5f), {side, side},
                                dragIconTex_, {1.0f, 1.0f, 1.0f, 0.85f});
        }
    }

    // TOOLTIP (G-148), drawn last of all so it sits over every screen, every plate and
    // the panel it belongs to - a tooltip clipped by its own action bar is useless.
    // It hangs ABOVE and slightly right of the pointer and is pushed back inside the
    // canvas at the edges, so the rightmost action-bar slot still reads its own text.
    if (!hoverTooltip_.empty()) {
        const float sc    = theme_.textScale;
        const float tipLineH = font_.lineHeight(sc);
        const float tipPadX = 8.0f, tipPadY = 6.0f;
        std::vector<std::string> lines;
        for (size_t i = 0, j; i <= hoverTooltip_.size(); i = j + 1) {
            j = hoverTooltip_.find(char(10), i);
            if (j == std::string::npos) j = hoverTooltip_.size();
            lines.push_back(hoverTooltip_.substr(i, j - i));
            if (j == hoverTooltip_.size()) break;
        }
        float textW = 0.0f;
        for (const auto& l : lines) textW = std::max(textW, font_.measureText(l, sc));
        const glm::vec2 box{textW + tipPadX * 2.0f,
                            tipLineH * static_cast<float>(lines.size()) + tipPadY * 2.0f};
        glm::vec2 at{lastMousePos_.x + 14.0f, lastMousePos_.y - box.y - 10.0f};
        at.x = std::min(at.x, screenSize.x - box.x - 4.0f);
        at.x = std::max(at.x, 4.0f);
        if (at.y < 4.0f) at.y = lastMousePos_.y + 20.0f;   // no room above: go below
        renderer_.drawRect(at - glm::vec2(1.0f), box + glm::vec2(2.0f), {0.62f, 0.56f, 0.40f, 0.95f});
        renderer_.drawRect(at, box, {0.05f, 0.05f, 0.07f, 0.96f});
        for (size_t i = 0; i < lines.size(); ++i) {
            // The first line is the name, the rest are detail - dimmer, so the eye lands
            // on the name first.
            const glm::vec4 col = (i == 0) ? glm::vec4(1.0f, 0.93f, 0.76f, 1.0f)
                                           : glm::vec4(0.82f, 0.82f, 0.86f, 1.0f);
            font_.drawText(&renderer_, lines[i],
                           {at.x + tipPadX, at.y + tipPadY + tipLineH * static_cast<float>(i)}, col, sc);
        }
    }

    renderer_.endFrame(cmd);
}

} // namespace UI
} // namespace Phyxel


namespace Phyxel {
namespace UI {

std::vector<std::string> UISystem::visibleScreenNames() const {
    std::vector<std::string> out;
    for (const auto& [name, entry] : screens_)
        if (entry.visible && entry.panel) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

nlohmann::json UISystem::lintLayout() {
    std::vector<UIPanel*> panels;
    for (auto& [name, entry] : screens_)
        if (entry.visible && entry.panel) {
            if (entry.panel->id.empty()) entry.panel->id = "screen:" + name;   // report by screen, never ''
            panels.push_back(entry.panel.get());
        }
    const glm::vec2 screenSize(static_cast<float>(screenWidth_), static_cast<float>(screenHeight_));
    nlohmann::json out = nlohmann::json::array();
    for (const auto& d : Phyxel::UI::lintLayout(panels, screenSize, &font_, theme_))
        out.push_back({{"kind", d.kind}, {"panel", d.panel}, {"other", d.other},
                       {"amount", d.amount}, {"message", d.message}});
    return out;
}

}  // namespace UI
}  // namespace Phyxel
