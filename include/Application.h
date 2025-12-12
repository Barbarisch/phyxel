#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "core/Types.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "graphics/RenderCoordinator.h"
#include "graphics/RaycastVisualizer.h"
#include "scene/VoxelInteractionSystem.h"
#include "physics/PhysicsWorld.h"
#include "utils/Timer.h"
#include "utils/PerformanceProfiler.h"
#include "utils/PerformanceMonitor.h"
#include "utils/Frustum.h"
#include "ui/ImGuiRenderer.h"
#include "ui/WindowManager.h"
#include "input/InputManager.h"
#include "input/InputController.h"
#include "core/ChunkManager.h"
#include "core/ForceSystem.h"
#include "core/WorldInitializer.h"
#include "core/ObjectTemplateManager.h"
#include "core/AudioSystem.h"
#include "scripting/ScriptingSystem.h"
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
    void quit() { isRunning = false; }

    // Configuration
    void setWindowSize(int width, int height);
    void setTitle(const std::string& title);
    
    // Performance overlay methods
    void togglePerformanceOverlay();
    void toggleDebugRendering();
    void cycleDebugVisualizationMode();
    void toggleRaycastVisualization();
    void cycleRaycastTargetMode();

    // Accessors
    ObjectTemplateManager* getObjectTemplateManager() const { return objectTemplateManager.get(); }
    RaycastVisualizer* getRaycastVisualizer() const { return raycastVisualizer.get(); }
    VoxelInteractionSystem* getVoxelInteractionSystem() const { return voxelInteractionSystem.get(); }
    ChunkManager* getChunkManager() const { return chunkManager.get(); }
    Input::InputManager* getInputManager() const { return inputManager.get(); }

private:
    // ============================================================================
    // CORE SYSTEMS (Ownership)
    // ============================================================================
    
    // Window and display
    std::unique_ptr<UI::WindowManager> windowManager;
    
    // Rendering pipeline
    std::unique_ptr<Vulkan::VulkanDevice> vulkanDevice;                // Low-level Vulkan operations
    std::unique_ptr<Vulkan::RenderPipeline> renderPipeline;            // Static geometry pipeline
    std::unique_ptr<Vulkan::RenderPipeline> dynamicRenderPipeline;     // Dynamic objects pipeline
    std::unique_ptr<Graphics::RenderCoordinator> renderCoordinator;    // Coordinates all rendering
    std::unique_ptr<UI::ImGuiRenderer> imguiRenderer;                  // Debug UI rendering
    std::unique_ptr<RaycastVisualizer> raycastVisualizer;              // Raycast debug visualization
    
    // World and physics
    std::unique_ptr<ChunkManager> chunkManager;                        // Voxel world management
    std::unique_ptr<Physics::PhysicsWorld> physicsWorld;               // Bullet physics simulation
    std::unique_ptr<VoxelInteractionSystem> voxelInteractionSystem;    // Cube/subcube interaction
    std::unique_ptr<ForceSystem> forceSystem;                          // Force propagation system
    std::unique_ptr<ObjectTemplateManager> objectTemplateManager;      // Voxel object templates
    
    // Input and interaction
    std::unique_ptr<Input::InputManager> inputManager;                 // Keyboard/mouse input
    std::unique_ptr<InputController> inputController;                  // Input bindings and control
    std::unique_ptr<MouseVelocityTracker> mouseVelocityTracker;        // Mouse velocity tracking
    
    // Performance and timing
    std::unique_ptr<Timer> timer;                                      // Game loop timing
    std::unique_ptr<PerformanceProfiler> performanceProfiler;          // Frame profiling
    std::unique_ptr<Utils::PerformanceMonitor> performanceMonitor;     // Performance metrics
    
    // Initialization
    std::unique_ptr<Core::WorldInitializer> worldInitializer;          // Handles initialization

    // Audio System
    std::unique_ptr<Core::AudioSystem> audioSystem;

    // Scripting System
    std::unique_ptr<ScriptingSystem> scriptingSystem;

    // ============================================================================
    // APPLICATION STATE
    // ============================================================================
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
    // Debug flags moved to InputController
    
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

    // Main loop
    void update(float deltaTime);
    void render();
    void renderImGui();
    void handleInput();
    void spawnTestDynamicSubcube();  // Spawn a test dynamic subcube above the chunks
    void placeNewCube();            // Place a new cube adjacent to the hovered cube face

    // Ray-AABB intersection utility
    bool rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                         const glm::vec3& aabbMin, const glm::vec3& aabbMax, 
                         float& distance) const;

    // Utility methods
    void debugCoordinateSystem(); // Debug coordinate conversion and physics positioning
    
    // Color utility methods
    glm::vec3 calculateLighterColor(const glm::vec3& originalColor) const;
    
    // Initialization state
    bool m_initialized = false;
    void renderPerformanceOverlay();
};

} // namespace VulkanCube
