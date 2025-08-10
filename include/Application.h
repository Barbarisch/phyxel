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
        glm::ivec3 localPos;
        glm::ivec3 worldPos;
        
        CubeLocation() : chunk(nullptr), localPos(-1), worldPos(-1) {}
        CubeLocation(Chunk* c, const glm::ivec3& local, const glm::ivec3& world) 
            : chunk(c), localPos(local), worldPos(world) {}
        
        bool isValid() const { return chunk != nullptr; }
    };

    // Window management
    GLFWwindow* window;
    
    // Core components
    std::unique_ptr<Vulkan::VulkanDevice> vulkanDevice;
    std::unique_ptr<Vulkan::RenderPipeline> renderPipeline;
    std::unique_ptr<Scene::SceneManager> sceneManager;
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
    } debugFlags;
    
    // GPU frustum culling results for UI display
    uint32_t lastVisibleInstances = 0;
    uint32_t lastCulledInstances = 0;

    // Initialization methods
    bool initializeWindow();
    bool initializeVulkan();
    bool initializeScene();
    bool initializePhysics();
    bool loadAssets();

    // Main loop
    void update(float deltaTime);
    void render();
    void drawFrame();
    void handleInput();
    void processInput();

    // Camera controls
    void initializeCamera();
    static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    
    // Mouse picking / hover functionality
    void updateMouseHover();
    glm::vec3 screenToWorldRay(double mouseX, double mouseY) const;
    
    // Chunk-based hover detection helpers (optimized)
    CubeLocation pickCubeInChunksOptimized(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;
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
    void createTestScene();
    
    // Frame profiling methods
    FrameTiming profileFrame();
    void printProfilingInfo(int fps);
    void printDetailedTimings();
    
    // Performance overlay methods
    void togglePerformanceOverlay();
    void renderPerformanceOverlay();
};

} // namespace VulkanCube
