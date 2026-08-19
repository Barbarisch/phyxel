#include "ui/UISystem.h"
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

bool UISystem::handleInput(Input::InputManager* input) {
    if (!initialized_ || !hasVisibleScreens()) return false;

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
    glm::vec2 mousePos(static_cast<float>(mx), static_cast<float>(my));

    bool mousePressed = input->isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT);
    bool mouseJustClicked = mousePressed && !wasMousePressed_;
    bool mouseDragging = mousePressed && wasMousePressed_;
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

    for (auto* entry : activeScreens) {
        auto* panel = entry->panel.get();

        // Resolve panel position from anchor
        glm::vec2 panelPos = resolveAnchor(panel->anchor, {0, 0}, screenSize,
                                            panel->size, panel->offset);

        // Hover always updates
        panel->handleHover(mousePos, panelPos, theme_);

        if (mouseJustClicked) {
            if (panel->handleClick(mousePos, panelPos, theme_)) {
                consumed = true;
            }
        } else if (mouseDragging) {
            if (panel->handleDrag(mousePos, panelPos, theme_)) {
                consumed = true;
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

    glm::vec2 screenSize(static_cast<float>(screenWidth_), static_cast<float>(screenHeight_));
    bool consumed = false;

    // Snapshot before dispatch — same soft-lock guard as handleInput (an injected
    // click via POST /api/ui/click must not be re-delivered to a screen revealed
    // by this very click). (game-dev feedback round 5 — UIShowcase.)
    auto activeScreens = visibleScreenSnapshot();

    for (auto* entry : activeScreens) {
        auto* panel = entry->panel.get();

        glm::vec2 panelPos = resolveAnchor(panel->anchor, {0, 0}, screenSize,
                                            panel->size, panel->offset);
        // Update hover so button visuals match, then click.
        panel->handleHover(pos, panelPos, theme_);
        if (panel->handleClick(pos, panelPos, theme_)) consumed = true;
    }

    return consumed;
}

bool UISystem::handleScroll(glm::vec2 pos, float delta) {
    if (!initialized_ || delta == 0.0f || !hasVisibleScreens()) return false;

    glm::vec2 screenSize(static_cast<float>(screenWidth_), static_cast<float>(screenHeight_));
    auto activeScreens = visibleScreenSnapshot();

    for (auto* entry : activeScreens) {
        auto* panel = entry->panel.get();
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

void UISystem::addWorldLabel(glm::vec2 screenPos, const std::string& text,
                             glm::vec4 textColor, float bgAlpha) {
    if (text.empty()) return;
    worldLabels_.push_back({screenPos, text, textColor, bgAlpha});
}

void UISystem::render(VkCommandBuffer cmd) {
    // Draw whenever there are visible screens OR queued world labels (a speech
    // bubble can show with no HUD panel visible).
    if (!initialized_ || (!hasVisibleScreens() && worldLabels_.empty())) {
        worldLabels_.clear();
        return;
    }

    renderer_.beginFrame();

    glm::vec2 screenSize(static_cast<float>(screenWidth_), static_cast<float>(screenHeight_));

    const auto now = std::chrono::steady_clock::now();
    for (auto& [name, entry] : screens_) {
        if (!entry.visible || !entry.panel) continue;
        auto* panel = entry.panel.get();

        glm::vec2 panelPos = resolveAnchor(panel->anchor, {0, 0}, screenSize,
                                            panel->size, panel->offset);

        // Per-screen appear-animation clock (see UITheme::screenElapsed).
        theme_.screenElapsed = std::chrono::duration<float>(now - entry.shownAt).count();
        panel->render(&renderer_, &font_, theme_, panelPos);
    }
    theme_.screenElapsed = 1.0e9f;   // world labels & any later draws render settled

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

    renderer_.endFrame(cmd);
}

} // namespace UI
} // namespace Phyxel
