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
        
        // Face information for cube placement
        int hitFace;                // Which face was hit: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
        glm::vec3 hitNormal;        // Surface normal of hit face
        glm::vec3 hitPoint;         // Exact hit point on the cube surface
        
        CubeLocation() : chunk(nullptr), localPos(-1), worldPos(-1), isSubcube(false), subcubePos(-1), hitFace(-1), hitNormal(0), hitPoint(0) {}
        CubeLocation(Chunk* c, const glm::ivec3& local, const glm::ivec3& world) 
            : chunk(c), localPos(local), worldPos(world), isSubcube(false), subcubePos(-1), hitFace(-1), hitNormal(0), hitPoint(0) {}
        CubeLocation(Chunk* c, const glm::ivec3& local, const glm::ivec3& world, const glm::ivec3& sub) 
            : chunk(c), localPos(local), worldPos(world), isSubcube(true), subcubePos(sub), hitFace(-1), hitNormal(0), hitPoint(0) {}
        
        bool isValid() const { return chunk != nullptr; }
        
        // Get the world position where a new cube should be placed adjacent to this face
        glm::ivec3 getAdjacentPlacementPosition() const {
            if (hitFace < 0) return worldPos; // No face data available
            
            // Face normals: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
            glm::ivec3 faceOffset;
            switch (hitFace) {
                case 0: faceOffset = glm::ivec3(1, 0, 0); break;   // +X face
                case 1: faceOffset = glm::ivec3(-1, 0, 0); break;  // -X face
                case 2: faceOffset = glm::ivec3(0, 1, 0); break;   // +Y face
                case 3: faceOffset = glm::ivec3(0, -1, 0); break;  // -Y face
                case 4: faceOffset = glm::ivec3(0, 0, 1); break;   // +Z face
                case 5: faceOffset = glm::ivec3(0, 0, -1); break;  // -Z face
                default: return worldPos;
            }
            return worldPos + faceOffset;
        }
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
    
    // Hover performance tracking
    mutable double hoverDetectionTimeMs = 0.0;
    mutable int hoverDetectionSamples = 0;
    mutable double avgHoverDetectionTimeMs = 0.0;
    
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
    void placeNewCube();            // Place a new cube adjacent to the hovered cube face

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
    CubeLocation findExistingSubcubeHit(Chunk* chunk, const glm::ivec3& localPos, const glm::ivec3& cubeWorldPos, const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;
    void setHoveredCubeInChunksOptimized(const CubeLocation& location);
    void clearHoveredCubeInChunksOptimized();
    
    // NEW: VoxelLocation-based O(1) hover detection system
    VoxelLocation pickVoxelOptimized(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;
    VoxelLocation resolveSubcubeInVoxel(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const VoxelLocation& voxelHit) const;
    
    // Adapter: Convert VoxelLocation to CubeLocation for backward compatibility
    CubeLocation voxelLocationToCubeLocation(const VoxelLocation& voxelLoc) const;
    
    // Ray-AABB intersection utility
    bool rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                         const glm::vec3& aabbMin, const glm::vec3& aabbMax, 
                         float& distance) const;

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
