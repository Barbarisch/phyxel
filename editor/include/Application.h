#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <array>
#include <chrono>
#include "core/Types.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "graphics/RenderCoordinator.h"
#include "graphics/RaycastVisualizer.h"
#include "graphics/Camera.h"
#include "graphics/CameraManager.h"
#include "core/GameplayCameraController.h"
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
#include "core/FaunaSpawner.h"
#include "core/ResidentSpawner.h"
#include "core/ForceSystem.h"
// WorldInitializer now lives in engine/ and is used by EngineRuntime internally
#include "core/ObjectTemplateManager.h"
#include "core/RuntimeEntityStore.h"
#include "core/AudioSystem.h"
#include "core/ChopManager.h"
#include "scripting/ScriptingSystem.h"
#include "ai/AISystem.h"
#include "ai/AIEnhancer.h"
#include "ai/AIConversationService.h"

#ifdef _WIN32
#include "TerminalPanel.h"
#include "PropertiesPanel.h"
#include "CameraPanel.h"
#include "TextureEditorPanel.h"
#endif
#include "WorldOutlinerPanel.h"
#include "MenuEditorPanel.h"
#include "ui/GameMenuRenderer.h"
#include "core/EntityRegistry.h"
#include "core/APICommandQueue.h"
#include "core/CommandRegistry.h"
#include "core/EngineAPIServer.h"
#include "core/JobSystem.h"
#include "core/MainThreadJobs.h"
#include "core/Inventory.h"
#include "core/HealthComponent.h"
#include "core/RespawnSystem.h"
#include "core/MusicPlaylist.h"
#include "core/PlayerProfile.h"
#include "core/ObjectiveTracker.h"
#include "core/GameEventLog.h"
#include "core/TriggerSystem.h"
#include "core/SnapshotManager.h"
#include "core/PlacedObjectManager.h"
#include "core/CombatSystem.h"
#include "core/Party.h"
#include "core/InitiativeTracker.h"
#include "core/CombatDirector.h"
#include "core/CombatAISystem.h"
#include "core/PlayerTurnController.h"
#include "core/WorldClock.h"
#include "core/CampaignJournal.h"
#include "core/NPCManager.h"
#include "core/InteractionManager.h"
#include "core/InteractionProfileManager.h"
#include "core/InteractionHandler.h"
#include "core/interactions/SeatInteractionHandler.h"
#include "core/interactions/DoorInteractionHandler.h"
#include "core/interactions/NPCInteractionHandler.h"
#include "core/interactions/PickupInteractionHandler.h"
#include "core/KinematicVoxelManager.h"
#include "core/KinematicAnimator.h"
#include "core/DoorManager.h"
#include "core/DynamicFurnitureManager.h"
#include "core/CoherentFragmentManager.h"
#include "core/ItemPropManager.h"
#include "core/ItemEffectSystem.h"
#include "core/LocationRegistry.h"
#include "ui/DialogueSystem.h"
#include "ui/SpeechBubbleManager.h"
#include "ui/HudDataContext.h"
#include "story/StoryEngine.h"
#include "story/RuleBasedCharacterAgent.h"
#include "core/EngineConfig.h"
#include "core/EngineRuntime.h"
#include "core/SceneManager.h"
#include "core/GpuParticlePhysics.h"
#include "core/WaterManager.h"
#include "scene/NPCEntity.h"
#include "scene/Entity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/CharacterTurnBody.h"
#include "ProjectLauncher.h"
#include <map>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <chrono>
#include <thread>
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

    /// Left-click during the player's turn (turn-based): resolve the cursor ray
    /// into a move/attack intent (S6). Returns true if it handled the click
    /// (so the normal break/interact LMB behavior is skipped).
    bool tryCombatClick();

    /// BG3-hybrid combat camera (S7): during a turn-based encounter, frame the
    /// ACTIVE combatant (auto-pans on turn change), orbit via RMB + scroll-zoom,
    /// pulled back for tactical framing. Feeds no character movement input.
    void updateCombatCamera(float dt);
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

    // Spell-cast click tool (editor). When spell mode is enabled, left-click casts
    // the selected spell at the hovered voxel (VFX + delayed destruction) instead
    // of breaking it. Driven from InputController's "Break Voxel" mouse action.
    bool isSpellModeEnabled() const { return m_spellModeEnabled; }
    void castSpellAtHover();
    /// Left-click attack vs a settled item prop: ray-test placed item AABBs and
    /// physicalize the nearest with an impulse (static-first explicit revive).
    bool tryHitItemPropAtRay(const glm::vec3& origin, const glm::vec3& dir);

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
    std::unique_ptr<Core::WaterManager> waterManager;                  // CPU water cellular-automaton sim
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
    // Action -> handler map for API commands (incrementally replacing the giant if-chain in
    // processAPICommands). Registered per domain by registerXCommands(); see CommandRegistry.h.
    Core::CommandRegistry m_commandRegistry;
    std::unique_ptr<Core::EngineAPIServer> apiServer;
    std::chrono::steady_clock::time_point m_apiServerStartTime;
    std::unique_ptr<Core::JobSystem> jobSystem;
    // [no-frozen-engine] main-thread work sliced into per-frame units with visible progress
    // (settlement/structure builds); ticked in update() next to processCompletedJobs.
    std::unique_ptr<Core::MainThreadJobs> mainThreadJobs;
    std::thread m_animWarmThread;  // background pre-parse of the default character .anim into the cache
    std::unique_ptr<Core::Inventory> inventory;
    std::unique_ptr<Core::GameEventLog> gameEventLog;
    std::unique_ptr<Core::TriggerSystem> triggerSystem;
    Core::GameSubsystems m_sceneSubsystems; // persistent — SceneManager keeps a pointer to it
    std::unique_ptr<Core::SnapshotManager> snapshotManager;
    std::unique_ptr<Core::PlacedObjectManager> placedObjectManager;

    // Combat
    std::unique_ptr<Core::CombatSystem> combatSystem;

    // Axe tree-chopping — accumulates chop progress per tree (keyed by trunk
    // base) and fires onTreeFelled once. Changes no voxels; the destruction
    // session owns topple/fall + gatherable-log drops (see ChopManager.h).
    Core::ChopManager m_chopManager;

    // D&D RPG Layer (Phase 8)
    Core::Party             m_rpgParty;
    Core::CombatDirector    m_combatDirector;   // single source of truth: mode + initiative + lifecycle
    Core::CombatAISystem    m_combatAI;
    Core::PlayerTurnController m_playerTurn;     // player's turn execution (S5)
    // Persistent TurnActor body adapters (one per character), handed to the
    // combat AI's body provider so enemy turns drive live characters (S4).
    std::unordered_map<Scene::AnimatedVoxelCharacter*,
                       std::unique_ptr<Scene::CharacterTurnBody>> m_turnBodies;

    // Pending player turn intent, set by the HTTP combat/player_* handlers
    // (HTTP thread) and drained on the game thread before m_playerTurn.tick.
    struct PendingPlayerIntent {
        enum class Kind { None, Move, Attack, EndTurn, Select, Cast };
        Kind        kind = Kind::None;
        glm::vec3   point{0.0f};
        std::string targetId;
        std::string spellId;
    };

    // Plays the cast animation + VFX for a spell and invokes onRelease at the
    // release frame (reused by the turn-based cast executor). S-spellcasting.
    void playCastVisual(const std::string& spellId, Scene::AnimatedVoxelCharacter* caster,
                        const glm::vec3& targetPos, std::function<void()> onRelease);
    std::mutex          m_playerIntentMutex;
    PendingPlayerIntent m_pendingPlayerIntent;

    // S7 combat camera state.
    bool  m_combatCamWasActive = false;  // detects the enter-combat transition
    float m_combatCamDistance  = 8.0f;   // default pulled-back tactical distance

    // S6 click picking: resolve a world-space ray (from the cursor or an HTTP
    // test) into a player turn intent — nearest enemy combatant hit = attack
    // (move toward it if out of reach), otherwise the ground point = move.
    PendingPlayerIntent resolveCombatPick(const glm::vec3& origin, const glm::vec3& dir) const;
    void setPendingPlayerIntent(const PendingPlayerIntent& intent);
    Core::WorldClock        m_rpgWorldClock;
    Core::CampaignJournal   m_rpgJournal;

    // NPC System
    std::unique_ptr<Core::NPCManager> npcManager;
    Core::FaunaSpawner m_faunaSpawner;   // biome-driven wildlife population
    bool m_faunaConfigured = false;
    Core::ResidentSpawner m_residentSpawner;   // settlement residents from persisted Locations
    bool m_residentSpawnerConfigured = false;
    float m_farTreeExclusionPoll = 0.0f;   ///< 1s cadence for far-tree structure exclusions
    std::unique_ptr<Core::InteractionManager> interactionManager;
    std::unique_ptr<Core::InteractionProfileManager> interactionProfileManager;
    std::unique_ptr<Core::InteractionHandlerRegistry> interactionHandlerRegistry;

    // Door / Kinematic Voxel System
    std::unique_ptr<Core::KinematicVoxelManager> kinematicVoxelManager;
    std::unique_ptr<Core::KinematicAnimator>     kinematicAnimator;
    std::unique_ptr<Core::DoorManager> doorManager;
    std::unique_ptr<Core::DynamicFurnitureManager> dynamicFurnitureManager;
    // Coherent world-collapse fragments (docs/DestructionSystemV2.md §5.G) — owns + ticks
    // physicalize'd slabs from apply_damage's coherent path. Deps set lazily at first use.
    Core::CoherentFragmentManager coherentFragmentManager;

    // Items: world props + held-in-hand presentation + declarative effects
    std::unique_ptr<Core::ItemPropManager> itemPropManager;
    std::unique_ptr<Core::ItemEffectSystem> itemEffectSystem;
    std::string m_heldItemId;            // item currently shown in the player's hand
    std::string m_heldKinId;             // kinematic group id of the held visual
    int         m_heldAnchorId = -1;     // invisible grip-bone attachment id
    bool        m_heldComboInit = false; // attack combo initialized for the boot-state hand
    void updateHeldItem();               // per-frame: sync held visual with selected hotbar slot
    void dropHeldItem();                 // drop one of the selected item as a world prop

    // Called from the player's melee hit-frame when an axe is equipped: find the
    // tree voxel the swing lands on, accumulate chop progress, spawn wood-chip
    // + sound feedback, and (via ChopManager) fire onTreeFelled when the tree is
    // chopped through. Changes no voxels — see m_chopManager / ChopManager.h.
    // Blade-contact chop: computes the held axe head's world position and bites
    // the wood it touches (kerf carve + feedback). Called EVERY FRAME during a
    // swing (contact decides the moment, not a clip fraction); returns true when
    // a bite registered so the caller latches one-bite-per-swing.
    bool tryAxeChopOnHitFrame(const Core::ItemDefinition* heldDef, float yaw);
    bool m_swingBiteDone = false;   // one bite per swing latch

    // Held weapons for combat NPCs — same template + grip orientation as the
    // player's held item, but driven per-NPC from CombatBehavior::getWeaponId().
    struct NpcHeldItem { std::string itemId; std::string kinId; int anchorId = -1; };
    std::unordered_map<std::string, NpcHeldItem> m_npcHeld;  // npc name -> held visual
    void updateNpcHeldItems();           // per-frame: sync each combat NPC's held weapon visual

    // Story Engine
    std::unique_ptr<Story::StoryEngine> storyEngine;
    // Shared agent driving Guided/Autonomous NPCs (StoryDrivenBehavior). One instance for all.
    std::unique_ptr<Story::RuleBasedCharacterAgent> m_characterAgent;

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

    // Spawn recipes for RUNTIME-spawned entities (spawn_entity), keyed by uuid, so they
    // persist across save/reload with the same stable id. Authored NPCs/player come from
    // game.json and are NOT tracked here. Positions refreshed from the live entity at save.
    std::unordered_map<std::string, Core::RuntimeEntity> m_runtimeEntities;

    // Shared input->character->camera driver for the animated character (same
    // path the standalone games use). The active rig is derived from CameraMode
    // (V toggle) unless gameplayRigOverride_ names one explicitly (set by the
    // Camera panel or the set_camera MCP tool — e.g. "overhead"/"isometric");
    // pressing V clears the override. See docs/CameraControlSystem.md.
    Core::GameplayCameraController cameraCtl_;
    std::string gameplayRigOverride_;

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
    float m_playtimeSeconds = 0.0f;  // unpaused gameplay time ({{playtime}} in menus)

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

    // Item Equipper panel — GUI equip/give/spawn for registered items
    bool showItemEquipper = true;
    char m_itemFilter[64] = {0};
    bool m_itemHoldableOnly = true;
    void renderItemEquipper();

    // Spell Caster panel + state
    bool  showSpellCaster    = true;
    bool  m_spellModeEnabled = false;
    int   m_spellTypeIndex   = 0;
    float m_spellPower       = 500.0f;  // -> DamageSystem blast energy
    float m_spellRadius      = 4.0f;    // -> DamageSystem blast radius
    void renderSpellCaster();
    // Impact-timed destruction: damage is deferred so the wall breaks roughly when
    // the projectile arrives, rather than instantly on click.
    struct PendingSpellHit { float delay; glm::vec3 center; float radius; float energy; };
    std::vector<PendingSpellHit> m_pendingSpellHits;
    void updatePendingSpellHits(float dt);

    // Debug system
    // Debug flags moved to InputController
    
    // New chunk-level frustum culling
    Utils::Frustum cameraFrustum;
    // Render distance configuration - two-tier system.
    //
    // ⚠️ THIS IS THE ONE THAT ACTUALLY GOVERNS IN THE EDITOR. EngineConfig::maxChunkRenderDistance,
    // WorldInitializer::maxChunkRenderDistance and GameSettings::renderDistance all exist and none
    // of them feed this path — Application owns its own copy and pushes it to RenderCoordinator at
    // startup (Application.cpp:349). Changing those three and expecting the editor to follow is a
    // trap; it cost a whole measurement session on 2026-08-01, where a 192-unit far plane clipped
    // every far-terrain tile past 192u and the resulting "far terrain draws almost nothing" reading
    // was misdiagnosed as a regression in far terrain itself. It was the far plane.
    //
    // maxChunkRenderDistance IS the projection far plane
    // (RenderCoordinator: getProjectionMatrix(aspect, 0.1f, maxChunkRenderDistance)), so it must
    // reach past the outermost far tier or that tier is invisible no matter how it is configured.
    // It bounds only what ALREADY-RESIDENT chunks are culled to — residency is
    // ChunkManager::loadDistance — so raising it does NOT add near-field chunk cost.
    // Measured at 4096 on LodTest (Release): far terrain 9 -> 57 tiles drawn, 17k -> 71.6k
    // triangles, terrain to the horizon, 319 FPS.
    float maxChunkRenderDistance = 4096.0f; // Frustum culling distance == projection far plane
    float chunkInclusionDistance = 6144.0f; // Chunk inclusion bound (kept at the 1.5x the setters use)
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
    // Domain command registrations (CommandRegistry-based, replacing if-chain branches one
    // domain at a time). Called once during init.
    void registerWaterCommands();
    void registerSettlementCommands();   // build_settlement: compose a settlement (subdivide+populate+build each)
    void registerWorldForgeCommands();   // worldforge_*: world-scale plan preview/apply/status/map (docs/WorldForge.md)
    void registerDoorCommands();
    void registerLightCommands();
    void registerSnapshotCommands();
    void registerProfilingCommands();
    void registerEffectsCommands();
    void registerEnvAudioCommands();
    void registerCameraCommands();
    void registerStoryCommands();
    bool dispatchAnimationAPICommand(const Phyxel::Core::APICommand& cmd, nlohmann::json& response);
    bool dispatchDebugAPICommand(const Phyxel::Core::APICommand& cmd, nlohmann::json& response);
    bool dispatchItemAPICommand(const Phyxel::Core::APICommand& cmd, nlohmann::json& response);
    void autoLoadGameDefinition();   // Auto-load game.json if present
    void setupGameHud(const nlohmann::json& gameDef);  // Load game.json "hud" into the UISystem + register data bindings (docs/HudSystem.md)
    void applyFarTerrainConfig(const nlohmann::json& gameDef);  // world.renderDistance + world.farTerrain (docs/CameraRelativeRendering.md, FarRepresentationProviders.md)
    Core::GameSubsystems buildGameSubsystems(); // Build subsystems struct for GameDefinitionLoader
    void initializeSceneManager();   // Wire SceneCallbacks and configure SceneManager
    // Keep the SceneManager's GameSubsystems pointers current (refreshed each frame
    // before pumping transitions — systems can be recreated across project loads).
    void refreshSceneSubsystems();
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

    // Rebuild the grounded water render grid FROM CHUNK-RESIDENT SPANS (docs/Water.md §6 step 2,
    // editor path). Chunks are the only source — the renderer draws what the world data holds, so
    // saved water appears at boot with no command, and every writer (generation, ground_sync)
    // renders through one derivation. No-op on baked/streaming worlds (their layer is the
    // hydrology upload) and when no chunks are loaded. Returns the number of wet columns bound.
    long rebuildGroundedWaterFromSpans();

    // Main menu bar
    void renderMainMenuBar();
    void renderStatusBar();
    void renderMaterialHotbar();
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

    void setApiPortOverride(int port) {
        m_apiPortOverride = port;
    }

