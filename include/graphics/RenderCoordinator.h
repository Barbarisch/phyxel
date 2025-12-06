#pragma once

#include "core/Types.h"
#include "utils/PerformanceMonitor.h"
#include "utils/PerformanceProfiler.h"
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
}

namespace VulkanCube {
namespace Graphics {

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
        ChunkManager* chunkManager,
        Utils::PerformanceMonitor* performanceMonitor,
        PerformanceProfiler* performanceProfiler
    );
    ~RenderCoordinator() = default;

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
    
    // Frame state accessors
    void setFrameStartTime(std::chrono::high_resolution_clock::time_point time) { frameStartTime = time; }
    void setCachedViewMatrix(const glm::mat4& view) { cachedViewMatrix = view; }
    void setProjectionMatrixNeedsUpdate(bool needsUpdate) { projectionMatrixNeedsUpdate = needsUpdate; }
    
    uint32_t getCurrentFrame() const { return currentFrame; }

private:
    // Rendering subsystems
    size_t renderStaticGeometry();
    void renderDynamicSubcubes();
    
    // Dependencies (non-owning pointers)
    Vulkan::VulkanDevice* vulkanDevice;
    Vulkan::RenderPipeline* renderPipeline;
    Vulkan::RenderPipeline* dynamicRenderPipeline;
    UI::ImGuiRenderer* imguiRenderer;
    UI::WindowManager* windowManager;
    Input::InputManager* inputManager;
    ChunkManager* chunkManager;
    Utils::PerformanceMonitor* performanceMonitor;
    PerformanceProfiler* performanceProfiler;
    
    // Render state
    uint32_t currentFrame = 0;
    float maxChunkRenderDistance = 1000.0f;
    float chunkInclusionDistance = 2000.0f;
    bool debugModeEnabled = false;  // Toggle for debug visualization
    uint32_t debugVisualizationMode = 0;  // 0=wireframe, 1=normals, 2=hierarchy
    
    // Cached matrices
    glm::mat4 cachedViewMatrix;
    glm::mat4 cachedProjectionMatrix;
    bool projectionMatrixNeedsUpdate = true;
    
    // Timing
    std::chrono::high_resolution_clock::time_point frameStartTime;
    
    // GPU culling results (for future GPU frustum culling)
    uint32_t lastVisibleInstances = 0;
    uint32_t lastCulledInstances = 0;
};

} // namespace Graphics
} // namespace VulkanCube
