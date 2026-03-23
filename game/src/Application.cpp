#include "Application.h"
#include "scene/VoxelInteractionSystem.h"
#include "scene/PhysicsCharacter.h"
#include "scene/SpiderCharacter.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "scene/NPCEntity.h"
#include "scene/behaviors/IdleBehavior.h"
#include "scene/behaviors/PatrolBehavior.h"
#include "core/EntityRegistry.h"
#include "core/APICommandQueue.h"
#include "core/EngineAPIServer.h"
#include "utils/FileUtils.h"
#include "utils/Math.h"
#include "utils/PerformanceProfiler.h"
#include "utils/PerformanceMonitor.h"
#include "utils/Frustum.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include "examples/MultiChunkDemo.h"
#include "ai/AISystem.h"
#include "core/Chunk.h"
#include "core/WorldGenerator.h"
#include "core/VoxelTemplate.h"
#include "physics/Material.h"
#include "story/StoryWorldLoader.h"
#include "story/StoryDirectorTypes.h"
#include "core/EngineConfig.h"
#include "core/AssetManager.h"
#include "core/GameDefinitionLoader.h"
#include <imgui.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdio>
#include <limits>
#include <random>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>

// Screenshot support
#include "stb_image_write.h"
#ifdef _WIN32
#include <direct.h>  // _mkdir
#else
#include <sys/stat.h> // mkdir
#endif

namespace Phyxel {

/**
 * Application Constructor
 * 
 * INITIALIZATION ORDER:
 * 1. Core engine systems created by EngineRuntime in initialize()
 * 2. Game-specific systems created in initialize() after EngineRuntime
 */
Application::Application() 
    : isRunning(false)
    , deltaTime(0.0f)
    , frameCount(0) {
    
    // Initialize profiling timers
    lastFrameTime = 0.0;
    fpsTimer = 0.0;
}

Application::~Application() {
    cleanup();
}

/**
 * Initialize all application subsystems
 * 
 * Core engine initialization is delegated to EngineRuntime.
 * Application handles game-specific setup afterwards.
 */
bool Application::initialize(const std::string& gameDefinitionPath) {
    // STEP 0: LOAD ENGINE CONFIGURATION
    if (!Core::EngineConfig::loadFromFile("engine.json", engineConfig)) {
        LOG_WARN("Application", "Failed to parse engine.json — using defaults");
        engineConfig = Core::EngineConfig{};
    }

    // CLI override for game definition path
    if (!gameDefinitionPath.empty()) {
        engineConfig.gameDefinitionFile = gameDefinitionPath;
    }

    // --project: override worldsDir and window title from the project
    if (!projectDir_.empty()) {
        namespace fs = std::filesystem;
        fs::path projPath(projectDir_);

        // Store project dir in the config so the API server can find it
        engineConfig.projectDir = projectDir_;

        // Use project's worlds directory so DB reads/writes go to the project
        engineConfig.worldsDir = (projPath / "worlds").string();

        // Load project's engine.json for window title etc.
        auto projConfig = projPath / "engine.json";
        if (fs::exists(projConfig)) {
            Core::EngineConfig projCfg;
            Core::EngineConfig::loadFromFile(projConfig.string(), projCfg);
            if (engineConfig.windowTitle == "Phyxel") {
                engineConfig.windowTitle = projCfg.windowTitle;
            }
        }

        LOG_INFO("Application", "Project mode: worlds={}, game={}", engineConfig.worldsDir, engineConfig.gameDefinitionFile);
    }

    // STEP 1: CREATE AND INITIALIZE ENGINE RUNTIME
    // EngineRuntime creates all core subsystems: window, Vulkan, physics,
    // chunks, audio, input, timing, profiling, camera.
    runtime = std::make_unique<Core::EngineRuntime>();
    if (!runtime->initialize(engineConfig)) {
        LOG_ERROR("Application", "EngineRuntime initialization failed!");
        return false;
    }

    // Cache convenience pointers (non-owning aliases into EngineRuntime)
    windowManager       = runtime->getWindowManager();
    vulkanDevice        = runtime->getVulkanDevice();
    renderPipeline      = runtime->getRenderPipeline();
    dynamicRenderPipeline = runtime->getDynamicRenderPipeline();
    imguiRenderer       = runtime->getImGuiRenderer();
    camera              = runtime->getCamera();
    cameraManager       = runtime->getCameraManager();
    chunkManager        = runtime->getChunkManager();
    physicsWorld        = runtime->getPhysicsWorld();
    forceSystem         = runtime->getForceSystem();
    inputManager        = runtime->getInputManager();
    mouseVelocityTracker = runtime->getMouseVelocityTracker();
    timer               = runtime->getTimer();
    performanceProfiler = runtime->getPerformanceProfiler();
    performanceMonitor  = runtime->getPerformanceMonitor();
    audioSystem         = runtime->getAudioSystem();

    auto& assets = Core::AssetManager::instance();

    // Initialize last camera position to avoid velocity spike
    lastCameraPos = inputManager->getCameraPosition();

    // STEP 4: CREATE VoxelInteractionSystem (DEPENDS ON INITIALIZED WORLD)
    // This system handles mouse picking and voxel manipulation (breaking, subdividing)
    // Must be created AFTER WorldInitializer because it needs:
    // - Initialized ChunkManager with chunks
    // - Configured PhysicsWorld
    // - Ready WindowManager for screen-to-world ray conversion
    voxelInteractionSystem = std::make_unique<VoxelInteractionSystem>(
        chunkManager,
        physicsWorld,
        mouseVelocityTracker,
        windowManager,
        forceSystem,
        audioSystem
    );
    LOG_INFO("Application", "VoxelInteractionSystem initialized successfully!");

    // STEP 4.5: CREATE ObjectTemplateManager
    objectTemplateManager = std::make_unique<ObjectTemplateManager>(
        chunkManager,
        &chunkManager->m_dynamicObjectManager
    );
    objectTemplateManager->loadTemplates(assets.templatesDir());
    LOG_INFO("Application", "ObjectTemplateManager initialized successfully!");

    // STEP 5: CREATE RaycastVisualizer (DEBUG VISUALIZATION)
    // This system visualizes raycast operations for debugging
    raycastVisualizer = std::make_unique<RaycastVisualizer>();
    raycastVisualizer->initialize(vulkanDevice);
    LOG_INFO("Application", "RaycastVisualizer initialized successfully!");

    // STEP 5.5: INITIALIZE ENTITIES
    // Configure camera position (camera already created by EngineRuntime)
    camera->setPosition(glm::vec3(45.0f, 55.0f, 45.0f));
    camera->setYaw(-135.0f);
    camera->setPitch(-30.0f);
    camera->setMode(Graphics::CameraMode::Free);
    
    // Wire entity position lookup to CameraManager
    if (entityRegistry) {
        cameraManager->setEntityPositionLookup([this](const std::string& entityId) -> std::optional<glm::vec3> {
            auto* entity = entityRegistry->getEntity(entityId);
            if (entity) return entity->getPosition();
            return std::nullopt;
        });
    }
    // Create default camera slot from initial position
    cameraManager->createSlot("default", glm::vec3(45.0f, 55.0f, 45.0f), -135.0f, -30.0f, Graphics::CameraMode::Free);
    
    // Sync InputManager with initial camera state
    inputManager->setCameraPosition(camera->getPosition());
    inputManager->setYawPitch(camera->getYaw(), camera->getPitch());

    // Entities are now created via scripting (scripts/startup.py)
    // See Application::createPhysicsCharacter etc.
    
    LOG_INFO("Application", "Entities initialized successfully!");

    // Create ScriptingSystem (initialized later, but created here for dependency injection)
    scriptingSystem = std::make_unique<ScriptingSystem>(this);

    // STEP 6: CREATE RenderCoordinator (DEPENDS ON INITIALIZED PIPELINES)
    // This system orchestrates frame rendering:
    // - Frustum culling for visible chunks
    // - Instance buffer updates
    // - Draw call submission
    // - ImGui overlay rendering
    renderCoordinator = std::make_unique<Graphics::RenderCoordinator>(
        vulkanDevice,
        renderPipeline,
        dynamicRenderPipeline,
        imguiRenderer,
        windowManager,
        inputManager,
        camera,
        chunkManager,
        performanceMonitor,
        performanceProfiler,
        raycastVisualizer.get(),
        scriptingSystem.get()
    );
    renderCoordinator->setMaxChunkRenderDistance(maxChunkRenderDistance);
    renderCoordinator->setChunkInclusionDistance(chunkInclusionDistance);
    renderCoordinator->setEntities(&entities);
    LOG_INFO("Application", "RenderCoordinator initialized successfully!");

    // STEP 7: REGISTER INPUT ACTIONS
    // Create InputController to handle input bindings
    inputController = std::make_unique<InputController>(
        inputManager,
        voxelInteractionSystem.get(),
        this
    );
    inputController->initializeBindings();

    // STEP 8: INITIALIZE SCRIPTING SYSTEM
    // scriptingSystem created earlier for dependency injection
    scriptingSystem->init();

    // STEP 9: INITIALIZE AI SYSTEM
    // Create and initialize the AI system (goose-server sidecar, NPC management, story director)
    aiSystem = std::make_unique<AI::AISystem>();
    {
        AI::GooseConfig aiConfig;
        // Default config: localhost:3000, no TLS
        // autoStart = false: don't start goose-server automatically
        // User can start it manually or toggle with F9
        if (!aiSystem->initialize(aiConfig, /*autoStart=*/false)) {
            LOG_WARN("Application", "AI system initialization failed (non-critical)");
        } else {
            LOG_INFO("Application", "AI system initialized (server not auto-started)");
        }
    }

    // STEP 10: INITIALIZE ENTITY REGISTRY & HTTP API SERVER
    entityRegistry = std::make_unique<Core::EntityRegistry>();
    apiCommandQueue = std::make_unique<Core::APICommandQueue>();
    gameEventLog = std::make_unique<Core::GameEventLog>(1000);
    jobSystem = std::make_unique<Core::JobSystem>();
    apiServer = std::make_unique<Core::EngineAPIServer>(apiCommandQueue.get(), engineConfig.apiPort, jobSystem.get());

    // Wire up read-only handlers (called directly on HTTP thread — must be thread-safe)
    apiServer->setEntityListHandler([this]() -> nlohmann::json {
        return entityRegistry->toJson();
    });

    apiServer->setEntityDetailHandler([this](const std::string& id) -> nlohmann::json {
        return entityRegistry->entityToJson(id);
    });

    apiServer->setCameraHandler([this]() -> nlohmann::json {
        nlohmann::json cam;
        if (inputManager) {
            auto pos = inputManager->getCameraPosition();
            cam["position"] = {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}};
            cam["yaw"] = inputManager->getYaw();
            cam["pitch"] = inputManager->getPitch();
        }
        if (camera) {
            auto front = camera->getFront();
            cam["front"] = {{"x", front.x}, {"y", front.y}, {"z", front.z}};
        }
        if (cameraManager) {
            cam["activeSlot"] = cameraManager->getActiveSlotName();
            nlohmann::json slotsArr = nlohmann::json::array();
            for (const auto& slot : cameraManager->getSlots()) {
                slotsArr.push_back({
                    {"name", slot.name},
                    {"position", {{"x", slot.position.x}, {"y", slot.position.y}, {"z", slot.position.z}}},
                    {"yaw", slot.yaw},
                    {"pitch", slot.pitch},
                    {"followEntityId", slot.followEntityId}
                });
            }
            cam["slots"] = slotsArr;
        }
        return cam;
    });

    apiServer->setVoxelQueryHandler([this](int x, int y, int z) -> nlohmann::json {
        nlohmann::json result;
        result["position"] = {{"x", x}, {"y", y}, {"z", z}};
        if (chunkManager) {
            auto* cube = chunkManager->getCubeAt(glm::ivec3(x, y, z));
            result["exists"] = (cube != nullptr);
        } else {
            result["exists"] = false;
        }
        return result;
    });

    apiServer->setWorldStateHandler([this]() -> nlohmann::json {
        nlohmann::json state;
        state["entities"] = entityRegistry->toJson();
        if (inputManager) {
            auto pos = inputManager->getCameraPosition();
            state["camera"] = {
                {"position", {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}}},
                {"yaw", inputManager->getYaw()},
                {"pitch", inputManager->getPitch()}
            };
        }
        state["entity_count"] = entityRegistry->size();
        return state;
    });

    apiServer->setMaterialListHandler([]() -> nlohmann::json {
        static Physics::MaterialManager matMgr;
        auto names = matMgr.getAllMaterialNames();
        nlohmann::json matArr = nlohmann::json::array();
        for (const auto& name : names) {
            const auto& mat = matMgr.getMaterial(name);
            matArr.push_back({
                {"name", name},
                {"mass", mat.mass},
                {"friction", mat.friction},
                {"restitution", mat.restitution},
                {"colorTint", {{"r", mat.colorTint.r}, {"g", mat.colorTint.g}, {"b", mat.colorTint.b}}},
                {"metallic", mat.metallic},
                {"roughness", mat.roughness}
            });
        }
        return nlohmann::json{{"materials", matArr}};
    });

    apiServer->setChunkInfoHandler([this]() -> nlohmann::json {
        if (!chunkManager) return nlohmann::json{{"error", "ChunkManager not available"}};
        size_t chunkCount = chunkManager->chunks.size();
        auto stats = chunkManager->getPerformanceStats();
        return nlohmann::json{
            {"chunkCount", chunkCount},
            {"stats", {
                {"totalVertices", stats.totalVertices},
                {"totalVisibleFaces", stats.totalVisibleFaces},
                {"totalHiddenFaces", stats.totalHiddenFaces},
                {"fullyOccludedCubes", stats.fullyOccludedCubes}
            }}
        };
    });

    apiServer->setRegionScanHandler([this](int x1, int y1, int z1, int x2, int y2, int z2) -> nlohmann::json {
        if (!chunkManager) return nlohmann::json{{"error", "ChunkManager not available"}};

        int minX = std::min(x1, x2), maxX = std::max(x1, x2);
        int minY = std::min(y1, y2), maxY = std::max(y1, y2);
        int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);

        int64_t volume = (int64_t)(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
        if (volume > 100000) {
            return nlohmann::json{{"error", "Region too large"}, {"volume", volume}, {"max_volume", 100000}};
        }

        nlohmann::json voxels = nlohmann::json::array();
        int count = 0;
        for (int ix = minX; ix <= maxX; ++ix) {
            for (int iy = minY; iy <= maxY; ++iy) {
                for (int iz = minZ; iz <= maxZ; ++iz) {
                    auto* cube = chunkManager->getCubeAt(glm::ivec3(ix, iy, iz));
                    if (cube) {
                        voxels.push_back({
                            {"x", ix}, {"y", iy}, {"z", iz},
                            {"material", cube->getMaterialName()},
                            {"visible", cube->isVisible()}
                        });
                        ++count;
                    }
                }
            }
        }
        return nlohmann::json{
            {"count", count},
            {"volume", volume},
            {"min", {{"x", minX}, {"y", minY}, {"z", minZ}}},
            {"max", {{"x", maxX}, {"y", maxY}, {"z", maxZ}}},
            {"voxels", voxels}
        };
    });

    apiServer->setEventPollHandler([this](uint64_t sinceId) -> nlohmann::json {
        if (!gameEventLog) return nlohmann::json{{"error", "GameEventLog not available"}};
        auto result = gameEventLog->pollSince(sinceId);
        nlohmann::json eventsArr = nlohmann::json::array();
        for (const auto& event : result.events) {
            eventsArr.push_back(Core::GameEventLog::eventToJson(event));
        }
        return nlohmann::json{
            {"events", eventsArr},
            {"count", result.events.size()},
            {"cursor", result.nextCursor}
        };
    });

    // Initialize Snapshot Manager
    snapshotManager = std::make_unique<Core::SnapshotManager>();

    apiServer->setSnapshotListHandler([this]() -> nlohmann::json {
        if (!snapshotManager) return nlohmann::json{{"error", "SnapshotManager not available"}};
        auto snapshots = snapshotManager->listSnapshots();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& s : snapshots) {
            arr.push_back({
                {"name", s.name},
                {"min", {{"x", s.min.x}, {"y", s.min.y}, {"z", s.min.z}}},
                {"max", {{"x", s.max.x}, {"y", s.max.y}, {"z", s.max.z}}},
                {"size", {{"x", s.size.x}, {"y", s.size.y}, {"z", s.size.z}}},
                {"volume", s.totalVolume},
                {"voxel_count", static_cast<int>(s.voxels.size())}
            });
        }
        return nlohmann::json{{"snapshots", arr}, {"count", snapshots.size()}};
    });

    apiServer->setClipboardInfoHandler([this]() -> nlohmann::json {
        if (!snapshotManager) return nlohmann::json{{"error", "SnapshotManager not available"}};
        if (!snapshotManager->hasClipboard()) {
            return nlohmann::json{{"has_data", false}};
        }
        const auto* clip = snapshotManager->getClipboard();
        return nlohmann::json{
            {"has_data", true},
            {"size", {{"x", clip->size.x}, {"y", clip->size.y}, {"z", clip->size.z}}},
            {"voxel_count", static_cast<int>(clip->voxels.size())},
            {"volume", clip->totalVolume}
        };
    });

    // Initialize Story Engine
    storyEngine = std::make_unique<Story::StoryEngine>();

    // Wire story read-only handlers
    apiServer->setStoryStateHandler([this]() -> nlohmann::json {
        return storyEngine->saveState();
    });

    apiServer->setStoryCharacterListHandler([this]() -> nlohmann::json {
        auto ids = storyEngine->getCharacterIds();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& id : ids) {
            const auto* profile = storyEngine->getCharacter(id);
            if (profile) {
                arr.push_back({{"id", id}, {"name", profile->name}, {"faction", profile->factionId}});
            }
        }
        return nlohmann::json{{"characters", arr}, {"count", arr.size()}};
    });

    apiServer->setStoryCharacterDetailHandler([this](const std::string& id) -> nlohmann::json {
        const auto* profile = storyEngine->getCharacter(id);
        if (!profile) return nlohmann::json{{"error", "Character not found: " + id}};
        nlohmann::json result = *profile; // uses to_json
        const auto* memory = storyEngine->getCharacterMemory(id);
        if (memory) result["knowledgeSummary"] = memory->buildContextSummary();
        const auto* emotion = storyEngine->getCharacterEmotion(id);
        if (emotion) result["currentEmotion"] = emotion->dominantEmotion();
        return result;
    });

    apiServer->setStoryArcListHandler([this]() -> nlohmann::json {
        const auto& arcs = storyEngine->getDirector().getArcs();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& arc : arcs) {
            arr.push_back(nlohmann::json(arc)); // uses to_json
        }
        return nlohmann::json{{"arcs", arr}, {"count", arcs.size()}};
    });

    apiServer->setStoryArcDetailHandler([this](const std::string& id) -> nlohmann::json {
        const auto* arc = storyEngine->getDirector().getArc(id);
        if (!arc) return nlohmann::json{{"error", "Arc not found: " + id}};
        return nlohmann::json(*arc);
    });

    apiServer->setStoryWorldHandler([this]() -> nlohmann::json {
        return nlohmann::json(storyEngine->getWorldState()); // uses to_json
    });

    // ========================================================================
    // Lighting read-only handler
    // ========================================================================
    apiServer->setLightListHandler([this]() -> nlohmann::json {
        nlohmann::json result;
        result["ambient_strength"] = renderCoordinator ? renderCoordinator->getAmbientLightStrength() : 1.0f;

        if (renderCoordinator) {
            auto& lm = renderCoordinator->getLightManager();
            nlohmann::json pointArr = nlohmann::json::array();
            for (const auto& pl : lm.getPointLights()) {
                pointArr.push_back({
                    {"id", pl.id},
                    {"position", {{"x", pl.position.x}, {"y", pl.position.y}, {"z", pl.position.z}}},
                    {"color", {{"r", pl.color.r}, {"g", pl.color.g}, {"b", pl.color.b}}},
                    {"intensity", pl.intensity},
                    {"radius", pl.radius},
                    {"enabled", pl.enabled}
                });
            }
            result["point_lights"] = pointArr;
            result["point_light_count"] = lm.getPointLightCount();

            nlohmann::json spotArr = nlohmann::json::array();
            for (const auto& sl : lm.getSpotLights()) {
                spotArr.push_back({
                    {"id", sl.id},
                    {"position", {{"x", sl.position.x}, {"y", sl.position.y}, {"z", sl.position.z}}},
                    {"direction", {{"x", sl.direction.x}, {"y", sl.direction.y}, {"z", sl.direction.z}}},
                    {"color", {{"r", sl.color.r}, {"g", sl.color.g}, {"b", sl.color.b}}},
                    {"intensity", sl.intensity},
                    {"radius", sl.radius},
                    {"inner_cone", sl.innerCone},
                    {"outer_cone", sl.outerCone},
                    {"enabled", sl.enabled}
                });
            }
            result["spot_lights"] = spotArr;
            result["spot_light_count"] = lm.getSpotLightCount();
            result["max_point_lights"] = Graphics::MAX_POINT_LIGHTS;
            result["max_spot_lights"] = Graphics::MAX_SPOT_LIGHTS;
        }
        return result;
    });

    // ========================================================================
    // Audio read-only handler
    // ========================================================================
    apiServer->setSoundListHandler([this]() -> nlohmann::json {
        nlohmann::json result;
        nlohmann::json sounds = nlohmann::json::array();
        // List available sound files from resources/sounds/
        std::string soundDir = "resources/sounds";
        if (std::filesystem::exists(soundDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(soundDir)) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension().string();
                    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
                        sounds.push_back(entry.path().filename().string());
                    }
                }
            }
        }
        result["sounds"] = sounds;
        result["count"] = sounds.size();
        result["channels"] = nlohmann::json::array({"Master", "SFX", "Music", "Voice"});
        return result;
    });

    // ========================================================================
    // Inventory System
    // ========================================================================
    inventory = std::make_unique<Core::Inventory>();
    // Start in creative mode with all materials in hotbar
    {
        static const std::vector<std::string> defaultHotbar = {
            "Stone", "Wood", "Metal", "Glass", "Rubber", "Ice", "Cork", "glow", "Default"
        };
        for (int i = 0; i < static_cast<int>(defaultHotbar.size()); ++i) {
            inventory->setSlot(i, Core::ItemStack{defaultHotbar[i], 64, 64});
        }
    }

    apiServer->setInventoryHandler([this]() -> nlohmann::json {
        if (!inventory) return nlohmann::json{{"error", "Inventory not available"}};
        return inventory->toJson();
    });

    apiServer->setDayNightHandler([this]() -> nlohmann::json {
        if (!renderCoordinator) return nlohmann::json{{"error", "RenderCoordinator not available"}};
        return renderCoordinator->getDayNightCycle().toJson();
    });

    // Initialize NPC Manager
    npcManager = std::make_unique<Core::NPCManager>();
    npcManager->setPhysicsWorld(physicsWorld);
    npcManager->setEntityRegistry(entityRegistry.get());
    if (renderCoordinator) {
        npcManager->setLightManager(&renderCoordinator->getLightManager());
        renderCoordinator->setNPCManager(npcManager.get());
    }

    // Initialize Interaction Manager
    interactionManager = std::make_unique<Core::InteractionManager>();
    interactionManager->setEntityRegistry(entityRegistry.get());

    // Initialize Dialogue System
    dialogueSystem = std::make_unique<UI::DialogueSystem>();
    dialogueSystem->setGameEventLog(gameEventLog.get());

    // Initialize Speech Bubble Manager
    speechBubbleManager = std::make_unique<UI::SpeechBubbleManager>();
    speechBubbleManager->setEntityRegistry(entityRegistry.get());

    // Wire speech bubble manager to NPC manager
    if (npcManager) {
        npcManager->setSpeechBubbleManager(speechBubbleManager.get());
    }

    // Wire interaction callback to start dialogues
    interactionManager->setInteractCallback([this](Scene::NPCEntity* npc) {
        if (!dialogueSystem || !npc) return;
        auto* provider = npc->getDialogueProvider();
        if (provider && provider->getDialogueTree()) {
            dialogueSystem->startConversation(npc, provider->getDialogueTree());
        } else {
            LOG_INFO("Application", "NPC '{}' has no dialogue tree", npc->getName());
        }
    });

    // Start the API server
    if (apiServer->start()) {
        LOG_INFO("Application", "Engine HTTP API available at http://localhost:{}/api/status", engineConfig.apiPort);
    } else {
        LOG_WARN("Application", "Failed to start HTTP API server (non-critical)");
    }

    m_initialized = true;

    // STEP 11: AUTO-LOAD GAME DEFINITION (after all subsystems are ready)
    autoLoadGameDefinition();

    return true;
}

