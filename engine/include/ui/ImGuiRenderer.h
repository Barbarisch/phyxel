#pragma once

#include <memory>
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

// Forward declarations
struct ImGuiContext;
struct ImGuiInputTextCallbackData;
struct ImFont;
namespace Phyxel {
    class Timer;
    class PerformanceProfiler;
    class GpuProfiler;
    struct FrameTiming;
    struct DetailedFrameTiming;
    class ForceSystem;
    class MouseVelocityTracker;
    namespace Physics { class PhysicsWorld; }
    namespace Graphics { class LightManager; }
    namespace Vulkan { class VulkanDevice; class RenderPipeline; }
    class ScriptingSystem;
    namespace Core { class InitiativeTracker; class Party; class EntityRegistry; }
    namespace UI { class DialogueSystem; class SpeechBubbleManager; }
}

namespace Phyxel::UI {

class ImGuiRenderer {
public:
    ImGuiRenderer();
    ~ImGuiRenderer();

    // Initialization and cleanup
    bool initialize(GLFWwindow* window, Vulkan::VulkanDevice* vulkanDevice, VkRenderPass renderPass, bool enableViewports = false);
    void cleanup();

    // Frame lifecycle
    void newFrame();
    void endFrame();
    void render(uint32_t currentFrame, uint32_t imageIndex);
    void updatePlatformWindows();  // Multi-viewport: update/render secondary OS windows

    // Scripting Console
    void renderScriptingConsole(bool showConsole, ScriptingSystem* scriptingSystem);

    // Overlay rendering
    void renderPerformanceOverlay(
        bool showOverlay,
        Timer* timer,
        PerformanceProfiler* performanceProfiler,
        const FrameTiming& frameTiming,
        const std::vector<DetailedFrameTiming>& detailedTimings,
        Physics::PhysicsWorld* physicsWorld,
        const glm::vec3& cameraPos,
        uint64_t frameCount,
        float& renderDistance,          // Reference to allow modification
        float& chunkInclusionDistance   // Reference to allow modification
    );

    void renderProfilerWindow(
        bool showProfiler,
        PerformanceProfiler* cpuProfiler,
        GpuProfiler* gpuProfiler
    );
    
    void renderForceSystemDebug(
        bool showDebug,
        Phyxel::ForceSystem* forceSystem,
        Phyxel::MouseVelocityTracker* mouseVelocityTracker,
        bool hasHoveredCube,
        const glm::vec3& hoveredCubePos,
        float& manualForceValue  // Reference to allow modification by slider
    );

    void renderLightingControls(
        bool showControls,
        glm::vec3& sunDirection,
        glm::vec3& sunColor,
        float& ambientStrength,
        float& emissiveMultiplier,
        Graphics::LightManager* lightManager = nullptr
    );

    /// Render the RPG dialogue box (bottom of screen) when a conversation is active.
    void renderDialogueBox(DialogueSystem* dialogueSystem);

    /// Render floating speech bubbles above entities.
    /// @param viewMatrix       Camera view matrix for world-to-screen projection
    /// @param projectionMatrix Camera projection matrix
    /// @param screenWidth      Viewport width in pixels
    /// @param screenHeight     Viewport height in pixels
    void renderSpeechBubbles(SpeechBubbleManager* bubbleManager,
                             const glm::mat4& viewMatrix,
                             const glm::mat4& projectionMatrix,
                             float screenWidth, float screenHeight);

    /// Render interaction prompt above a world position. Custom text overrides default.
    void renderInteractionPrompt(bool show, const glm::vec3& npcWorldPos,
                                  const glm::mat4& viewMatrix,
                                  const glm::mat4& projectionMatrix,
                                  float screenWidth, float screenHeight,
                                  const char* customText = nullptr);

    /// Render D&D combat HUD: initiative order, HP bars, whose-turn indicator.
    /// Only visible when combat is active (InitiativeTracker::isCombatActive()).
    /// @param tracker        The initiative tracker (may be nullptr).
    /// @param party          Party info for player/NPC labelling (may be nullptr).
    /// @param entityRegistry Used to look up HealthComponent per entity (may be nullptr).
    void renderCombatHUD(Core::InitiativeTracker* tracker,
                         Core::Party*             party,
                         Core::EntityRegistry*    entityRegistry);

    // Helper for callbacks
    int handleInputTextCallback(struct ::ImGuiInputTextCallbackData* data);

    /// Get the monospace font loaded during initialization (may be nullptr).
    ImFont* getMonospaceFont() const { return m_monoFont; }

private:
    ImGuiContext* m_context;
    bool m_initialized;
    ImFont* m_monoFont = nullptr;  // Monospace font for terminal rendering
    
    // Vulkan objects
    Vulkan::VulkanDevice* m_vulkanDevice;
    Vulkan::RenderPipeline* m_renderPipeline;
    GLFWwindow* m_window;
    
    // Scripting console state
    char m_scriptInputBuffer[1024] = "";
    char m_scriptEditorBuffer[1024 * 16] = ""; // 16KB buffer for script editor
    
    // Completion state
    std::vector<std::string> m_completions;
    int m_selectedCompletionIndex = 0;
    bool m_showCompletionPopup = false;
    std::string m_completionPrefix;
    
    // Helper for callbacks
    ScriptingSystem* m_currentScriptingSystem = nullptr;
};

} // namespace Phyxel::UI
