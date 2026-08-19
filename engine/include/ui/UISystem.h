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
#include <chrono>

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

    /// Inject a synthetic left-click at a screen position (UI space = the offscreen
    /// resolution, i.e. window size). Routes through the same per-screen hit-testing
    /// as handleInput and fires the hit widget's handlers. Returns true if a widget
    /// consumed it. For agent/test-driven UI interaction without a real mouse.
    bool injectClick(glm::vec2 pos);

    /// Route mouse-wheel input to the scrollable panel under `pos` (topmost
    /// visible screen wins; nested scrollables win over parents). Returns
    /// true when consumed. delta > 0 = wheel up.
    bool handleScroll(glm::vec2 pos, float delta);

    // ── Key capture (keybinding rebind) ──────────────────────────
    // One-shot "press a key" capture for the settings rebind buttons. After
    // beginKeyCapture, handleInput consumes ALL input until the user presses a
    // key: that key (and held modifiers) is delivered to `onCaptured` and capture
    // ends. ESCAPE cancels (onCaptured not called; onCancelled is, if set). The
    // capture arms only once all keys are released, so the click/Enter that
    // started it isn't mistaken for the new binding.

    /// Begin one-shot key capture. `onCaptured(glfwKey, mods)` fires on the next
    /// key press; ESC cancels and fires `onCancelled` (if provided).
    void beginKeyCapture(std::function<void(int key, int mods)> onCaptured,
                         std::function<void()> onCancelled = {});

    /// True while waiting for a key (the rebind UI shows "Press a key…").
    bool isCapturingKey() const { return keyCaptureActive_; }

    /// Abort capture without binding (does not fire either callback).
    void cancelKeyCapture();

    // ── Rendering ───────────────────────────────────────────────

    /// Render all visible screens. Call inside the post-process render pass
    /// (after drawQuad, before endPostProcessRenderPass).
    void render(VkCommandBuffer cmd);

    // ── Accessors ───────────────────────────────────────────────

    UIRenderer* getRenderer() { return &renderer_; }
    const BitmapFont* getFont() const { return &font_; }
    uint32_t width() const { return screenWidth_; }
    uint32_t height() const { return screenHeight_; }
    UITheme& getTheme() { return theme_; }
    const UITheme& getTheme() const { return theme_; }

    // ── World-anchored overlay (speech bubbles, interaction prompts) ──
    // These replace the ImGui renderSpeechBubbles/renderInteractionPrompt
    // (docs/HudSystem.md §11a). The host projects a world position to screen
    // pixels (worldToScreen) and queues a label each frame (addWorldLabel); the
    // labels draw in render() AFTER the retained screens — centered horizontally
    // and sitting ABOVE the point — then clear. No input (purely decorative).

    /// Project a world position to screen pixels. Returns false if behind the
    /// camera. Matches the engine's Vulkan projection (Y already flipped).
    static bool worldToScreen(const glm::vec3& worldPos, const glm::mat4& view,
                              const glm::mat4& proj, float screenW, float screenH,
                              glm::vec2& outScreen);

    /// Queue a world-anchored text label for THIS frame (cleared after render()).
    /// `bgAlpha` <= 0 draws text only (no backing box).
    void addWorldLabel(glm::vec2 screenPos, const std::string& text,
                       glm::vec4 textColor, float bgAlpha);

private:
    UIRenderer renderer_;
    BitmapFont font_;
    UITheme theme_;

    struct ScreenEntry {
        std::unique_ptr<UIPanel> panel;
        bool visible = false;
        // Stamped on the hidden→visible transition; drives appear animations
        // (UITheme::screenElapsed). Re-showing an already-visible screen does
        // NOT restart its animations.
        std::chrono::steady_clock::time_point shownAt{};
    };
    std::unordered_map<std::string, ScreenEntry> screens_;

    // Snapshot of the currently-visible screens, taken before click dispatch so a
    // screen revealed by an onClick handler can't receive that same click (menu
    // Back soft-lock — see handleInput / injectClick).
    std::vector<ScreenEntry*> visibleScreenSnapshot();

    uint32_t screenWidth_;
    uint32_t screenHeight_;
    bool initialized_ = false;

    // Input state
    bool wasMousePressed_ = false;
    bool prevBackspace_ = false;  // edge-tracking for the focused text field
    bool prevEnter_ = false;

    // One-shot key capture (rebind). keyCaptureArmed_ gates capture until all keys
    // are released once, so the key that confirmed the rebind button isn't grabbed.
    bool keyCaptureActive_ = false;
    bool keyCaptureArmed_ = false;
    std::function<void(int, int)> keyCaptureCb_;
    std::function<void()> keyCaptureCancelCb_;

    // Per-frame world-anchored overlay labels (addWorldLabel), drawn + cleared in render().
    struct WorldLabel {
        glm::vec2 screenPos;
        std::string text;
        glm::vec4 textColor;
        float bgAlpha;
    };
    std::vector<WorldLabel> worldLabels_;
};

} // namespace UI
} // namespace Phyxel