private:
    void initAssetEditorScene();
    void renderAssetEditorUI();
    void saveAssetTemplate();

    // ============================================================================
    // ANIM EDITOR MODE  (--anim-editor <file>)
    // ============================================================================
    bool m_animEditorMode = false;
    int m_apiPortOverride = -1;                             // --port override (0 = use engine.json default)
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
    float m_animEditorCharPos[3] = {16.0f, 16.0f, 16.0f}; // InputFloat3 buffer for char position UI
    float m_animEditorStartOffset[3] = {0.0f, 0.0f, 0.0f}; // Offset added to dest when testing clips

    void initAnimEditorScene();
    void renderAnimEditorUI();
    void saveAnimModel();
    void renameAnimationInFile(const std::string& oldName, const std::string& newName);

    // ---- Clip Parameter Tuner ----
    struct AnimClipMeta {
        bool  warpEnabled      = false;
        float authoredFallDist = 0.667f;
        float takeoffEnd       = 0.1f;
        float contactFrame     = 0.85f;
        float warpScaleMin     = 0.4f;
        float warpScaleMax     = 2.5f;
        float hitFrameFraction = 0.4f;
        bool  interruptible    = false;
        float interruptAfter   = 0.5f;
        bool  footIKEnabled    = true;   // apply foot IK (terrain planting) during this clip
        float stairStepHeight   = 0.0f;  // world-units to descend per play (0 = disabled)
        float stairStepDepth    = 0.0f;  // world-units of forward travel per play
        float contactFrame1     = 0.0f;  // normalized time (0-1) when first foot contacts step 1
        float contactFrame2     = 0.0f;  // normalized time (0-1) when second foot contacts step 2
        std::string clipType;            // "locomotion"|"jump"|"stair"|"combat"|"transition" (empty = auto)
        // Foot planting IK — how far above a surface the foot can be before IK activates,
        // and how much the pelvis can shift to help the legs reach the IK target.
        // Defaults match 1 microcube (0.111 = 1/9) for subtle correction on flat terrain.
        // Stair clips typically need larger values (e.g. 0.333 / 0.222).
        float footIKSurfaceReach = 0.111f;
        float footIKBodyRange    = 0.111f;
    };
    // Live-edited metadata indexed by clip name; populated on scene init, saved on Ctrl+S
    std::map<std::string, AnimClipMeta> m_animClipMeta;

    // Wizard step-through tuner state
    bool  m_animTunerWizard      = false;  // wizard mode active
    int   m_animTunerStep        = 0;      // which parameter step (0-based)
    bool  m_animTunerDirty       = false;  // unsaved changes exist
    // Drop-test anchors (used in authoredFallDist wizard step)
    float m_tunerLandingY        = 16.0f; // world Y where character should land
    float m_tunerLaunchY         = 17.0f; // world Y from which character is dropped
    // Warp preview test height (used by "Test at Height" in playback controls)
    float m_warpTestHeight       = 1.0f;  // fall distance to simulate
    // One-shot play: auto-pause when animation reaches the end
    bool  m_animEditorPlayOnce   = false;
    float m_animEditorPrevProgress = 0.0f; // previous frame's progress (loop-wrap detection)
    bool  m_stairTestActive      = false;  // true while a Test Step Down preview is running
    glm::vec3 m_stairTestReturnPos{0.0f}; // position to restore when the test completes
    float m_stairTestSpeed       = 0.25f; // playback speed for Test Step Down (1=normal)
    float m_stairTestYOffset     = -1.0f; // Y raise before test (-1 = use stairStepHeight)
    float m_stairTestZOffset     = -1.0f; // forward setback before test (-1 = use stairStepDepth)

    void renderClipParameterTuner();           // renders the Clip Settings panel
    void animTunerReplayCurrentClip();         // replays selected clip for visual feedback
    void loadAnimClipMetaFromFile(const std::string& animFile); // parse # clip_meta: comments
    static std::string autoDetectClipType(const std::string& clipName); // infer type from clip name
    void saveAnimClipMetaToFile(const std::string& animFile,
                                std::vector<std::string>& fileLines); // write # clip_meta: lines

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

    // World Map panel (WorldForge minimap rendered in-engine): texture + the window it
    // covers (for the hover→world-coordinate readout). Refresh-driven, never per frame.
    // Per-frame jitter trace (see the update-loop recorder + the "frame_trace" command).
    struct FrameTraceSample {
        uint64_t frame = 0;
        glm::vec3 charPos{0.0f};
        glm::vec3 partPos{0.0f};   // first active ragdoll part (a render input)
        glm::vec3 camPos{0.0f};
        float camYaw = 0.0f;
        float dtMs = 0.0f;   // frame delta in ms - hitch spikes make orbits snap
    };
    static constexpr size_t kFrameTraceLen = 240;
    std::array<FrameTraceSample, kFrameTraceLen> m_frameTrace{};
    uint64_t m_frameTraceCounter = 0;

    bool m_showWorldMapPanel = false;
    bool m_worldMapTried = false;        // gates auto-render to once; failures need Refresh
    void* m_worldMapTex = nullptr;
    int m_worldMapZoom = 0;              // 0 = region, 1 = 4x on camera, 2 = 16x on camera
    float m_worldMapX0 = 0.0f, m_worldMapZ0 = 0.0f, m_worldMapSize = 1.0f;
    // Fill `img` (px*px RGB) with the biome/water/road/site map for the given world
    // window — the SAME renderer the worldforge_minimap API serves. False + *err when the
    // world has no hydrology bake.
    bool renderWorldMapImage(int px, float cx, float cz, float radius,
                             std::vector<unsigned char>& img, std::string* err);
    void refreshWorldMapTexture();
    void renderWorldMapPanel();

    std::unique_ptr<Editor::TextureEditorPanel> m_textureEditor; // Dockable texture pixel editor
    bool m_showTextureEditor = false;
    bool m_needsLayoutReset = false;

    bool m_showScenePanel = true;          // Dockable scene management panel
    bool showAnimatedCharPanel = false;    // Animated character inspector panel

    std::unique_ptr<Editor::MenuEditorPanel> m_menuEditorPanel; // Menu scene canvas editor
    bool m_showMenuEditor = false;         // Visible when active scene is type "menu"

    std::unique_ptr<UI::GameMenuRenderer> m_gameMenuRenderer; // Runtime game menu renderer
    bool m_showGameMenuPreview = false;    // Show full-screen menu preview in editor

#ifdef _WIN32
    std::unique_ptr<Editor::TerminalPanel> m_terminalPanel; // Dockable terminal emulator
    bool m_showTerminal = true;
#endif
};

} // namespace Phyxel