void Application::run() {
    LOG_INFO("Application", "Starting Phyxel application...");
    isRunning = true;
    
    lastFrameTime = glfwGetTime();
    fpsTimer = lastFrameTime;
    int fpsFrameCount = 0;
    
    while (isRunning && !windowManager->shouldClose()) {
        frameStartTime = std::chrono::high_resolution_clock::now();
        
        // Start new frame profiling
        performanceProfiler->startFrame();
        
        double currentTime = glfwGetTime();
        deltaTime = currentTime - lastFrameTime;
        lastFrameTime = currentTime;
        
        // Reset mouse delta before polling new events
        if (inputManager) {
            inputManager->resetMouseDelta();
        }

        // Capture input start time for latency tracking
        auto inputStartTime = std::chrono::high_resolution_clock::now();

        // Poll GLFW events
        windowManager->pollEvents();

        // Skip rendering when minimized (0x0 framebuffer crashes Vulkan)
        if (windowManager->isMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        timer->update();
        
        {
            ScopedTimer inputTimer(*performanceProfiler, "input");
            handleInput();
        }
        
        {
            ScopedTimer updateTimer(*performanceProfiler, "update");
            update(deltaTime);
        }
        
        // Start ImGui frame
        imguiRenderer->newFrame();
        
        // Store current distances before UI update
        float currentRenderDistance = maxChunkRenderDistance;
        float currentChunkInclusionDistance = chunkInclusionDistance;
        
        // Render ImGui performance overlay instead of console output
        imguiRenderer->renderPerformanceOverlay(
            showPerformanceOverlay,
            timer,
            performanceProfiler,
            performanceMonitor->getCurrentFrameTiming(),
            performanceMonitor->getDetailedTimings(),
            physicsWorld,
            inputManager->getCameraPosition(),
            frameCount,
            currentRenderDistance,         // Pass by reference to allow UI modification
            currentChunkInclusionDistance  // Pass by reference to allow UI modification
        );
        
        // Render Force System Debug overlay
        auto& flags = inputController->getDebugFlags();
        imguiRenderer->renderForceSystemDebug(
            flags.showForceSystemDebug,
            forceSystem,
            mouseVelocityTracker,
            voxelInteractionSystem->hasHoveredCube(),
            voxelInteractionSystem->hasHoveredCube() ? glm::vec3(voxelInteractionSystem->getCurrentHoveredLocation().worldPos) : glm::vec3(0.0f),
            flags.manualForceValue
        );
        
        // Check if distances were changed by UI
        if (currentRenderDistance != maxChunkRenderDistance) {
            setRenderDistance(currentRenderDistance);
        }
        if (currentChunkInclusionDistance != chunkInclusionDistance) {
            setChunkInclusionDistance(currentChunkInclusionDistance);
        }

        // Render Scripting Console
        imguiRenderer->renderScriptingConsole(showScriptingConsole, scriptingSystem.get());
        
        // Render Dialogue Box
        if (dialogueSystem) {
            imguiRenderer->renderDialogueBox(dialogueSystem.get());
        }

        // Render Speech Bubbles & Interaction Prompt
        if (speechBubbleManager || interactionManager) {
            float sw = static_cast<float>(windowManager->getWidth());
            float sh = static_cast<float>(windowManager->getHeight());

            if (speechBubbleManager) {
                imguiRenderer->renderSpeechBubbles(
                    speechBubbleManager.get(),
                    cachedViewMatrix, cachedProjectionMatrix, sw, sh);
            }

            if (interactionManager && interactionManager->shouldShowPrompt()) {
                // Show prompt above the nearest NPC
                auto* nearestNPC = interactionManager->getNearestInteractableNPC();
                if (nearestNPC) {
                    bool showPrompt = !dialogueSystem || !dialogueSystem->isActive();
                    imguiRenderer->renderInteractionPrompt(
                        showPrompt,
                        nearestNPC->getPosition(),
                        cachedViewMatrix, cachedProjectionMatrix, sw, sh);
                }
            }
        }

        // Render Lighting Controls
        if (renderCoordinator) {
            renderCoordinator->renderUI();
        }
        
        // End ImGui frame
        imguiRenderer->endFrame();

        {
            ScopedTimer renderTimer(*performanceProfiler, "render");
            // Pass frameStartTime to RenderCoordinator
            renderCoordinator->setFrameStartTime(frameStartTime);
            render();
        }
        
        // End frame profiling
        performanceProfiler->endFrame();
        
        // Profile the frame with PerformanceMonitor
        FrameTiming timing = performanceMonitor->profileFrame();
        
        // --- STUTTER DETECTOR ---
        // Check if frame time exceeded threshold (e.g., 30ms)
        // We skip the first 60 frames to allow initialization to settle
        if (deltaTime * 1000.0 > 30.0 && frameCount > 60) {
            auto frameEndTime = std::chrono::high_resolution_clock::now();
            double inputLatencyMs = std::chrono::duration<double, std::milli>(frameEndTime - inputStartTime).count();
            
            LOG_WARN_FMT("Performance", "STUTTER DETECTED! Frame Time: " << (deltaTime * 1000.0) << "ms, Input Latency: " << inputLatencyMs << "ms");
            LOG_INFO_FMT("Performance", "Profile Dump: " << performanceProfiler->dumpFrameToJSON());
        }
        // ------------------------

        performanceMonitor->updateFrameTiming(deltaTime);
        frameCount++;
        fpsFrameCount++;
        
        // Print detailed performance stats every second (only when overlay is disabled)
        if (currentTime - fpsTimer >= 1.0) {
            if (!showPerformanceOverlay) {
                // Temporarily disabled performance reports
                // performanceMonitor->printProfilingInfo(fpsFrameCount, inputManager);
                // performanceMonitor->printDetailedTimings();
                
                // Print new performance profiler reports
                // performanceProfiler->printFrameReport();
                // performanceProfiler->printMemoryReport();
                // performanceProfiler->printCullingReport();
            }
            
            fpsFrameCount = 0;
            fpsTimer = currentTime;
        }
        
        // Print basic stats every 60 frames (only when overlay is disabled)
        if (frameCount % 60 == 0 && !showPerformanceOverlay) {
            // performanceMonitor->printPerformanceStats(timer, timing, chunkManager, physicsWorld);
        }

    }
    
    LOG_INFO("Application", "Application shutting down...");
}

void Application::cleanup() {
    // Guard against double cleanup
    if (!m_initialized) {
        return;
    }
    m_initialized = false;

    // Shutdown HTTP API server first (background thread must stop before systems go away)
    if (apiServer) {
        apiServer->stop();
        apiServer.reset();
    }
    apiCommandQueue.reset();
    gameEventLog.reset();
    entityRegistry.reset();
    
    // Shutdown AI system before scripting
    if (aiSystem) {
        aiSystem->shutdown();
        aiSystem.reset();
    }

    // Shutdown scripting first to stop any running scripts
    if (scriptingSystem) {
        scriptingSystem->shutdown();
        scriptingSystem.reset();
    }
    
    // Reset game-specific subsystems before engine cleanup
    renderCoordinator.reset();
    voxelInteractionSystem.reset();
    raycastVisualizer.reset();
    objectTemplateManager.reset();
    inputController.reset();
    
    // Clear entities BEFORE physics cleanup — they hold raw pointers to physics bodies
    entities.clear();
    player = nullptr;
    physicsCharacter = nullptr;
    spiderCharacter = nullptr;
    animatedCharacter = nullptr;

    // Clear NPC / story / dialogue subsystems
    npcManager.reset();
    interactionManager.reset();
    dialogueSystem.reset();
    speechBubbleManager.reset();
    storyEngine.reset();

    // Null out convenience pointers (owned by EngineRuntime)
    windowManager = nullptr;
    vulkanDevice = nullptr;
    renderPipeline = nullptr;
    dynamicRenderPipeline = nullptr;
    imguiRenderer = nullptr;
    physicsWorld = nullptr;
    chunkManager = nullptr;
    forceSystem = nullptr;
    inputManager = nullptr;
    mouseVelocityTracker = nullptr;
    timer = nullptr;
    performanceProfiler = nullptr;
    performanceMonitor = nullptr;
    audioSystem = nullptr;
    camera = nullptr;
    cameraManager = nullptr;

    // Delegate core engine cleanup to EngineRuntime
    if (runtime) {
        runtime->shutdown();
        runtime.reset();
    }
}

void Application::setWindowSize(int width, int height) {
    if (windowManager) {
        windowManager->setSize(width, height);
        projectionMatrixNeedsUpdate = true;
    }
}

void Application::setTitle(const std::string& title) {
    if (windowManager) {
        windowManager->setTitle(title);
    }
}

void Application::update(float deltaTime) {
    PROFILE_SCOPE(*performanceProfiler, "Update");
    this->deltaTime = deltaTime;
    
    // Process HTTP API commands (from external agents)
    processAPICommands();

    // Process completed background jobs (finalize on main thread)
    if (jobSystem) {
        jobSystem->processCompletedJobs();
    }

    // Update scripting system
    if (scriptingSystem) {
        PROFILE_SCOPE(*performanceProfiler, "Scripting");
        scriptingSystem->update(deltaTime);
    }

    // Update AI system
    if (aiSystem) {
        PROFILE_SCOPE(*performanceProfiler, "AI");
        aiSystem->update(deltaTime);
    }

    // Update NPC system
    if (npcManager) {
        PROFILE_SCOPE(*performanceProfiler, "NPCs");
        npcManager->update(deltaTime);
    }

    // Update interaction detection
    if (interactionManager) {
        glm::vec3 playerPos = camera ? camera->getPosition() : glm::vec3(0);
        interactionManager->update(deltaTime, playerPos);
    }

    // Update dialogue & speech bubbles
    if (dialogueSystem) dialogueSystem->update(deltaTime);
    if (speechBubbleManager) speechBubbleManager->update(deltaTime);

    // Update entities
    {
        PROFILE_SCOPE(*performanceProfiler, "Entities");
        for (auto& entity : entities) {
            entity->update(deltaTime);
        }
    }

    // Camera sync
    {
        PROFILE_SCOPE(*performanceProfiler, "Camera Sync");
        if (camera->getMode() == Graphics::CameraMode::Free) {
            // Debug sync
            static int syncLogCounter = 0;
            if (++syncLogCounter % 60 == 0) {
                 LOG_INFO_FMT("Application", "Syncing Free Camera: Pos(" << inputManager->getCameraPosition().x << ") Yaw(" << inputManager->getYaw() << ")");
            }

            camera->setPosition(inputManager->getCameraPosition());
            // Sync yaw/pitch so the camera actually rotates
            // Note: We do NOT sync setFront() because Camera calculates it from yaw/pitch
            camera->setYaw(inputManager->getYaw());
            camera->setPitch(inputManager->getPitch());
        } else if (isControllingPhysicsCharacter && physicsCharacter) {
            physicsCharacter->updateCamera();
        } else if (!isControllingPhysicsCharacter) {
            camera->setYaw(inputManager->getYaw());
            camera->setPitch(inputManager->getPitch());
            
            if (currentControlTarget == ControlTarget::Spider && spiderCharacter) {
                camera->updatePositionFromTarget(spiderCharacter->getPosition(), 0.5f);
            } else if (currentControlTarget == ControlTarget::AnimatedCharacter && animatedCharacter) {
                camera->updatePositionFromTarget(animatedCharacter->getPosition(), 0.5f);
            }
        }

        // CameraManager handles transitions, cinematic paths, and entity-follow
        if (cameraManager) {
            cameraManager->update(deltaTime);
        }
    }
    
    // Input is processed in handleInput() which is called from run() loop
    // Update input controller logic (e.g. previews)
    if (inputController) {
        PROFILE_SCOPE(*performanceProfiler, "Input Controller");
        inputController->update(deltaTime);
    }

    // Update object template manager (for sequential spawning)
    if (objectTemplateManager) {
        objectTemplateManager->update(deltaTime);
    }
    
    // Update mouse hover detection via VoxelInteractionSystem
    if (voxelInteractionSystem) {
        PROFILE_SCOPE(*performanceProfiler, "Voxel Interaction");
        double mouseX, mouseY;
        inputManager->getCurrentMousePosition(mouseX, mouseY);
        voxelInteractionSystem->updateMouseHover(
            inputManager->getCameraPosition(),
            inputManager->getCameraFront(),
            inputManager->getCameraUp(),
            mouseX,
            mouseY,
            inputManager->isMouseCaptured()
        );
        
        // Sync debug flags
        auto& flags = inputController->getDebugFlags();
        voxelInteractionSystem->setDebugFlags(
            flags.hoverDetection,
            flags.disableBreakingForces,
            flags.showForceSystemDebug,
            flags.manualForceValue
        );
        
        // Update raycast visualizer if enabled
        if (raycastVisualizer && raycastVisualizer->isEnabled()) {
            const auto& raycastDebugData = voxelInteractionSystem->getLastRaycastDebugData();
            
            // Convert to RaycastVisualizer format
            RaycastVisualizer::RaycastDebugData vizData;
            vizData.rayOrigin = raycastDebugData.rayOrigin;
            vizData.rayDirection = raycastDebugData.rayDirection;
            vizData.traversedVoxels = raycastDebugData.traversedVoxels;
            vizData.hitPoint = raycastDebugData.hitPoint;
            vizData.hitNormal = raycastDebugData.hitNormal;
            vizData.hasHit = raycastDebugData.hasHit;
            vizData.hitFace = raycastDebugData.hitFace;
            
            // Copy target visualization data
            vizData.targetSubcubeCenter = raycastDebugData.targetSubcubeCenter;
            vizData.hasTarget = raycastDebugData.hasTarget;
            
            raycastVisualizer->setRaycastData(vizData);
            raycastVisualizer->updateBuffers(renderCoordinator->getCurrentFrame());
        }

        // Update PhysicsCharacter look target
        if (physicsCharacter) {
            const auto& raycastDebugData = voxelInteractionSystem->getLastRaycastDebugData();
            if (raycastDebugData.hasHit) {
                physicsCharacter->setLookTarget(raycastDebugData.hitPoint);
            } else {
                // Look far away along ray
                glm::vec3 farPoint = raycastDebugData.rayOrigin + raycastDebugData.rayDirection * 100.0f;
                physicsCharacter->setLookTarget(farPoint);
            }
        }
    }
    
    // Update chunks that have been modified (for hover color changes, etc.)
    // OPTIMIZED: Only update chunks that have actually changed
    if (chunkManager) {
        chunkManager->updateDirtyChunks();
        // Use the combined update method which also updates the DebrisSystem
        chunkManager->m_dynamicObjectManager.updateAllDynamicObjects(deltaTime);
    }
    
    // Update physics with FIXED timestep for smooth, jitter-free simulation
    auto physicsStart = std::chrono::high_resolution_clock::now();
    
    // Accumulate time for fixed timestep physics
    static float physicsDeltaAccumulator = 0.0f;
    static const float FIXED_TIMESTEP = 1.0f / 60.0f; // 60 FPS physics
    static const float MAX_DELTA = 0.25f; // Prevent spiral of death
    
    // Add frame time to accumulator, but clamp to prevent spiral of death
    float frameTime = std::min(deltaTime, MAX_DELTA);
    physicsDeltaAccumulator += frameTime;
    
    // Run physics in fixed timesteps
    while (physicsDeltaAccumulator >= FIXED_TIMESTEP) {
        physicsWorld->stepSimulation(FIXED_TIMESTEP, 1, FIXED_TIMESTEP); // Pure fixed timestep
        physicsDeltaAccumulator -= FIXED_TIMESTEP;
    }
    
    auto physicsEnd = std::chrono::high_resolution_clock::now();
    
    // Update Camera AFTER physics step to prevent jitter/lag
    if (isControllingPhysicsCharacter && physicsCharacter) {
        physicsCharacter->updateCamera();
    }

    // Update dynamic subcube positions from physics bodies (batched + throttled)
    if (chunkManager) {
        chunkManager->m_dynamicObjectManager.updateAllDynamicObjectPositions();
    }
    
    static int frameCount = 0;
    if (frameCount % 60 == 0) { // Log every 60 frames
        //std::cout << "[PHYSICS STEP] Using FIXED timestep: " << (1.0f/60.0f) << "s (60Hz), frame deltaTime: " << deltaTime << std::endl;
    }
    frameCount++;

    // Physics is handled by ChunkManager for dynamic objects
    // No need for separate scene manager physics sync
    
    // NOTE: Frustum culling is now handled in renderStaticGeometry()

    // Sync Camera to InputManager for other systems (Audio, Scripts, etc.)
    // Only sync if NOT in Free mode, otherwise InputManager is the master
    if (camera && inputManager && camera->getMode() != Graphics::CameraMode::Free) {
        inputManager->setCameraPosition(camera->getPosition());
        inputManager->setYawPitch(camera->getYaw(), camera->getPitch());
    }

    // Update view and projection matrices for Vulkan rendering
    glm::vec3 cameraPos = camera->getPosition();
    glm::vec3 cameraFront = camera->getFront();
    glm::vec3 cameraUp = camera->getUp();
    
    // Update Audio Listener
    if (audioSystem) {
        glm::vec3 velocity = (deltaTime > 0.0f) ? (cameraPos - lastCameraPos) / deltaTime : glm::vec3(0.0f);
        audioSystem->update(cameraPos, cameraFront, cameraUp, velocity);
    }
    lastCameraPos = cameraPos;

    glm::mat4 view = camera->getViewMatrix();
    glm::mat4 proj = glm::perspective(
        glm::radians(45.0f), 
        static_cast<float>(windowManager->getWidth()) / static_cast<float>(windowManager->getHeight()), 
        0.1f, 
        maxChunkRenderDistance  // Use configurable render distance
    );
    proj[1][1] *= -1; // Flip Y for Vulkan
    
    // Store matrices for use in render functions
    cachedViewMatrix = view;
    cachedProjectionMatrix = proj;
    
    // Pass cached matrices to RenderCoordinator
    renderCoordinator->setCachedViewMatrix(cachedViewMatrix);
    renderCoordinator->setProjectionMatrixNeedsUpdate(projectionMatrixNeedsUpdate);
}

void Application::render() {
    renderCoordinator->render();
}

void Application::handleInput() {
    // Process keyboard and mouse input through InputManager
    inputManager->processInput(deltaTime);

    // Suppress movement/camera input while dialogue is active
    if (dialogueSystem && dialogueSystem->isActive()) {
        return;
    }

    // Pass movement input to active character
    // Only if NOT in Free Camera mode
    if (!isControllingPhysicsCharacter && camera && camera->getMode() != Graphics::CameraMode::Free) {
        float forward = 0.0f;
        float turn = 0.0f;
        float strafe = 0.0f;

        // Check for sprint modifier (Shift)
        bool isSprinting = inputManager->isKeyPressed(GLFW_KEY_LEFT_SHIFT) || inputManager->isKeyPressed(GLFW_KEY_RIGHT_SHIFT);
        
        float moveMagnitude = isSprinting ? 1.0f : 0.5f;

        if (inputManager->isKeyPressed(GLFW_KEY_W) || inputManager->isKeyPressed(GLFW_KEY_UP)) forward -= moveMagnitude;
        if (inputManager->isKeyPressed(GLFW_KEY_S) || inputManager->isKeyPressed(GLFW_KEY_DOWN)) forward += moveMagnitude;
        
        // A/D for Strafe (if controlling AnimatedCharacter)
        if (currentControlTarget == ControlTarget::AnimatedCharacter) {
            if (inputManager->isKeyPressed(GLFW_KEY_A) || inputManager->isKeyPressed(GLFW_KEY_LEFT)) strafe -= moveMagnitude;
            if (inputManager->isKeyPressed(GLFW_KEY_D) || inputManager->isKeyPressed(GLFW_KEY_RIGHT)) strafe += moveMagnitude;
            
            // Q/E for Turn
            if (inputManager->isKeyPressed(GLFW_KEY_Q)) turn -= 1.0f;
            if (inputManager->isKeyPressed(GLFW_KEY_E)) turn += 1.0f;
        } else {
            // Standard Tank Controls for others
            if (inputManager->isKeyPressed(GLFW_KEY_A) || inputManager->isKeyPressed(GLFW_KEY_LEFT)) turn -= 1.0f;
            if (inputManager->isKeyPressed(GLFW_KEY_D) || inputManager->isKeyPressed(GLFW_KEY_RIGHT)) turn += 1.0f;
        }

        if (currentControlTarget == ControlTarget::Spider && spiderCharacter) {
            spiderCharacter->setControlInput(forward, turn);
        } else if (currentControlTarget == ControlTarget::AnimatedCharacter && animatedCharacter) {
            animatedCharacter->setControlInput(forward, turn, strafe);
            animatedCharacter->setSprint(isSprinting);

            // New Inputs for Enhanced Animation System
            // Use a static flag to prevent rapid-fire jumping if key is held
            static bool spaceWasPressed = false;
            bool spaceIsPressed = inputManager->isKeyPressed(GLFW_KEY_SPACE);
            
            if (spaceIsPressed && !spaceWasPressed) {
                animatedCharacter->jump();
            }
            spaceWasPressed = spaceIsPressed;

            if (inputManager->isMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) {
                animatedCharacter->attack();
            }
            bool isCrouching = inputManager->isKeyPressed(GLFW_KEY_LEFT_CONTROL);
            animatedCharacter->setCrouch(isCrouching);

            // Animation Preview Cycling
            static bool prevAnimWasPressed = false;
            bool prevAnimIsPressed = inputManager->isKeyPressed(GLFW_KEY_B);
            if (prevAnimIsPressed && !prevAnimWasPressed) {
                animatedCharacter->cycleAnimation(false);
            }
            prevAnimWasPressed = prevAnimIsPressed;

            static bool nextAnimWasPressed = false;
            bool nextAnimIsPressed = inputManager->isKeyPressed(GLFW_KEY_N);
            if (nextAnimIsPressed && !nextAnimWasPressed) {
                animatedCharacter->cycleAnimation(true);
            }
            nextAnimWasPressed = nextAnimIsPressed;
        }
    }
}



void Application::togglePerformanceOverlay() {
    showPerformanceOverlay = !showPerformanceOverlay;
    LOG_INFO_FMT("Application", "Performance Overlay: " << (showPerformanceOverlay ? "ENABLED" : "DISABLED"));
}

void Application::toggleLightingControls() {
    if (renderCoordinator) {
        renderCoordinator->toggleLightingControls();
        LOG_INFO_FMT("Application", "Lighting Controls: " << (renderCoordinator->isLightingControlsVisible() ? "ENABLED" : "DISABLED"));
    }
}

void Application::toggleProfiler() {
    if (renderCoordinator) {
        renderCoordinator->toggleProfiler();
        LOG_INFO_FMT("Application", "Profiler: " << (renderCoordinator->isProfilerVisible() ? "ENABLED" : "DISABLED"));
    }
}

void Application::toggleDebugRendering() {
    if (renderCoordinator) {
        renderCoordinator->toggleDebugMode();
        bool isEnabled = renderCoordinator->isDebugModeEnabled();
        uint32_t mode = renderCoordinator->getDebugVisualizationMode();
        const char* modeNames[] = {"Wireframe", "Normals", "Hierarchy", "UV Coords", "Emissive"};
        const char* modeName = (mode < 5) ? modeNames[mode] : "Unknown";
        LOG_INFO_FMT("Application", "Debug Rendering: " << (isEnabled ? "ENABLED" : "DISABLED") 
                     << " (Mode: " << modeName << ")");
    }
}

void Application::cycleDebugVisualizationMode() {
    if (renderCoordinator) {
        uint32_t currentMode = renderCoordinator->getDebugVisualizationMode();
        uint32_t nextMode = (currentMode + 1) % 5;  // Cycle through 0-4
        renderCoordinator->setDebugVisualizationMode(nextMode);
        
        const char* modeNames[] = {"Wireframe", "Normals", "Hierarchy", "UV Coords", "Emissive"};
        const char* modeName = (nextMode < 5) ? modeNames[nextMode] : "Unknown";
        LOG_INFO_FMT("Application", "Debug Visualization Mode: " << modeName);
    }
}

void Application::adjustAmbientLight(float delta) {
    if (renderCoordinator) {
        renderCoordinator->adjustAmbientLightStrength(delta);
        LOG_INFO_FMT("Application", "Ambient Light: " << renderCoordinator->getAmbientLightStrength());
    }
}

void Application::toggleScriptingConsole() {
    if (renderCoordinator) {
        showScriptingConsole = !showScriptingConsole;
        renderCoordinator->setShowScriptingConsole(showScriptingConsole);
        
        // Toggle mouse cursor visibility when console is open
        if (showScriptingConsole) {
            windowManager->setCursorVisible(true);
        } else {
            // Keep cursor visible as requested by user
            windowManager->setCursorVisible(true);
        }
        
        // Update InputManager mode
        if (inputManager) {
            inputManager->setScriptingConsoleMode(showScriptingConsole);
        }
        
        LOG_INFO_FMT("Application", "Scripting Console: " << (showScriptingConsole ? "OPEN" : "CLOSED"));
    }
}

void Application::toggleRaycastVisualization() {
    if (renderCoordinator && voxelInteractionSystem && raycastVisualizer) {
        renderCoordinator->toggleRaycastVisualization();
        bool isEnabled = renderCoordinator->isRaycastVisualizationEnabled();
        
        // Enable/disable debug capture in raycaster
        voxelInteractionSystem->setRaycastDebugCaptureEnabled(isEnabled);
        raycastVisualizer->setEnabled(isEnabled);
        
        LOG_INFO_FMT("Application", "Raycast Visualization: " << (isEnabled ? "ENABLED" : "DISABLED"));
    }
}

void Application::cycleRaycastTargetMode() {
    if (voxelInteractionSystem) {
        voxelInteractionSystem->cycleTargetMode();
        
        // Sync with visualizer
        if (raycastVisualizer) {
            raycastVisualizer->setTargetMode(voxelInteractionSystem->getTargetMode());
        }
    }
}

void Application::setRenderDistance(float distance) {
    if (distance != maxChunkRenderDistance) {
        maxChunkRenderDistance = distance;
        
        // Ensure chunk inclusion distance is always >= render distance
        if (chunkInclusionDistance < maxChunkRenderDistance) {
            chunkInclusionDistance = maxChunkRenderDistance * 1.5f; // 50% buffer
        }
        
        projectionMatrixNeedsUpdate = true; // Force projection matrix recalculation
        
        // Update RenderCoordinator with new distances
        if (renderCoordinator) {
            renderCoordinator->setMaxChunkRenderDistance(maxChunkRenderDistance);
            renderCoordinator->setChunkInclusionDistance(chunkInclusionDistance);
            renderCoordinator->setProjectionMatrixNeedsUpdate(true);
        }
        
        LOG_INFO_FMT("Application", "Render distance updated to: " << distance 
                  << " (chunk inclusion: " << chunkInclusionDistance << ")");
    }
}

void Application::setChunkInclusionDistance(float distance) {
    if (distance != chunkInclusionDistance) {
        // Ensure chunk inclusion distance is always >= render distance
        if (distance < maxChunkRenderDistance) {
            distance = maxChunkRenderDistance;
            LOG_INFO_FMT("Application", "Chunk inclusion distance clamped to render distance: " << distance);
        }
        
        chunkInclusionDistance = distance;
        
        // Update RenderCoordinator with new distance
        if (renderCoordinator) {
            renderCoordinator->setChunkInclusionDistance(chunkInclusionDistance);
        }
        
        LOG_INFO_FMT("Application", "Chunk inclusion distance updated to: " << distance);
    }
}

void Application::toggleCameraMode() {
    if (camera) {
        Graphics::CameraMode currentMode = camera->getMode();
        Graphics::CameraMode newMode;
        
        if (currentMode == Graphics::CameraMode::FirstPerson) {
            newMode = Graphics::CameraMode::ThirdPerson;
            LOG_INFO("Application", "Switched to Third Person Camera");
        } else if (currentMode == Graphics::CameraMode::ThirdPerson) {
            newMode = Graphics::CameraMode::Free;
            LOG_INFO("Application", "Switched to Free Camera");
            
            // Sync InputManager to current camera state so we don't jump
            if (inputManager) {
                inputManager->setCameraPosition(camera->getPosition());
                inputManager->setYawPitch(camera->getYaw(), camera->getPitch());
            }
        } else {
            newMode = Graphics::CameraMode::FirstPerson;
            LOG_INFO("Application", "Switched to First Person Camera");
            
            // If switching to character modes, ensure we have a character to control
            // If we are controlling spider, stay on spider
        }
        
        camera->setMode(newMode);
    }
}

void Application::cycleCameraSlot() {
    if (cameraManager && cameraManager->slotCount() > 1) {
        cameraManager->cycleSlot(0.5f);
        LOG_INFO("Application", "Cycled to camera slot '{}'", cameraManager->getActiveSlotName());
    }
}

void Application::cycleCameraSlotReverse() {
    if (cameraManager && cameraManager->slotCount() > 1) {
        cameraManager->cyclePrevSlot(0.5f);
        LOG_INFO("Application", "Cycled to camera slot '{}'", cameraManager->getActiveSlotName());
    }
}

void Application::toggleCharacterControl() {
    // Cycle through control targets: PhysicsCharacter -> Spider -> AnimatedCharacter -> PhysicsCharacter
    
    if (currentControlTarget == ControlTarget::PhysicsCharacter) {
        currentControlTarget = ControlTarget::Spider;
        LOG_INFO("Application", "Control switched to Spider");
        
        if (physicsCharacter) physicsCharacter->setControlActive(false);
        if (camera) {
            camera->setDistanceFromTarget(3.0f);
            if (camera->getMode() == Graphics::CameraMode::Free) {
                camera->setMode(Graphics::CameraMode::ThirdPerson);
            }
        }
        // Spider control is handled in handleInput()
        
    } else if (currentControlTarget == ControlTarget::Spider) {
        currentControlTarget = ControlTarget::AnimatedCharacter;
        LOG_INFO("Application", "Control switched to Animated Character");
        
        if (spiderCharacter) spiderCharacter->setControlInput(0, 0);
        if (camera) {
            camera->setDistanceFromTarget(4.0f);
            if (camera->getMode() == Graphics::CameraMode::Free) {
                camera->setMode(Graphics::CameraMode::ThirdPerson);
            }
        }
        // Animated character control logic will need to be added
        
    } else {
        currentControlTarget = ControlTarget::PhysicsCharacter;
        LOG_INFO("Application", "Control switched to Physics Character");
        
        if (physicsCharacter) physicsCharacter->setControlActive(true);
        if (camera) {
            camera->setDistanceFromTarget(4.0f);
            if (camera->getMode() == Graphics::CameraMode::Free) {
                camera->setMode(Graphics::CameraMode::ThirdPerson);
            }
        }
    }
    
    // Update flag for legacy checks (though we should migrate to using enum everywhere)
    isControllingPhysicsCharacter = (currentControlTarget == ControlTarget::PhysicsCharacter);
}

Scene::PhysicsCharacter* Application::createPhysicsCharacter(const glm::vec3& pos) {
    auto physicsCharPtr = std::make_unique<Scene::PhysicsCharacter>(physicsWorld, inputManager, camera, pos);
    physicsCharacter = physicsCharPtr.get();
    physicsCharacter->setFaction(Scene::Faction::Player);
    entities.push_back(std::move(physicsCharPtr));
    if (entityRegistry) {
        entityRegistry->registerEntity(physicsCharacter, "physics_" + std::to_string(entities.size()), "physics");
    }
    LOG_INFO("Application", "Created PhysicsCharacter");
    return physicsCharacter;
}

Scene::SpiderCharacter* Application::createSpiderCharacter(const glm::vec3& pos) {
    auto spiderPtr = std::make_unique<Scene::SpiderCharacter>(physicsWorld, pos);
    spiderCharacter = spiderPtr.get();
    spiderCharacter->setFaction(Scene::Faction::Enemy);
    entities.push_back(std::move(spiderPtr));
    if (entityRegistry) {
        entityRegistry->registerEntity(spiderCharacter, "spider_" + std::to_string(entities.size()), "spider");
    }
    LOG_INFO("Application", "Created SpiderCharacter");
    return spiderCharacter;
}

Scene::AnimatedVoxelCharacter* Application::createAnimatedCharacter(const glm::vec3& pos, const std::string& animFile) {
    auto animatedCharPtr = std::make_unique<Scene::AnimatedVoxelCharacter>(physicsWorld, pos);
    animatedCharacter = animatedCharPtr.get();
    if (animatedCharacter->loadModel(animFile)) {
        // Play default animation if available
        animatedCharacter->playAnimation("idle"); 
        LOG_INFO("Application", "Loaded animated character model: " + animFile);
    } else {
        LOG_ERROR("Application", "Failed to load animated character model: " + animFile);
    }
    entities.push_back(std::move(animatedCharPtr));
    if (entityRegistry) {
        entityRegistry->registerEntity(animatedCharacter, "animated_" + std::to_string(entities.size()), "animated");
    }
    return animatedCharacter;
}

void Application::setControlTarget(const std::string& targetName) {
    if (targetName == "spider") {
        currentControlTarget = ControlTarget::Spider;
        if (physicsCharacter) physicsCharacter->setControlActive(false);
        if (camera) {
            camera->setDistanceFromTarget(3.0f);
            if (camera->getMode() == Graphics::CameraMode::Free) {
                camera->setMode(Graphics::CameraMode::ThirdPerson);
            }
        }
    } else if (targetName == "animated") {
        currentControlTarget = ControlTarget::AnimatedCharacter;
        if (physicsCharacter) physicsCharacter->setControlActive(false);
        if (spiderCharacter) spiderCharacter->setControlInput(0, 0);
        if (camera) {
            camera->setDistanceFromTarget(4.0f);
            if (camera->getMode() == Graphics::CameraMode::Free) {
                camera->setMode(Graphics::CameraMode::ThirdPerson);
            }
        }
    } else {
        currentControlTarget = ControlTarget::PhysicsCharacter;
        if (physicsCharacter) physicsCharacter->setControlActive(true);
        if (camera) {
            camera->setDistanceFromTarget(4.0f);
            if (camera->getMode() == Graphics::CameraMode::Free) {
                camera->setMode(Graphics::CameraMode::ThirdPerson);
            }
        }
    }
    isControllingPhysicsCharacter = (currentControlTarget == ControlTarget::PhysicsCharacter);
    LOG_INFO("Application", "Control target set to: " + targetName);
}

void Application::derezCharacter(float explosionStrength) {
    if (animatedCharacter && chunkManager) {
        LOG_INFO("Application", "Triggering derez on animated character");
        
        // 1. Spawn debris
        chunkManager->m_dynamicObjectManager.derezCharacter(animatedCharacter, explosionStrength);
        
        // 2. Remove character from entities list
        // Find and remove the unique_ptr that matches animatedCharacter
        auto it = std::remove_if(entities.begin(), entities.end(), 
            [this](const std::unique_ptr<Scene::Entity>& entity) {
                return entity.get() == animatedCharacter;
            });
            
        if (it != entities.end()) {
            entities.erase(it, entities.end());
            LOG_INFO("Application", "Animated character removed from scene");
        }
        
        // 3. Clear the pointer
        animatedCharacter = nullptr;
        
        // If we were controlling it, switch control
        if (currentControlTarget == ControlTarget::AnimatedCharacter) {
            toggleCharacterControl(); // Switch to next available
        }
    } else {
        LOG_WARN("Application", "Cannot derez: No animated character or chunk manager");
    }
}

void Application::spawnTestAINPC() {
    if (!aiSystem) {
        LOG_WARN("Application", "Cannot spawn AI NPC: AI system not initialized");
        return;
    }

    // Create a physics character near the camera as the NPC body
    glm::vec3 spawnPos(0.0f, 20.0f, 0.0f);
    if (camera) {
        // Spawn 5 units in front of the camera
        glm::vec3 camPos = camera->getPosition();
        glm::vec3 camFront = camera->getFront();
        spawnPos = camPos + camFront * 5.0f + glm::vec3(0.0f, 2.0f, 0.0f);
    }

    auto npc = std::make_unique<Scene::PhysicsCharacter>(physicsWorld, inputManager, camera, spawnPos);
    npc->debugColor = glm::vec4(0.2f, 0.8f, 0.2f, 1.0f); // Green tint for AI NPCs
    npc->setControlActive(false); // AI-controlled, not player-controlled

    auto* rawPtr = npc.get();

    // Generate a unique ID for this NPC
    static int npcCounter = 0;
    std::string npcId = "ai_npc_" + std::to_string(npcCounter++);

    // Register with AI system using guard recipe as default
    if (aiSystem->createAINPC(rawPtr, npcId, Core::AssetManager::instance().resolveRecipe("characters/guard.yaml"),
                               "You are a guard NPC in a voxel world. Be helpful but cautious.")) {
        LOG_INFO("Application", "Spawned AI NPC '{}' at ({}, {}, {})", npcId, spawnPos.x, spawnPos.y, spawnPos.z);
    } else {
        LOG_WARN("Application", "AI NPC created but AI registration failed for: {}", npcId);
    }

    entities.push_back(std::move(npc));
}

void Application::toggleAISystem() {
    if (!aiSystem) {
        LOG_WARN("Application", "AI system not available");
        return;
    }

    auto stats = aiSystem->getStats();
    if (stats.serverRunning) {
        aiSystem->shutdown();
        LOG_INFO("Application", "AI system stopped");
    } else {
        AI::GooseConfig config;
        if (aiSystem->initialize(config, /*autoStart=*/true)) {
            LOG_INFO("Application", "AI system started with goose-server");
        } else {
            LOG_WARN("Application", "Failed to start AI system");
        }
    }
}

void Application::interactWithNPC() {
    if (!interactionManager) return;

    // If dialogue is already active, advance it instead of starting new interaction
    if (dialogueSystem && dialogueSystem->isActive()) {
        dialogueSystem->advanceDialogue();
        return;
    }

    // Use the current active character as the "player" interactor
    Scene::Entity* playerEntity = nullptr;
    if (currentControlTarget == ControlTarget::PhysicsCharacter && physicsCharacter)
        playerEntity = physicsCharacter;
    else if (currentControlTarget == ControlTarget::AnimatedCharacter && animatedCharacter)
        playerEntity = animatedCharacter;
    else if (currentControlTarget == ControlTarget::Spider && spiderCharacter)
        playerEntity = spiderCharacter;
    interactionManager->tryInteract(playerEntity);
}

// ============================================================================
// Auto-load Game Definition
// Checks for a game definition file (from config or game.json in cwd) and
// loads it after all subsystems are initialized.
// ============================================================================

void Application::autoLoadGameDefinition() {
    std::string defPath = engineConfig.gameDefinitionFile;

    // If not configured in engine.json, check for game.json next to the executable
    if (defPath.empty()) {
        if (std::filesystem::exists("game.json")) {
            defPath = "game.json";
        }
    }

    if (defPath.empty() || !std::filesystem::exists(defPath)) return;

    LOG_INFO("Application", "Auto-loading game definition: {}", defPath);

    try {
        std::ifstream f(defPath);
        if (!f.is_open()) {
            LOG_ERROR("Application", "Cannot open game definition: {}", defPath);
            return;
        }

        nlohmann::json gameDef = nlohmann::json::parse(f);

        // If chunks were already loaded from the database (pre-baked world),
        // skip world generation from the definition — it would overwrite the
        // pre-existing terrain and is very slow.
        if (gameDef.contains("world") && chunkManager && !chunkManager->chunks.empty()) {
            LOG_INFO("Application", "Skipping world generation - {} chunks already loaded from database", chunkManager->chunks.size());
            gameDef.erase("world");
        }

        Core::GameSubsystems subsystems;
        subsystems.chunkManager     = chunkManager;
        subsystems.npcManager       = npcManager.get();
        subsystems.entityRegistry   = entityRegistry.get();
        subsystems.templateManager  = objectTemplateManager.get();
        subsystems.gameEventLog     = gameEventLog.get();
        subsystems.camera           = camera;
        subsystems.dialogueSystem   = dialogueSystem.get();
        subsystems.storyEngine      = storyEngine.get();

        subsystems.entitySpawner = [this](const std::string& type, const glm::vec3& pos,
                                          const std::string& animFile) -> Scene::Entity* {
            if (type == "physics")       return createPhysicsCharacter(pos);
            else if (type == "spider")   return createSpiderCharacter(pos);
            else if (type == "animated") return createAnimatedCharacter(pos, animFile);
            return nullptr;
        };

        auto result = Core::GameDefinitionLoader::load(gameDef, subsystems);
        if (result.success) {
            LOG_INFO("Application", "Game definition loaded: {} chunks, {} structures, {} NPCs", result.chunksGenerated, result.structuresPlaced, result.npcsSpawned);

            // Sync InputManager with camera set by game definition
            if (inputManager && camera) {
                inputManager->setCameraPosition(camera->getPosition());
                inputManager->setYawPitch(camera->getYaw(), camera->getPitch());
            }
        } else {
            LOG_ERROR("Application", "Failed to load game definition: {}", result.error);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Application", "Error loading game definition '{}': {}", defPath, e.what());
    }
}

// ============================================================================
// HTTP API Command Processor
// Called once per frame from update() to handle commands from the API server.
// ============================================================================

void Application::processAPICommands() {
    if (!apiCommandQueue || !apiCommandQueue->hasPending()) return;

    std::vector<Core::APICommand> commands;
    apiCommandQueue->drainCommands(commands);

    for (auto& cmd : commands) {
        nlohmann::json response;
        try {
            if (cmd.action == "spawn_entity") {
                // Required: "type" (physics/spider/animated), "position" {x,y,z}
                // Optional: "id", "animFile"
                std::string type = cmd.params.value("type", "physics");
                float x = 0, y = 20, z = 0;
                if (cmd.params.contains("position")) {
                    x = cmd.params["position"].value("x", 0.0f);
                    y = cmd.params["position"].value("y", 20.0f);
                    z = cmd.params["position"].value("z", 0.0f);
                }

                Scene::Entity* spawned = nullptr;
                std::string entityType = type;

                if (type == "physics") {
                    spawned = createPhysicsCharacter(glm::vec3(x, y, z));
                } else if (type == "spider") {
                    spawned = createSpiderCharacter(glm::vec3(x, y, z));
                } else if (type == "animated") {
                    std::string animFile = cmd.params.value("animFile", "character.anim");
                    spawned = createAnimatedCharacter(glm::vec3(x, y, z), animFile);
                } else {
                    response = {{"error", "Unknown entity type: " + type}};
                    if (cmd.onComplete) cmd.onComplete(response);
                    continue;
                }

                if (spawned && entityRegistry) {
                    std::string id = cmd.params.value("id", "");
                    if (id.empty()) {
                        id = entityRegistry->registerEntity(spawned);
                    } else {
                        entityRegistry->registerEntity(spawned, id, entityType);
                    }
                    response = {{"success", true}, {"id", id}, {"type", entityType},
                                {"position", {{"x", x}, {"y", y}, {"z", z}}}};
                    if (gameEventLog) {
                        gameEventLog->emit("entity_spawned", {
                            {"id", id}, {"type", entityType},
                            {"position", {{"x", x}, {"y", y}, {"z", z}}}
                        });
                    }
                } else {
                    response = {{"error", "Failed to spawn entity"}};
                }

            } else if (cmd.action == "move_entity") {
                std::string id = cmd.params.value("id", "");
                if (id.empty() || !entityRegistry) {
                    response = {{"error", "Entity ID required"}};
                } else {
                    auto* entity = entityRegistry->getEntity(id);
                    if (!entity) {
                        response = {{"error", "Entity not found: " + id}};
                    } else {
                        float x = cmd.params["position"].value("x", 0.0f);
                        float y = cmd.params["position"].value("y", 0.0f);
                        float z = cmd.params["position"].value("z", 0.0f);
                        entity->setPosition(glm::vec3(x, y, z));
                        response = {{"success", true}, {"id", id},
                                    {"position", {{"x", x}, {"y", y}, {"z", z}}}};
                        if (gameEventLog) {
                            gameEventLog->emit("entity_moved", {
                                {"id", id}, {"position", {{"x", x}, {"y", y}, {"z", z}}}
                            });
                        }
                    }
                }

            } else if (cmd.action == "remove_entity") {
                std::string id = cmd.params.value("id", "");
                if (id.empty() || !entityRegistry) {
                    response = {{"error", "Entity ID required"}};
                } else {
                    auto* entity = entityRegistry->getEntity(id);
                    if (!entity) {
                        response = {{"error", "Entity not found: " + id}};
                    } else {
                        // Remove from entities vector
                        auto it = std::remove_if(entities.begin(), entities.end(),
                            [entity](const std::unique_ptr<Scene::Entity>& e) {
                                return e.get() == entity;
                            });
                        if (it != entities.end()) {
                            entities.erase(it, entities.end());
                        }
                        // Clear named pointers if they match
                        if (entity == physicsCharacter) physicsCharacter = nullptr;
                        if (entity == spiderCharacter) spiderCharacter = nullptr;
                        if (entity == animatedCharacter) animatedCharacter = nullptr;
                        entityRegistry->unregisterEntity(id);
                        response = {{"success", true}, {"id", id}};
                        if (gameEventLog) {
                            gameEventLog->emit("entity_removed", {{"id", id}});
                        }
                    }
                }

            } else if (cmd.action == "place_voxel") {
                int x = cmd.params.value("x", 0);
                int y = cmd.params.value("y", 0);
                int z = cmd.params.value("z", 0);
                if (chunkManager) {
                    std::string material = cmd.params.value("material", "");
                    bool ok = false;
                    if (!material.empty()) {
                        ok = chunkManager->m_voxelModificationSystem.addCubeWithMaterial(
                            glm::ivec3(x, y, z), material);
                    } else {
                        ok = chunkManager->addCube(glm::ivec3(x, y, z));
                    }
                    response = {{"success", ok}, {"position", {{"x", x}, {"y", y}, {"z", z}}}};
                    if (ok && gameEventLog) {
                        gameEventLog->emit("voxel_placed", {
                            {"x", x}, {"y", y}, {"z", z},
                            {"material", material.empty() ? "Default" : material}
                        });
                    }
                } else {
                    response = {{"error", "ChunkManager not available"}};
                }

            } else if (cmd.action == "fill_region") {
                // Fill a 3D box with voxels
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else {
                    int x1 = cmd.params.value("x1", 0);
                    int y1 = cmd.params.value("y1", 0);
                    int z1 = cmd.params.value("z1", 0);
                    int x2 = cmd.params.value("x2", 0);
                    int y2 = cmd.params.value("y2", 0);
                    int z2 = cmd.params.value("z2", 0);
                    std::string material = cmd.params.value("material", "");
                    bool hollow = cmd.params.value("hollow", false);

                    // Normalize coordinates
                    int minX = std::min(x1, x2), maxX = std::max(x1, x2);
                    int minY = std::min(y1, y2), maxY = std::max(y1, y2);
                    int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);

                    // Safety limit: max 100k voxels per call
                    int64_t volume = (int64_t)(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
                    if (volume > 100000) {
                        response = {{"error", "Region too large"},
                                    {"volume", volume}, {"max_volume", 100000}};
                    } else {
                        int placed = 0, failed = 0;
                        for (int ix = minX; ix <= maxX; ++ix) {
                            for (int iy = minY; iy <= maxY; ++iy) {
                                for (int iz = minZ; iz <= maxZ; ++iz) {
                                    // Skip interior voxels if hollow
                                    if (hollow &&
                                        ix > minX && ix < maxX &&
                                        iy > minY && iy < maxY &&
                                        iz > minZ && iz < maxZ) {
                                        continue;
                                    }
                                    bool ok = false;
                                    if (!material.empty()) {
                                        ok = chunkManager->m_voxelModificationSystem.addCubeWithMaterial(
                                            glm::ivec3(ix, iy, iz), material);
                                    } else {
                                        ok = chunkManager->addCube(glm::ivec3(ix, iy, iz));
                                    }
                                    if (ok) ++placed; else ++failed;
                                }
                            }
                        }
                        response = {{"success", true}, {"placed", placed}, {"failed", failed},
                                    {"volume", volume}, {"hollow", hollow},
                                    {"min", {{"x", minX}, {"y", minY}, {"z", minZ}}},
                                    {"max", {{"x", maxX}, {"y", maxY}, {"z", maxZ}}}};
                        if (placed > 0 && gameEventLog) {
                            gameEventLog->emit("region_filled", {
                                {"placed", placed}, {"failed", failed}, {"volume", volume},
                                {"material", material.empty() ? "Default" : material},
                                {"hollow", hollow},
                                {"min", {{"x", minX}, {"y", minY}, {"z", minZ}}},
                                {"max", {{"x", maxX}, {"y", maxY}, {"z", maxZ}}}
                            });
                        }
                    }
                }

            } else if (cmd.action == "list_materials") {
                static Physics::MaterialManager matMgr;
                auto names = matMgr.getAllMaterialNames();
                nlohmann::json matArr = nlohmann::json::array();
                for (const auto& name : names) {
                    const auto& mat = matMgr.getMaterial(name);
                    matArr.push_back({
                        {"name", name},
                        {"mass", mat.mass},
                        {"friction", mat.friction},
                        {"restitution", mat.restitution},
                        {"colorTint", {{"r", mat.colorTint.r}, {"g", mat.colorTint.g}, {"b", mat.colorTint.b}}},
                        {"metallic", mat.metallic},
                        {"roughness", mat.roughness}
                    });
                }
                response = {{"materials", matArr}};

            } else if (cmd.action == "get_chunk_info") {
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else {
                    size_t chunkCount = chunkManager->chunks.size();
                    auto stats = chunkManager->getPerformanceStats();

                    // Collect chunk positions and bounds
                    nlohmann::json chunkArr = nlohmann::json::array();
                    for (const auto& chunk : chunkManager->chunks) {
                        if (!chunk) continue;
                        auto pos = chunk->getWorldOrigin();
                        auto cubeCount = chunk->getCubeCount();
                        chunkArr.push_back({
                            {"position", {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}}},
                            {"cubeCount", cubeCount}
                        });
                    }

                    response = {
                        {"chunkCount", chunkCount},
                        {"chunks", chunkArr},
                        {"stats", {
                            {"totalVertices", stats.totalVertices},
                            {"totalVisibleFaces", stats.totalVisibleFaces},
                            {"totalHiddenFaces", stats.totalHiddenFaces},
                            {"fullyOccludedCubes", stats.fullyOccludedCubes}
                        }}
                    };
                }

            } else if (cmd.action == "clear_region") {
                // Clear (remove) all voxels in a 3D box
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else {
                    int x1 = cmd.params.value("x1", 0);
                    int y1 = cmd.params.value("y1", 0);
                    int z1 = cmd.params.value("z1", 0);
                    int x2 = cmd.params.value("x2", 0);
                    int y2 = cmd.params.value("y2", 0);
                    int z2 = cmd.params.value("z2", 0);

                    int minX = std::min(x1, x2), maxX = std::max(x1, x2);
                    int minY = std::min(y1, y2), maxY = std::max(y1, y2);
                    int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);

                    int64_t volume = (int64_t)(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
                    if (volume > 100000) {
                        response = {{"error", "Region too large"},
                                    {"volume", volume}, {"max_volume", 100000}};
                    } else {
                        int removed = 0, skipped = 0;
                        for (int ix = minX; ix <= maxX; ++ix) {
                            for (int iy = minY; iy <= maxY; ++iy) {
                                for (int iz = minZ; iz <= maxZ; ++iz) {
                                    bool ok = chunkManager->removeCube(glm::ivec3(ix, iy, iz));
                                    if (ok) ++removed; else ++skipped;
                                }
                            }
                        }
                        response = {{"success", true}, {"removed", removed}, {"skipped", skipped},
                                    {"volume", volume},
                                    {"min", {{"x", minX}, {"y", minY}, {"z", minZ}}},
                                    {"max", {{"x", maxX}, {"y", maxY}, {"z", maxZ}}}};
                        if (removed > 0 && gameEventLog) {
                            gameEventLog->emit("region_cleared", {
                                {"removed", removed}, {"skipped", skipped}, {"volume", volume},
                                {"min", {{"x", minX}, {"y", minY}, {"z", minZ}}},
                                {"max", {{"x", maxX}, {"y", maxY}, {"z", maxZ}}}
                            });
                        }
                    }
                }

            } else if (cmd.action == "save_world") {
                // Save world chunks to SQLite database
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else {
                    bool saveAll = cmd.params.value("all", false);
                    bool ok = false;
                    if (saveAll) {
                        ok = chunkManager->saveAllChunks();
                    } else {
                        ok = chunkManager->saveDirtyChunks();
                    }
                    response = {{"success", ok},
                                {"mode", saveAll ? "all" : "dirty"}};
                    if (ok) {
                        LOG_INFO("Application", "World saved via API (mode: {})", saveAll ? "all" : "dirty");
                        if (gameEventLog) {
                            gameEventLog->emit("world_saved", {{"mode", saveAll ? "all" : "dirty"}});
                        }
                    }
                }

            } else if (cmd.action == "update_entity") {
                std::string id = cmd.params.value("id", "");
                if (id.empty() || !entityRegistry) {
                    response = {{"error", "Entity ID required"}};
                } else {
                    auto* entity = entityRegistry->getEntity(id);
                    if (!entity) {
                        response = {{"error", "Entity not found: " + id}};
                    } else {
                        if (cmd.params.contains("position")) {
                            float x = cmd.params["position"].value("x", 0.0f);
                            float y = cmd.params["position"].value("y", 0.0f);
                            float z = cmd.params["position"].value("z", 0.0f);
                            entity->setPosition(glm::vec3(x, y, z));
                        }
                        if (cmd.params.contains("rotation")) {
                            float w = cmd.params["rotation"].value("w", 1.0f);
                            float x = cmd.params["rotation"].value("x", 0.0f);
                            float y = cmd.params["rotation"].value("y", 0.0f);
                            float z = cmd.params["rotation"].value("z", 0.0f);
                            entity->setRotation(glm::quat(w, x, y, z));
                        }
                        if (cmd.params.contains("scale")) {
                            float sx = cmd.params["scale"].value("x", 1.0f);
                            float sy = cmd.params["scale"].value("y", 1.0f);
                            float sz = cmd.params["scale"].value("z", 1.0f);
                            entity->setScale(glm::vec3(sx, sy, sz));
                        }
                        if (cmd.params.contains("debugColor")) {
                            float r = cmd.params["debugColor"].value("r", 1.0f);
                            float g = cmd.params["debugColor"].value("g", 1.0f);
                            float b = cmd.params["debugColor"].value("b", 1.0f);
                            float a = cmd.params["debugColor"].value("a", 1.0f);
                            entity->debugColor = glm::vec4(r, g, b, a);
                        }
                        response = {{"success", true}, {"id", id}};
                        if (gameEventLog) {
                            gameEventLog->emit("entity_updated", {{"id", id}});
                        }
                    }
                }

            // ============================================================
            // Health/Damage Commands
            // ============================================================
            } else if (cmd.action == "damage_entity") {
                std::string id = cmd.params.value("id", "");
                float amount = cmd.params.value("amount", 0.0f);
                std::string source = cmd.params.value("source", "");
                if (id.empty() || !entityRegistry) {
                    response = {{"error", "Entity ID required"}};
                } else {
                    auto* entity = entityRegistry->getEntity(id);
                    if (!entity) {
                        response = {{"error", "Entity not found: " + id}};
                    } else {
                        auto* health = entity->getHealthComponent();
                        if (!health) {
                            response = {{"error", "Entity has no health component: " + id}};
                        } else {
                            float actual = health->takeDamage(amount, source);
                            response = {{"success", true}, {"id", id}, {"damageDealt", actual}, {"health", health->toJson()}};
                            if (gameEventLog) {
                                gameEventLog->emit("entity_damaged", {{"id", id}, {"amount", actual}, {"source", source}, {"alive", health->isAlive()}});
                            }
                        }
                    }
                }

            } else if (cmd.action == "heal_entity") {
                std::string id = cmd.params.value("id", "");
                float amount = cmd.params.value("amount", 0.0f);
                if (id.empty() || !entityRegistry) {
                    response = {{"error", "Entity ID required"}};
                } else {
                    auto* entity = entityRegistry->getEntity(id);
                    if (!entity) {
                        response = {{"error", "Entity not found: " + id}};
                    } else {
                        auto* health = entity->getHealthComponent();
                        if (!health) {
                            response = {{"error", "Entity has no health component: " + id}};
                        } else {
                            float actual = health->heal(amount);
                            response = {{"success", true}, {"id", id}, {"healed", actual}, {"health", health->toJson()}};
                            if (gameEventLog) {
                                gameEventLog->emit("entity_healed", {{"id", id}, {"amount", actual}});
                            }
                        }
                    }
                }

            } else if (cmd.action == "set_entity_health") {
                std::string id = cmd.params.value("id", "");
                if (id.empty() || !entityRegistry) {
                    response = {{"error", "Entity ID required"}};
                } else {
                    auto* entity = entityRegistry->getEntity(id);
                    if (!entity) {
                        response = {{"error", "Entity not found: " + id}};
                    } else {
                        auto* health = entity->getHealthComponent();
                        if (!health) {
                            response = {{"error", "Entity has no health component: " + id}};
                        } else {
                            if (cmd.params.contains("maxHealth")) {
                                health->setMaxHealth(cmd.params["maxHealth"].get<float>());
                            }
                            if (cmd.params.contains("health")) {
                                health->setHealth(cmd.params["health"].get<float>());
                            }
                            if (cmd.params.contains("invulnerable")) {
                                health->setInvulnerable(cmd.params["invulnerable"].get<bool>());
                            }
                            response = {{"success", true}, {"id", id}, {"health", health->toJson()}};
                        }
                    }
                }

            } else if (cmd.action == "kill_entity") {
                std::string id = cmd.params.value("id", "");
                if (id.empty() || !entityRegistry) {
                    response = {{"error", "Entity ID required"}};
                } else {
                    auto* entity = entityRegistry->getEntity(id);
                    if (!entity) {
                        response = {{"error", "Entity not found: " + id}};
                    } else {
                        auto* health = entity->getHealthComponent();
                        if (!health) {
                            response = {{"error", "Entity has no health component: " + id}};
                        } else {
                            health->kill();
                            response = {{"success", true}, {"id", id}, {"health", health->toJson()}};
                            if (gameEventLog) {
                                gameEventLog->emit("entity_killed", {{"id", id}});
                            }
                        }
                    }
                }

            } else if (cmd.action == "revive_entity") {
                std::string id = cmd.params.value("id", "");
                float healthPercent = cmd.params.value("healthPercent", 1.0f);
                if (id.empty() || !entityRegistry) {
                    response = {{"error", "Entity ID required"}};
                } else {
                    auto* entity = entityRegistry->getEntity(id);
                    if (!entity) {
                        response = {{"error", "Entity not found: " + id}};
                    } else {
                        auto* health = entity->getHealthComponent();
                        if (!health) {
                            response = {{"error", "Entity has no health component: " + id}};
                        } else {
                            health->revive(healthPercent);
                            response = {{"success", true}, {"id", id}, {"health", health->toJson()}};
                            if (gameEventLog) {
                                gameEventLog->emit("entity_revived", {{"id", id}});
                            }
                        }
                    }
                }

            } else if (cmd.action == "remove_voxel") {
                int x = cmd.params.value("x", 0);
                int y = cmd.params.value("y", 0);
                int z = cmd.params.value("z", 0);
                if (chunkManager) {
                    bool ok = chunkManager->removeCube(glm::ivec3(x, y, z));
                    response = {{"success", ok}, {"position", {{"x", x}, {"y", y}, {"z", z}}}};
                    if (ok && gameEventLog) {
                        gameEventLog->emit("voxel_removed", {{"x", x}, {"y", y}, {"z", z}});
                    }
                } else {
                    response = {{"error", "ChunkManager not available"}};
                }

            } else if (cmd.action == "place_voxels_batch") {
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else if (!cmd.params.contains("voxels")) {
                    response = {{"error", "Missing 'voxels' array"}};
                } else {
                    int placed = 0;
                    int failed = 0;
                    for (const auto& v : cmd.params["voxels"]) {
                        int x = v.value("x", 0);
                        int y = v.value("y", 0);
                        int z = v.value("z", 0);
                        std::string material = v.value("material", "");
                        bool ok = false;
                        if (!material.empty()) {
                            ok = chunkManager->m_voxelModificationSystem.addCubeWithMaterial(
                                glm::ivec3(x, y, z), material);
                        } else {
                            ok = chunkManager->addCube(glm::ivec3(x, y, z));
                        }
                        if (ok) ++placed; else ++failed;
                    }
                    response = {{"success", true}, {"placed", placed}, {"failed", failed}};
                }

            } else if (cmd.action == "spawn_template") {
                std::string name = cmd.params.value("name", "");
                if (name.empty()) {
                    response = {{"error", "Template name required"}};
                } else if (!objectTemplateManager) {
                    response = {{"error", "ObjectTemplateManager not available"}};
                } else {
                    float x = 0, y = 0, z = 0;
                    if (cmd.params.contains("position")) {
                        x = cmd.params["position"].value("x", 0.0f);
                        y = cmd.params["position"].value("y", 0.0f);
                        z = cmd.params["position"].value("z", 0.0f);
                    }
                    bool isStatic = cmd.params.value("static", true);
                    bool ok = objectTemplateManager->spawnTemplate(name, glm::vec3(x, y, z), isStatic);
                    response = {{"success", ok}, {"template", name},
                                {"position", {{"x", x}, {"y", y}, {"z", z}}}};
                }

            } else if (cmd.action == "list_templates") {
                if (objectTemplateManager) {
                    auto names = objectTemplateManager->getTemplateNames();
                    response = {{"templates", names}};
                } else {
                    response = {{"templates", nlohmann::json::array()}};
                }

            } else if (cmd.action == "set_camera") {
                if (cmd.params.contains("position") && inputManager) {
                    float x = cmd.params["position"].value("x", 0.0f);
                    float y = cmd.params["position"].value("y", 0.0f);
                    float z = cmd.params["position"].value("z", 0.0f);
                    inputManager->setCameraPosition(glm::vec3(x, y, z));
                }
                if (cmd.params.contains("yaw") && inputManager) {
                    float yaw = cmd.params.value("yaw", 0.0f);
                    float pitch = cmd.params.value("pitch", 0.0f);
                    inputManager->setYawPitch(yaw, pitch);
                }
                response = {{"success", true}};

            } else if (cmd.action == "create_camera_slot") {
                if (!cameraManager || !cmd.params.contains("name")) {
                    response = {{"error", "Missing name or CameraManager not available"}};
                } else {
                    Graphics::CameraSlot slot;
                    slot.name = cmd.params.value("name", "");
                    if (cmd.params.contains("position")) {
                        slot.position.x = cmd.params["position"].value("x", 0.0f);
                        slot.position.y = cmd.params["position"].value("y", 0.0f);
                        slot.position.z = cmd.params["position"].value("z", 0.0f);
                    }
                    slot.yaw = cmd.params.value("yaw", -90.0f);
                    slot.pitch = cmd.params.value("pitch", 0.0f);
                    int idx = cameraManager->createSlot(slot);
                    response = {{"success", idx >= 0}, {"index", idx}};
                }

            } else if (cmd.action == "set_active_camera_slot") {
                if (!cameraManager || !cmd.params.contains("name")) {
                    response = {{"error", "Missing name or CameraManager not available"}};
                } else {
                    std::string name = cmd.params.value("name", "");
                    float duration = cmd.params.value("transition_duration", 0.0f);
                    bool ok = (duration > 0.0f)
                        ? cameraManager->transitionToSlot(name, duration)
                        : cameraManager->setActiveSlot(name);
                    response = {{"success", ok}};
                }

            } else if (cmd.action == "follow_entity_camera") {
                if (!cameraManager || !cmd.params.contains("slot") || !cmd.params.contains("entity_id")) {
                    response = {{"error", "Missing slot/entity_id or CameraManager not available"}};
                } else {
                    std::string slotName = cmd.params.value("slot", "");
                    std::string entityId = cmd.params.value("entity_id", "");
                    float distance = cmd.params.value("distance", 5.0f);
                    float height = cmd.params.value("height", 1.5f);
                    bool ok = cameraManager->followEntity(slotName, entityId, distance, height);
                    response = {{"success", ok}};
                }

            } else if (cmd.action == "capture_screenshot") {
                // Capture the current frame via RenderCoordinator
                if (!renderCoordinator) {
                    response = {{"error", "RenderCoordinator not available"}};
                } else {
                    auto pixels = renderCoordinator->captureScreenshot();
                    if (pixels.empty()) {
                        response = {{"error", "Screenshot capture failed"}};
                    } else {
                        auto extent = vulkanDevice->getSwapChainExtent();
                        uint32_t w = extent.width;
                        uint32_t h = extent.height;

                        // Ensure screenshots directory exists
                        std::string screenshotDir = "screenshots";
                        #ifdef _WIN32
                        _mkdir(screenshotDir.c_str());
                        #else
                        mkdir(screenshotDir.c_str(), 0755);
                        #endif

                        // Generate timestamped filename
                        auto now = std::chrono::system_clock::now();
                        auto time = std::chrono::system_clock::to_time_t(now);
                        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()) % 1000;
                        std::tm tm_buf;
                        #ifdef _WIN32
                        localtime_s(&tm_buf, &time);
                        #else
                        localtime_r(&time, &tm_buf);
                        #endif
                        std::ostringstream oss;
                        oss << screenshotDir << "/screenshot_"
                            << std::put_time(&tm_buf, "%Y%m%d_%H%M%S")
                            << "_" << std::setfill('0') << std::setw(3) << ms.count()
                            << ".png";
                        std::string filepath = oss.str();

                        // Write PNG using stbi_write_png (writes to file)
                        int ok = stbi_write_png(filepath.c_str(), w, h, 4, pixels.data(), w * 4);
                        if (ok) {
                            response = {
                                {"success", true},
                                {"width", w},
                                {"height", h},
                                {"format", "png"},
                                {"path", filepath},
                                {"size_pixels", w * h}
                            };
                            LOG_INFO("Application", "Screenshot saved to {}", filepath);
                        } else {
                            response = {{"error", "Failed to write PNG file"}};
                        }
                    }
                }

            } else if (cmd.action == "run_script") {
                std::string code = cmd.params.value("code", "");
                if (code.empty()) {
                    response = {{"error", "Missing 'code' parameter"}};
                } else if (!scriptingSystem) {
                    response = {{"error", "ScriptingSystem not available"}};
                } else {
                    scriptingSystem->runCommand(code);
                    response = {{"success", true}, {"executed", code}};
                }

            // ================================================================
            // SNAPSHOT COMMANDS
            // ================================================================
            } else if (cmd.action == "create_snapshot") {
                if (!chunkManager || !snapshotManager) {
                    response = {{"error", "ChunkManager or SnapshotManager not available"}};
                } else {
                    std::string name = cmd.params.value("name", "");
                    if (name.empty()) {
                        response = {{"error", "Snapshot name required"}};
                    } else {
                        int x1 = cmd.params.value("x1", 0), y1 = cmd.params.value("y1", 0), z1 = cmd.params.value("z1", 0);
                        int x2 = cmd.params.value("x2", 0), y2 = cmd.params.value("y2", 0), z2 = cmd.params.value("z2", 0);
                        int minX = std::min(x1, x2), maxX = std::max(x1, x2);
                        int minY = std::min(y1, y2), maxY = std::max(y1, y2);
                        int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);
                        int64_t volume = (int64_t)(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
                        if (volume > Core::SnapshotManager::MAX_VOLUME) {
                            response = {{"error", "Region too large"}, {"volume", volume},
                                        {"max_volume", Core::SnapshotManager::MAX_VOLUME}};
                        } else {
                            Core::RegionSnapshot snap;
                            snap.name = name;
                            snap.min = glm::ivec3(minX, minY, minZ);
                            snap.max = glm::ivec3(maxX, maxY, maxZ);
                            snap.size = snap.max - snap.min + glm::ivec3(1);
                            snap.totalVolume = volume;
                            snap.createdAt = std::chrono::system_clock::now();
                            for (int ix = minX; ix <= maxX; ++ix) {
                                for (int iy = minY; iy <= maxY; ++iy) {
                                    for (int iz = minZ; iz <= maxZ; ++iz) {
                                        auto* cube = chunkManager->getCubeAt(glm::ivec3(ix, iy, iz));
                                        if (cube && cube->isVisible()) {
                                            Core::VoxelEntry entry;
                                            entry.offset = glm::ivec3(ix - minX, iy - minY, iz - minZ);
                                            entry.material = cube->getMaterialName();
                                            snap.voxels.push_back(entry);
                                        }
                                    }
                                }
                            }
                            bool ok = snapshotManager->addSnapshot(snap);
                            if (ok) {
                                response = {{"success", true}, {"name", name},
                                            {"voxel_count", snap.voxels.size()}, {"volume", volume},
                                            {"min", {{"x", minX}, {"y", minY}, {"z", minZ}}},
                                            {"max", {{"x", maxX}, {"y", maxY}, {"z", maxZ}}}};
                                if (gameEventLog) {
                                    gameEventLog->emit("snapshot_created", {
                                        {"name", name}, {"voxel_count", snap.voxels.size()}, {"volume", volume}});
                                }
                            } else {
                                response = {{"error", "Snapshot name already exists or invalid"}, {"name", name}};
                            }
                        }
                    }
                }

            } else if (cmd.action == "restore_snapshot") {
                if (!chunkManager || !snapshotManager) {
                    response = {{"error", "ChunkManager or SnapshotManager not available"}};
                } else {
                    std::string name = cmd.params.value("name", "");
                    if (name.empty()) {
                        response = {{"error", "Snapshot name required"}};
                    } else {
                        const auto* snap = snapshotManager->getSnapshot(name);
                        if (!snap) {
                            response = {{"error", "Snapshot not found"}, {"name", name}};
                        } else {
                            // Clear the original region first
                            int cleared = 0;
                            for (int ix = snap->min.x; ix <= snap->max.x; ++ix) {
                                for (int iy = snap->min.y; iy <= snap->max.y; ++iy) {
                                    for (int iz = snap->min.z; iz <= snap->max.z; ++iz) {
                                        if (chunkManager->removeCube(glm::ivec3(ix, iy, iz)))
                                            ++cleared;
                                    }
                                }
                            }
                            // Restore voxels from snapshot
                            int placed = 0, failed = 0;
                            for (const auto& v : snap->voxels) {
                                glm::ivec3 worldPos = snap->min + v.offset;
                                bool ok = chunkManager->m_voxelModificationSystem.addCubeWithMaterial(
                                    worldPos, v.material);
                                if (ok) ++placed; else ++failed;
                            }
                            response = {{"success", true}, {"name", name},
                                        {"cleared", cleared}, {"placed", placed}, {"failed", failed}};
                            if (gameEventLog) {
                                gameEventLog->emit("snapshot_restored", {
                                    {"name", name}, {"cleared", cleared}, {"placed", placed}});
                            }
                        }
                    }
                }

            } else if (cmd.action == "delete_snapshot") {
                if (!snapshotManager) {
                    response = {{"error", "SnapshotManager not available"}};
                } else {
                    std::string name = cmd.params.value("name", "");
                    if (name.empty()) {
                        response = {{"error", "Snapshot name required"}};
                    } else {
                        bool ok = snapshotManager->deleteSnapshot(name);
                        response = {{"success", ok}, {"name", name}};
                    }
                }

            // ================================================================
            // CLIPBOARD COMMANDS (copy / paste)
            // ================================================================
            } else if (cmd.action == "copy_region") {
                if (!chunkManager || !snapshotManager) {
                    response = {{"error", "ChunkManager or SnapshotManager not available"}};
                } else {
                    int x1 = cmd.params.value("x1", 0), y1 = cmd.params.value("y1", 0), z1 = cmd.params.value("z1", 0);
                    int x2 = cmd.params.value("x2", 0), y2 = cmd.params.value("y2", 0), z2 = cmd.params.value("z2", 0);
                    int minX = std::min(x1, x2), maxX = std::max(x1, x2);
                    int minY = std::min(y1, y2), maxY = std::max(y1, y2);
                    int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);
                    int64_t volume = (int64_t)(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
                    if (volume > Core::SnapshotManager::MAX_VOLUME) {
                        response = {{"error", "Region too large"}, {"volume", volume},
                                    {"max_volume", Core::SnapshotManager::MAX_VOLUME}};
                    } else {
                        Core::RegionSnapshot clip;
                        clip.name = "__clipboard__";
                        clip.min = glm::ivec3(minX, minY, minZ);
                        clip.max = glm::ivec3(maxX, maxY, maxZ);
                        clip.size = clip.max - clip.min + glm::ivec3(1);
                        clip.totalVolume = volume;
                        clip.createdAt = std::chrono::system_clock::now();
                        for (int ix = minX; ix <= maxX; ++ix) {
                            for (int iy = minY; iy <= maxY; ++iy) {
                                for (int iz = minZ; iz <= maxZ; ++iz) {
                                    auto* cube = chunkManager->getCubeAt(glm::ivec3(ix, iy, iz));
                                    if (cube && cube->isVisible()) {
                                        Core::VoxelEntry entry;
                                        entry.offset = glm::ivec3(ix - minX, iy - minY, iz - minZ);
                                        entry.material = cube->getMaterialName();
                                        clip.voxels.push_back(entry);
                                    }
                                }
                            }
                        }
                        snapshotManager->setClipboard(clip);
                        response = {{"success", true}, {"voxel_count", clip.voxels.size()},
                                    {"size", {{"x", clip.size.x}, {"y", clip.size.y}, {"z", clip.size.z}}},
                                    {"volume", volume}};
                    }
                }

            } else if (cmd.action == "paste_region") {
                if (!chunkManager || !snapshotManager) {
                    response = {{"error", "ChunkManager or SnapshotManager not available"}};
                } else if (!snapshotManager->hasClipboard()) {
                    response = {{"error", "Clipboard is empty — use copy_region first"}};
                } else {
                    int px = cmd.params.value("x", 0);
                    int py = cmd.params.value("y", 0);
                    int pz = cmd.params.value("z", 0);
                    int rotate = cmd.params.value("rotate", 0);

                    // Make a copy so we can rotate without affecting stored clipboard
                    Core::RegionSnapshot clip = *snapshotManager->getClipboard();

                    // Apply rotation (90° increments around Y)
                    int rotSteps = ((rotate % 360) + 360) % 360 / 90;
                    for (int r = 0; r < rotSteps; ++r) {
                        Core::SnapshotManager::rotateY90(clip);
                    }

                    int placed = 0, failed = 0;
                    for (const auto& v : clip.voxels) {
                        glm::ivec3 worldPos = glm::ivec3(px, py, pz) + v.offset;
                        bool ok = chunkManager->m_voxelModificationSystem.addCubeWithMaterial(
                            worldPos, v.material);
                        if (ok) ++placed; else ++failed;
                    }
                    response = {{"success", true}, {"placed", placed}, {"failed", failed},
                                {"position", {{"x", px}, {"y", py}, {"z", pz}}},
                                {"rotation", rotate},
                                {"size", {{"x", clip.size.x}, {"y", clip.size.y}, {"z", clip.size.z}}}};
                    if (placed > 0 && gameEventLog) {
                        gameEventLog->emit("region_pasted", {
                            {"placed", placed}, {"failed", failed},
                            {"position", {{"x", px}, {"y", py}, {"z", pz}}},
                            {"rotation", rotate}});
                    }
                }

            // ================================================================
            // WORLD GENERATION
            // ================================================================
            } else if (cmd.action == "generate_world") {
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else {
                    std::string typeStr = cmd.params.value("type", "Perlin");
                    uint32_t seed = cmd.params.value("seed", 0u);

                    // Map string to GenerationType
                    WorldGenerator::GenerationType genType = WorldGenerator::GenerationType::Perlin;
                    if (typeStr == "Random") genType = WorldGenerator::GenerationType::Random;
                    else if (typeStr == "Perlin") genType = WorldGenerator::GenerationType::Perlin;
                    else if (typeStr == "Flat") genType = WorldGenerator::GenerationType::Flat;
                    else if (typeStr == "Mountains") genType = WorldGenerator::GenerationType::Mountains;
                    else if (typeStr == "Caves") genType = WorldGenerator::GenerationType::Caves;
                    else if (typeStr == "City") genType = WorldGenerator::GenerationType::City;
                    else {
                        response = {{"error", "Unknown generation type: " + typeStr},
                                    {"valid_types", {"Random", "Perlin", "Flat", "Mountains", "Caves", "City"}}};
                        goto done_generate;
                    }

                    {
                        WorldGenerator generator(genType, seed);

                        // Apply optional terrain params
                        if (cmd.params.contains("params")) {
                            auto& tp = generator.getTerrainParams();
                            auto& p = cmd.params["params"];
                            if (p.contains("heightScale")) tp.heightScale = p["heightScale"].get<float>();
                            if (p.contains("frequency")) tp.frequency = p["frequency"].get<float>();
                            if (p.contains("octaves")) tp.octaves = p["octaves"].get<int>();
                            if (p.contains("persistence")) tp.persistence = p["persistence"].get<float>();
                            if (p.contains("lacunarity")) tp.lacunarity = p["lacunarity"].get<float>();
                            if (p.contains("caveThreshold")) tp.caveThreshold = p["caveThreshold"].get<float>();
                            if (p.contains("stoneLevel")) tp.stoneLevel = p["stoneLevel"].get<float>();
                        }

                        // Collect chunk coords: either explicit list or from/to range
                        std::vector<glm::ivec3> chunkCoords;

                        if (cmd.params.contains("chunks")) {
                            for (const auto& c : cmd.params["chunks"]) {
                                chunkCoords.push_back(glm::ivec3(
                                    c.value("x", 0), c.value("y", 0), c.value("z", 0)));
                            }
                        } else if (cmd.params.contains("from") && cmd.params.contains("to")) {
                            auto& f = cmd.params["from"];
                            auto& t = cmd.params["to"];
                            int fx = f.value("x", 0), fy = f.value("y", 0), fz = f.value("z", 0);
                            int tx = t.value("x", 0), ty = t.value("y", 0), tz = t.value("z", 0);
                            int minCx = std::min(fx, tx), maxCx = std::max(fx, tx);
                            int minCy = std::min(fy, ty), maxCy = std::max(fy, ty);
                            int minCz = std::min(fz, tz), maxCz = std::max(fz, tz);
                            for (int cx = minCx; cx <= maxCx; ++cx)
                                for (int cy = minCy; cy <= maxCy; ++cy)
                                    for (int cz = minCz; cz <= maxCz; ++cz)
                                        chunkCoords.push_back(glm::ivec3(cx, cy, cz));
                        } else {
                            // Default: single chunk at origin
                            chunkCoords.push_back(glm::ivec3(0, 0, 0));
                        }

                        // Limit to prevent stalling
                        if (chunkCoords.size() > 64) {
                            response = {{"error", "Too many chunks"}, {"count", chunkCoords.size()}, {"max", 64}};
                            goto done_generate;
                        }

                        int generated = 0;
                        std::unordered_set<Chunk*> modifiedChunks;
                        for (const auto& cc : chunkCoords) {
                            glm::ivec3 origin = cc * 32;
                            // Create chunk if it doesn't exist
                            if (!chunkManager->getChunkAtCoord(cc)) {
                                chunkManager->createChunk(origin, false);
                            }
                            Chunk* chunk = chunkManager->getChunkAtCoord(cc);
                            if (chunk) {
                                generator.generateChunk(*chunk, cc);
                                modifiedChunks.insert(chunk);
                                ++generated;
                            }
                        }

                        // Rebuild faces and GPU buffers for all modified chunks
                        for (Chunk* chunk : modifiedChunks) {
                            chunk->rebuildFaces();
                            chunk->updateVulkanBuffer();
                        }

                        response = {{"success", true}, {"type", typeStr}, {"seed", seed},
                                    {"chunks_generated", generated}};
                        if (gameEventLog) {
                            gameEventLog->emit("world_generated", {
                                {"type", typeStr}, {"seed", seed}, {"chunks", generated}});
                        }
                    }
                }
                done_generate:;

            // ================================================================
            // SAVE TEMPLATE (region → .txt file)
            // ================================================================
            } else if (cmd.action == "save_template") {
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else {
                    std::string name = cmd.params.value("name", "");
                    if (name.empty()) {
                        response = {{"error", "Template name required"}};
                    } else {
                        int x1 = cmd.params.value("x1", 0), y1 = cmd.params.value("y1", 0), z1 = cmd.params.value("z1", 0);
                        int x2 = cmd.params.value("x2", 0), y2 = cmd.params.value("y2", 0), z2 = cmd.params.value("z2", 0);
                        int minX = std::min(x1, x2), maxX = std::max(x1, x2);
                        int minY = std::min(y1, y2), maxY = std::max(y1, y2);
                        int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);
                        int64_t volume = (int64_t)(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);

                        if (volume > 100000) {
                            response = {{"error", "Region too large"}, {"volume", volume}, {"max_volume", 100000}};
                        } else {
                            // Scan region and build template lines
                            std::vector<std::string> lines;
                            lines.push_back("# Template: " + name);
                            lines.push_back("# Generated from region (" +
                                std::to_string(minX) + "," + std::to_string(minY) + "," + std::to_string(minZ) + ") to (" +
                                std::to_string(maxX) + "," + std::to_string(maxY) + "," + std::to_string(maxZ) + ")");

                            int cubeCount = 0;
                            for (int ix = minX; ix <= maxX; ++ix) {
                                for (int iy = minY; iy <= maxY; ++iy) {
                                    for (int iz = minZ; iz <= maxZ; ++iz) {
                                        auto* cube = chunkManager->getCubeAt(glm::ivec3(ix, iy, iz));
                                        if (cube && cube->isVisible()) {
                                            int rx = ix - minX, ry = iy - minY, rz = iz - minZ;
                                            std::string mat = cube->getMaterialName();
                                            if (mat.empty()) mat = "Default";
                                            lines.push_back("C " + std::to_string(rx) + " " +
                                                std::to_string(ry) + " " + std::to_string(rz) + " " + mat);
                                            ++cubeCount;
                                        }
                                    }
                                }
                            }

                            // Write to templates dir/<name>.txt
                            auto& assets = Core::AssetManager::instance();
                            std::string filepath = assets.resolveTemplate(name + ".txt");
                            std::ofstream file(filepath);
                            if (file.is_open()) {
                                for (const auto& line : lines) {
                                    file << line << "\n";
                                }
                                file.close();

                                // Reload into ObjectTemplateManager so it's immediately usable
                                if (objectTemplateManager) {
                                    objectTemplateManager->loadTemplate(filepath);
                                }

                                response = {{"success", true}, {"name", name}, {"path", filepath},
                                            {"voxel_count", cubeCount}};
                                LOG_INFO("Application", "Template saved: {} ({} cubes)", name, cubeCount);
                                if (gameEventLog) {
                                    gameEventLog->emit("template_saved", {
                                        {"name", name}, {"voxel_count", cubeCount}, {"path", filepath}});
                                }
                            } else {
                                response = {{"error", "Failed to write file: " + filepath}};
                            }
                        }
                    }
                }

            // ================================================================
            // NPC COMMANDS
            // ================================================================
            } else if (cmd.action == "spawn_npc") {
                if (!npcManager) {
                    response = {{"error", "NPCManager not available"}};
                } else {
                    std::string name = cmd.params.value("name", "");
                    if (name.empty()) {
                        response = {{"error", "NPC name required"}};
                    } else {
                        std::string animFile = cmd.params.value("animFile", "character.anim");
                        float x = 0, y = 20, z = 0;
                        if (cmd.params.contains("position")) {
                            x = cmd.params["position"].value("x", 0.0f);
                            y = cmd.params["position"].value("y", 20.0f);
                            z = cmd.params["position"].value("z", 0.0f);
                        } else {
                            x = cmd.params.value("x", 0.0f);
                            y = cmd.params.value("y", 20.0f);
                            z = cmd.params.value("z", 0.0f);
                        }
                        std::string behaviorStr = cmd.params.value("behavior", "idle");
                        Core::NPCBehaviorType behaviorType = Core::NPCBehaviorType::Idle;
                        std::vector<glm::vec3> waypoints;
                        float walkSpeed = cmd.params.value("walkSpeed", 2.0f);
                        float waitTime = cmd.params.value("waitTime", 2.0f);

                        if (behaviorStr == "patrol") {
                            behaviorType = Core::NPCBehaviorType::Patrol;
                            if (cmd.params.contains("waypoints")) {
                                for (const auto& wp : cmd.params["waypoints"]) {
                                    waypoints.emplace_back(
                                        wp.value("x", 0.0f),
                                        wp.value("y", 0.0f),
                                        wp.value("z", 0.0f));
                                }
                            }
                        }

                        auto* npc = npcManager->spawnNPC(name, animFile, glm::vec3(x, y, z),
                                                          behaviorType, waypoints, walkSpeed, waitTime);
                        if (npc) {
                            response = {{"success", true}, {"name", name}, {"behavior", behaviorStr},
                                        {"position", {{"x", x}, {"y", y}, {"z", z}}}};
                            if (gameEventLog) {
                                gameEventLog->emit("npc_spawned", {
                                    {"name", name}, {"behavior", behaviorStr},
                                    {"position", {{"x", x}, {"y", y}, {"z", z}}}
                                });
                            }
                        } else {
                            response = {{"error", "Failed to spawn NPC (name may already exist)"}};
                        }
                    }
                }

            } else if (cmd.action == "remove_npc") {
                if (!npcManager) {
                    response = {{"error", "NPCManager not available"}};
                } else {
                    std::string name = cmd.params.value("name", "");
                    if (name.empty()) {
                        response = {{"error", "NPC name required"}};
                    } else {
                        bool ok = npcManager->removeNPC(name);
                        response = {{"success", ok}};
                        if (ok && gameEventLog) {
                            gameEventLog->emit("npc_removed", {{"name", name}});
                        }
                    }
                }

            } else if (cmd.action == "list_npcs") {
                if (!npcManager) {
                    response = {{"error", "NPCManager not available"}};
                } else {
                    auto names = npcManager->getAllNPCNames();
                    nlohmann::json arr = nlohmann::json::array();
                    for (const auto& n : names) {
                        auto* npc = npcManager->getNPC(n);
                        if (npc) {
                            auto pos = npc->getPosition();
                            nlohmann::json entry = {
                                {"name", n},
                                {"position", {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}}},
                                {"interactionRadius", npc->getInteractionRadius()}
                            };
                            if (npc->getBehavior()) {
                                entry["behavior"] = npc->getBehavior()->getBehaviorName();
                            }
                            arr.push_back(entry);
                        }
                    }
                    response = {{"success", true}, {"npcs", arr}, {"count", names.size()}};
                }

            } else if (cmd.action == "set_npc_behavior") {
                if (!npcManager) {
                    response = {{"error", "NPCManager not available"}};
                } else {
                    std::string name = cmd.params.value("name", "");
                    std::string behaviorStr = cmd.params.value("behavior", "");
                    if (name.empty() || behaviorStr.empty()) {
                        response = {{"error", "NPC name and behavior required"}};
                    } else {
                        auto* npc = npcManager->getNPC(name);
                        if (!npc) {
                            response = {{"error", "NPC not found: " + name}};
                        } else {
                            std::unique_ptr<Scene::NPCBehavior> behavior;
                            if (behaviorStr == "idle") {
                                behavior = std::make_unique<Scene::IdleBehavior>();
                            } else if (behaviorStr == "patrol") {
                                std::vector<glm::vec3> waypoints;
                                if (cmd.params.contains("waypoints")) {
                                    for (const auto& wp : cmd.params["waypoints"]) {
                                        waypoints.emplace_back(
                                            wp.value("x", 0.0f),
                                            wp.value("y", 0.0f),
                                            wp.value("z", 0.0f));
                                    }
                                }
                                float walkSpeed = cmd.params.value("walkSpeed", 2.0f);
                                float waitTime = cmd.params.value("waitTime", 2.0f);
                                behavior = std::make_unique<Scene::PatrolBehavior>(
                                    waypoints, walkSpeed, waitTime);
                            } else {
                                response = {{"error", "Unknown behavior: " + behaviorStr}};
                                if (cmd.onComplete) cmd.onComplete(response);
                                continue;
                            }
                            npc->setBehavior(std::move(behavior));
                            response = {{"success", true}, {"name", name}, {"behavior", behaviorStr}};
                        }
                    }
                }

            // ================================================================
            // DIALOGUE COMMANDS
            // ================================================================
            } else if (cmd.action == "start_dialogue") {
                if (!dialogueSystem) {
                    response = {{"error", "DialogueSystem not available"}};
                } else if (dialogueSystem->isActive()) {
                    response = {{"error", "A conversation is already active"}};
                } else {
                    std::string npcName = cmd.params.value("npc", "");
                    if (npcName.empty() || !cmd.params.contains("tree")) {
                        response = {{"error", "Required: 'npc' (speaker name) and 'tree' (dialogue JSON)"}};
                    } else {
                        auto parsedTree = UI::DialogueTree::fromJson(cmd.params["tree"]);
                        Scene::NPCEntity* npc = npcManager ? npcManager->getNPC(npcName) : nullptr;
                        bool ok = false;
                        if (npc) {
                            // Set provider first so tree has stable ownership
                            npc->setDialogueProvider(
                                std::make_unique<UI::StaticDialogueProvider>(std::move(parsedTree)));
                            ok = dialogueSystem->startConversation(npc,
                                npc->getDialogueProvider()->getDialogueTree());
                        } else {
                            // No NPC entity — store tree in a member to keep it alive
                            m_apiDialogueTree = std::make_unique<UI::DialogueTree>(std::move(parsedTree));
                            ok = dialogueSystem->startConversation(npcName, m_apiDialogueTree.get());
                        }
                        response = {{"success", ok}};
                    }
                }

            } else if (cmd.action == "end_dialogue") {
                if (!dialogueSystem || !dialogueSystem->isActive()) {
                    response = {{"error", "No active conversation"}};
                } else {
                    dialogueSystem->endConversation();
                    response = {{"success", true}};
                }

            } else if (cmd.action == "say_bubble") {
                if (!speechBubbleManager) {
                    response = {{"error", "SpeechBubbleManager not available"}};
                } else {
                    std::string entityId = cmd.params.value("entityId", "");
                    std::string text = cmd.params.value("text", "");
                    float duration = cmd.params.value("duration", 3.0f);
                    if (entityId.empty() || text.empty()) {
                        response = {{"error", "Required: 'entityId' and 'text'"}};
                    } else {
                        speechBubbleManager->say(entityId, text, duration);
                        response = {{"success", true}};
                    }
                }

            } else if (cmd.action == "set_npc_dialogue") {
                if (!npcManager) {
                    response = {{"error", "NPCManager not available"}};
                } else {
                    std::string npcName = cmd.params.value("name", "");
                    if (npcName.empty() || !cmd.params.contains("tree")) {
                        response = {{"error", "Required: 'name' (NPC name) and 'tree' (dialogue JSON)"}};
                    } else {
                        auto* npc = npcManager->getNPC(npcName);
                        if (!npc) {
                            response = {{"error", "NPC not found: " + npcName}};
                        } else {
                            auto tree = UI::DialogueTree::fromJson(cmd.params["tree"]);
                            npc->setDialogueProvider(
                                std::make_unique<UI::StaticDialogueProvider>(std::move(tree)));
                            response = {{"success", true}, {"name", npcName}};
                        }
                    }
                }

            } else if (cmd.action == "get_dialogue_state") {
                if (!dialogueSystem) {
                    response = {{"active", false}};
                } else {
                    response = {{"active", dialogueSystem->isActive()}};
                    if (dialogueSystem->isActive()) {
                        std::string stateStr;
                        switch (dialogueSystem->getState()) {
                            case UI::DialogueState::Typing: stateStr = "typing"; break;
                            case UI::DialogueState::WaitingForInput: stateStr = "waiting_for_input"; break;
                            case UI::DialogueState::ChoiceSelection: stateStr = "choice_selection"; break;
                            default: stateStr = "inactive"; break;
                        }
                        response["state"] = stateStr;
                        response["speaker"] = dialogueSystem->getCurrentSpeaker();
                        response["text"] = dialogueSystem->getCurrentText();
                        response["revealedText"] = dialogueSystem->getRevealedText();
                        response["emotion"] = dialogueSystem->getCurrentEmotion();
                        nlohmann::json choicesArr = nlohmann::json::array();
                        for (const auto& c : dialogueSystem->getAvailableChoices()) {
                            choicesArr.push_back({{"text", c.text}, {"targetNodeId", c.targetNodeId}});
                        }
                        response["choices"] = choicesArr;
                    }
                }

            } else if (cmd.action == "advance_dialogue") {
                if (!dialogueSystem || !dialogueSystem->isActive()) {
                    response = {{"error", "No active conversation"}};
                } else {
                    dialogueSystem->advanceDialogue();
                    response = {{"success", true}};
                }

            } else if (cmd.action == "select_dialogue_choice") {
                if (!dialogueSystem || dialogueSystem->getState() != UI::DialogueState::ChoiceSelection) {
                    response = {{"error", "Not in choice selection state"}};
                } else {
                    int index = cmd.params.value("index", -1);
                    if (index < 0 || index >= static_cast<int>(dialogueSystem->getAvailableChoices().size())) {
                        response = {{"error", "Invalid choice index"}};
                    } else {
                        dialogueSystem->selectChoice(index);
                        response = {{"success", true}};
                    }
                }

            } else if (cmd.action == "load_dialogue_file") {
                std::string filename = cmd.params.value("filename", "");
                std::string npcName = cmd.params.value("npc", "");
                if (filename.empty()) {
                    response = {{"error", "Required: 'filename'"}};
                } else {
                    std::string fullPath = Core::AssetManager::instance().resolveDialogue(filename);
                    auto tree = UI::loadDialogueFile(fullPath);
                    if (!tree) {
                        response = {{"error", "Failed to load dialogue file: " + filename}};
                    } else if (!npcName.empty() && npcManager) {
                        auto* npc = npcManager->getNPC(npcName);
                        if (!npc) {
                            response = {{"error", "NPC not found: " + npcName}};
                        } else {
                            npc->setDialogueProvider(
                                std::make_unique<UI::StaticDialogueProvider>(std::move(*tree)));
                            response = {{"success", true}, {"npc", npcName}, {"file", filename}};
                        }
                    } else {
                        response = {{"success", true}, {"tree", tree->toJson()}};
                    }
                }

            } else if (cmd.action == "list_dialogue_files") {
                auto files = UI::listDialogueFiles(Core::AssetManager::instance().dialoguesDir());
                response = {{"files", files}};

            // ================================================================
            // Story System Commands
            // ================================================================
            } else if (cmd.action == "story_load_world") {
                if (!storyEngine) {
                    response = {{"error", "Story engine not initialized"}};
                } else {
                    nlohmann::json def = cmd.params.value("definition", cmd.params);
                    std::string err;
                    if (Story::StoryWorldLoader::loadFromJson(def, *storyEngine, &err)) {
                        response = {{"success", true}, {"message", "World definition loaded"}};
                    } else {
                        response = {{"error", err}};
                    }
                }

            } else if (cmd.action == "story_add_character") {
                if (!storyEngine) {
                    response = {{"error", "Story engine not initialized"}};
                } else {
                    Story::CharacterProfile profile;
                    profile.id = cmd.params.at("id").get<std::string>();
                    profile.name = cmd.params.at("name").get<std::string>();
                    if (cmd.params.contains("faction"))
                        profile.factionId = cmd.params["faction"].get<std::string>();
                    if (cmd.params.contains("agencyLevel"))
                        profile.agencyLevel = static_cast<Story::AgencyLevel>(cmd.params["agencyLevel"].get<int>());
                    if (cmd.params.contains("traits")) {
                        const auto& t = cmd.params["traits"];
                        profile.traits.openness = t.value("openness", 0.5f);
                        profile.traits.conscientiousness = t.value("conscientiousness", 0.5f);
                        profile.traits.extraversion = t.value("extraversion", 0.5f);
                        profile.traits.agreeableness = t.value("agreeableness", 0.5f);
                        profile.traits.neuroticism = t.value("neuroticism", 0.5f);
                    }
                    if (cmd.params.contains("goals")) {
                        for (const auto& gj : cmd.params["goals"]) {
                            Story::CharacterGoal goal;
                            goal.id = gj.at("id").get<std::string>();
                            goal.description = gj.value("description", "");
                            goal.priority = gj.value("priority", 0.5f);
                            profile.goals.push_back(std::move(goal));
                        }
                    }
                    storyEngine->addCharacter(std::move(profile));
                    response = {{"success", true}, {"id", cmd.params["id"]}};
                }

            } else if (cmd.action == "story_remove_character") {
                if (!storyEngine) {
                    response = {{"error", "Story engine not initialized"}};
                } else {
                    std::string id = cmd.params.at("id").get<std::string>();
                    bool removed = storyEngine->removeCharacter(id);
                    response = {{"success", removed}, {"id", id}};
                }

            } else if (cmd.action == "story_trigger_event") {
                if (!storyEngine) {
                    response = {{"error", "Story engine not initialized"}};
                } else {
                    Story::WorldEvent event;
                    event.type = cmd.params.at("type").get<std::string>();
                    if (cmd.params.contains("data"))
                        event.details = cmd.params["data"];
                    if (cmd.params.contains("location")) {
                        auto loc = cmd.params["location"];
                        if (loc.is_object())
                            event.location = glm::vec3(loc.value("x", 0.0f), loc.value("y", 0.0f), loc.value("z", 0.0f));
                    }
                    if (cmd.params.contains("participants")) {
                        for (const auto& p : cmd.params["participants"])
                            event.participants.push_back(p.get<std::string>());
                    }
                    storyEngine->triggerEvent(std::move(event));
                    response = {{"success", true}};
                }

            } else if (cmd.action == "story_add_arc") {
                if (!storyEngine) {
                    response = {{"error", "Story engine not initialized"}};
                } else {
                    Story::StoryArc arc;
                    arc.id = cmd.params.at("id").get<std::string>();
                    arc.name = cmd.params.value("name", arc.id);
                    if (cmd.params.contains("constraintMode")) {
                        arc.constraintMode = Story::arcConstraintModeFromString(
                            cmd.params["constraintMode"].get<std::string>());
                    }
                    if (cmd.params.contains("beats")) {
                        for (const auto& bj : cmd.params["beats"]) {
                            Story::StoryBeat beat;
                            beat.id = bj.at("id").get<std::string>();
                            beat.description = bj.value("description", "");
                            if (bj.contains("type"))
                                beat.type = Story::beatTypeFromString(bj["type"].get<std::string>());
                            if (bj.contains("triggerCondition"))
                                beat.triggerCondition = bj["triggerCondition"].get<std::string>();
                            arc.beats.push_back(std::move(beat));
                        }
                    }
                    if (cmd.params.contains("tensionCurve")) {
                        for (const auto& v : cmd.params["tensionCurve"])
                            arc.tensionCurve.push_back(v.get<float>());
                    }
                    storyEngine->addStoryArc(std::move(arc));
                    response = {{"success", true}, {"id", cmd.params["id"]}};
                }

            } else if (cmd.action == "story_set_variable") {
                if (!storyEngine) {
                    response = {{"error", "Story engine not initialized"}};
                } else {
                    std::string name = cmd.params.at("name").get<std::string>();
                    const auto& val = cmd.params.at("value");
                    if (val.is_boolean())
                        storyEngine->getWorldState().setVariable(name, val.get<bool>());
                    else if (val.is_number_integer())
                        storyEngine->getWorldState().setVariable(name, val.get<int>());
                    else if (val.is_number_float())
                        storyEngine->getWorldState().setVariable(name, val.get<float>());
                    else if (val.is_string())
                        storyEngine->getWorldState().setVariable(name, val.get<std::string>());
                    response = {{"success", true}, {"name", name}};
                }

            } else if (cmd.action == "story_set_agency") {
                if (!storyEngine) {
                    response = {{"error", "Story engine not initialized"}};
                } else {
                    std::string id = cmd.params.at("id").get<std::string>();
                    int level = cmd.params.at("level").get<int>();
                    bool ok = storyEngine->setAgencyLevel(id, static_cast<Story::AgencyLevel>(level));
                    response = {{"success", ok}, {"id", id}};
                }

            } else if (cmd.action == "story_add_knowledge") {
                if (!storyEngine) {
                    response = {{"error", "Story engine not initialized"}};
                } else {
                    std::string charId = cmd.params.at("characterId").get<std::string>();
                    std::string fact = cmd.params.at("fact").get<std::string>();
                    std::string category = cmd.params.value("category", "general");
                    std::string factId = charId + "_api_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
                    storyEngine->addStartingKnowledge(charId, factId, fact);
                    response = {{"success", true}, {"characterId", charId}, {"factId", factId}};
                }

            // ================================================================
            // GAME DEFINITION (AI Game Development)
            // ================================================================

            } else if (cmd.action == "load_game_definition") {
                Core::GameSubsystems subsystems;
                subsystems.chunkManager = chunkManager;
                subsystems.npcManager = npcManager.get();
                subsystems.entityRegistry = entityRegistry.get();
                subsystems.templateManager = objectTemplateManager.get();
                subsystems.gameEventLog = gameEventLog.get();
                subsystems.camera = camera;
                subsystems.dialogueSystem = dialogueSystem.get();
                subsystems.storyEngine = storyEngine.get();

                // Entity spawner callback — delegates to Application factory methods
                subsystems.entitySpawner = [this](const std::string& type, const glm::vec3& pos,
                                                   const std::string& animFile) -> Scene::Entity* {
                    if (type == "physics") return createPhysicsCharacter(pos);
                    else if (type == "spider") return createSpiderCharacter(pos);
                    else if (type == "animated") return createAnimatedCharacter(pos, animFile);
                    return nullptr;
                };

                auto loadResult = Core::GameDefinitionLoader::load(cmd.params, subsystems);
                response = loadResult.toJson();

            } else if (cmd.action == "export_game_definition") {
                Core::GameSubsystems subsystems;
                subsystems.camera = camera;
                subsystems.npcManager = npcManager.get();
                subsystems.storyEngine = storyEngine.get();
                response = Core::GameDefinitionLoader::exportDefinition(subsystems);

            } else if (cmd.action == "validate_game_definition") {
                auto [valid, err] = Core::GameDefinitionLoader::validate(cmd.params);
                response = {{"valid", valid}};
                if (!err.empty()) response["error"] = err;

            } else if (cmd.action == "create_game_npc") {
                // Composite NPC creation: spawn + dialogue + story character in one call
                if (!npcManager) {
                    response = {{"error", "NPCManager not available"}};
                } else {
                    // Use GameDefinitionLoader with a single-NPC definition
                    Core::GameSubsystems subsystems;
                    subsystems.chunkManager = chunkManager;
                    subsystems.npcManager = npcManager.get();
                    subsystems.entityRegistry = entityRegistry.get();
                    subsystems.gameEventLog = gameEventLog.get();
                    subsystems.dialogueSystem = dialogueSystem.get();
                    subsystems.storyEngine = storyEngine.get();

                    nlohmann::json npcArray = nlohmann::json::array();
                    npcArray.push_back(cmd.params);
                    nlohmann::json fakeDef = {{"npcs", npcArray}};

                    auto loadResult = Core::GameDefinitionLoader::load(fakeDef, subsystems);
                    response = loadResult.toJson();
                }

            // ================================================================
            // PROJECT BUILD (engine-as-editor workflow)
            // ================================================================
            } else if (cmd.action == "project_info") {
                auto projDir = runtime->getConfig().projectDir;
                if (projDir.empty()) {
                    response = {{"error", "No project loaded. Launch engine with --project <dir>"}};
                } else {
                    namespace fs = std::filesystem;
                    fs::path projPath(projDir);
                    response = {
                        {"project_dir", projDir},
                        {"has_game_json", fs::exists(projPath / "game.json")},
                        {"has_worlds_db", fs::exists(projPath / "worlds" / "default.db")},
                        {"has_cmakelists", fs::exists(projPath / "CMakeLists.txt")},
                        {"game_definition", runtime->getConfig().gameDefinitionFile}
                    };
                    // Check for built executable
                    auto exeName = projPath.stem().string() + ".exe";
                    auto buildExe = projPath / "build" / "Debug" / exeName;
                    auto rootExe  = projPath / exeName;
                    response["has_built_exe"] = fs::exists(buildExe) || fs::exists(rootExe);
                    if (fs::exists(buildExe))
                        response["exe_path"] = buildExe.string();
                    else if (fs::exists(rootExe))
                        response["exe_path"] = rootExe.string();
                }

            } else if (cmd.action == "project_build") {
                auto projDir = runtime->getConfig().projectDir;
                if (projDir.empty()) {
                    response = {{"error", "No project loaded. Launch engine with --project <dir>"}};
                } else {
                    namespace fs = std::filesystem;
                    fs::path projPath(projDir);

                    if (!fs::exists(projPath / "CMakeLists.txt")) {
                        response = {{"error", "No CMakeLists.txt found in project directory"},
                                    {"project_dir", projDir}};
                    } else {
                        std::string config = cmd.params.value("config", "Debug");
                        bool reconfigure = cmd.params.value("reconfigure", false);

                        // Find cmake — check common VS 2022 location, then PATH
                        std::string cmake = "cmake";
                        fs::path vsCmake("C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe");
                        if (fs::exists(vsCmake)) cmake = vsCmake.string();

                        std::string output;
                        bool success = true;

                        auto runProc = [&](const std::string& cmdStr) -> int {
                            FILE* pipe = _popen(cmdStr.c_str(), "r");
                            if (!pipe) { output += "Failed to run: " + cmdStr + "\n"; return -1; }
                            char buf[256];
                            while (fgets(buf, sizeof(buf), pipe)) output += buf;
                            return _pclose(pipe);
                        };

                        // Configure if needed
                        if (reconfigure || !fs::exists(projPath / "build" / "CMakeCache.txt")) {
                            output += "=== CMake Configure ===\n";
                            std::string configCmd = "\"" + cmake + "\" -B build -S . 2>&1";
                            std::string savedDir = fs::current_path().string();
                            fs::current_path(projPath);
                            int rc = runProc(configCmd);
                            fs::current_path(savedDir);
                            if (rc != 0) {
                                response = {{"success", false}, {"output", output},
                                            {"error", "CMake configure failed"}};
                                if (cmd.onComplete) cmd.onComplete(response);
                                continue;
                            }
                        }

                        // Build
                        output += "=== CMake Build (" + config + ") ===\n";
                        std::string buildCmd = "\"" + cmake + "\" --build build --config " + config + " 2>&1";
                        std::string savedDir = fs::current_path().string();
                        fs::current_path(projPath);
                        int rc = runProc(buildCmd);
                        fs::current_path(savedDir);

                        success = (rc == 0);
                        response = {{"success", success}, {"output", output},
                                    {"project_dir", projDir}, {"config", config}};
                        if (!success) response["error"] = "Build failed (exit code " + std::to_string(rc) + ")";

                        // Copy exe to project root if build succeeded
                        if (success) {
                            auto exeName = projPath.stem().string() + ".exe";
                            auto buildExe = projPath / "build" / "Debug" / exeName;
                            auto rootExe  = projPath / exeName;
                            if (fs::exists(buildExe)) {
                                try { fs::copy_file(buildExe, rootExe, fs::copy_options::overwrite_existing); }
                                catch (...) {}
                                response["exe_path"] = rootExe.string();
                            }
                        }
                    }
                }

            } else if (cmd.action == "project_run") {
                auto projDir = runtime->getConfig().projectDir;
                if (projDir.empty()) {
                    response = {{"error", "No project loaded. Launch engine with --project <dir>"}};
                } else {
                    namespace fs = std::filesystem;
                    fs::path projPath(projDir);
                    auto exeName = projPath.stem().string() + ".exe";
                    auto rootExe = projPath / exeName;
                    auto buildExe = projPath / "build" / "Debug" / exeName;
                    fs::path exePath;
                    if (fs::exists(rootExe)) exePath = rootExe;
                    else if (fs::exists(buildExe)) exePath = buildExe;

                    if (exePath.empty()) {
                        response = {{"error", "Game executable not found. Build first."},
                                    {"expected", rootExe.string()}};
                    } else {
                        // Launch as detached process
                        std::string launchCmd = "start \"\" \"" + exePath.string() + "\"";
                        std::system(launchCmd.c_str());
                        response = {{"success", true}, {"exe_path", exePath.string()}};
                    }
                }

            // ================================================================
            // INVENTORY COMMANDS
            // ================================================================
            } else if (cmd.action == "inventory_give") {
                if (!inventory) {
                    response = {{"error", "Inventory not available"}};
                } else {
                    std::string material = cmd.params.value("material", "");
                    int count = cmd.params.value("count", 1);
                    if (material.empty()) {
                        response = {{"error", "Missing 'material' field"}};
                    } else {
                        int overflow = inventory->addItem(material, count);
                        response = {{"success", true}, {"material", material},
                                    {"given", count - overflow}, {"overflow", overflow},
                                    {"total", inventory->countItem(material)}};
                    }
                }

            } else if (cmd.action == "inventory_take") {
                if (!inventory) {
                    response = {{"error", "Inventory not available"}};
                } else {
                    std::string material = cmd.params.value("material", "");
                    int count = cmd.params.value("count", 1);
                    if (material.empty()) {
                        response = {{"error", "Missing 'material' field"}};
                    } else {
                        int removed = inventory->removeItem(material, count);
                        response = {{"success", true}, {"material", material},
                                    {"removed", removed}, {"remaining", inventory->countItem(material)}};
                    }
                }

            } else if (cmd.action == "inventory_select") {
                if (!inventory) {
                    response = {{"error", "Inventory not available"}};
                } else {
                    int slot = cmd.params.value("slot", 0);
                    bool ok = inventory->setSelectedSlot(slot);
                    if (ok) {
                        response = {{"success", true}, {"selected_slot", slot},
                                    {"material", inventory->getSelectedMaterial()}};
                    } else {
                        response = {{"error", "Invalid slot"}, {"slot", slot},
                                    {"valid_range", "0-8"}};
                    }
                }

            } else if (cmd.action == "inventory_set_slot") {
                if (!inventory) {
                    response = {{"error", "Inventory not available"}};
                } else {
                    int slot = cmd.params.value("slot", -1);
                    if (slot < 0) {
                        response = {{"error", "Missing 'slot' field"}};
                    } else if (cmd.params.contains("material")) {
                        std::string material = cmd.params["material"].get<std::string>();
                        int count = cmd.params.value("count", 1);
                        inventory->setSlot(slot, Core::ItemStack{material, count, 64});
                        response = {{"success", true}, {"slot", slot}, {"material", material}, {"count", count}};
                    } else {
                        inventory->clearSlot(slot);
                        response = {{"success", true}, {"slot", slot}, {"cleared", true}};
                    }
                }

            } else if (cmd.action == "inventory_clear") {
                if (!inventory) {
                    response = {{"error", "Inventory not available"}};
                } else {
                    inventory->clear();
                    response = {{"success", true}};
                }

            } else if (cmd.action == "inventory_creative") {
                if (!inventory) {
                    response = {{"error", "Inventory not available"}};
                } else {
                    bool creative = cmd.params.value("enabled", true);
                    inventory->setCreativeMode(creative);
                    response = {{"success", true}, {"creative", creative}};
                }

            // ================================================================
            // DAY/NIGHT CYCLE COMMANDS
            // ================================================================
            } else if (cmd.action == "daynight_set") {
                if (!renderCoordinator) {
                    response = {{"error", "RenderCoordinator not available"}};
                } else {
                    auto& cycle = renderCoordinator->getDayNightCycle();
                    if (cmd.params.contains("timeOfDay")) {
                        cycle.setTimeOfDay(cmd.params["timeOfDay"].get<float>());
                    }
                    if (cmd.params.contains("dayLengthSeconds")) {
                        cycle.setDayLengthSeconds(cmd.params["dayLengthSeconds"].get<float>());
                    }
                    if (cmd.params.contains("timeScale")) {
                        cycle.setTimeScale(cmd.params["timeScale"].get<float>());
                    }
                    if (cmd.params.contains("enabled")) {
                        cycle.setEnabled(cmd.params["enabled"].get<bool>());
                    }
                    if (cmd.params.contains("paused")) {
                        cycle.setPaused(cmd.params["paused"].get<bool>());
                    }
                    response = {{"success", true}, {"daynight", cycle.toJson()}};
                }

            // ================================================================
            // LIGHTING COMMANDS
            // ================================================================
            } else if (cmd.action == "add_point_light") {
                if (!renderCoordinator) {
                    response = {{"error", "RenderCoordinator not available"}};
                } else {
                    auto& lm = renderCoordinator->getLightManager();
                    Graphics::PointLight pl;
                    pl.position = glm::vec3(
                        cmd.params.value("x", 0.0f),
                        cmd.params.value("y", 0.0f),
                        cmd.params.value("z", 0.0f));
                    if (cmd.params.contains("color")) {
                        pl.color = glm::vec3(
                            cmd.params["color"].value("r", 1.0f),
                            cmd.params["color"].value("g", 1.0f),
                            cmd.params["color"].value("b", 1.0f));
                    }
                    pl.intensity = cmd.params.value("intensity", 1.0f);
                    pl.radius = cmd.params.value("radius", 10.0f);
                    pl.enabled = cmd.params.value("enabled", true);
                    int id = lm.addPointLight(pl);
                    if (id >= 0) {
                        response = {{"success", true}, {"id", id}, {"type", "point"}};
                    } else {
                        response = {{"error", "At capacity"}, {"max", Graphics::MAX_POINT_LIGHTS}};
                    }
                }

            } else if (cmd.action == "add_spot_light") {
                if (!renderCoordinator) {
                    response = {{"error", "RenderCoordinator not available"}};
                } else {
                    auto& lm = renderCoordinator->getLightManager();
                    Graphics::SpotLight sl;
                    sl.position = glm::vec3(
                        cmd.params.value("x", 0.0f),
                        cmd.params.value("y", 0.0f),
                        cmd.params.value("z", 0.0f));
                    sl.direction = glm::vec3(
                        cmd.params.value("dx", 0.0f),
                        cmd.params.value("dy", -1.0f),
                        cmd.params.value("dz", 0.0f));
                    if (cmd.params.contains("color")) {
                        sl.color = glm::vec3(
                            cmd.params["color"].value("r", 1.0f),
                            cmd.params["color"].value("g", 1.0f),
                            cmd.params["color"].value("b", 1.0f));
                    }
                    sl.intensity = cmd.params.value("intensity", 1.0f);
                    sl.radius = cmd.params.value("radius", 20.0f);
                    sl.innerCone = cmd.params.value("inner_cone", 0.9f);
                    sl.outerCone = cmd.params.value("outer_cone", 0.8f);
                    sl.enabled = cmd.params.value("enabled", true);
                    int id = lm.addSpotLight(sl);
                    if (id >= 0) {
                        response = {{"success", true}, {"id", id}, {"type", "spot"}};
                    } else {
                        response = {{"error", "At capacity"}, {"max", Graphics::MAX_SPOT_LIGHTS}};
                    }
                }

            } else if (cmd.action == "remove_light") {
                if (!renderCoordinator) {
                    response = {{"error", "RenderCoordinator not available"}};
                } else {
                    int lightId = cmd.params.value("id", -1);
                    if (lightId < 0) {
                        response = {{"error", "Missing 'id' field"}};
                    } else {
                        bool ok = renderCoordinator->getLightManager().removeLight(lightId);
                        response = {{"success", ok}, {"id", lightId}};
                    }
                }

            } else if (cmd.action == "update_light") {
                if (!renderCoordinator) {
                    response = {{"error", "RenderCoordinator not available"}};
                } else {
                    auto& lm = renderCoordinator->getLightManager();
                    int lightId = cmd.params.value("id", -1);
                    if (lightId < 0) {
                        response = {{"error", "Missing 'id' field"}};
                    } else {
                        // Try point light first
                        const auto* pl = lm.getPointLight(lightId);
                        if (pl) {
                            Graphics::PointLight updated = *pl;
                            if (cmd.params.contains("x")) updated.position.x = cmd.params["x"].get<float>();
                            if (cmd.params.contains("y")) updated.position.y = cmd.params["y"].get<float>();
                            if (cmd.params.contains("z")) updated.position.z = cmd.params["z"].get<float>();
                            if (cmd.params.contains("color")) {
                                updated.color = glm::vec3(
                                    cmd.params["color"].value("r", updated.color.r),
                                    cmd.params["color"].value("g", updated.color.g),
                                    cmd.params["color"].value("b", updated.color.b));
                            }
                            if (cmd.params.contains("intensity")) updated.intensity = cmd.params["intensity"].get<float>();
                            if (cmd.params.contains("radius")) updated.radius = cmd.params["radius"].get<float>();
                            if (cmd.params.contains("enabled")) updated.enabled = cmd.params["enabled"].get<bool>();
                            lm.updatePointLight(lightId, updated);
                            response = {{"success", true}, {"id", lightId}, {"type", "point"}};
                        } else {
                            const auto* sl = lm.getSpotLight(lightId);
                            if (sl) {
                                Graphics::SpotLight updated = *sl;
                                if (cmd.params.contains("x")) updated.position.x = cmd.params["x"].get<float>();
                                if (cmd.params.contains("y")) updated.position.y = cmd.params["y"].get<float>();
                                if (cmd.params.contains("z")) updated.position.z = cmd.params["z"].get<float>();
                                if (cmd.params.contains("dx")) updated.direction.x = cmd.params["dx"].get<float>();
                                if (cmd.params.contains("dy")) updated.direction.y = cmd.params["dy"].get<float>();
                                if (cmd.params.contains("dz")) updated.direction.z = cmd.params["dz"].get<float>();
                                if (cmd.params.contains("color")) {
                                    updated.color = glm::vec3(
                                        cmd.params["color"].value("r", updated.color.r),
                                        cmd.params["color"].value("g", updated.color.g),
                                        cmd.params["color"].value("b", updated.color.b));
                                }
                                if (cmd.params.contains("intensity")) updated.intensity = cmd.params["intensity"].get<float>();
                                if (cmd.params.contains("radius")) updated.radius = cmd.params["radius"].get<float>();
                                if (cmd.params.contains("inner_cone")) updated.innerCone = cmd.params["inner_cone"].get<float>();
                                if (cmd.params.contains("outer_cone")) updated.outerCone = cmd.params["outer_cone"].get<float>();
                                if (cmd.params.contains("enabled")) updated.enabled = cmd.params["enabled"].get<bool>();
                                lm.updateSpotLight(lightId, updated);
                                response = {{"success", true}, {"id", lightId}, {"type", "spot"}};
                            } else {
                                response = {{"error", "Light not found"}, {"id", lightId}};
                            }
                        }
                    }
                }

            } else if (cmd.action == "set_ambient") {
                if (!renderCoordinator) {
                    response = {{"error", "RenderCoordinator not available"}};
                } else {
                    float strength = cmd.params.value("strength", 1.0f);
                    renderCoordinator->setAmbientLightStrength(strength);
                    response = {{"success", true}, {"ambient_strength", renderCoordinator->getAmbientLightStrength()}};
                }

            // ================================================================
            // AUDIO COMMANDS
            // ================================================================
            } else if (cmd.action == "play_sound") {
                if (!audioSystem) {
                    response = {{"error", "AudioSystem not available"}};
                } else {
                    std::string file = cmd.params.value("file", "");
                    if (file.empty()) {
                        response = {{"error", "Missing 'file' field"}};
                    } else {
                        std::string path = "resources/sounds/" + file;
                        float volume = cmd.params.value("volume", 1.0f);
                        std::string channelStr = cmd.params.value("channel", "SFX");
                        Core::AudioChannel channel = Core::AudioChannel::SFX;
                        if (channelStr == "Master") channel = Core::AudioChannel::Master;
                        else if (channelStr == "Music") channel = Core::AudioChannel::Music;
                        else if (channelStr == "Voice") channel = Core::AudioChannel::Voice;

                        if (cmd.params.contains("x") && cmd.params.contains("y") && cmd.params.contains("z")) {
                            glm::vec3 pos(cmd.params["x"].get<float>(),
                                          cmd.params["y"].get<float>(),
                                          cmd.params["z"].get<float>());
                            audioSystem->playSound3D(path, pos, channel, volume);
                            response = {{"success", true}, {"file", file}, {"mode", "3D"},
                                        {"position", {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}}}};
                        } else {
                            audioSystem->playSound(path, channel, volume);
                            response = {{"success", true}, {"file", file}, {"mode", "2D"}};
                        }
                    }
                }

            } else if (cmd.action == "set_volume") {
                if (!audioSystem) {
                    response = {{"error", "AudioSystem not available"}};
                } else {
                    std::string channelStr = cmd.params.value("channel", "Master");
                    float volume = cmd.params.value("volume", 1.0f);
                    Core::AudioChannel channel = Core::AudioChannel::Master;
                    if (channelStr == "SFX") channel = Core::AudioChannel::SFX;
                    else if (channelStr == "Music") channel = Core::AudioChannel::Music;
                    else if (channelStr == "Voice") channel = Core::AudioChannel::Voice;
                    audioSystem->setChannelVolume(channel, volume);
                    response = {{"success", true}, {"channel", channelStr}, {"volume", volume}};
                }

            } else if (cmd.action == "job_submit") {
                // Submit a background job
                if (!jobSystem) {
                    response = {{"error", "Job system not available"}};
                } else {
                    std::string jobType = cmd.params.value("type", "");
                    nlohmann::json jobParams = cmd.params.value("params", nlohmann::json::object());
                    
                    Core::JobDescriptor desc;
                    desc.type = jobType;
                    desc.params = jobParams;
                    
                    if (jobType == "fill_region") {
                        if (!chunkManager) {
                            response = {{"error", "ChunkManager not available"}};
                            if (cmd.onComplete) cmd.onComplete(response);
                            continue;
                        }
                        
                        int x1 = jobParams.value("x1", 0), y1 = jobParams.value("y1", 0), z1 = jobParams.value("z1", 0);
                        int x2 = jobParams.value("x2", 0), y2 = jobParams.value("y2", 0), z2 = jobParams.value("z2", 0);
                        std::string material = jobParams.value("material", "");
                        bool hollow = jobParams.value("hollow", false);
                        
                        int minX = std::min(x1, x2), maxX = std::max(x1, x2);
                        int minY = std::min(y1, y2), maxY = std::max(y1, y2);
                        int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);
                        int64_t volume = (int64_t)(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
                        
                        if (volume > 100000) {
                            response = {{"error", "Region too large"}, {"volume", volume}, {"max_volume", 100000}};
                            if (cmd.onComplete) cmd.onComplete(response);
                            continue;
                        }
                        
                        // Pre-create chunks on main thread (modifies chunks vector)
                        for (int ix = minX; ix <= maxX; ix += 32) {
                            for (int iy = minY; iy <= maxY; iy += 32) {
                                for (int iz = minZ; iz <= maxZ; iz += 32) {
                                    glm::ivec3 cc = Phyxel::ChunkManager::worldToChunkCoord(glm::ivec3(ix, iy, iz));
                                    if (!chunkManager->getChunkAtCoord(cc)) {
                                        chunkManager->createChunk(Phyxel::ChunkManager::chunkCoordToOrigin(cc), false);
                                    }
                                }
                            }
                        }
                        
                        ChunkManager* cmFill = chunkManager;
                        Core::GameEventLog* evFill = gameEventLog.get();
                        desc.backgroundWork = [cmFill, minX, maxX, minY, maxY, minZ, maxZ, material, hollow, volume](Core::JobContext& ctx) -> nlohmann::json {
                            int placed = 0, failed = 0;
                            int64_t total = volume;
                            int64_t count = 0;
                            
                            auto lock = cmFill->acquireWriteLock();
                            for (int ix = minX; ix <= maxX; ++ix) {
                                for (int iy = minY; iy <= maxY; ++iy) {
                                    for (int iz = minZ; iz <= maxZ; ++iz) {
                                        if (ctx.cancelled.load()) {
                                            return {{"success", false}, {"cancelled", true}, {"placed", placed}};
                                        }
                                        if (hollow && ix > minX && ix < maxX && iy > minY && iy < maxY && iz > minZ && iz < maxZ) {
                                            continue;
                                        }
                                        bool ok = false;
                                        if (!material.empty()) {
                                            ok = cmFill->m_voxelModificationSystem.addCubeWithMaterial(glm::ivec3(ix, iy, iz), material);
                                        } else {
                                            ok = cmFill->addCubeFast(glm::ivec3(ix, iy, iz));
                                        }
                                        if (ok) ++placed; else ++failed;
                                        ++count;
                                        if (count % 1000 == 0) {
                                            ctx.setProgress(static_cast<float>(count) / total, "Placing voxels...");
                                        }
                                    }
                                }
                            }
                            return {{"success", true}, {"placed", placed}, {"failed", failed},
                                    {"volume", volume}, {"hollow", hollow},
                                    {"min", {{"x", minX}, {"y", minY}, {"z", minZ}}},
                                    {"max", {{"x", maxX}, {"y", maxY}, {"z", maxZ}}}};
                        };
                        desc.mainThreadFinalize = [cmFill, evFill, material, hollow](nlohmann::json& result) {
                            cmFill->updateDirtyChunks();
                            int placed = result.value("placed", 0);
                            if (placed > 0 && evFill) {
                                evFill->emit("region_filled", {
                                    {"placed", placed}, {"material", material.empty() ? "Default" : material},
                                    {"hollow", hollow}, {"async", true}
                                });
                            }
                        };
                        
                    } else if (jobType == "clear_region") {
                        if (!chunkManager) {
                            response = {{"error", "ChunkManager not available"}};
                            if (cmd.onComplete) cmd.onComplete(response);
                            continue;
                        }
                        
                        int x1 = jobParams.value("x1", 0), y1 = jobParams.value("y1", 0), z1 = jobParams.value("z1", 0);
                        int x2 = jobParams.value("x2", 0), y2 = jobParams.value("y2", 0), z2 = jobParams.value("z2", 0);
                        int minX = std::min(x1, x2), maxX = std::max(x1, x2);
                        int minY = std::min(y1, y2), maxY = std::max(y1, y2);
                        int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);
                        int64_t volume = (int64_t)(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
                        
                        if (volume > 100000) {
                            response = {{"error", "Region too large"}, {"volume", volume}, {"max_volume", 100000}};
                            if (cmd.onComplete) cmd.onComplete(response);
                            continue;
                        }
                        
                        ChunkManager* cmClear = chunkManager;
                        Core::GameEventLog* evClear = gameEventLog.get();
                        desc.backgroundWork = [cmClear, minX, maxX, minY, maxY, minZ, maxZ, volume](Core::JobContext& ctx) -> nlohmann::json {
                            int removed = 0;
                            int64_t count = 0;
                            
                            auto lock = cmClear->acquireWriteLock();
                            for (int ix = minX; ix <= maxX; ++ix) {
                                for (int iy = minY; iy <= maxY; ++iy) {
                                    for (int iz = minZ; iz <= maxZ; ++iz) {
                                        if (ctx.cancelled.load()) {
                                            return {{"success", false}, {"cancelled", true}, {"removed", removed}};
                                        }
                                        if (cmClear->removeCubeFast(glm::ivec3(ix, iy, iz))) ++removed;
                                        ++count;
                                        if (count % 1000 == 0) {
                                            ctx.setProgress(static_cast<float>(count) / volume, "Removing voxels...");
                                        }
                                    }
                                }
                            }
                            return {{"success", true}, {"removed", removed}, {"volume", volume},
                                    {"min", {{"x", minX}, {"y", minY}, {"z", minZ}}},
                                    {"max", {{"x", maxX}, {"y", maxY}, {"z", maxZ}}}};
                        };
                        desc.mainThreadFinalize = [cmClear, evClear](nlohmann::json& result) {
                            cmClear->updateDirtyChunks();
                            int removed = result.value("removed", 0);
                            if (removed > 0 && evClear) {
                                evClear->emit("region_cleared", {{"removed", removed}, {"async", true}});
                            }
                        };
                        
                    } else if (jobType == "generate_world") {
                        if (!chunkManager) {
                            response = {{"error", "ChunkManager not available"}};
                            if (cmd.onComplete) cmd.onComplete(response);
                            continue;
                        }
                        
                        std::string genType = jobParams.value("type", "Perlin");
                        int fromX = jobParams.value("from_x", 0), fromY = jobParams.value("from_y", 0), fromZ = jobParams.value("from_z", 0);
                        int toX = jobParams.value("to_x", 0), toY = jobParams.value("to_y", 0), toZ = jobParams.value("to_z", 0);
                        int seed = jobParams.value("seed", 42);
                        
                        int minCX = std::min(fromX, toX), maxCX = std::max(fromX, toX);
                        int minCY = std::min(fromY, toY), maxCY = std::max(fromY, toY);
                        int minCZ = std::min(fromZ, toZ), maxCZ = std::max(fromZ, toZ);
                        int64_t chunkCount = (int64_t)(maxCX - minCX + 1) * (maxCY - minCY + 1) * (maxCZ - minCZ + 1);
                        
                        if (chunkCount > 64) {
                            response = {{"error", "Too many chunks"}, {"count", chunkCount}, {"max", 64}};
                            if (cmd.onComplete) cmd.onComplete(response);
                            continue;
                        }
                        
                        // Pre-create chunks on main thread
                        for (int cx = minCX; cx <= maxCX; ++cx) {
                            for (int cy = minCY; cy <= maxCY; ++cy) {
                                for (int cz = minCZ; cz <= maxCZ; ++cz) {
                                    glm::ivec3 cc(cx, cy, cz);
                                    if (!chunkManager->getChunkAtCoord(cc)) {
                                        chunkManager->createChunk(Phyxel::ChunkManager::chunkCoordToOrigin(cc), false);
                                    }
                                }
                            }
                        }
                        
                        ChunkManager* cmGen = chunkManager;
                        Core::GameEventLog* evGen = gameEventLog.get();
                        desc.backgroundWork = [cmGen, genType, minCX, maxCX, minCY, maxCY, minCZ, maxCZ, seed, chunkCount](Core::JobContext& ctx) -> nlohmann::json {
                            WorldGenerator::GenerationType wgType = WorldGenerator::GenerationType::Perlin;
                            if (genType == "Random") wgType = WorldGenerator::GenerationType::Random;
                            else if (genType == "Flat") wgType = WorldGenerator::GenerationType::Flat;
                            else if (genType == "Mountains") wgType = WorldGenerator::GenerationType::Mountains;
                            else if (genType == "Caves") wgType = WorldGenerator::GenerationType::Caves;
                            else if (genType == "City") wgType = WorldGenerator::GenerationType::City;
                            
                            WorldGenerator generator(wgType, seed);
                            
                            int generated = 0;
                            auto lock = cmGen->acquireWriteLock();
                            for (int cx = minCX; cx <= maxCX; ++cx) {
                                for (int cy = minCY; cy <= maxCY; ++cy) {
                                    for (int cz = minCZ; cz <= maxCZ; ++cz) {
                                        if (ctx.cancelled.load()) {
                                            return {{"success", false}, {"cancelled", true}, {"generated", generated}};
                                        }
                                        glm::ivec3 cc(cx, cy, cz);
                                        Chunk* chunk = cmGen->getChunkAtCoord(cc);
                                        if (chunk) {
                                            generator.generateChunk(*chunk, cc);
                                            cmGen->markChunkDirty(chunk);
                                            ++generated;
                                        }
                                        ctx.setProgress(static_cast<float>(generated) / chunkCount, "Generating terrain...");
                                    }
                                }
                            }
                            return {{"success", true}, {"chunks_generated", generated}, {"type", genType}, {"seed", seed}};
                        };
                        desc.mainThreadFinalize = [cmGen, evGen](nlohmann::json& result) {
                            cmGen->updateDirtyChunks();
                            if (evGen) {
                                evGen->emit("world_generated", {
                                    {"chunks", result.value("chunks_generated", 0)},
                                    {"type", result.value("type", "")},
                                    {"async", true}
                                });
                            }
                        };
                        
                    } else if (jobType == "save_world") {
                        if (!chunkManager) {
                            response = {{"error", "ChunkManager not available"}};
                            if (cmd.onComplete) cmd.onComplete(response);
                            continue;
                        }
                        
                        bool dirtyOnly = jobParams.value("dirty_only", true);
                        ChunkManager* cmSave = chunkManager;
                        Core::GameEventLog* evSave = gameEventLog.get();
                        desc.backgroundWork = [cmSave, dirtyOnly](Core::JobContext& ctx) -> nlohmann::json {
                            ctx.setProgress(0.0f, "Saving world...");
                            auto lock = cmSave->acquireReadLock();
                            bool ok = dirtyOnly ? cmSave->saveDirtyChunks() : cmSave->saveAllChunks();
                            ctx.setProgress(1.0f, "Save complete");
                            return {{"success", ok}, {"dirty_only", dirtyOnly}};
                        };
                        desc.mainThreadFinalize = [evSave](nlohmann::json& result) {
                            if (evSave) {
                                evSave->emit("world_saved", {{"async", true}});
                            }
                        };
                        
                    } else if (jobType == "project_build") {
                        std::string config = jobParams.value("config", "Debug");
                        std::string projDir = engineConfig.projectDir;
                        desc.backgroundWork = [config, projDir](Core::JobContext& ctx) -> nlohmann::json {
                            ctx.setProgress(0.0f, "Building project...");
                            
                            // Run cmake configure + build
                            std::string cmakePath = "cmake";
                            std::string buildDir = projDir + "/build";
                            std::string sourceDir = projDir;
                            
                            // Configure
                            ctx.setProgress(0.1f, "Running cmake configure...");
                            std::string configCmd = cmakePath + " -B \"" + buildDir + "\" -S \"" + sourceDir + "\" 2>&1";
                            std::string output;
                            FILE* pipe = _popen(configCmd.c_str(), "r");
                            if (pipe) {
                                char buf[256];
                                while (fgets(buf, sizeof(buf), pipe)) {
                                    output += buf;
                                    if (ctx.cancelled.load()) { _pclose(pipe); return {{"success", false}, {"cancelled", true}}; }
                                }
                                _pclose(pipe);
                            }
                            
                            // Build
                            ctx.setProgress(0.5f, "Running cmake build...");
                            std::string buildCmd = cmakePath + " --build \"" + buildDir + "\" --config " + config + " 2>&1";
                            std::string buildOutput;
                            pipe = _popen(buildCmd.c_str(), "r");
                            if (pipe) {
                                char buf[256];
                                while (fgets(buf, sizeof(buf), pipe)) {
                                    buildOutput += buf;
                                    if (ctx.cancelled.load()) { _pclose(pipe); return {{"success", false}, {"cancelled", true}}; }
                                }
                                int exitCode = _pclose(pipe);
                                ctx.setProgress(1.0f, "Build complete");
                                return {{"success", exitCode == 0}, {"config", config},
                                        {"configure_output", output}, {"build_output", buildOutput}};
                            }
                            return {{"success", false}, {"error", "Failed to start build process"}};
                        };
                        
                    } else {
                        response = {{"error", "Unknown job type: " + jobType}};
                        if (cmd.onComplete) cmd.onComplete(response);
                        continue;
                    }
                    
                    Core::JobId jobId = jobSystem->submitJob(std::move(desc));
                    response = {{"success", true}, {"job_id", jobId}, {"status", "Pending"}, {"type", jobType}};
                }

            } else {
                response = {{"error", "Unknown action: " + cmd.action}};
            }
        } catch (const std::exception& e) {
            response = {{"error", e.what()}, {"action", cmd.action}};
        }

        if (cmd.onComplete) {
            cmd.onComplete(response);
        }
    }
}

} // namespace Phyxel
