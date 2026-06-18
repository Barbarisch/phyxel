#include "ui/UISystem.h"
#include "input/InputManager.h"
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
    if (it != screens_.end()) it->second.visible = true;
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

bool UISystem::handleInput(Input::InputManager* input) {
    if (!initialized_ || !hasVisibleScreens()) return false;

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

// ── Rendering ───────────────────────────────────────────────

void UISystem::render(VkCommandBuffer cmd) {
    if (!initialized_ || !hasVisibleScreens()) return;

    renderer_.beginFrame();

    glm::vec2 screenSize(static_cast<float>(screenWidth_), static_cast<float>(screenHeight_));

    for (auto& [name, entry] : screens_) {
        if (!entry.visible || !entry.panel) continue;
        auto* panel = entry.panel.get();

        glm::vec2 panelPos = resolveAnchor(panel->anchor, {0, 0}, screenSize,
                                            panel->size, panel->offset);

        panel->render(&renderer_, &font_, theme_, panelPos);
    }

    renderer_.endFrame(cmd);
}

} // namespace UI
} // namespace Phyxel
