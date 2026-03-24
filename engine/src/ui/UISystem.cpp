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
    if (!font_.initialize(&renderer_)) return false;
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

// ── Input routing ───────────────────────────────────────────

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

    for (auto& [name, entry] : screens_) {
        if (!entry.visible || !entry.panel) continue;
        auto* panel = entry.panel.get();

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
