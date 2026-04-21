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
#include "ai/AIEnhancer.h"
#include "ai/AIConversationService.h"

#ifdef _WIN32
#include "TerminalPanel.h"
#include "PropertiesPanel.h"
#include "CameraPanel.h"
#endif
#include "WorldOutlinerPanel.h"
#include "core/EntityRegistry.h"
#include "core/APICommandQueue.h"
#include "core/EngineAPIServer.h"
#include "core/JobSystem.h"
#include "core/Inventory.h"
#include "core/HealthComponent.h"
#include "core/RespawnSystem.h"
#include "core/MusicPlaylist.h"
#include "core/PlayerProfile.h"
#include "core/ObjectiveTracker.h"
#include "core/GameEventLog.h"
#include "core/SnapshotManager.h"
#include "core/PlacedObjectManager.h"
#include "core/CombatSystem.h"
#include "core/Party.h"
#include "core/InitiativeTracker.h"
#include "core/CombatAISystem.h"
#include "core/WorldClock.h"
#include "core/CampaignJournal.h"
#include "core/NPCManager.h"
#include "core/InteractionManager.h"
#include "core/InteractionProfileManager.h"
#include "core/InteractionHandler.h"
#include "core/interactions/SeatInteractionHandler.h"
#include "core/interactions/DoorInteractionHandler.h"
#include "core/interactions/NPCInteractionHandler.h"
#include "core/KinematicVoxelManager.h"
#include "core/DoorManager.h"
#include "core/DynamicFurnitureManager.h"
#include "core/LocationRegistry.h"
#include "ui/DialogueSystem.h"
#include "ui/SpeechBubbleManager.h"
#include "story/StoryEngine.h"
#include "core/EngineConfig.h"
#include "core/EngineRuntime.h"
#include "core/SceneManager.h"
#include "core/GpuParticlePhysics.h"
#include "scene/NPCEntity.h"
#include "scene/Entity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "ProjectLauncher.h"
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Phyxel {

/// Editor mode — Edit for development workflow, Play for game testing.
enum class EditorMode {
    Edit,   // Free camera, full panel access, simulation can be paused
    Play    // Game-like behavior, simulation running (future)
};

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
    
    // Pause control
    void togglePause();
    bool isPaused() const { return gamePaused; }
    void setPaused(bool paused);

    // Performance overlay methods
    void togglePerformanceOverlay();
    void toggleScriptingConsole();
    void toggleDebugRendering();
    void cycleDebugVisualizationMode();
    void toggleRaycastVisualization();
    void cycleRaycastTargetMode(int direction = 1);
    bool isViewportHovered() const { return m_viewportHovered; }
    bool isViewportFocused() const { return m_viewportFocused; }
    void adjustAmbientLight(float delta);
    void toggleLightingControls();
    void toggleProfiler();
    void toggleCharacterCustomizer();
    void toggleInteractionTuner();
    void toggleCameraMode();
    void toggleCharacterControl();
    void cycleCameraSlot();
    void cycleCameraSlotReverse();

    // Character Management
    Scene::AnimatedVoxelCharacter* createAnimatedCharacter(const glm::vec3& pos, const std::string& animFile);
    void setControlTarget(const std::string& targetName);
    void derezCharacter(float duration = 2.0f);
    void renderAnimatedCharPanel();

    // AI NPC Management
    void spawnTestAINPC();
    void toggleAISystem();
    void interactWithNPC();

    // Custom UI menu management
    void toggleGameMenu(const std::string& name);
    Graphics::RenderCoordinator* getRenderCoordinator() const { return renderCoordinator.get(); }

    // Accessors
    UI::WindowManager* getWindowManager() const { return windowManager; }
    ObjectTemplateManager* getObjectTemplateManager() const { return objectTemplateManager.get(); }
    Core::PlacedObjectManager* getPlacedObjectManager() const { return placedObjectManager.get(); }
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
    Core::DynamicFurnitureManager* getDynamicFurnitureManager() const { return dynamicFurnitureManager.get(); }
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
    Core::LocationRegistry* locationRegistry = nullptr;

    // Game-specific subsystems (still owned by Application)
    std::unique_ptr<GpuParticlePhysics> gpuParticlePhysics;            // GPU-accelerated debris physics
    std::unique_ptr<Graphics::RenderCoordinator> renderCoordinator;    // Coordinates all rendering
    std::unique_ptr<RaycastVisualizer> raycastVisualizer;              // Raycast debug visualization
    std::unique_ptr<VoxelInteractionSystem> voxelInteractionSystem;    // Cube/subcube interaction
    std::unique_ptr<ObjectTemplateManager> objectTemplateManager;      // Voxel object templates
    std::unique_ptr<InputController> inputController;                  // Input bindings and control

    // Scripting System
    std::unique_ptr<ScriptingSystem> scriptingSystem;

    // AI System
    std::unique_ptr<AI::AISystem> aiSystem;
    std::unique_ptr<AI::AIEnhancer> aiEnhancer;
    std::unique_ptr<AI::AIConversationService> aiConversationService;

    // Entity Registry & HTTP API
    std::unique_ptr<Core::EntityRegistry> entityRegistry;
    std::unique_ptr<Core::APICommandQueue> apiCommandQueue;
    std::unique_ptr<Core::EngineAPIServer> apiServer;
    std::unique_ptr<Core::JobSystem> jobSystem;
    std::unique_ptr<Core::Inventory> inventory;
    std::unique_ptr<Core::GameEventLog> gameEventLog;
    std::unique_ptr<Core::SnapshotManager> snapshotManager;
    std::unique_ptr<Core::PlacedObjectManager> placedObjectManager;

    // Combat
    std::unique_ptr<Core::CombatSystem> combatSystem;

    // D&D RPG Layer (Phase 8)
    Core::Party             m_rpgParty;
    Core::InitiativeTracker m_rpgInitiative;
    Core::CombatAISystem    m_combatAI;
    Core::WorldClock        m_rpgWorldClock;
    Core::CampaignJournal   m_rpgJournal;

    // NPC System
    std::unique_ptr<Core::NPCManager> npcManager;
    std::unique_ptr<Core::InteractionManager> interactionManager;
    std::unique_ptr<Core::InteractionProfileManager> interactionProfileManager;
    std::unique_ptr<Core::InteractionHandlerRegistry> interactionHandlerRegistry;

    // Door / Kinematic Voxel System
    std::unique_ptr<Core::KinematicVoxelManager> kinematicVoxelManager;
    std::unique_ptr<Core::DoorManager> doorManager;
    std::unique_ptr<Core::DynamicFurnitureManager> dynamicFurnitureManager;

    // Story Engine
    std::unique_ptr<Story::StoryEngine> storyEngine;

    // Dialogue System
    std::unique_ptr<UI::DialogueSystem> dialogueSystem;
    std::unique_ptr<UI::SpeechBubbleManager> speechBubbleManager;
    std::unique_ptr<UI::DialogueTree> m_apiDialogueTree; // Keeps API-started dialogue trees alive

    // Entities — the unique_ptrs own the objects; the raw pointers below are
    // non-owning observers. If you add a new raw pointer here, you MUST also
    // null it in resetEditorScene() BEFORE entities.clear(), otherwise the
    // main loop will dereference a dangling pointer after a File > Open switch.
    std::vector<std::unique_ptr<Scene::Entity>> entities;
    Scene::AnimatedVoxelCharacter* animatedCharacter = nullptr;

    // Player health & respawn
    Core::HealthComponent playerHealth{100.0f};
    Core::RespawnSystem respawnSystem;
    Core::MusicPlaylist musicPlaylist;
    Core::PlayerProfile playerProfile;
    Core::ObjectiveTracker objectiveTracker;
    
    enum class ControlTarget {
        AnimatedCharacter
    };
    ControlTarget currentControlTarget = ControlTarget::AnimatedCharacter;

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

    // Game state
    EditorMode m_editorMode = EditorMode::Edit;
    bool gamePaused = false;

    // Performance overlay
    bool showPerformanceOverlay = false;
    bool showScriptingConsole = false;
    bool showCharacterCustomizer = false;
    std::string customizerSelectedNPC;
    void renderCharacterCustomizer();

    bool showInteractionTuner = false;
    std::string tunerSelectedTemplate;
    void renderInteractionTuner();

    // Template Spawner panel
    bool showTemplateSpawner = false;
    float spawnerPos[3] = {0.0f, 20.0f, 0.0f};
    int spawnerRotation = 0;
    int spawnerTemplateIdx = 0;
    void renderTemplateSpawner();

    // Click Actions panel
    bool showClickActions = false;
    void renderClickActions();

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
    Core::GameSubsystems buildGameSubsystems(); // Build subsystems struct for GameDefinitionLoader
    void initializeSceneManager();   // Wire SceneCallbacks and configure SceneManager
    void renderScenePanel();         // Dockable ImGui panel for scene management

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

    // Project launcher
    std::unique_ptr<ProjectLauncher> projectLauncher_;
    bool launcherActive_ = false;
    void onLauncherResult(const LauncherResult& result);
    void applyProjectSelection(const std::string& projectPath);

    // Main menu bar
    void renderMainMenuBar();
    void renderStatusBar();
    void openFileDialog();             // Show native file open dialog
    void openProjectDialog();          // Show native folder picker to open a project
    void switchToEditorMode(const std::string& filePath); // Detect type & switch mode
    void resetEditorScene();           // Clean up current editor scene state
    std::string m_pendingOpenFile;     // Deferred file open (processed at frame start, not mid-render)
    std::string m_pendingOpenProject;  // Deferred project open (processed at frame start)
    bool m_projectCtrlSPrev = false;   // Edge-detect Ctrl+S for project-mode world save

    // New creation dialogs
    bool m_showNewProjectPopup = false;        // Show the "New Project" name popup
    char m_newProjectNameBuf[256] = {};         // Name buffer for new project popup
    std::string m_newProjectError;              // Validation error message
    std::string m_pendingNewObject;             // Deferred: path for new .voxel to create
    std::string m_pendingNewAnim;               // Deferred: path for new .anim to create
    void newObjectDialog();                     // Show native save dialog for new .voxel
    void newAnimDialog();                       // Show native save dialog for new .anim

    // ============================================================================
    // ASSET EDITOR MODE  (--asset-editor <file>)
    // ============================================================================
    bool m_assetEditorMode = false;
    std::string m_assetEditorFile;                          // Full path to the .voxel template being edited
    glm::ivec3 m_assetTemplateOrigin{13, 16, 13};          // World position where the template is placed
    std::string m_assetEditorMaterial{"Wood"};              // Currently selected placement material
    bool m_assetRefCharVisible = false;                     // Whether the humanoid reference char is spawned
    Scene::AnimatedVoxelCharacter* m_assetRefChar = nullptr;// Non-owning pointer into entities list
    bool m_assetEditorHPrev = false;                        // Edge-detect H key
    bool m_assetEditorCtrlSPrev = false;                    // Edge-detect Ctrl+S

public:
    void setAssetEditorFile(const std::string& path) {
        m_assetEditorMode = true;
        m_assetEditorFile = path;
    }

    void setAnimEditorFile(const std::string& path) {
        m_animEditorMode = true;
        m_animEditorFile = path;
    }

    void setInteractionEditorFile(const std::string& assetPath, const std::string& charPath = "") {
        m_interactionEditorMode = true;
        m_interactionEditorFile = assetPath;
        m_interactionEditorCharFile = charPath;
    }

private:
    void initAssetEditorScene();
    void renderAssetEditorUI();
    void saveAssetTemplate();

    // ============================================================================
    // ANIM EDITOR MODE  (--anim-editor <file>)
    // ============================================================================
    bool m_animEditorMode = false;
    std::string m_animEditorFile;                           // Full path to the .anim file being edited
    Scene::AnimatedVoxelCharacter* m_animEditorChar = nullptr; // Non-owning pointer into entities list
    int m_animEditorSelectedBone = -1;                     // Currently selected bone index in MODEL list
    // Per-bone scale overrides (boneId -> scale factor, default 1.0)
    std::map<int, float> m_animEditorBoneScale;
    // Body bones to show in the editor (filtered from full skeleton)
    std::vector<std::pair<int,std::string>> m_animEditorBodyBones; // {boneId, boneName}
    int m_animEditorAnimIdx = 0;                           // Currently previewing animation index
    bool m_animEditorCtrlSPrev = false;                    // Edge-detect Ctrl+S
    int m_animEditorRenamingIdx = -1;                      // Index of clip being renamed (-1 = none)
    char m_animEditorRenameBuffer[128] = {};               // ImGui InputText buffer for rename

    void initAnimEditorScene();
    void renderAnimEditorUI();
    void saveAnimModel();
    void renameAnimationInFile(const std::string& oldName, const std::string& newName);

    // ============================================================================
    // INTERACTION EDITOR MODE  (--interaction-editor <file> [--character <file>])
    // ============================================================================
    enum class InteractionPreviewState {
        None,           // Character standing idle
        SittingDown,    // sitAt() called, playing sit-down anim
        SittingIdle,    // Seated idle loop (manual or auto-wait)
        StandingUp,     // standUp() called, playing stand-up anim
        AutoIdle        // Auto-preview: waiting in seated idle before auto stand-up
    };

    bool m_interactionEditorMode = false;
    std::string m_interactionEditorFile;                   // Full path to the .voxel template
    std::string m_interactionEditorCharFile;               // Optional .anim path (default: humanoid.anim)
    glm::ivec3 m_ieAssetOrigin{13, 16, 13};               // Where the asset is placed
    Scene::AnimatedVoxelCharacter* m_ieChar = nullptr;     // Non-owning pointer into entities list
    int m_ieSelectedPoint = 0;                             // Currently selected interaction point index
    InteractionPreviewState m_iePreviewState = InteractionPreviewState::None;
    bool m_ieAutoPreview = false;                          // True when auto-preview (sit→wait→stand)
    float m_ieAutoTimer = 0.0f;                            // Timer for auto-preview idle wait
    bool m_ieCtrlSPrev = false;                            // Edge-detect Ctrl+S
    glm::vec3 m_ieCharRestPos{17.0f, 16.0f, 13.0f};       // Stand position for character when not previewing

    // Material palette (same as asset editor)
    std::string m_ieMaterial{"Wood"};                      // Currently selected placement material

    // Interaction point editing
    char m_ieNewPointId[64] = {};                          // Buffer for new point ID input
    char m_ieNewGroupBuf[64] = {};                         // Buffer for adding new supported group

    // Character modification (bone scales + archetype)
    std::vector<std::pair<int,std::string>> m_ieBodyBones; // {boneId, boneName} for bone scale sliders
    std::map<int, float> m_ieBoneScale;                    // Per-bone scale overrides
    int m_ieSelectedBone = -1;                             // Currently selected bone index
    char m_ieArchetypeBuf[64] = {};                        // ImGui InputText buffer for archetype rename
    std::string m_ieProfileArchetype;                      // Currently loaded profile archetype (may differ from character's)

    void initInteractionEditorScene();
    void renderInteractionEditorUI();
    void ieStartPreview(bool autoPlay);
    void ieStopPreview();
    void ieSaveAnimModel();                                // Save modified bone scales to .anim file
    void ieSaveAssetTemplate();                            // Save full .voxel (voxels + interaction defs)

    // ============================================================================
    // DOCKING / VIEWPORT  (editor DockSpace infrastructure)
    // ============================================================================
    VkDescriptorSet m_viewportTextureId = VK_NULL_HANDLE;  // ImGui texture for 3D viewport
    VkImageView m_viewportLastImageView = VK_NULL_HANDLE;  // Track for reregistration on resize
    bool m_dockLayoutInitialized = false;                  // Whether default layout has been set up
    bool m_viewportHovered = false;                        // Whether mouse is over the Viewport window
    bool m_viewportFocused = false;                        // Whether the Viewport window has input focus
    float m_viewportPosX = 0.0f, m_viewportPosY = 0.0f;   // Viewport content top-left in window coords
    float m_viewportSizeW = 1.0f, m_viewportSizeH = 1.0f; // Viewport content size in pixels
    void renderDockableViewport();                         // Render the 3D viewport as an ImGui window
    void setupDefaultDockLayout(unsigned int dockSpaceId); // Set up initial dock layout

    std::unique_ptr<Editor::PropertiesPanel> m_propertiesPanel; // Dockable properties inspector
    bool m_showProperties = true;

    std::unique_ptr<Editor::WorldOutlinerPanel> m_worldOutliner; // Dockable world outliner
    bool m_showWorldOutliner = true;

    std::unique_ptr<Editor::CameraPanel> m_cameraPanel;        // Dockable camera management
    bool m_showCameraPanel = false;
    bool m_needsLayoutReset = false;

    bool m_showScenePanel = true;          // Dockable scene management panel
    bool showAnimatedCharPanel = false;    // Animated character inspector panel

#ifdef _WIN32
    std::unique_ptr<Editor::TerminalPanel> m_terminalPanel; // Dockable terminal emulator
    bool m_showTerminal = true;
#endif
};

} // namespace Phyxel
