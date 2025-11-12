#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "core/Types.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "graphics/RenderCoordinator.h"
#include "scene/SceneManager.h"
#include "scene/VoxelInteractionSystem.h"
#include "physics/PhysicsWorld.h"
#include "utils/Timer.h"
#include "utils/PerformanceProfiler.h"
#include "utils/PerformanceMonitor.h"
#include "utils/Frustum.h"
#include "ui/ImGuiRenderer.h"
#include "ui/WindowManager.h"
#include "input/InputManager.h"
#include "core/ChunkManager.h"
#include "core/ForceSystem.h"
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace VulkanCube {

class Application {
public:
    Application();
    ~Application();

    // Application lifecycle
    bool initialize();
    void run();
    void cleanup();

    // Configuration
    void setWindowSize(int width, int height);
    void setTitle(const std::string& title);

private:
    // Window management
    std::unique_ptr<UI::WindowManager> windowManager;
    
    // Core components
    std::unique_ptr<Vulkan::VulkanDevice> vulkanDevice;
    std::unique_ptr<Vulkan::RenderPipeline> renderPipeline;         // Static cubes and static subcubes
    std::unique_ptr<Vulkan::RenderPipeline> dynamicRenderPipeline;  // Dynamic subcubes with physics
    std::unique_ptr<Scene::SceneManager> sceneManager;             // Still in use for legacy cube rendering
    std::unique_ptr<Physics::PhysicsWorld> physicsWorld;
    std::unique_ptr<Timer> timer;
    std::unique_ptr<PerformanceProfiler> performanceProfiler;
    std::unique_ptr<Utils::PerformanceMonitor> performanceMonitor;
    std::unique_ptr<UI::ImGuiRenderer> imguiRenderer;
    std::unique_ptr<Input::InputManager> inputManager;
    std::unique_ptr<ChunkManager> chunkManager;
    std::unique_ptr<VoxelInteractionSystem> voxelInteractionSystem;
    std::unique_ptr<Graphics::RenderCoordinator> renderCoordinator;

    // Force-based breaking system
    std::unique_ptr<ForceSystem> forceSystem;
    std::unique_ptr<MouseVelocityTracker> mouseVelocityTracker;

    // Application state
    bool isRunning;

    // Frame timing
    float deltaTime;
    int frameCount;
    
    // Cached matrices for performance (used by RenderCoordinator)
    glm::mat4 cachedViewMatrix;
    glm::mat4 cachedProjectionMatrix;
    bool projectionMatrixNeedsUpdate = true;

    // Frame profiling timing
    std::chrono::high_resolution_clock::time_point frameStartTime;
    std::chrono::high_resolution_clock::time_point cpuStartTime;
    double lastFrameTime;
    double fpsTimer;

    // Performance overlay
    bool showPerformanceOverlay = false;
    
    // Debug system
    struct DebugFlags {
        bool hoverDetection = false;
        bool cameraMovement = false;
        bool performanceStats = false;
        bool chunkOperations = false;
        bool cubeOperations = false;
        bool disableBreakingForces = false; // For testing exact positioning without physics forces
        bool showForceSystemDebug = false;  // Show force system debug overlay
        float manualForceValue = 500.0f;    // User-controllable force value via slider
    } debugFlags;
    
    // New chunk-level frustum culling
    Utils::Frustum cameraFrustum;
    // Render distance configuration - two-tier system
    float maxChunkRenderDistance = 96.0f; // Frustum culling distance (actual render distance)
    float chunkInclusionDistance = 128.0f; // Chunk loading distance (always >= maxChunkRenderDistance)
    void updateCameraFrustum(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    std::vector<uint32_t> getVisibleChunks();
    std::vector<uint32_t> getVisibleChunksOptimized(); // Spatial query version for large worlds
    
    // Render distance controls
    float getRenderDistance() const { return maxChunkRenderDistance; }
    float getChunkInclusionDistance() const { return chunkInclusionDistance; }
    void setRenderDistance(float distance);
    void setChunkInclusionDistance(float distance);

    // Initialization methods
    bool initializeWindow();
    bool initializeVulkan();
    bool initializeTextureAtlas();
    bool initializePhysics();
    bool initializeScene();
    bool loadAssets();

    // Main loop
    void update(float deltaTime);
    void render();
    void renderImGui();
    void handleInput();
    void spawnTestDynamicSubcube();  // Spawn a test dynamic subcube above the chunks
    void placeNewCube();            // Place a new cube adjacent to the hovered cube face

    // Input initialization (registers actions with InputManager)
    void initializeInputActions();
    
    // Ray-AABB intersection utility
    bool rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                         const glm::vec3& aabbMin, const glm::vec3& aabbMax, 
                         float& distance) const;

    // Utility methods
    void debugCoordinateSystem(); // Debug coordinate conversion and physics positioning
    
    // Color utility methods
    glm::vec3 calculateLighterColor(const glm::vec3& originalColor) const;
    
    // Performance overlay methods
    void togglePerformanceOverlay();
    void renderPerformanceOverlay();
};

} // namespace VulkanCube
