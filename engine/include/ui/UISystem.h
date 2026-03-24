#pragma once

#include "ui/UIRenderer.h"
#include "ui/BitmapFont.h"
#include "ui/UIWidget.h"

#include <string>
#include <unordered_map>
#include <memory>
#include <functional>
#include <vector>
#include <utility>

namespace Phyxel {
    namespace Vulkan { class VulkanDevice; }
    namespace Input { class InputManager; }
namespace UI {

/**
 * @brief High-level UI system that owns the renderer, font, and screen stack.
 *
 * Orchestrates rendering and input for custom (non-ImGui) game menus.
 * Typical usage:
 *   1. Create UISystem with VulkanDevice + screen dimensions
 *   2. initialize() with the post-process render pass
 *   3. addScreen() to register menu panels
 *   4. showScreen() / hideScreen() to control visibility
 *   5. Each frame: handleInput() then render(cmd)
 */
class UISystem {
public:
    UISystem(Vulkan::VulkanDevice* device, uint32_t width, uint32_t height);
    ~UISystem();

    /// Create Vulkan resources and font atlas. Call after VulkanDevice is ready.
    bool initialize(VkRenderPass renderPass);

    /// Release all Vulkan resources.
    void cleanup();

    /// Recreate after swapchain resize.
    void resize(uint32_t width, uint32_t height);

    // ── Screen management ───────────────────────────────────────

    /// Register a screen panel by name. The UISystem takes ownership.
    void addScreen(const std::string& name, std::unique_ptr<UIPanel> panel);

    /// Remove a screen by name.
    void removeScreen(const std::string& name);

    /// Show/hide a screen. Only visible screens are rendered and receive input.
    void showScreen(const std::string& name);
    void hideScreen(const std::string& name);
    void toggleScreen(const std::string& name);
    bool isScreenVisible(const std::string& name) const;

    /// Hide all screens.
    void hideAllScreens();

    /// Get a screen panel by name (for dynamic modification).
    UIPanel* getScreen(const std::string& name);

    /// Returns true if any screen is currently visible.
    bool hasVisibleScreens() const;

    /// Get all screen names and their visibility (for API/debug listing).
    std::vector<std::pair<std::string, bool>> getScreenList() const;

    // ── Input routing ───────────────────────────────────────────

    /// Process mouse/keyboard input. Call once per frame before render.
    /// Returns true if UI consumed the input (game should ignore it).
    bool handleInput(Input::InputManager* input);

    // ── Rendering ───────────────────────────────────────────────

    /// Render all visible screens. Call inside the post-process render pass
    /// (after drawQuad, before endPostProcessRenderPass).
    void render(VkCommandBuffer cmd);

    // ── Accessors ───────────────────────────────────────────────

    UIRenderer* getRenderer() { return &renderer_; }
    const BitmapFont* getFont() const { return &font_; }
    UITheme& getTheme() { return theme_; }
    const UITheme& getTheme() const { return theme_; }

private:
    UIRenderer renderer_;
    BitmapFont font_;
    UITheme theme_;

    struct ScreenEntry {
        std::unique_ptr<UIPanel> panel;
        bool visible = false;
    };
    std::unordered_map<std::string, ScreenEntry> screens_;

    uint32_t screenWidth_;
    uint32_t screenHeight_;
    bool initialized_ = false;

    // Input state
    bool wasMousePressed_ = false;
};

} // namespace UI
} // namespace Phyxel
