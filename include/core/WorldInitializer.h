#pragma once

#include <memory>
#include <string>
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
    namespace Scene { class SceneManager; }
    namespace Physics { class PhysicsWorld; }
    namespace Graphics { class RenderCoordinator; }
    namespace Utils { 
        class PerformanceMonitor;
    }
    class Timer;
    class ChunkManager;
    class ForceSystem;
    class MouseVelocityTracker;
    class PerformanceProfiler;
}

namespace VulkanCube {
namespace Core {

/**
 * @brief Handles all application initialization and world setup
 * 
 * Coordinates the initialization of:
 * - Window and rendering systems
 * - Vulkan graphics pipeline
 * - Physics simulation
 * - Chunk world generation
 * - Input systems
 * - Asset loading
 */
class WorldInitializer {
public:
    WorldInitializer(
        UI::WindowManager* windowManager,
        Input::InputManager* inputManager,
        Vulkan::VulkanDevice* vulkanDevice,
        Vulkan::RenderPipeline* renderPipeline,
        Vulkan::RenderPipeline* dynamicRenderPipeline,
        Scene::SceneManager* sceneManager,
        Physics::PhysicsWorld* physicsWorld,
        Timer* timer,
        ChunkManager* chunkManager,
        ForceSystem* forceSystem,
        MouseVelocityTracker* mouseVelocityTracker,
        PerformanceProfiler* performanceProfiler,
        Utils::PerformanceMonitor* performanceMonitor,
        UI::ImGuiRenderer* imguiRenderer
    );
    ~WorldInitializer() = default;

    // Main initialization
    bool initialize();
    
    // Subsystem initialization
    bool initializeWindow();
    bool initializeVulkan();
    bool initializeTextureAtlas();
    bool initializeScene();
    bool initializePhysics();
    bool loadAssets();
    
    // Configuration setters
    void setMaxChunkRenderDistance(float distance) { maxChunkRenderDistance = distance; }
    void setChunkInclusionDistance(float distance) { chunkInclusionDistance = distance; }

private:
    // Dependencies (non-owning pointers)
    UI::WindowManager* windowManager;
    Input::InputManager* inputManager;
    Vulkan::VulkanDevice* vulkanDevice;
    Vulkan::RenderPipeline* renderPipeline;
    Vulkan::RenderPipeline* dynamicRenderPipeline;
    Scene::SceneManager* sceneManager;
    Physics::PhysicsWorld* physicsWorld;
    Timer* timer;
    ChunkManager* chunkManager;
    ForceSystem* forceSystem;
    MouseVelocityTracker* mouseVelocityTracker;
    PerformanceProfiler* performanceProfiler;
    Utils::PerformanceMonitor* performanceMonitor;
    UI::ImGuiRenderer* imguiRenderer;
    
    // Configuration
    float maxChunkRenderDistance = 96.0f;
    float chunkInclusionDistance = 128.0f;
};

} // namespace Core
} // namespace VulkanCube
