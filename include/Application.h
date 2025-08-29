#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "core/Types.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "scene/SceneManager.h"
#include "physics/PhysicsWorld.h"
#include "utils/Timer.h"
#include "utils/PerformanceProfiler.h"
#include "utils/Frustum.h"
#include "ui/ImGuiRenderer.h"
#include "core/ChunkManager.h"
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
    // Optimized cube location struct to avoid repeated coordinate conversions
    struct CubeLocation {
        Chunk* chunk;
        glm::ivec3 localPos;        // Local position within chunk
        glm::ivec3 worldPos;        // World position
        bool isSubcube;             // True if this location refers to a subcube
        glm::ivec3 subcubePos;      // Local position within parent cube (0-2 for each axis)
        
        CubeLocation() : chunk(nullptr), localPos(-1), worldPos(-1), isSubcube(false), subcubePos(-1) {}
        CubeLocation(Chunk* c, const glm::ivec3& local, const glm::ivec3& world) 
            : chunk(c), localPos(local), worldPos(world), isSubcube(false), subcubePos(-1) {}
        CubeLocation(Chunk* c, const glm::ivec3& local, const glm::ivec3& world, const glm::ivec3& sub) 
            : chunk(c), localPos(local), worldPos(world), isSubcube(true), subcubePos(sub) {}
        
        bool isValid() const { return chunk != nullptr; }
    };

    // Window management
    GLFWwindow* window;
    
    // Core components
    std::unique_ptr<Vulkan::VulkanDevice> vulkanDevice;
    std::unique_ptr<Vulkan::RenderPipeline> renderPipeline;         // Static cubes and static subcubes
    std::unique_ptr<Vulkan::RenderPipeline> dynamicRenderPipeline;  // Dynamic subcubes with physics
    std::unique_ptr<Scene::SceneManager> sceneManager;             // Still in use for legacy cube rendering
    std::unique_ptr<Physics::PhysicsWorld> physicsWorld;
    std::unique_ptr<Timer> timer;
    std::unique_ptr<PerformanceProfiler> performanceProfiler;
    std::unique_ptr<UI::ImGuiRenderer> imguiRenderer;
    std::unique_ptr<ChunkManager> chunkManager;

    // Application state
    bool isRunning;
    int windowWidth;
    int windowHeight;
    std::string windowTitle;

    // Frame timing
    FrameTiming frameTiming;
    float deltaTime;
    int frameCount;
    uint32_t currentFrame = 0;
    
    // Cached matrices for performance
    glm::mat4 cachedViewMatrix;
    glm::mat4 cachedProjectionMatrix;
    bool projectionMatrixNeedsUpdate = true;

    // Frame profiling
    std::vector<FrameTiming> frameTimings;
    std::vector<DetailedFrameTiming> detailedTimings;
    std::chrono::high_resolution_clock::time_point frameStartTime;
    std::chrono::high_resolution_clock::time_point cpuStartTime;
    double lastFrameTime;
    double fpsTimer;

    // Camera controls
    glm::vec3 cameraPos;
    glm::vec3 cameraFront;
    glm::vec3 cameraUp;
    float yaw;
    float pitch;
    float lastX;
    float lastY;
    bool firstMouse;
    bool mouseCaptured;
    
    // Mouse hover state
    double currentMouseX;
    double currentMouseY;
    int lastHoveredCube;
    
    // Chunk-based hover state
    glm::ivec3 currentHoveredWorldPos;
    glm::vec3 originalHoveredColor;
    bool hasHoveredCube = false;
    
    // Optimized hover state (avoids repeated coordinate conversions)
    CubeLocation currentHoveredLocation;
    
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
    } debugFlags;
    
    // GPU frustum culling results for UI display
    uint32_t lastVisibleInstances = 0;
    uint32_t lastCulledInstances = 0;
    
    // New chunk-level frustum culling
    Utils::Frustum cameraFrustum;
    float maxChunkRenderDistance = 96.0f; // Maximum distance for chunk rendering (configurable)
    void updateCameraFrustum(const glm::mat4& viewMatrix, const glm::mat4& projectionMatrix);
    std::vector<uint32_t> getVisibleChunks();
    std::vector<uint32_t> getVisibleChunksOptimized(); // Spatial query version for large worlds
    
    // Render distance management
    void setRenderDistance(float distance);
    float getRenderDistance() const { return maxChunkRenderDistance; }

    // Initialization methods
    bool initializeWindow();
    bool initializeVulkan();
    bool initializePhysics();
    bool initializeScene();
    bool loadAssets();

    // Main loop
    void update(float deltaTime);
    void render();
    void drawFrame();
    void renderImGui();
    size_t renderStaticGeometry();                                              // Render static cubes and subcubes - returns number of rendered chunks
    void renderDynamicGeometry();                                               // Render dynamic cubes and subcubes
    void renderDynamicSubcubes();     // Render dynamic subcubes with physics
    void handleInput();
    void processInput();
    void spawnTestDynamicSubcube();  // Spawn a test dynamic subcube above the chunks

    // Camera controls
    void initializeCamera();
    static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    
    // Mouse picking / hover functionality
    void updateMouseHover();
    glm::vec3 screenToWorldRay(double mouseX, double mouseY) const;
    void removeHoveredCube();    // Remove the currently hovered cube
    void subdivideHoveredCube(); // Subdivide the currently hovered cube into 27 subcubes
    void breakHoveredCube();     // Break the currently hovered cube into a dynamic cube with physics
    
    // Chunk-based hover detection helpers (optimized)
    CubeLocation pickCubeInChunksOptimized(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;
    glm::ivec3 findSubcubeHit(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::ivec3& cubeWorldPos) const;
    CubeLocation findExistingSubcubeHit(Chunk* chunk, const glm::ivec3& localPos, const glm::ivec3& cubeWorldPos, const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;
    void setHoveredCubeInChunksOptimized(const CubeLocation& location);
    void clearHoveredCubeInChunksOptimized();
    
    // Legacy chunk-based hover detection helpers (for compatibility)
    glm::ivec3 pickCubeInChunks(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;
    void setHoveredCubeInChunks(const glm::ivec3& worldPos);
    void clearHoveredCubeInChunks();
    bool rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                         const glm::vec3& aabbMin, const glm::vec3& aabbMax, 
                         float& distance) const;
    
    // Efficient voxel raycasting helpers
    glm::ivec3 raycastVoxelGrid(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::ivec3& chunkOrigin) const;

    // Utility methods
    void updateFrameTiming();
    void printPerformanceStats();
    void debugCoordinateSystem(); // Debug coordinate conversion and physics positioning
    
    // Frame profiling methods
    FrameTiming profileFrame();
    void printProfilingInfo(int fps);
    void printDetailedTimings();
    
    // Performance overlay methods
    void togglePerformanceOverlay();
    void renderPerformanceOverlay();
};

} // namespace VulkanCube
