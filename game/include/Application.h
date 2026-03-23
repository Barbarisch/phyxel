#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "core/Types.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "graphics/RenderCoordinator.h"
#include "graphics/RaycastVisualizer.h"
#include "graphics/Camera.h"
#include "graphics/CameraManager.h"
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
// WorldInitializer now lives in engine/ and is used by EngineRuntime internally
#include "core/ObjectTemplateManager.h"
#include "core/AudioSystem.h"
#include "scripting/ScriptingSystem.h"
#include "ai/AISystem.h"
#include "core/EntityRegistry.h"
#include "core/APICommandQueue.h"
#include "core/EngineAPIServer.h"
#include "core/JobSystem.h"
#include "core/Inventory.h"
#include "core/GameEventLog.h"
#include "core/SnapshotManager.h"
#include "core/NPCManager.h"
#include "core/InteractionManager.h"
#include "ui/DialogueSystem.h"
#include "ui/SpeechBubbleManager.h"
#include "story/StoryEngine.h"
#include "core/EngineConfig.h"
#include "core/EngineRuntime.h"
#include "scene/NPCEntity.h"
#include "scene/Entity.h"
#include "scene/Player.h"
#include "scene/Enemy.h"
#include "scene/PhysicsCharacter.h"
#include "scene/SpiderCharacter.h"
#include "scene/AnimatedVoxelCharacter.h"
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Phyxel {

class Application {
public:
    Application();
    ~Application();

    // Application lifecycle
    bool initialize(const std::string& gameDefinitionPath = "");
    void run();
    void cleanup();
    void quit() { isRunning = false; }

    // Configuration
    void setWindowSize(int width, int height);
    void setTitle(const std::string& title);
    void setProjectDir(const std::string& dir) { projectDir_ = dir; }
    
    // Performance overlay methods
    void togglePerformanceOverlay();
    void toggleScriptingConsole();
    void toggleDebugRendering();
    void cycleDebugVisualizationMode();
    void toggleRaycastVisualization();
    void cycleRaycastTargetMode();
    void adjustAmbientLight(float delta);
    void toggleLightingControls();
    void toggleProfiler();
    void toggleCameraMode();
    void toggleCharacterControl();
    void cycleCameraSlot();
    void cycleCameraSlotReverse();

    // Character Management
    Scene::PhysicsCharacter* createPhysicsCharacter(const glm::vec3& pos);
    Scene::SpiderCharacter* createSpiderCharacter(const glm::vec3& pos);
    Scene::AnimatedVoxelCharacter* createAnimatedCharacter(const glm::vec3& pos, const std::string& animFile);
    void setControlTarget(const std::string& targetName);
    void derezCharacter(float explosionStrength = 1.0f);

    // AI NPC Management
    void spawnTestAINPC();
    void toggleAISystem();
    void interactWithNPC();

    // Accessors
    ObjectTemplateManager* getObjectTemplateManager() const { return objectTemplateManager.get(); }
    RaycastVisualizer* getRaycastVisualizer() const { return raycastVisualizer.get(); }
    VoxelInteractionSystem* getVoxelInteractionSystem() const { return voxelInteractionSystem.get(); }
    ChunkManager* getChunkManager() const { return chunkManager; }
    Input::InputManager* getInputManager() const { return inputManager; }
    ScriptingSystem* getScriptingSystem() const { return scriptingSystem.get(); }
    Core::AudioSystem* getAudioSystem() const { return audioSystem; }
    AI::AISystem* getAISystem() const { return aiSystem.get(); }
    Core::EntityRegistry* getEntityRegistry() const { return entityRegistry.get(); }
    Core::EngineAPIServer* getAPIServer() const { return apiServer.get(); }
    Graphics::CameraManager* getCameraManager() const { return cameraManager; }
    Core::NPCManager* getNPCManager() const { return npcManager.get(); }
    Core::InteractionManager* getInteractionManager() const { return interactionManager.get(); }
    UI::DialogueSystem* getDialogueSystem() const { return dialogueSystem.get(); }
    UI::SpeechBubbleManager* getSpeechBubbleManager() const { return speechBubbleManager.get(); }

private:
    // ============================================================================
    // ENGINE RUNTIME (owns all core subsystems)
    // ============================================================================
    std::unique_ptr<Core::EngineRuntime> runtime;

    // Engine configuration (loaded from engine.json or defaults)
    Core::EngineConfig engineConfig;

    // Project directory (set via --project flag for dev workflow)
    std::string projectDir_;

    // Convenience aliases — non-owning pointers into EngineRuntime's subsystems.
    // These are set in initialize() after runtime->initialize() succeeds.
    // Using raw pointers keeps existing code unchanged (same -> syntax).
    UI::WindowManager* windowManager = nullptr;
    Vulkan::VulkanDevice* vulkanDevice = nullptr;
    Vulkan::RenderPipeline* renderPipeline = nullptr;
    Vulkan::RenderPipeline* dynamicRenderPipeline = nullptr;
    UI::ImGuiRenderer* imguiRenderer = nullptr;
    Graphics::Camera* camera = nullptr;
    Graphics::CameraManager* cameraManager = nullptr;
    ChunkManager* chunkManager = nullptr;
    Physics::PhysicsWorld* physicsWorld = nullptr;
    ForceSystem* forceSystem = nullptr;
    Input::InputManager* inputManager = nullptr;
    MouseVelocityTracker* mouseVelocityTracker = nullptr;
    Timer* timer = nullptr;
    PerformanceProfiler* performanceProfiler = nullptr;
    Utils::PerformanceMonitor* performanceMonitor = nullptr;
    Core::AudioSystem* audioSystem = nullptr;

    // Game-specific subsystems (still owned by Application)
    std::unique_ptr<Graphics::RenderCoordinator> renderCoordinator;    // Coordinates all rendering
    std::unique_ptr<RaycastVisualizer> raycastVisualizer;              // Raycast debug visualization
    std::unique_ptr<VoxelInteractionSystem> voxelInteractionSystem;    // Cube/subcube interaction
    std::unique_ptr<ObjectTemplateManager> objectTemplateManager;      // Voxel object templates
    std::unique_ptr<InputController> inputController;                  // Input bindings and control

    // Scripting System
    std::unique_ptr<ScriptingSystem> scriptingSystem;

    // AI System
    std::unique_ptr<AI::AISystem> aiSystem;

    // Entity Registry & HTTP API
    std::unique_ptr<Core::EntityRegistry> entityRegistry;
    std::unique_ptr<Core::APICommandQueue> apiCommandQueue;
    std::unique_ptr<Core::EngineAPIServer> apiServer;
    std::unique_ptr<Core::JobSystem> jobSystem;
    std::unique_ptr<Core::Inventory> inventory;
    std::unique_ptr<Core::GameEventLog> gameEventLog;
    std::unique_ptr<Core::SnapshotManager> snapshotManager;

    // NPC System
    std::unique_ptr<Core::NPCManager> npcManager;
    std::unique_ptr<Core::InteractionManager> interactionManager;

    // Story Engine
    std::unique_ptr<Story::StoryEngine> storyEngine;

    // Dialogue System
    std::unique_ptr<UI::DialogueSystem> dialogueSystem;
    std::unique_ptr<UI::SpeechBubbleManager> speechBubbleManager;
    std::unique_ptr<UI::DialogueTree> m_apiDialogueTree; // Keeps API-started dialogue trees alive

    // Entities
    std::vector<std::unique_ptr<Scene::Entity>> entities;
    Scene::Player* player = nullptr;
    Scene::PhysicsCharacter* physicsCharacter = nullptr;
    Scene::SpiderCharacter* spiderCharacter = nullptr;
    Scene::AnimatedVoxelCharacter* animatedCharacter = nullptr;
    
    enum class ControlTarget {
        Spider,
        PhysicsCharacter,
        AnimatedCharacter
    };
    ControlTarget currentControlTarget = ControlTarget::PhysicsCharacter;
    
    bool isControllingPhysicsCharacter = false;

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
    
    // Camera state for velocity calculation
    glm::vec3 lastCameraPos = glm::vec3(0.0f);

    // Frame profiling timing
    std::chrono::high_resolution_clock::time_point frameStartTime;
    std::chrono::high_resolution_clock::time_point cpuStartTime;
    double lastFrameTime;
    double fpsTimer;

    // Performance overlay
    bool showPerformanceOverlay = false;
    bool showScriptingConsole = false;
    
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
    void processAPICommands();       // Process pending HTTP API commands
    void autoLoadGameDefinition();   // Auto-load game.json if present

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

} // namespace Phyxel
