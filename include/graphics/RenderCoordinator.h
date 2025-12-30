#pragma once

#include "core/Types.h"
#include "utils/PerformanceMonitor.h"
#include "utils/PerformanceProfiler.h"
#include "utils/GpuProfiler.h"
#include "scene/Entity.h"
#include <memory>
#include <chrono>
#include <glm/glm.hpp>

// Forward declarations
namespace VulkanCube {
    namespace UI { 
        class WindowManager;
        class ImGuiRenderer;
    }
    namespace Input { class InputManager; }
    namespace Vulkan { 
        class VulkanDevice;
        class RenderPipeline;
    }
    namespace Utils {
        class PerformanceMonitor;
    }
    class ChunkManager;
    class PerformanceProfiler;
    class RaycastVisualizer;
    class ScriptingSystem;
    namespace Graphics {
        class DebrisRenderPipeline;
    }
}

namespace VulkanCube {
namespace Graphics {

class ShadowMap;
class PostProcessor;
class Camera;
class DebrisRenderPipeline;

/**
 * @brief Manages the rendering pipeline and frame rendering
 * 
 * Coordinates all rendering operations including:
 * - Frame synchronization and swapchain management
 * - Static geometry rendering with frustum culling
 * - Dynamic subcube rendering
 * - Command buffer recording and submission
 * - Performance statistics tracking
 */
class RenderCoordinator {
public:
    RenderCoordinator(
        Vulkan::VulkanDevice* vulkanDevice,
        Vulkan::RenderPipeline* renderPipeline,
        Vulkan::RenderPipeline* dynamicRenderPipeline,
        UI::ImGuiRenderer* imguiRenderer,
        UI::WindowManager* windowManager,
        Input::InputManager* inputManager,
        Camera* camera,
        ChunkManager* chunkManager,
        Utils::PerformanceMonitor* performanceMonitor,
        PerformanceProfiler* performanceProfiler,
        RaycastVisualizer* raycastVisualizer,
        ScriptingSystem* scriptingSystem
    );
    ~RenderCoordinator();

    // Main rendering interface
    void render();
    void drawFrame();
    
    // Render distance management
    void setMaxChunkRenderDistance(float distance) { maxChunkRenderDistance = distance; }
    void setChunkInclusionDistance(float distance) { chunkInclusionDistance = distance; }
    
    // Debug visualization
    void toggleDebugMode() { debugModeEnabled = !debugModeEnabled; }
    void setDebugMode(bool enabled) { debugModeEnabled = enabled; }
    bool isDebugModeEnabled() const { return debugModeEnabled; }
    void setDebugVisualizationMode(uint32_t mode) { debugVisualizationMode = mode; }
    uint32_t getDebugVisualizationMode() const { return debugVisualizationMode; }
    
    // Raycast visualization
    void toggleRaycastVisualization() { raycastVisualizationEnabled = !raycastVisualizationEnabled; }
    void setRaycastVisualization(bool enabled) { raycastVisualizationEnabled = enabled; }
    bool isRaycastVisualizationEnabled() const { return raycastVisualizationEnabled; }
    RaycastVisualizer* getRaycastVisualizer() const { return raycastVisualizer; }
    
    // Scripting Console
    void setShowScriptingConsole(bool show) { showScriptingConsole = show; }
    bool isScriptingConsoleVisible() const { return showScriptingConsole; }

    // Ambient Light Control
    void setAmbientLightStrength(float strength) { ambientLightStrength = glm::clamp(strength, 0.0f, 2.0f); }
    float getAmbientLightStrength() const { return ambientLightStrength; }
    void adjustAmbientLightStrength(float delta) { setAmbientLightStrength(ambientLightStrength + delta); }

    // Lighting Control
    glm::vec3& getSunDirection() { return sunDirection; }
    glm::vec3& getSunColor() { return sunColor; }
    float& getAmbientLightRef() { return ambientLightStrength; }
    float& getEmissiveMultiplierRef() { return emissiveMultiplier; }

    GpuProfiler* getGpuProfiler() { return gpuProfiler.get(); }
    
    // Lighting Controls UI
    void toggleLightingControls() { showLightingControls = !showLightingControls; }
    bool isLightingControlsVisible() const { return showLightingControls; }

    // Profiler UI
    void toggleProfiler() { showProfiler = !showProfiler; }
    bool isProfilerVisible() const { return showProfiler; }

    // Entity rendering
    void setEntities(const std::vector<std::unique_ptr<Scene::Entity>>* entities) { this->entities = entities; }
    
    // Render UI elements (must be called between ImGui::NewFrame and ImGui::Render)
    void renderUI();

    // Frame state accessors
    void setFrameStartTime(std::chrono::high_resolution_clock::time_point time) { frameStartTime = time; }
    void setCachedViewMatrix(const glm::mat4& view) { cachedViewMatrix = view; }
    void setProjectionMatrixNeedsUpdate(bool needsUpdate) { projectionMatrixNeedsUpdate = needsUpdate; }
    
    uint32_t getCurrentFrame() const { return currentFrame; }

private:
    const std::vector<std::unique_ptr<Scene::Entity>>* entities = nullptr;

    // Rendering subsystems
    size_t renderStaticGeometry();
    void renderDynamicSubcubes();
    void renderEntities(VkCommandBuffer commandBuffer);
    void renderShadowPass(VkCommandBuffer commandBuffer, const glm::mat4& lightSpaceMatrix);
    
    // Dependencies (non-owning pointers)
    Vulkan::VulkanDevice* vulkanDevice;
    Vulkan::RenderPipeline* renderPipeline;
    Vulkan::RenderPipeline* dynamicRenderPipeline;
    std::unique_ptr<ShadowMap> shadowMap;
    std::unique_ptr<PostProcessor> postProcessor;
    std::unique_ptr<GpuProfiler> gpuProfiler;
    UI::ImGuiRenderer* imguiRenderer;
    UI::WindowManager* windowManager;
    Input::InputManager* inputManager;
    Camera* camera;
    ChunkManager* chunkManager;
    Utils::PerformanceMonitor* performanceMonitor;
    PerformanceProfiler* performanceProfiler;
    RaycastVisualizer* raycastVisualizer;
    ScriptingSystem* scriptingSystem;
    
    // Render state
    bool showScriptingConsole = false;
    bool showLightingControls = false;
    bool showProfiler = false;
    uint32_t currentFrame = 0;
    float maxChunkRenderDistance = 1000.0f;
    float chunkInclusionDistance = 2000.0f;
    bool debugModeEnabled = false;  // Toggle for debug visualization
    uint32_t debugVisualizationMode = 0;  // 0=wireframe, 1=normals, 2=hierarchy, 3=uv, 4=emissive
    bool raycastVisualizationEnabled = false;  // Toggle for raycast visualization
    float ambientLightStrength = 1.0f; // Default brightness multiplier
    glm::vec3 sunDirection = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
    glm::vec3 sunColor = glm::vec3(1.0f, 1.0f, 1.0f);
    float emissiveMultiplier = 2.0f;
    
    // Cached matrices
    glm::mat4 cachedViewMatrix;
    glm::mat4 cachedProjectionMatrix;
    bool projectionMatrixNeedsUpdate = true;
    
    // Timing
    std::chrono::high_resolution_clock::time_point frameStartTime;
    
    // GPU culling results (for future GPU frustum culling)
    uint32_t lastVisibleInstances = 0;
    uint32_t lastCulledInstances = 0;

    // Debris Rendering
    std::unique_ptr<DebrisRenderPipeline> debrisPipeline;
};

} // namespace Graphics
} // namespace VulkanCube
