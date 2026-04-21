#ifdef _WIN32
// Must define NOMINMAX before any Windows header to avoid min/max macro conflicts
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include "Application.h"
#include "core/MaterialRegistry.h"
#include "core/AtlasManager.h"
#include "scene/VoxelInteractionSystem.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "graphics/AnimationSystem.h"
#include "scene/NPCEntity.h"
#include "scene/behaviors/IdleBehavior.h"
#include "scene/behaviors/PatrolBehavior.h"
#include "scene/behaviors/BehaviorTreeBehavior.h"
#include "scene/behaviors/ScheduledBehavior.h"
#include "ai/Schedule.h"
#include "ai/NeedsSystem.h"
#include "ai/RelationshipManager.h"
#include "ai/WorldView.h"
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
#include "ai/AIEnhancer.h"
#include "ai/AIConversationService.h"
#include "ai/LLMClient.h"
#include "core/WorldStorage.h"
#include "core/Chunk.h"
#include "core/WorldGenerator.h"
#include "core/VoxelTemplate.h"
#include "physics/Material.h"
#include "story/StoryWorldLoader.h"
#include "story/StoryDirectorTypes.h"
#include "core/EngineConfig.h"
#include "core/AssetManager.h"
#include "core/GameDefinitionLoader.h"
#include "core/StructureGenerator.h"
#include "core/ProjectInfo.h"
#include "core/LauncherState.h"
#include "core/ItemRegistry.h"
#include "core/EquipmentSystem.h"
#include "core/CombatSystem.h"
#include "ui/MenuDefinition.h"
#include "ui/UISystem.h"
#include "ui/GameMenus.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_vulkan.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <algorithm>
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
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // MessageBoxA
#include <commdlg.h>  // GetOpenFileName file dialog
#include <shobjidl.h> // IFileOpenDialog for folder picker
#include <shlobj.h>   // SHCreateItemFromParsingName
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
        LOG_WARN("Application", "Failed to parse engine.json  --  using defaults");
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

    // Enable editor multi-viewport (pop-out OS windows for docked panels)
    engineConfig.enableEditorViewports = true;

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
    locationRegistry    = runtime->getLocationRegistry();

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

    // Wire up material provider: in asset editor mode use m_assetEditorMaterial,
    // otherwise use the selected hotbar slot material from inventory.
    voxelInteractionSystem->setMaterialProvider([this]() -> std::string {
        if (m_assetEditorMode) return m_assetEditorMaterial;
        if (inventory) return inventory->getSelectedMaterial();
        return "";
    });

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
    
    LOG_INFO("Application", "Entities initialized successfully!");

    // Setup player death/respawn
    playerHealth.setOnDeathCallback([this]() {
        LOG_WARN("Application", "Player died!");
        respawnSystem.startDeathSequence();
    });
    respawnSystem.setSpawnPoint(camera->getPosition());
    respawnSystem.setOnRespawnCallback([this](const glm::vec3& spawnPos) {
        LOG_INFO("Application", "Respawning player...");
        playerHealth.revive(1.0f);
        if (animatedCharacter)
            animatedCharacter->setPosition(spawnPos);
        else
            camera->setPosition(spawnPos);
    });

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

    // Initialize custom UI system (non-ImGui menus)
    renderCoordinator->initUISystem();

    LOG_INFO("Application", "RenderCoordinator initialized successfully!");

    // STEP 6b: INITIALIZE GPU PARTICLE PHYSICS
    gpuParticlePhysics = std::make_unique<GpuParticlePhysics>();
    if (gpuParticlePhysics->initialize(vulkanDevice, "")) {
        // Wire to RenderCoordinator (for compute dispatch + GPU draw)
        renderCoordinator->setGpuParticlePhysics(gpuParticlePhysics.get());
        // Wire to ChunkManager (for occupancy grid updates on voxel change + streaming)
        chunkManager->setGpuParticlePhysics(gpuParticlePhysics.get());
        // Populate 3D occupancy grid from chunks already loaded at startup
        chunkManager->rebuildOccupancyFromChunks();
        LOG_INFO("Application", "GpuParticlePhysics initialized successfully!");
    } else {
        LOG_WARN("Application", "GpuParticlePhysics initialization failed  --  falling back to CPU physics");
        gpuParticlePhysics.reset();
    }

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
        aiConfig.defaultProvider = engineConfig.aiProvider;
        if (!engineConfig.aiModel.empty())
            aiConfig.defaultModel = engineConfig.aiModel;

        bool autoStart = engineConfig.aiAutoStart;
        if (!aiSystem->initialize(aiConfig, autoStart)) {
            LOG_WARN("Application", "AI system initialization failed (non-critical)");
        } else {
            if (autoStart)
                LOG_INFO("Application", "AI system initialized (server auto-started, provider={})", engineConfig.aiProvider);
            else
                LOG_INFO("Application", "AI system initialized (server not auto-started, provider={})", engineConfig.aiProvider);
        }
    }

    // STEP 9.5: INITIALIZE LIGHTWEIGHT AI ENHANCER (bypasses Goose, calls Claude API directly)
    {
        // Resolve path to ai_enhance.py  --  try engine source tree first, then project scripts dir
        std::string scriptPath;
        for (const auto& candidate : {
            std::string("scripts/ai_enhance.py"),
            std::string("../scripts/ai_enhance.py"),
            engineConfig.scriptsDir + "/ai_enhance.py"
        }) {
            if (std::filesystem::exists(candidate)) {
                scriptPath = std::filesystem::absolute(candidate).string();
                break;
            }
        }
        std::string model = engineConfig.aiModel.empty() ? "claude-sonnet-4-20250514" : engineConfig.aiModel;
        std::string apiKey = engineConfig.aiApiKey;
        // Fallback: try reading env var directly (in case EngineConfig didn't pick it up)
        if (apiKey.empty()) {
            if (const char* envKey = std::getenv("PHYXEL_AI_API_KEY"); envKey && envKey[0] != '\0')
                apiKey = envKey;
        }
        aiEnhancer = std::make_unique<AI::AIEnhancer>(apiKey, model, scriptPath);
    }

    // STEP 10: INITIALIZE ENTITY REGISTRY & HTTP API SERVER
    entityRegistry = std::make_unique<Core::EntityRegistry>();
    apiCommandQueue = std::make_unique<Core::APICommandQueue>();
    gameEventLog = std::make_unique<Core::GameEventLog>(1000);

    // Wire D&D combat AI (pointers are stable — members outlive all systems)
    m_combatAI.setInitiativeTracker(&m_rpgInitiative);
    m_combatAI.setParty(&m_rpgParty);
    m_combatAI.setEntityRegistry(entityRegistry.get());
    jobSystem = std::make_unique<Core::JobSystem>();
    apiServer = std::make_unique<Core::EngineAPIServer>(apiCommandQueue.get(), engineConfig.apiPort, jobSystem.get());

    // Wire up read-only handlers (called directly on HTTP thread  --  must be thread-safe)
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

    apiServer->setSegmentDebugHandler([this](const std::string& id) -> nlohmann::json {
        Scene::AnimatedVoxelCharacter* ch = animatedCharacter;
        if (!id.empty() && npcManager) {
            std::string n = (id.size() > 4 && id.substr(0,4) == "npc_") ? id.substr(4) : id;
            auto* npc = npcManager->getNPC(n);
            if (npc) ch = npc->getAnimatedCharacter();
        }
        nlohmann::json arr = nlohmann::json::array();
        if (ch) {
            for (const auto& b : ch->getSegmentBoxInfo()) {
                arr.push_back({
                    {"bone", b.boneName},
                    {"he", {{"x",b.halfExtents.x},{"y",b.halfExtents.y},{"z",b.halfExtents.z}}},
                    {"pos", {{"x",b.position.x},{"y",b.position.y},{"z",b.position.z}}},
                    {"arm", b.isArm}, {"hit", b.colliding}
                });
            }
        }
        return {{"count", static_cast<int>(arr.size())}, {"boxes", arr}};
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
        auto& reg = Phyxel::Core::MaterialRegistry::instance();
        const auto& materials = reg.getAllMaterials();
        nlohmann::json matArr = nlohmann::json::array();
        for (const auto& mat : materials) {
            nlohmann::json entry = {
                {"name", mat.name},
                {"id", reg.getMaterialID(mat.name)},
                {"emissive", mat.emissive}
            };
            if (mat.hasPhysics()) {
                entry["mass"] = mat.physics.mass;
                entry["friction"] = mat.physics.friction;
                entry["restitution"] = mat.physics.restitution;
            }
            // Texture face files
            nlohmann::json faces = nlohmann::json::array();
            for (int f = 0; f < 6; f++) {
                faces.push_back(mat.textures.faceFiles[f]);
            }
            entry["textures"] = faces;
            matArr.push_back(entry);
        }
        return nlohmann::json{{"materials", matArr}, {"count", materials.size()}};
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

    // Subcube query: returns all subcubes at a parent cube world position
    apiServer->setSubcubeQueryHandler([this](int x, int y, int z) -> nlohmann::json {
        if (!chunkManager) return nlohmann::json{{"error", "ChunkManager not available"}};
        glm::ivec3 worldPos(x, y, z);
        auto* chunk = chunkManager->getChunkAt(worldPos);
        if (!chunk) {
            return nlohmann::json{{"position", {{"x", x}, {"y", y}, {"z", z}}}, {"subcubes", nlohmann::json::array()}, {"microcubes", nlohmann::json::array()}, {"subcube_count", 0}, {"microcube_count", 0}, {"has_full_cube", false}};
        }
        glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
        auto* cube = chunk->getCubeAt(localPos);
        auto subcubes = chunk->getSubcubesAt(localPos);
        nlohmann::json subArr = nlohmann::json::array();
        nlohmann::json microArr = nlohmann::json::array();
        for (auto* sub : subcubes) {
            if (!sub) continue;
            auto lp = sub->getLocalPosition();
            float scale = sub->getScale();
            float worldMinX = static_cast<float>(x) + lp.x * scale;
            float worldMinY = static_cast<float>(y) + lp.y * scale;
            float worldMinZ = static_cast<float>(z) + lp.z * scale;
            subArr.push_back({
                {"local", {{"sx", lp.x}, {"sy", lp.y}, {"sz", lp.z}}},
                {"world_min", {{"x", worldMinX}, {"y", worldMinY}, {"z", worldMinZ}}},
                {"world_max", {{"x", worldMinX + scale}, {"y", worldMinY + scale}, {"z", worldMinZ + scale}}},
                {"scale", scale},
                {"material", sub->getMaterialName()},
                {"type", "subcube"},
                {"visible", sub->isVisible()}
            });
        }
        // Enumerate microcubes: iterate all 27 subcube slots
        for (int sx = 0; sx < 3; ++sx) {
            for (int sy = 0; sy < 3; ++sy) {
                for (int sz = 0; sz < 3; ++sz) {
                    auto micros = chunk->getMicrocubesAt(localPos, glm::ivec3(sx, sy, sz));
                    for (auto* mic : micros) {
                        if (!mic) continue;
                        auto mp = mic->getMicrocubeLocalPosition();
                        float microScale = mic->getScale(); // 1/9
                        float subScale = 1.0f / 3.0f;
                        float wmx = static_cast<float>(x) + sx * subScale + mp.x * microScale;
                        float wmy = static_cast<float>(y) + sy * subScale + mp.y * microScale;
                        float wmz = static_cast<float>(z) + sz * subScale + mp.z * microScale;
                        microArr.push_back({
                            {"subcube_local", {{"sx", sx}, {"sy", sy}, {"sz", sz}}},
                            {"micro_local", {{"mx", mp.x}, {"my", mp.y}, {"mz", mp.z}}},
                            {"world_min", {{"x", wmx}, {"y", wmy}, {"z", wmz}}},
                            {"world_max", {{"x", wmx + microScale}, {"y", wmy + microScale}, {"z", wmz + microScale}}},
                            {"scale", microScale},
                            {"material", mic->getMaterialName()},
                            {"type", "microcube"},
                            {"visible", mic->isVisible()}
                        });
                    }
                }
            }
        }
        return nlohmann::json{
            {"position", {{"x", x}, {"y", y}, {"z", z}}},
            {"has_full_cube", cube != nullptr},
            {"subcube_count", subArr.size()},
            {"microcube_count", microArr.size()},
            {"subcubes", subArr},
            {"microcubes", microArr}
        };
    });

    // Detailed region scan: full cubes + subcubes in a region
    apiServer->setDetailedRegionScanHandler([this](int x1, int y1, int z1, int x2, int y2, int z2) -> nlohmann::json {
        if (!chunkManager) return nlohmann::json{{"error", "ChunkManager not available"}};

        int minX = std::min(x1, x2), maxX = std::max(x1, x2);
        int minY = std::min(y1, y2), maxY = std::max(y1, y2);
        int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);

        int64_t volume = (int64_t)(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
        if (volume > 10000) {
            return nlohmann::json{{"error", "Region too large for detailed scan"}, {"volume", volume}, {"max_volume", 10000}};
        }

        nlohmann::json cubes = nlohmann::json::array();
        nlohmann::json subcubesList = nlohmann::json::array();
        nlohmann::json microcubesList = nlohmann::json::array();
        int cubeCount = 0, subcubeCount = 0, microcubeCount = 0;

        for (int ix = minX; ix <= maxX; ++ix) {
            for (int iy = minY; iy <= maxY; ++iy) {
                for (int iz = minZ; iz <= maxZ; ++iz) {
                    glm::ivec3 worldPos(ix, iy, iz);
                    auto* chunk = chunkManager->getChunkAt(worldPos);
                    if (!chunk) continue;
                    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);

                    auto* cube = chunk->getCubeAt(localPos);
                    if (cube) {
                        cubes.push_back({
                            {"x", ix}, {"y", iy}, {"z", iz},
                            {"material", cube->getMaterialName()},
                            {"type", "cube"}
                        });
                        ++cubeCount;
                    }

                    auto subs = chunk->getSubcubesAt(localPos);
                    for (auto* sub : subs) {
                        if (!sub) continue;
                        auto lp = sub->getLocalPosition();
                        float scale = sub->getScale();
                        float wmx = static_cast<float>(ix) + lp.x * scale;
                        float wmy = static_cast<float>(iy) + lp.y * scale;
                        float wmz = static_cast<float>(iz) + lp.z * scale;
                        subcubesList.push_back({
                            {"parent", {{"x", ix}, {"y", iy}, {"z", iz}}},
                            {"local", {{"sx", lp.x}, {"sy", lp.y}, {"sz", lp.z}}},
                            {"world_min", {{"x", wmx}, {"y", wmy}, {"z", wmz}}},
                            {"world_max", {{"x", wmx + scale}, {"y", wmy + scale}, {"z", wmz + scale}}},
                            {"scale", scale},
                            {"material", sub->getMaterialName()},
                            {"type", "subcube"}
                        });
                        ++subcubeCount;
                    }
                    // Enumerate microcubes at all 27 subcube slots
                    for (int msx = 0; msx < 3; ++msx) {
                        for (int msy = 0; msy < 3; ++msy) {
                            for (int msz = 0; msz < 3; ++msz) {
                                auto micros = chunk->getMicrocubesAt(localPos, glm::ivec3(msx, msy, msz));
                                for (auto* mic : micros) {
                                    if (!mic) continue;
                                    auto mp = mic->getMicrocubeLocalPosition();
                                    float microScale = mic->getScale();
                                    float subScale = 1.0f / 3.0f;
                                    float mwx = static_cast<float>(ix) + msx * subScale + mp.x * microScale;
                                    float mwy = static_cast<float>(iy) + msy * subScale + mp.y * microScale;
                                    float mwz = static_cast<float>(iz) + msz * subScale + mp.z * microScale;
                                    microcubesList.push_back({
                                        {"parent", {{"x", ix}, {"y", iy}, {"z", iz}}},
                                        {"subcube_local", {{"sx", msx}, {"sy", msy}, {"sz", msz}}},
                                        {"micro_local", {{"mx", mp.x}, {"my", mp.y}, {"mz", mp.z}}},
                                        {"world_min", {{"x", mwx}, {"y", mwy}, {"z", mwz}}},
                                        {"world_max", {{"x", mwx + microScale}, {"y", mwy + microScale}, {"z", mwz + microScale}}},
                                        {"scale", microScale},
                                        {"material", mic->getMaterialName()},
                                        {"type", "microcube"}
                                    });
                                    ++microcubeCount;
                                }
                            }
                        }
                    }
                }
            }
        }
        return nlohmann::json{
            {"cube_count", cubeCount},
            {"subcube_count", subcubeCount},
            {"microcube_count", microcubeCount},
            {"volume", volume},
            {"min", {{"x", minX}, {"y", minY}, {"z", minZ}}},
            {"max", {{"x", maxX}, {"y", maxY}, {"z", maxZ}}},
            {"cubes", cubes},
            {"subcubes", subcubesList},
            {"microcubes", microcubesList}
        };
    });

    // Step-up debug log: returns ring buffer of recent step-up events
    apiServer->setStepDebugLogHandler([this]() -> nlohmann::json {
        nlohmann::json entries = nlohmann::json::array();
        // Collect from all animated characters
        if (entityRegistry) {
            auto animatedEntities = entityRegistry->getEntitiesByType("animated");
            for (const auto& [entityId, entity] : animatedEntities) {
                auto* animChar = dynamic_cast<Scene::AnimatedVoxelCharacter*>(entity);
                if (!animChar) continue;
                const auto& log = animChar->getStepDebugLog();
                for (const auto& entry : log) {
                    entries.push_back({
                        {"entity", entityId},
                        {"timestamp", entry.timestamp},
                        {"char_pos", {{"x", entry.charX}, {"y", entry.charY}, {"z", entry.charZ}}},
                        {"obstacle_height", entry.obstacleHeight},
                        {"step_height", entry.stepHeight},
                        {"result", entry.result},
                        {"blocked_frames", entry.blockedFrames}
                    });
                }
            }
        }
        return nlohmann::json{{"entries", entries}, {"count", entries.size()}};
    });

    // GPU particle physics timing debug stats
    apiServer->setParticleTimingHandler([this]() -> nlohmann::json {
        if (!gpuParticlePhysics || !gpuParticlePhysics->isInitialized()) {
            return nlohmann::json{{"error", "GpuParticlePhysics not available"}};
        }
        const auto& ring = gpuParticlePhysics->getTimingRing();
        size_t head = gpuParticlePhysics->getTimingRingHead();
        size_t ringSize = ring.size();
        if (ringSize == 0) return nlohmann::json{{"entries", nlohmann::json::array()}, {"count", 0}};

        // Output in chronological order (oldest first)
        nlohmann::json entries = nlohmann::json::array();
        size_t count = std::min(head, ringSize);
        size_t start = (head >= ringSize) ? head - ringSize : 0;
        // Summary stats
        uint32_t ticks0 = 0, ticks1 = 0, ticks2plus = 0;
        float minDt = 1.0f, maxDt = 0.0f, sumDt = 0.0f;
        float minAlpha = 1.0f, maxAlpha = 0.0f;
        for (size_t i = start; i < head; ++i) {
            const auto& e = ring[i % ringSize];
            entries.push_back({
                {"frame", e.frameNumber}, {"dt", e.dt}, {"accum", e.accumulator},
                {"alpha", e.interpAlpha}, {"ticks", e.physicsTicks}, {"active", e.activeCount}
            });
            if (e.physicsTicks == 0) ++ticks0;
            else if (e.physicsTicks == 1) ++ticks1;
            else ++ticks2plus;
            minDt = std::min(minDt, e.dt);
            maxDt = std::max(maxDt, e.dt);
            sumDt += e.dt;
            minAlpha = std::min(minAlpha, e.interpAlpha);
            maxAlpha = std::max(maxAlpha, e.interpAlpha);
        }
        float avgDt = count > 0 ? sumDt / count : 0.0f;
        return nlohmann::json{
            {"count", count},
            {"fixed_dt", gpuParticlePhysics->getFixedDt()},
            {"summary", {
                {"avg_dt", avgDt}, {"min_dt", minDt}, {"max_dt", maxDt},
                {"avg_fps", avgDt > 0 ? 1.0f / avgDt : 0.0f},
                {"frames_0_ticks", ticks0}, {"frames_1_tick", ticks1}, {"frames_2plus_ticks", ticks2plus},
                {"min_alpha", minAlpha}, {"max_alpha", maxAlpha}
            }},
            {"entries", entries}
        };
    });

    apiServer->setParticleLogHandler([this](const std::string& action, const std::string& filePath) -> nlohmann::json {
        if (!gpuParticlePhysics || !gpuParticlePhysics->isInitialized()) {
            return nlohmann::json{{"error", "GpuParticlePhysics not available"}};
        }
        if (action == "start") {
            bool ok = gpuParticlePhysics->startPositionLog(filePath);
            return nlohmann::json{{"success", ok}, {"action", "start"}, {"file", filePath}};
        } else if (action == "stop") {
            gpuParticlePhysics->stopPositionLog();
            return nlohmann::json{{"success", true}, {"action", "stop"}};
        } else if (action == "status") {
            return nlohmann::json{{"logging", gpuParticlePhysics->isPositionLogging()}};
        }
        return nlohmann::json{{"error", "Unknown action. Use 'start', 'stop', or 'status'."}};
    });

    // Engine-wide frame timing: FPS, cpu/gpu times, draw calls, active counts
    apiServer->setEngineTimingHandler([this]() -> nlohmann::json {
        auto ft = performanceMonitor->getCurrentFrameTiming();
        const auto& detailed = performanceMonitor->getDetailedTimings();

        nlohmann::json detail = nlohmann::json::object();
        if (!detailed.empty()) {
            const auto& d = detailed.back();
            detail = {
                {"totalFrameTime", d.totalFrameTime},
                {"physicsTime", d.physicsTime},
                {"instanceUpdateTime", d.instanceUpdateTime},
                {"commandRecordTime", d.commandRecordTime},
                {"gpuSubmitTime", d.gpuSubmitTime},
                {"presentTime", d.presentTime},
                {"frustumCullingTime", d.frustumCullingTime},
                {"occlusionCullingTime", d.occlusionCullingTime},
                {"faceCullingTime", d.faceCullingTime}
            };
        }

        size_t bulletActive = chunkManager ? chunkManager->m_dynamicObjectManager.getActiveBulletCount() : 0;
        uint32_t gpuActive = (gpuParticlePhysics && gpuParticlePhysics->isInitialized())
            ? gpuParticlePhysics->getActiveParticleCount() : 0;

        double fps = ft.cpuFrameTime > 0.0 ? 1000.0 / ft.cpuFrameTime : 0.0;
        return nlohmann::json{
            {"fps", fps},
            {"cpuFrameTime", ft.cpuFrameTime},
            {"gpuFrameTime", ft.gpuFrameTime},
            {"drawCalls", ft.drawCalls},
            {"vertexCount", ft.vertexCount},
            {"visibleInstances", ft.visibleInstances},
            {"culledInstances", ft.culledInstances},
            {"frustumCulled", ft.frustumCulledInstances},
            {"occlusionCulled", ft.occlusionCulledInstances},
            {"faceCulledFaces", ft.faceCulledFaces},
            {"bulletActive", bulletActive},
            {"bulletCap", DynamicObjectManager::MAX_DYNAMIC_OBJECTS},
            {"gpuActive", gpuActive},
            {"gpuCap", GpuParticlePhysics::MAX_PARTICLES},
            {"detailed", detail}
        };
    });

    // Dynamic object stats: bullet and GPU counts/caps
    apiServer->setDynamicStatsHandler([this]() -> nlohmann::json {
        size_t bulletActive = chunkManager ? chunkManager->m_dynamicObjectManager.getActiveBulletCount() : 0;
        size_t bulletTotal = chunkManager ? chunkManager->m_dynamicObjectManager.getTotalBulletCount() : 0;
        uint32_t gpuActive = (gpuParticlePhysics && gpuParticlePhysics->isInitialized())
            ? gpuParticlePhysics->getActiveParticleCount() : 0;
        return nlohmann::json{
            {"bullet_active", bulletActive},
            {"bullet_total", bulletTotal},
            {"bullet_cap", DynamicObjectManager::MAX_DYNAMIC_OBJECTS},
            {"gpu_active", gpuActive},
            {"gpu_cap", GpuParticlePhysics::MAX_PARTICLES}
        };
    });

    // -----------------------------------------------------------------------
    // RPG System handler  --  routes /api/rpg/<action> to D&D subsystems
    // -----------------------------------------------------------------------
    apiServer->setRpgHandler([this](const std::string& action, const nlohmann::json& params) -> nlohmann::json {
        using json = nlohmann::json;

        // ---- Party --------------------------------------------------------
        if (action == "party") {
            return m_rpgParty.toJson();
        }
        if (action == "party/add") {
            std::string id   = params.value("entity_id", "");
            std::string name = params.value("name", id);
            int level        = params.value("level", 1);
            if (id.empty()) return json{{"error","entity_id required"}};
            m_rpgParty.addMember(id, name, level);
            return json{{"ok", true}, {"party", m_rpgParty.toJson()}};
        }
        if (action == "party/remove") {
            std::string id = params.value("entity_id", "");
            if (id.empty()) return json{{"error","entity_id required"}};
            m_rpgParty.removeMember(id);
            return json{{"ok", true}};
        }
        if (action == "party/set_alive") {
            std::string id = params.value("entity_id", "");
            bool alive     = params.value("alive", true);
            if (id.empty()) return json{{"error","entity_id required"}};
            m_rpgParty.setAlive(id, alive);
            return json{{"ok", true}};
        }

        // ---- Combat / Initiative ------------------------------------------
        if (action == "combat/state") {
            return json{
                {"active",          m_rpgInitiative.isCombatActive()},
                {"round",           m_rpgInitiative.currentRound()},
                {"current_entity",  m_rpgInitiative.currentEntityId()},
                {"turn_order",      m_rpgInitiative.toJson()}
            };
        }
        if (action == "combat/start") {
            // params: participants = [{entity_id, initiative_bonus}]
            std::vector<std::string> entityIds;
            if (params.contains("participants") && params["participants"].is_array()) {
                for (const auto& p : params["participants"]) {
                    std::string eid = p.value("entity_id", "");
                    if (!eid.empty()) entityIds.push_back(eid);
                }
            }
            m_rpgInitiative.startCombat(entityIds);
            if (params.contains("participants") && params["participants"].is_array()) {
                Core::DiceSystem dice;
                for (const auto& p : params["participants"]) {
                    std::string eid = p.value("entity_id", "");
                    int bonus = p.value("initiative_bonus", 0);
                    if (!eid.empty()) m_rpgInitiative.rollInitiative(eid, bonus, dice);
                }
            }
            m_rpgInitiative.sortOrder();
            return json{{"ok", true}, {"state", m_rpgInitiative.toJson()}};
        }
        if (action == "combat/next_turn") {
            if (!m_rpgInitiative.isCombatActive())
                return json{{"error","no active combat"}};
            std::string next = m_rpgInitiative.endTurn();
            return json{{"ok", true}, {"next_entity", next}, {"round", m_rpgInitiative.currentRound()}};
        }
        if (action == "combat/end") {
            m_rpgInitiative.endCombat();
            return json{{"ok", true}};
        }
        if (action == "combat/set_initiative") {
            std::string eid = params.value("entity_id", "");
            int value = params.value("value", 0);
            if (eid.empty()) return json{{"error","entity_id required"}};
            m_rpgInitiative.setInitiative(eid, value);
            m_rpgInitiative.sortOrder();
            return json{{"ok", true}};
        }

        // ---- World Calendar -----------------------------------------------
        if (action == "world/date") {
            auto date = m_rpgWorldClock.getDate();
            return json{
                {"day",         date.day},
                {"month",       date.month},
                {"year",        date.year},
                {"day_of_week", Core::WorldClock::dayOfWeekName(m_rpgWorldClock.getDayOfWeek())},
                {"season",      Core::WorldClock::seasonName(m_rpgWorldClock.getSeason())},
                {"moon_phase",  Core::WorldClock::moonPhaseName(m_rpgWorldClock.getMoonPhase())},
                {"long_date",   date.toLongString()},
                {"total_days",  m_rpgWorldClock.getTotalDays()},
                {"is_holiday",  m_rpgWorldClock.isHoliday()},
                {"holiday_name",m_rpgWorldClock.isHoliday() ? m_rpgWorldClock.getHolidayName() : ""}
            };
        }
        if (action == "world/advance_date") {
            int days = params.value("days", 1);
            m_rpgWorldClock.advanceDays(days);
            auto date = m_rpgWorldClock.getDate();
            return json{{"ok", true}, {"new_total_days", m_rpgWorldClock.getTotalDays()}, {"date", date.toLongString()}};
        }
        if (action == "world/set_date") {
            int totalDays = params.value("total_days", -1);
            if (totalDays >= 0) m_rpgWorldClock.setTotalDays(totalDays);
            return json{{"ok", true}};
        }

        // ---- Campaign Journal ---------------------------------------------
        if (action == "journal/entries") {
            std::string typeFilter = params.value("type", "");
            std::string tagFilter  = params.value("tag", "");
            std::string query      = params.value("search", "");
            int dayFilter          = params.value("day", -1);

            std::vector<const Core::JournalEntry*> entries;
            if (!query.empty()) {
                entries = m_rpgJournal.searchEntries(query);
            } else if (!tagFilter.empty()) {
                entries = m_rpgJournal.getEntriesByTag(tagFilter);
            } else if (dayFilter >= 0) {
                entries = m_rpgJournal.getEntriesByDay(dayFilter);
            } else if (!typeFilter.empty()) {
                auto t = Core::journalEntryTypeFromString(typeFilter.c_str());
                entries = m_rpgJournal.getEntriesByType(t);
            } else {
                // All entries via full JSON
                return m_rpgJournal.toJson();
            }
            json arr = json::array();
            for (const auto* e : entries) arr.push_back(e->toJson());
            return json{{"entries", arr}, {"count", arr.size()}};
        }
        if (action == "journal/add") {
            std::string typeStr = params.value("type", "WorldEvent");
            std::string title   = params.value("title", "");
            std::string content = params.value("content", "");
            int day             = params.value("day", m_rpgWorldClock.getTotalDays());
            std::vector<std::string> tags;
            if (params.contains("tags") && params["tags"].is_array()) {
                for (const auto& t : params["tags"]) tags.push_back(t.get<std::string>());
            }
            if (title.empty()) return json{{"error","title required"}};
            auto entryType = Core::journalEntryTypeFromString(typeStr.c_str());
            int id = m_rpgJournal.addEntry(entryType, title, content, day, tags);
            return json{{"ok", true}, {"id", id}};
        }
        if (action == "journal/remove") {
            int id = params.value("id", -1);
            if (id < 0) return json{{"error","id required"}};
            m_rpgJournal.removeEntry(id);
            return json{{"ok", true}};
        }

        return json{{"error", "Unknown RPG action: " + action}};
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

    // Initialize Placed Object Manager
    placedObjectManager = std::make_unique<Core::PlacedObjectManager>(
        chunkManager, objectTemplateManager.get(), snapshotManager.get());

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
    // Item Registry & Inventory System
    // ========================================================================
    // Register all engine materials as material-type items
    Core::ItemRegistry::instance().registerMaterialItems();
    // Load item definitions from items.json (if available)
    Core::ItemRegistry::instance().loadFromFile("resources/items.json");

    inventory = std::make_unique<Core::Inventory>();
    // Start in creative mode with all materials in hotbar
    {
        static const std::vector<std::string> defaultHotbar = {
            "Stone", "Wood", "Metal", "Glass", "Rubber", "Ice", "Cork", "glow", "Leaf", "Default"
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

    apiServer->setItemListHandler([]() -> nlohmann::json {
        auto& registry = Core::ItemRegistry::instance();
        auto ids = registry.getAllItemIds();
        nlohmann::json items = nlohmann::json::array();
        for (const auto& id : ids) {
            if (auto* def = registry.getItem(id)) {
                items.push_back(def->toJson());
            }
        }
        return nlohmann::json{{"items", items}, {"count", items.size()}};
    });

    apiServer->setItemDetailHandler([](const std::string& id) -> nlohmann::json {
        auto& registry = Core::ItemRegistry::instance();
        if (auto* def = registry.getItem(id)) {
            return def->toJson();
        }
        return nlohmann::json{{"error", "Item not found"}, {"id", id}};
    });

    apiServer->setEquipmentGetHandler([this](const std::string& entityId) -> nlohmann::json {
        auto* entity = entityRegistry->getEntity(entityId);
        if (!entity) return nlohmann::json{{"error", "Entity not found"}, {"id", entityId}};
        auto* npc = dynamic_cast<Scene::NPCEntity*>(entity);
        if (!npc) return nlohmann::json{{"error", "Entity is not an NPC"}, {"id", entityId}};
        auto equipped = npc->getEquipment().getAllEquipped();
        nlohmann::json slots = nlohmann::json::object();
        for (const auto& [slot, itemId] : equipped) {
            slots[Core::equipSlotToString(slot)] = itemId;
        }
        return nlohmann::json{
            {"entityId", entityId},
            {"equipment", slots},
            {"totalDamage", npc->getEquipment().getTotalDamage()},
            {"totalReach", npc->getEquipment().getTotalReach()}
        };
    });

    apiServer->setMenuListHandler([this]() -> nlohmann::json {
        if (!renderCoordinator) return nlohmann::json{{"error", "RenderCoordinator not available"}};
        auto* uiSystem = renderCoordinator->getUISystem();
        if (!uiSystem) return nlohmann::json{{"error", "UISystem not initialized"}};
        auto screens = uiSystem->getScreenList();
        nlohmann::json arr = nlohmann::json::array();
        for (auto& [name, visible] : screens) {
            arr.push_back({{"name", name}, {"visible", visible}});
        }
        return nlohmann::json{{"menus", arr}, {"count", arr.size()}};
    });

    // Initialize NPC Manager
    npcManager = std::make_unique<Core::NPCManager>();
    npcManager->setPhysicsWorld(physicsWorld);
    npcManager->setEntityRegistry(entityRegistry.get());
    if (renderCoordinator) {
        npcManager->setLightManager(&renderCoordinator->getLightManager());
        npcManager->setDayNightCycle(&renderCoordinator->getDayNightCycle());
        renderCoordinator->setNPCManager(npcManager.get());
    }
    if (locationRegistry) {
        npcManager->setLocationRegistry(locationRegistry);
    }
    npcManager->setChunkManager(chunkManager);
    if (raycastVisualizer) {
        npcManager->setRaycastVisualizer(raycastVisualizer.get());
    }

    // Initialize Kinematic Voxel Manager (doors, rotating platforms, etc.)
    kinematicVoxelManager = std::make_unique<Core::KinematicVoxelManager>();
    kinematicVoxelManager->setPhysicsWorld(physicsWorld);
    if (renderCoordinator) {
        renderCoordinator->setKinematicVoxelManager(kinematicVoxelManager.get());
    }

    // Initialize Door Manager
    doorManager = std::make_unique<Core::DoorManager>();
    doorManager->setKinematicVoxelManager(kinematicVoxelManager.get());
    doorManager->setPlacedObjectManager(placedObjectManager.get());
    doorManager->setObjectTemplateManager(objectTemplateManager.get());
    if (npcManager) doorManager->setNavGrid(npcManager->getNavGrid());
    doorManager->setSettleCallback([this](const std::string& /*objId*/, bool /*isOpen*/) {
        // NavGrid rebuild is already done inside DoorManager::onSettle()
    });

    // Initialize Dynamic Furniture Manager
    dynamicFurnitureManager = std::make_unique<Core::DynamicFurnitureManager>();
    dynamicFurnitureManager->setPlacedObjectManager(placedObjectManager.get());
    dynamicFurnitureManager->setObjectTemplateManager(objectTemplateManager.get());
    dynamicFurnitureManager->setKinematicVoxelManager(kinematicVoxelManager.get());
    dynamicFurnitureManager->setPhysicsWorld(physicsWorld);
    dynamicFurnitureManager->setChunkManager(chunkManager);
    dynamicFurnitureManager->setGpuParticlePhysics(gpuParticlePhysics.get());

    // Wire furniture activation into the voxel interaction system
    voxelInteractionSystem->setPlacedObjectManager(placedObjectManager.get());
    voxelInteractionSystem->setDynamicFurnitureManager(dynamicFurnitureManager.get());

    // Initialize Interaction Manager
    interactionManager = std::make_unique<Core::InteractionManager>();
    interactionManager->setEntityRegistry(entityRegistry.get());
    interactionManager->setPlacedObjectManager(placedObjectManager.get());

    // Initialize Interaction Profile Manager (per-archetype tuning data)
    interactionProfileManager = std::make_unique<Core::InteractionProfileManager>();
    if (!projectDir_.empty()) {
        interactionProfileManager->setBasePath(projectDir_ + "/resources/interactions/");
    }
    interactionProfileManager->loadArchetype("humanoid_normal");
    interactionManager->setInteractionProfileManager(interactionProfileManager.get());

    // Create interaction handler registry and register handlers
    interactionHandlerRegistry = std::make_unique<Core::InteractionHandlerRegistry>();
    interactionHandlerRegistry->registerHandler("npc",
        std::make_unique<Core::NPCInteractionHandler>(entityRegistry.get()));
    interactionHandlerRegistry->registerHandler("seat",
        std::make_unique<Core::SeatInteractionHandler>(placedObjectManager.get(),
                                                        interactionProfileManager.get()));
    interactionHandlerRegistry->registerHandler("door_handle",
        std::make_unique<Core::DoorInteractionHandler>(doorManager.get()));
    interactionManager->setHandlerRegistry(interactionHandlerRegistry.get());

    // Auto-register interaction point defs from template .voxel file metadata.
    if (placedObjectManager && objectTemplateManager) {
        for (const auto& name : objectTemplateManager->getTemplateNames()) {
            const auto* tmpl = objectTemplateManager->getTemplate(name);
            if (tmpl && !tmpl->interactionPoints.empty()) {
                placedObjectManager->registerTemplateDefs(name, tmpl->interactionPoints);
                LOG_INFO("Application", "Auto-registered {} interaction point(s) for '{}'",
                         tmpl->interactionPoints.size(), name);
            }
        }
        // Fallback: if test_chair has no metadata yet, register a minimal default
        if (placedObjectManager->getMutableTemplateDefs().find("test_chair") ==
            placedObjectManager->getMutableTemplateDefs().end()) {
            placedObjectManager->registerTemplateDefs("test_chair", {
                {"seat_0", "seat",
                 glm::vec3(0.33f, 1.0f, 0.33f),
                 objectTemplateManager->getTemplateFacingYaw("test_chair")}
            });
        }
    }

    // Initialize Combat System
    combatSystem = std::make_unique<Core::CombatSystem>();

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

    // Wire interactive voxel changes to NavGrid so NPCs replan around placed/removed blocks
    if (voxelInteractionSystem && npcManager) {
        voxelInteractionSystem->setVoxelChangeCallback([this](const glm::ivec3& pos) {
            npcManager->onVoxelChanged(pos);
        });
    }

    // INITIALIZE AI CONVERSATION SERVICE (direct LLM client for shipped games)
    {
        aiConversationService = std::make_unique<AI::AIConversationService>(
            storyEngine.get(), entityRegistry.get(), dialogueSystem.get());

        // Get SQLite db handle from WorldStorage (shares the world database)
        sqlite3* worldDb = nullptr;
        auto* worldStorage = chunkManager->m_streamingManager.getWorldStorage();
        if (worldStorage) {
            worldDb = worldStorage->getDb();
        }

        if (worldDb) {
            // Build LLM config from engine settings
            AI::LLMConfig llmConfig;
            llmConfig.provider = engineConfig.aiProvider;
            llmConfig.model = engineConfig.aiModel;
            llmConfig.apiKey = engineConfig.aiApiKey;
            // Fallback: try env var
            if (llmConfig.apiKey.empty()) {
                if (const char* envKey = std::getenv("PHYXEL_AI_API_KEY"); envKey && envKey[0] != '\0')
                    llmConfig.apiKey = envKey;
            }

            if (aiConversationService->initialize(worldDb, llmConfig)) {
                LOG_INFO("Application", "AI Conversation Service initialized (provider={}, configured={})",
                         llmConfig.provider, aiConversationService->isConfigured() ? "yes" : "no");

                // Wire NPCManager for social context (needs, worldview, relationships)
                if (npcManager) {
                    aiConversationService->setNPCManager(npcManager.get());
                }
            } else {
                LOG_WARN("Application", "AI Conversation Service initialization failed (non-critical)");
            }
        } else {
            LOG_INFO("Application", "AI Conversation Service skipped (no world database available)");
        }
    }

    // Load placed objects from database, then recompute interaction points so that
    // chairs/seats restored from a save are immediately interactable.
    if (placedObjectManager && chunkManager) {
        auto* ws = chunkManager->m_streamingManager.getWorldStorage();
        if (ws && ws->getDb()) {
            placedObjectManager->loadFromDb(ws->getDb());
            placedObjectManager->recomputeAllInteractionPoints();
        }
    }

    // Wire interaction callback to start dialogues
    // Priority: 1) Full AI conversation (direct LLM), 2) AI-enhanced tree dialogue, 3) Plain tree dialogue
    interactionManager->setInteractCallback([this](Scene::NPCEntity* npc) {
        if (!dialogueSystem || !npc) return;
        auto* provider = npc->getDialogueProvider();

        // OPTION 1: Full AI conversation via direct LLM client
        // Used when NPC is in AI mode and AIConversationService is configured with an API key
        if (provider && provider->isAIMode() &&
            aiConversationService && aiConversationService->isConfigured()) {
            std::string npcId = entityRegistry ? entityRegistry->getEntityId(npc) : npc->getName();
            std::string npcName = npc->getName();
            if (aiConversationService->startConversation(npc, npcId, npcName)) {
                LOG_INFO("Application", "Started AI conversation with '{}' (direct LLM)", npcName);
                return;
            }
            LOG_WARN("Application", "AI conversation failed for '{}', falling back to tree dialogue", npcName);
        }

        // OPTION 2 & 3: Tree-based dialogue (with optional AI enhancement)
        const UI::DialogueTree* tree = provider ? provider->getDialogueTree() : nullptr;
        if (!tree) {
            LOG_INFO("Application", "NPC '{}' has no dialogue tree", npc->getName());
            return;
        }

        // Set up AI enhancement if this NPC has AI capabilities and AIEnhancer is ready
        if (provider->isAIMode() && aiEnhancer && aiEnhancer->isReady()) {
            auto* ds = dialogueSystem.get();
            auto* enhancer = aiEnhancer.get();
            // Wire AI enhancement: when a tree node loads, ask AI to rephrase in character
            ds->setAIEnhanceCallback([enhancer, ds](const std::string& nodeId,
                                                     const std::string& nodeText,
                                                     const std::string& speaker,
                                                     const std::string& emotion) {
                enhancer->enhance(nodeId, nodeText, speaker, emotion,
                    [ds](const std::string& nid, const std::string& text, const std::string& emo) {
                        ds->receiveAIEnhancement(nid, text, emo);
                    });
            });
        } else {
            dialogueSystem->setAIEnhanceCallback(nullptr);
        }

        dialogueSystem->startConversation(npc, tree);
    });

    // Wire seat callback: player presses E near a seat â†’ animated character sits
    interactionManager->setSeatCallback([this](const std::string& objectId,
                                               const std::string& pointId,
                                               const glm::vec3& seatAnchorPos,
                                               float facingYaw,
                                               const glm::vec3& sitDownOffset,
                                               const glm::vec3& sittingIdleOffset,
                                               const glm::vec3& sitStandUpOffset,
                                               float sitBlendDuration,
                                               float seatHeightOffset) {
        if (!animatedCharacter) return;
        animatedCharacter->sitAt(seatAnchorPos, facingYaw,
                                 sitDownOffset, sittingIdleOffset, sitStandUpOffset,
                                 sitBlendDuration, seatHeightOffset);
    });

    // Wire door callback: player presses E near a door handle â†’ toggle door
    interactionManager->setDoorCallback([this](const std::string& objectId,
                                               const std::string& /*pointId*/) {
        if (doorManager) doorManager->toggle(objectId);
    });

    // Start the API server
    if (apiServer->start()) {
        LOG_INFO("Application", "Engine HTTP API available at http://localhost:{}/api/status", engineConfig.apiPort);
    } else {
        LOG_ERROR("Application", "Failed to start HTTP API server on port {}  --  another engine instance may already be running.", engineConfig.apiPort);
#ifdef _WIN32
        std::string msg = "Cannot start the engine API server on port " + std::to_string(engineConfig.apiPort)
            + ".\n\nAnother instance of the Phyxel engine is likely already running.\n"
            "Please close the other instance and try again.";
        MessageBoxA(nullptr, msg.c_str(), "Phyxel  --  Port Conflict", MB_OK | MB_ICONERROR);
#endif
        return false;
    }

    m_initialized = true;

    // STEP 10.5: PROJECT LAUNCHER (if no project specified via CLI)
    // Skip launcher entirely when running in asset-editor mode.
    // Initialize the launcher so it can be rendered inside run()'s normal loop.
    LOG_INFO("Application", "Asset editor mode: {}, Anim editor mode: {}, Interaction editor mode: {}, projectDir: '{}'",
             m_assetEditorMode ? "ON" : "OFF", m_animEditorMode ? "ON" : "OFF",
             m_interactionEditorMode ? "ON" : "OFF", projectDir_);
    if (projectDir_.empty() && !m_assetEditorMode && !m_animEditorMode && !m_interactionEditorMode) {
        namespace fs = std::filesystem;
        std::string baseDir = Core::ProjectInfo::getDefaultProjectsDir();
        fs::create_directories(baseDir);

        projectLauncher_ = std::make_unique<ProjectLauncher>();
        projectLauncher_->initialize(baseDir);
        launcherActive_ = true;

        LOG_INFO("Application", "Project launcher will show on first frame (projects dir: {})", baseDir);
    }

    // STEP 11: AUTO-LOAD GAME DEFINITION (after all subsystems are ready)
    // Deferred until the launcher selects a project when launcherActive_ is true.
    if (!launcherActive_) {
        if (m_assetEditorMode) {
            initAssetEditorScene();
        } else if (m_animEditorMode) {
            initAnimEditorScene();
        } else if (m_interactionEditorMode) {
            initInteractionEditorScene();
        } else {
            autoLoadGameDefinition();
        }
    }

    // Create the properties inspector panel
    m_propertiesPanel = std::make_unique<Editor::PropertiesPanel>();
    m_propertiesPanel->setVoxelInteraction(voxelInteractionSystem.get());
    m_propertiesPanel->setEntityRegistry(entityRegistry.get());
    m_propertiesPanel->setPlacedObjectManager(placedObjectManager.get());
    m_propertiesPanel->setNPCManager(npcManager.get());
    m_propertiesPanel->setObjectTemplateManager(objectTemplateManager.get());
    m_propertiesPanel->setDynamicFurnitureManager(dynamicFurnitureManager.get());

    // Create the world outliner panel
    m_worldOutliner = std::make_unique<Editor::WorldOutlinerPanel>();

    // Create the camera management panel
    m_cameraPanel = std::make_unique<Editor::CameraPanel>();
    m_cameraPanel->setCameraManager(cameraManager);
    m_cameraPanel->setCamera(camera);
    m_cameraPanel->setEntityRegistry(entityRegistry.get());
    m_cameraPanel->setInputManager(inputManager);

    // Create the texture editor panel
    m_textureEditor = std::make_unique<Editor::TextureEditorPanel>();
    m_textureEditor->setVulkanDevice(vulkanDevice);

    m_worldOutliner->setEntityRegistry(entityRegistry.get());
    m_worldOutliner->setNPCManager(npcManager.get());
    m_worldOutliner->setPlacedObjectManager(placedObjectManager.get());
    m_worldOutliner->setChunkManager(chunkManager);
    m_worldOutliner->setObjectTemplateManager(objectTemplateManager.get());

    // Wire add/remove callbacks from the World Outliner panel
    m_worldOutliner->onRemoveEntity = [this](const std::string& id) -> bool {
        auto* entity = entityRegistry->getEntity(id);
        if (!entity) return false;
        auto it = std::remove_if(entities.begin(), entities.end(),
            [entity](const std::unique_ptr<Scene::Entity>& e) { return e.get() == entity; });
        if (it != entities.end()) entities.erase(it, entities.end());
        if (entity == animatedCharacter) animatedCharacter = nullptr;
        entityRegistry->unregisterEntity(id);
        return true;
    };

    m_worldOutliner->onSpawnEntity = [this](const std::string& type, const glm::vec3& pos) -> std::string {
        Scene::Entity* spawned = nullptr;
        if (type == "animated" || type == "physics" || type == "spider")
            spawned = createAnimatedCharacter(pos, "resources/animated_characters/humanoid.anim");
        if (!spawned || !entityRegistry) return "";
        std::string id = entityRegistry->registerEntity(spawned);
        return id;
    };

    m_worldOutliner->onRemoveNPC = [this](const std::string& name) -> bool {
        if (!npcManager) return false;
        return npcManager->removeNPC(name);
    };

    m_worldOutliner->onSpawnNPC = [this](const std::string& name, const glm::vec3& pos,
                                          const std::string& behavior) -> bool {
        if (!npcManager) return false;
        Core::NPCBehaviorType bt = Core::NPCBehaviorType::Idle;
        if (behavior == "patrol") bt = Core::NPCBehaviorType::Patrol;
        else if (behavior == "wander") bt = Core::NPCBehaviorType::BehaviorTree;
        auto* npc = npcManager->spawnNPC(name, "resources/animated_characters/humanoid.anim",
                                          pos, bt, {}, 2.0f, 2.0f, {});
        return npc != nullptr;
    };

    m_worldOutliner->onRemovePlacedObject = [this](const std::string& id) -> bool {
        if (!placedObjectManager) return false;
        return placedObjectManager->remove(id);
    };

    m_worldOutliner->onSpawnTemplate = [this](const std::string& name, const glm::ivec3& pos,
                                               int rotation) -> std::string {
        if (!placedObjectManager) return "";
        std::string objId = placedObjectManager->placeTemplate(name, pos, rotation, "");
        // Persist to DB so objects survive engine restart
        if (chunkManager) {
            auto* ws = chunkManager->m_streamingManager.getWorldStorage();
            if (ws) placedObjectManager->saveToDb(ws->getDb());
        }
        return objId;
    };

#ifdef _WIN32
    // Create the terminal panel and assign monospace font
    m_terminalPanel = std::make_unique<Editor::TerminalPanel>();
    if (imguiRenderer && imguiRenderer->getMonospaceFont()) {
        m_terminalPanel->setFont(imguiRenderer->getMonospaceFont());
    }
#endif

    return true;
}

// ============================================================================
// PROJECT LAUNCHER
// ============================================================================

void Application::onLauncherResult(const LauncherResult& result) {
    namespace fs = std::filesystem;
    std::string baseDir = Core::ProjectInfo::getDefaultProjectsDir();

    switch (result.action) {
        case LauncherResult::Action::Open: {
            applyProjectSelection(result.projectPath);
            Core::ProjectInfo info;
            info.path = result.projectPath;
            info.name = fs::path(result.projectPath).filename().string();
            projectLauncher_->getState().addRecentProject(info);
            projectLauncher_->getState().save(baseDir);
            projectLauncher_.reset();
            launcherActive_ = false;
            autoLoadGameDefinition();
            break;
        }
        case LauncherResult::Action::Create: {
            std::string path = Core::ProjectInfo::scaffoldProject(result.newProjectName, baseDir);
            if (path.empty()) {
                LOG_ERROR("Application", "Failed to create project '{}'", result.newProjectName);
                projectLauncher_->refresh();
                return; // Stay on launcher
            }
            applyProjectSelection(path);
            Core::ProjectInfo info;
            info.path = path;
            info.name = result.newProjectName;
            projectLauncher_->getState().addRecentProject(info);
            projectLauncher_->getState().save(baseDir);
            projectLauncher_.reset();
            launcherActive_ = false;
            autoLoadGameDefinition();
            break;
        }
        case LauncherResult::Action::Cancel:
            projectLauncher_.reset();
            launcherActive_ = false;
            break;
        default:
            break;
    }
}

void Application::applyProjectSelection(const std::string& projectPath) {
    namespace fs = std::filesystem;
    fs::path projPath(projectPath);

    projectDir_ = projectPath;
    engineConfig.projectDir = projectPath;
    engineConfig.worldsDir = (projPath / "worlds").string();

    // Load project's engine.json for window title
    auto projConfigPath = projPath / "engine.json";
    if (fs::exists(projConfigPath)) {
        Core::EngineConfig projCfg;
        Core::EngineConfig::loadFromFile(projConfigPath.string(), projCfg);
        if (projCfg.windowTitle != "Phyxel") {
            engineConfig.windowTitle = projCfg.windowTitle;
            windowManager->setTitle(projCfg.windowTitle);
        }
    }

    // Set game definition file if present
    auto gameJsonPath = projPath / "game.json";
    if (fs::exists(gameJsonPath)) {
        engineConfig.gameDefinitionFile = gameJsonPath.string();
    }

    // Update runtime config
    runtime->getConfigMutable().projectDir = projectPath;
    runtime->getConfigMutable().worldsDir = engineConfig.worldsDir;
    runtime->getConfigMutable().gameDefinitionFile = engineConfig.gameDefinitionFile;

    // ----------------------------------------------------------------
    // Switch the world: clear the chunks loaded from the default world,
    // open the project's database, and reload from it.
    // This must happen before autoLoadGameDefinition() which checks
    // whether chunks are already loaded.
    // ----------------------------------------------------------------
    if (chunkManager && vulkanDevice) {
        // Wait for any in-flight GPU work to finish before destroying chunk buffers
        vkDeviceWaitIdle(vulkanDevice->getDevice());

        // Drop all currently loaded chunks (clears the chunks + chunkMap vectors)
        chunkManager->cleanup();

        // Switch WorldStorage to the project's database
        std::string newDbPath = engineConfig.worldsDir + "/default.db";
        fs::create_directories(engineConfig.worldsDir);
        chunkManager->initializeWorldStorage(newDbPath);

        // Load whatever the project's DB already has (may be empty for new projects)
        auto loaded = chunkManager->loadAllChunksFromDatabase();
        if (!loaded.empty()) {
            chunkManager->rebuildAllChunkFaces();
            chunkManager->initializeAllChunkVoxelMaps();
            LOG_INFO("Application", "Loaded {} chunk(s) from project world database", loaded.size());
        } else {
            LOG_INFO("Application", "Project world database is empty  --  world will be built from game.json");
        }
    }

    LOG_INFO("Application", "Project selected: {} (worlds: {})", projectPath, engineConfig.worldsDir);
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
        
        // Always process API commands (even during launcher  --  enables MCP project management)
        processAPICommands();

        // Process deferred file open (set by File > Open menu, must run before rendering)
        if (!m_pendingOpenFile.empty()) {
            std::string fileToOpen = std::move(m_pendingOpenFile);
            m_pendingOpenFile.clear();
            switchToEditorMode(fileToOpen);
        }

        // Process deferred project open (set by File > Open Project menu)
        if (!m_pendingOpenProject.empty()) {
            std::string projPath = std::move(m_pendingOpenProject);
            m_pendingOpenProject.clear();

            // Reset any editor mode back to normal project mode
            resetEditorScene();

            // Apply project selection (switches DB, loads engine.json, etc.)
            applyProjectSelection(projPath);

            // Track in recent projects
            namespace fs = std::filesystem;
            std::string baseDir = Core::ProjectInfo::getDefaultProjectsDir();
            Core::ProjectInfo info;
            info.path = projPath;
            info.name = fs::path(projPath).filename().string();
            // Save to launcher state if available
            {
                Core::LauncherState state;
                state.load(baseDir);
                state.addRecentProject(info);
                state.save(baseDir);
            }

            autoLoadGameDefinition();
            LOG_INFO("Application", "Opened project from File menu: {}", projPath);
        }

        // Skip game input and update while launcher is active
        if (!launcherActive_) {
        // Route input to custom UI system first (consumes input when menus are visible)
        bool uiConsumedInput = false;
        if (renderCoordinator && renderCoordinator->getUISystem()) {
            uiConsumedInput = renderCoordinator->getUISystem()->handleInput(inputManager);
        }

        {
            ScopedTimer inputTimer(*performanceProfiler, "input");
            // Feed viewport hover state to InputManager so it can gate mouse/keyboard
            inputManager->setViewportHovered(m_viewportHovered);

            if (!uiConsumedInput) {
                handleInput();
            } else {
                // Still process input manager for key actions (like menu toggle)
                inputManager->processInput(deltaTime);
            }
        }
        
        {
            ScopedTimer updateTimer(*performanceProfiler, "update");
            update(deltaTime);
        }
        } // end if (!launcherActive_)
        
        // Start ImGui frame
        imguiRenderer->newFrame();
        
        // Create full-window DockSpace for editor layout
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGuiWindowFlags dockWindowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_MenuBar;
            ImGui::Begin("##DockSpaceWindow", nullptr, dockWindowFlags);
            ImGui::PopStyleVar(3);

            // Render main menu bar
            renderMainMenuBar();
            
            ImGuiID dockSpaceId = ImGui::GetID("EditorDockSpace");
            ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
            
            // Handle layout reset request from View menu
            if (m_needsLayoutReset) {
                m_dockLayoutInitialized = false;
                m_needsLayoutReset = false;
            }

            // Set up default layout on first run
            setupDefaultDockLayout(dockSpaceId);
            
            ImGui::End();

            // Status bar at bottom of window
            renderStatusBar();

            // Render 3D viewport as a dockable window
            renderDockableViewport();
            
            // Render world outliner panel (before Properties so selection is fresh)
            if (m_worldOutliner && m_showWorldOutliner) {
                m_worldOutliner->render(&m_showWorldOutliner);
            }

            // Pipe World Outliner selection into Properties panel
            if (m_propertiesPanel && m_worldOutliner) {
                const auto& sel = m_worldOutliner->selectedId();
                if (sel.empty()) {
                    m_propertiesPanel->clearSelection();
                } else if (sel.rfind("npc:", 0) == 0) {
                    m_propertiesPanel->setSelection(Editor::SelectionType::NPC, sel.substr(4));
                } else if (sel.rfind("po:", 0) == 0) {
                    m_propertiesPanel->setSelection(Editor::SelectionType::PlacedObject, sel.substr(3));
                } else {
                    m_propertiesPanel->setSelection(Editor::SelectionType::Entity, sel);
                }
            }

            // Render properties inspector panel
            if (m_propertiesPanel && m_showProperties) {
                m_propertiesPanel->setCameraState(camera->getPosition(), camera->getFront());
                m_propertiesPanel->render(&m_showProperties);
            }

            // Render camera management panel
            if (m_cameraPanel && m_showCameraPanel) {
                m_cameraPanel->render(&m_showCameraPanel);
            }

#ifdef _WIN32
            // Render terminal panel as a dockable window
            if (m_terminalPanel && m_showTerminal) {
                m_terminalPanel->render(&m_showTerminal);
            }
#endif
        }

        // Render Asset Editor panel (when launched with --asset-editor)
        renderAssetEditorUI();

        // Render Anim Editor panel (when launched with --anim-editor)
        renderAnimEditorUI();

        // Render Interaction Editor panel (when launched with --interaction-editor)
        renderInteractionEditorUI();
        
        // -- Project Launcher overlay (replaces game UI when active) --
        if (launcherActive_ && projectLauncher_) {
            LauncherResult result;
            if (projectLauncher_->render(result)) {
                onLauncherResult(result);
            }
        } else {
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

        // Voxel size mode HUD (persistent indicator + fade label on change)
        if (voxelInteractionSystem && inputController) {
            imguiRenderer->renderVoxelSizeHUD(
                voxelInteractionSystem->getTargetMode(),
                inputController->getModeChangeTimer(),
                m_viewportPosX, m_viewportPosY, m_viewportSizeW, m_viewportSizeH);
        }

        // Render AI Stats overlay (inside performance overlay toggle)
        if (showPerformanceOverlay && aiConversationService) {
            if (auto* client = aiConversationService->getLLMClient()) {
                auto usage = client->getTokenUsage();
                const auto& cfg = client->getConfig();
                ImGui::SetNextWindowPos(ImVec2(10, 420), ImGuiCond_FirstUseEver);
                ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("AI Stats", nullptr, ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "LLM STATUS");
                    ImGui::Separator();
                    ImGui::Text("Configured: %s", client->isConfigured() ? "Yes" : "No");
                    ImGui::Text("Provider:   %s", cfg.provider.c_str());
                    if (!cfg.model.empty())
                        ImGui::Text("Model:      %s", cfg.model.c_str());
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "TOKEN USAGE");
                    ImGui::Text("API Calls:   %lld", static_cast<long long>(usage.totalCalls));
                    ImGui::Text("Input Tokens: %lld", static_cast<long long>(usage.totalInput));
                    ImGui::Text("Output Tokens: %lld", static_cast<long long>(usage.totalOutput));
                    ImGui::Text("Total Tokens: %lld", static_cast<long long>(usage.totalInput + usage.totalOutput));
                }
                ImGui::End();
            }
        }
        
        // Check if distances were changed by UI
        if (currentRenderDistance != maxChunkRenderDistance) {
            setRenderDistance(currentRenderDistance);
        }
        if (currentChunkInclusionDistance != chunkInclusionDistance) {
            setChunkInclusionDistance(currentChunkInclusionDistance);
        }

        // Render Scripting Console
        imguiRenderer->renderScriptingConsole(showScriptingConsole, scriptingSystem.get());

        // Render Character Customizer
        renderCharacterCustomizer();

        // Render Interaction Point Tuner
        renderInteractionTuner();

        // Render Template Spawner
        renderTemplateSpawner();
        renderClickActions();

        // Render Texture Editor
        if (m_textureEditor && m_showTextureEditor) {
            m_textureEditor->render(&m_showTextureEditor);
        }
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
                bool showPrompt = !dialogueSystem || !dialogueSystem->isActive();
                if (showPrompt) {
                    const std::string& promptLabel = interactionManager->getActivePromptText();
                    glm::vec3 promptWorldPos = interactionManager->getActivePromptWorldPos();

                    // NPC: render above head in world-space; seats/doors: bottom-center HUD
                    auto* nearestNPC = interactionManager->getNearestInteractableNPC();
                    if (nearestNPC) {
                        imguiRenderer->renderInteractionPrompt(
                            true,
                            promptWorldPos,
                            cachedViewMatrix, cachedProjectionMatrix, sw, sh,
                            ("[E] " + promptLabel).c_str());
                    } else {
                        // Unified HUD prompt for seats, doors, and other interaction points
                        ImGui::SetNextWindowPos(ImVec2(sw * 0.5f, sh * 0.75f),
                                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                        ImGui::SetNextWindowBgAlpha(0.65f);
                        ImGui::Begin("##interaction_prompt", nullptr,
                            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
                        ImGui::Text("[E] %s", promptLabel.c_str());
                        ImGui::End();
                    }
                }
            }
        }

        // Render Lighting Controls
        if (renderCoordinator) {
            renderCoordinator->renderUI();
        }

        // Render D&D Combat HUD (initiative order, HP bars, whose-turn indicator)
        imguiRenderer->renderCombatHUD(&m_rpgInitiative, &m_rpgParty, entityRegistry.get());

        // Render Death overlay
        if (respawnSystem.isPlayerDead()) {
            UI::renderDeathOverlay(respawnSystem.getDeathTimer(),
                                   respawnSystem.getRespawnDelay(),
                                   respawnSystem.getDeathCount());
        }

        // Render Objective HUD (top-right corner)
        UI::renderObjectiveHUD(&objectiveTracker);

        // Render Pause Menu overlay
        if (gamePaused) {
            UI::PauseMenuActions pauseActions;
            pauseActions.onResume = [this]() { setPaused(false); };
            pauseActions.onSettings = [this]() { toggleGameMenu("settings"); };
            pauseActions.onMainMenu = nullptr;
            pauseActions.onQuit = [this]() { quit(); };
            UI::renderPauseMenu(pauseActions);
        }
        } // end else (game UI)
        
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

    // Drain pending API commands so blocked HTTP handlers can return
    // (prevents shutdown hang from unfulfilled promises)
    if (apiCommandQueue) {
        std::vector<Core::APICommand> pending;
        apiCommandQueue->drainCommands(pending);
        for (auto& cmd : pending) {
            if (cmd.onComplete) {
                cmd.onComplete({{"error", "Engine shutting down"}});
            }
        }
    }

    // Shutdown HTTP API server (background thread must stop before systems go away)
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
    
    // Clear entities BEFORE physics cleanup  --  they hold raw pointers to physics bodies
    entities.clear();

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

    // Skip simulation when paused (API commands and jobs still process)
    if (gamePaused) return;

    // Update GPU particle physics (CPU-side slot tracking + staging upload)
    if (gpuParticlePhysics) {
        // Feed character AABB to GPU so particles collide with the player
        if (animatedCharacter) {
            auto pos = animatedCharacter->getPosition(); // feet position
            float halfH = animatedCharacter->getControllerHalfHeight();
            float halfW = animatedCharacter->getControllerHalfWidth();
            glm::vec3 center = pos + glm::vec3(0.0f, halfH, 0.0f);
            glm::vec3 halfExt(halfW, halfH, halfW);
            glm::vec3 vel = animatedCharacter->getControllerVelocity();
            gpuParticlePhysics->setCharacterAABB(center, halfExt, vel);
        } else {
            gpuParticlePhysics->clearCharacterAABB();
        }
        gpuParticlePhysics->update(deltaTime);
    }

    // Update respawn system (handles death timer and auto-respawn)
    respawnSystem.update(deltaTime);

    // Advance music playlist (plays next track when current finishes)
    musicPlaylist.update(audioSystem);

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

    // Update combat system (invulnerability timers)
    if (combatSystem) {
        combatSystem->update(deltaTime);
    }

    // Drive enemy turns in D&D initiative combat
    m_combatAI.tick(deltaTime);

    // Update interaction detection (use actual player position, not camera)
    if (interactionManager) {
        glm::vec3 playerPos(0);
        glm::vec3 playerFront(0, 0, 1);
        if (animatedCharacter && currentControlTarget == ControlTarget::AnimatedCharacter)
            playerPos = animatedCharacter->getPosition();
        else if (camera)
            playerPos = camera->getPosition();
        if (camera)
            playerFront = camera->getFront();
        interactionManager->update(deltaTime, playerPos, playerFront);
    }

    // Update door animations (drives kinematic voxel transforms)
    if (doorManager) {
        doorManager->update(deltaTime);
    }

    // Update dynamic furniture (sync physics transforms, re-staticize on rest)
    if (dynamicFurnitureManager) {
        dynamicFurnitureManager->update(deltaTime);
        // Update grabbed furniture position to track camera
        if (dynamicFurnitureManager->isGrabbing() && inputManager) {
            dynamicFurnitureManager->updateGrabPosition(
                inputManager->getCameraPosition(),
                inputManager->getCameraFront());
        }
        // Sprint-to-move: check player collision with furniture while sprinting
        // Use character body position (feet), not camera, for accurate AABB overlap
        if (inputManager && deltaTime > 0.0f) {
            bool sprinting = inputManager->isKeyPressed(GLFW_KEY_LEFT_SHIFT) ||
                             inputManager->isKeyPressed(GLFW_KEY_RIGHT_SHIFT);
            glm::vec3 charPos(0.0f);
            glm::vec3 charVel(0.0f);
            bool hasChar = false;
            if (animatedCharacter) {
                charPos = animatedCharacter->getPosition(); // feet position
                // Approximate body center (half height up)
                charPos.y += animatedCharacter->getControllerHalfHeight();
                // Velocity from camera delta (character doesn't expose velocity)
                glm::vec3 camPos = inputManager->getCameraPosition();
                charVel = (camPos - lastCameraPos) / deltaTime;
                charVel.y = 0.0f; // Only horizontal movement matters
                hasChar = true;
            }
            if (hasChar) {
                dynamicFurnitureManager->checkPlayerFurnitureCollision(charPos, charVel, sprinting);
            }
        }
    }

    // Update dialogue & speech bubbles
    if (dialogueSystem) dialogueSystem->update(deltaTime);
    if (speechBubbleManager) speechBubbleManager->update(deltaTime);

    // Update entities
    {
        PROFILE_SCOPE(*performanceProfiler, "Entities");
        // Track animated character sitting state to release seat when stand-up completes
        bool wasAnimCharSitting = animatedCharacter && animatedCharacter->isSitting();
        for (auto& entity : entities) {
            entity->update(deltaTime);
        }
        // If animated character just finished standing up, release its seat claim
        if (wasAnimCharSitting && animatedCharacter && !animatedCharacter->isSitting()) {
            if (interactionManager) interactionManager->releaseSeat("player");
        }

        // Remove character once all its voxels have been derezed
        if (animatedCharacter && animatedCharacter->isFullyDerezed()) {
            LOG_INFO("Application", "Derez complete - removing character from scene");
            // Unregister from entity registry BEFORE erasing the entity, so nothing
            // (e.g. WorldOutliner) can dereference the dangling pointer after deletion.
            if (entityRegistry) {
                std::string entityId = entityRegistry->getEntityId(animatedCharacter);
                if (!entityId.empty()) entityRegistry->unregisterEntity(entityId);
            }
            auto it = std::remove_if(entities.begin(), entities.end(),
                [this](const std::unique_ptr<Scene::Entity>& e) {
                    return e.get() == animatedCharacter;
                });
            if (it != entities.end()) entities.erase(it, entities.end());
            animatedCharacter = nullptr;
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
        } else {
            camera->setYaw(inputManager->getYaw());
            camera->setPitch(inputManager->getPitch());

            if (currentControlTarget == ControlTarget::AnimatedCharacter && animatedCharacter) {
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
        // Block voxel hover when ImGui wants the mouse (e.g. hovering any panel),
        // or entirely in anim-editor mode (no voxel editing there).
        bool blockHover = inputManager->isMouseCaptured()
                       || (ImGui::GetIO().WantCaptureMouse && !m_viewportHovered)
                       || m_animEditorMode;
        // Remap mouse from window-space to viewport-local, then scale to
        // full-window coords so the existing ray calculation stays correct.
        double remappedX = (mouseX - m_viewportPosX) / m_viewportSizeW * windowManager->getWidth();
        double remappedY = (mouseY - m_viewportPosY) / m_viewportSizeH * windowManager->getHeight();
        voxelInteractionSystem->updateMouseHover(
            inputManager->getCameraPosition(),
            inputManager->getCameraFront(),
            inputManager->getCameraUp(),
            remappedX,
            remappedY,
            blockHover
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

            // Shared wireframe box helper for debug visualization
            auto addWireBox = [&](const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& col) {
                raycastVisualizer->addLine({mn.x,mn.y,mn.z},{mx.x,mn.y,mn.z},col);
                raycastVisualizer->addLine({mx.x,mn.y,mn.z},{mx.x,mn.y,mx.z},col);
                raycastVisualizer->addLine({mx.x,mn.y,mx.z},{mn.x,mn.y,mx.z},col);
                raycastVisualizer->addLine({mn.x,mn.y,mx.z},{mn.x,mn.y,mn.z},col);
                raycastVisualizer->addLine({mn.x,mx.y,mn.z},{mx.x,mx.y,mn.z},col);
                raycastVisualizer->addLine({mx.x,mx.y,mn.z},{mx.x,mx.y,mx.z},col);
                raycastVisualizer->addLine({mx.x,mx.y,mx.z},{mn.x,mx.y,mx.z},col);
                raycastVisualizer->addLine({mn.x,mx.y,mx.z},{mn.x,mx.y,mn.z},col);
                raycastVisualizer->addLine({mn.x,mn.y,mn.z},{mn.x,mx.y,mn.z},col);
                raycastVisualizer->addLine({mx.x,mn.y,mn.z},{mx.x,mx.y,mn.z},col);
                raycastVisualizer->addLine({mx.x,mn.y,mx.z},{mx.x,mx.y,mx.z},col);
                raycastVisualizer->addLine({mn.x,mn.y,mx.z},{mn.x,mx.y,mx.z},col);
            };

            // Draw character controller box only (segment boxes drawn by AnimatedVoxelCharacter itself)
            auto drawCharacterHitbox = [&](Scene::AnimatedVoxelCharacter* ch, const glm::vec3& controllerColor) {
                if (!ch) return;
                glm::vec3 feet = ch->getPosition();
                float hh = ch->getControllerHalfHeight();
                float hw = ch->getControllerHalfWidth();
                glm::vec3 cMin = feet - glm::vec3(hw, 0.0f, hw);
                glm::vec3 cMax = feet + glm::vec3(hw, 2.0f * hh, hw);
                addWireBox(cMin, cMax, controllerColor);
            };

            // Player character controller box (green)
            if (animatedCharacter) {
                drawCharacterHitbox(animatedCharacter, {0.0f, 1.0f, 0.0f});
            }
            // NPC character controller boxes (cyan)
            if (npcManager) {
                for (const auto& name : npcManager->getAllNPCNames()) {
                    auto* npc = npcManager->getNPC(name);
                    if (npc) {
                        drawCharacterHitbox(npc->getAnimatedCharacter(), {0.0f, 1.0f, 1.0f});
                    }
                }
            }

            // Door hinge pivot debug lines (yellow vertical + magenta cross)
            if (doorManager) {
                const glm::vec3 hingeColor{1.0f, 1.0f, 0.0f};  // yellow
                const glm::vec3 axisColor{1.0f, 0.0f, 1.0f};   // magenta
                for (const auto& [id, door] : doorManager->getDoors()) {
                    const glm::vec3& h = door.worldHingePos;
                    // Vertical line through hinge (Y-axis = rotation axis)
                    raycastVisualizer->addLine(h - glm::vec3(0, 0.5f, 0), h + glm::vec3(0, 4.0f, 0), hingeColor);
                    // X/Z cross at hinge base
                    raycastVisualizer->addLine(h + glm::vec3(-0.5f, 0, 0), h + glm::vec3(0.5f, 0, 0), axisColor);
                    raycastVisualizer->addLine(h + glm::vec3(0, 0, -0.5f), h + glm::vec3(0, 0, 0.5f), axisColor);
                    // X/Z cross at hinge top
                    raycastVisualizer->addLine(h + glm::vec3(-0.5f, 4.0f, 0), h + glm::vec3(0.5f, 4.0f, 0), axisColor);
                    raycastVisualizer->addLine(h + glm::vec3(0, 4.0f, -0.5f), h + glm::vec3(0, 4.0f, 0.5f), axisColor);
                }
            }

            // Dynamic furniture compound shape AABB (orange)
            if (dynamicFurnitureManager) {
                const glm::vec3 furnitureColor{1.0f, 0.6f, 0.1f};
                for (const auto& [id, obj] : dynamicFurnitureManager->getActiveObjects()) {
                    if (!obj.voxelBody) continue;
                    glm::vec3 mn, mx;
                    obj.voxelBody->getWorldAABB(mn, mx);
                    addWireBox(mn, mx, furnitureColor);
                }
            }

            // Placed object bounding boxes (dim yellow, only for nearby objects)
            if (placedObjectManager && camera) {
                const glm::vec3 placedColor{0.8f, 0.8f, 0.2f};
                glm::vec3 camPos = camera->getPosition();
                for (const auto& [id, placed] : placedObjectManager->getAllObjects()) {
                    // Only draw nearby objects (within 30 units)
                    glm::vec3 center = glm::vec3(placed.boundingMin + placed.boundingMax) * 0.5f;
                    if (glm::length(center - camPos) > 30.0f) continue;
                    glm::vec3 mn(placed.boundingMin);
                    glm::vec3 mx(placed.boundingMax);
                    addWireBox(mn, mx, placedColor);
                }
            }

            raycastVisualizer->updateBuffers(renderCoordinator->getCurrentFrame());
        }

    }
    
    // Update chunks that have been modified (for hover color changes, etc.)
    // OPTIMIZED: Only update chunks that have actually changed
    if (chunkManager) {
        chunkManager->updateDirtyChunks();
        // Reset per-frame break counter for hybrid physics routing
        chunkManager->resetFrameBreakCounter();
        // Update player position for hybrid Bullet/GPU proximity routing
        if (camera) chunkManager->setPlayerPosition(camera->getPosition());
        // Bullet dynamic object update  --  always runs in hybrid mode since
        // nearby cubes use Bullet while mass debris uses GPU particles.
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
    
    // Sync kinematic door/platform colliders into Bullet before physics step
    if (kinematicVoxelManager) {
        kinematicVoxelManager->syncCollidersToPhysics();
    }

    // Run physics in fixed timesteps
    while (physicsDeltaAccumulator >= FIXED_TIMESTEP) {
        physicsWorld->stepSimulation(FIXED_TIMESTEP, 1, FIXED_TIMESTEP); // Pure fixed timestep
        physicsDeltaAccumulator -= FIXED_TIMESTEP;
    }
    
    auto physicsEnd = std::chrono::high_resolution_clock::now();
    
    // Update dynamic subcube positions from physics bodies (batched + throttled)
    // In hybrid mode, Bullet objects need position sync alongside GPU particles.
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

    // ---- Asset Editor input ----
    if (m_assetEditorMode && inputManager) {
        // H: toggle reference character
        bool hNow = inputManager->isKeyPressed(GLFW_KEY_H);
        if (hNow && !m_assetEditorHPrev) {
            if (m_assetRefCharVisible) {
                entities.erase(
                    std::remove_if(entities.begin(), entities.end(),
                        [this](const std::unique_ptr<Scene::Entity>& e) {
                            return e.get() == m_assetRefChar;
                        }),
                    entities.end());
                m_assetRefChar = nullptr;
                m_assetRefCharVisible = false;
            } else {
                auto& assets = Core::AssetManager::instance();
                std::string animPath = assets.resolveAnimatedChar("humanoid.anim");
                glm::vec3 refPos(m_assetTemplateOrigin.x + 4, m_assetTemplateOrigin.y, m_assetTemplateOrigin.z);
                m_assetRefChar = createAnimatedCharacter(refPos, animPath);
                if (m_assetRefChar) {
                    m_assetRefChar->playAnimation("idle");
                    m_assetRefCharVisible = true;
                }
            }
        }
        m_assetEditorHPrev = hNow;

        // Ctrl+S: save template
        bool ctrlDown = inputManager->isKeyPressed(GLFW_KEY_LEFT_CONTROL) ||
                        inputManager->isKeyPressed(GLFW_KEY_RIGHT_CONTROL);
        bool sNow = ctrlDown && inputManager->isKeyPressed(GLFW_KEY_S);
        if (sNow && !m_assetEditorCtrlSPrev) {
            saveAssetTemplate();
        }
        m_assetEditorCtrlSPrev = sNow;
    }

    if (m_animEditorMode) {
        bool ctrlDown = inputManager->isKeyPressed(GLFW_KEY_LEFT_CONTROL) ||
                        inputManager->isKeyPressed(GLFW_KEY_RIGHT_CONTROL);
        bool sNow = ctrlDown && inputManager->isKeyPressed(GLFW_KEY_S);
        if (sNow && !m_animEditorCtrlSPrev) {
            saveAnimModel();
        }
        m_animEditorCtrlSPrev = sNow;
    }

    // ---- Interaction Editor input + preview state machine ----
    if (m_interactionEditorMode && inputManager) {
        bool ctrlDown = inputManager->isKeyPressed(GLFW_KEY_LEFT_CONTROL) ||
                        inputManager->isKeyPressed(GLFW_KEY_RIGHT_CONTROL);
        bool sNow = ctrlDown && inputManager->isKeyPressed(GLFW_KEY_S);
        if (sNow && !m_ieCtrlSPrev) {
            if (interactionProfileManager && !m_ieProfileArchetype.empty()) {
                interactionProfileManager->saveArchetype(m_ieProfileArchetype);
                LOG_INFO("Application", "Interaction Editor: saved profile (Ctrl+S)");
            }
        }
        m_ieCtrlSPrev = sNow;

        // Preview state machine
        if (m_ieChar && m_iePreviewState != InteractionPreviewState::None) {
            switch (m_iePreviewState) {
                case InteractionPreviewState::SittingDown:
                    // Wait for sit-down to finish â†’ auto-transitions to SittingIdle
                    if (m_ieChar->isSitting() &&
                        m_ieChar->getAnimationState() == Scene::AnimatedCharacterState::SittingIdle) {
                        m_iePreviewState = m_ieAutoPreview
                            ? InteractionPreviewState::AutoIdle
                            : InteractionPreviewState::SittingIdle;
                        m_ieAutoTimer = 0.0f;
                    }
                    break;
                case InteractionPreviewState::SittingIdle:
                    // Manual mode: stay until user clicks Stand Up
                    break;
                case InteractionPreviewState::AutoIdle:
                    // Auto-preview: wait 2 seconds then stand up
                    m_ieAutoTimer += deltaTime;
                    if (m_ieAutoTimer >= 2.0f) {
                        m_ieChar->standUp();
                        m_iePreviewState = InteractionPreviewState::StandingUp;
                    }
                    break;
                case InteractionPreviewState::StandingUp:
                    // Wait for stand-up to finish
                    if (!m_ieChar->isSitting()) {
                        m_iePreviewState = InteractionPreviewState::None;
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

void Application::render() {
    renderCoordinator->render();
}

void Application::handleInput() {
    // Process keyboard and mouse input through InputManager
    inputManager->processInput(deltaTime);

    // Suppress all game input while paused (ESC toggle still works via registerAction)
    if (gamePaused) {
        return;
    }

    // Suppress movement/camera input while dialogue is active
    if (dialogueSystem && dialogueSystem->isActive()) {
        return;
    }

    // Pass movement input to active character
    // Only if NOT in Free Camera mode
    if (camera && camera->getMode() != Graphics::CameraMode::Free) {
        float forward = 0.0f;
        float turn = 0.0f;
        float strafe = 0.0f;

        // Check for sprint modifier (Shift)
        bool isSprinting = inputManager->isKeyPressed(GLFW_KEY_LEFT_SHIFT) || inputManager->isKeyPressed(GLFW_KEY_RIGHT_SHIFT);
        
        float moveMagnitude = isSprinting ? 1.0f : 0.5f;

        if (inputManager->isKeyPressed(GLFW_KEY_W)) forward -= moveMagnitude;
        if (inputManager->isKeyPressed(GLFW_KEY_S)) forward += moveMagnitude;

        // A/D for Turn
        if (inputManager->isKeyPressed(GLFW_KEY_A)) turn -= 1.0f;
        if (inputManager->isKeyPressed(GLFW_KEY_D)) turn += 1.0f;

        // Q for Strafe left (E reserved for NPC interaction)
        if (inputManager->isKeyPressed(GLFW_KEY_Q)) strafe -= moveMagnitude;

        if (currentControlTarget == ControlTarget::AnimatedCharacter && animatedCharacter) {
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

void Application::togglePause() {
    setPaused(!gamePaused);
}

void Application::setPaused(bool paused) {
    gamePaused = paused;
    // Freeze/unfreeze day-night cycle
    if (renderCoordinator) {
        renderCoordinator->getDayNightCycle().setPaused(paused);
    }
    LOG_INFO_FMT("Application", "Game " << (paused ? "PAUSED" : "RESUMED"));
}

void Application::toggleCharacterCustomizer() {
    showCharacterCustomizer = !showCharacterCustomizer;
    LOG_INFO_FMT("Application", "Character Customizer: " << (showCharacterCustomizer ? "ENABLED" : "DISABLED"));
}

void Application::toggleInteractionTuner() {
    showInteractionTuner = !showInteractionTuner;
    LOG_INFO_FMT("Application", "Interaction Tuner: " << (showInteractionTuner ? "ENABLED" : "DISABLED"));
}

void Application::renderInteractionTuner() {
    if (!showInteractionTuner || !placedObjectManager) return;

    auto& allDefs = placedObjectManager->getMutableTemplateDefs();
    if (allDefs.empty()) {
        ImGui::SetNextWindowSize(ImVec2(340, 80), ImGuiCond_Always);
        if (ImGui::Begin("Interaction Tuner [F12]", &showInteractionTuner)) {
            ImGui::TextDisabled("No interaction point defs registered.");
        }
        ImGui::End();
        return;
    }

    if (tunerSelectedTemplate.empty() || !allDefs.count(tunerSelectedTemplate))
        tunerSelectedTemplate = allDefs.begin()->first;

    // Determine current archetype
    std::string archetype = "humanoid_normal";
    if (animatedCharacter) archetype = animatedCharacter->getArchetype();

    ImGui::SetNextWindowSize(ImVec2(440, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Interaction Tuner [F12]", &showInteractionTuner)) {
        ImGui::End();
        return;
    }

    // Archetype display
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Archetype: %s", archetype.c_str());
    ImGui::Separator();

    // Template selector
    if (ImGui::BeginCombo("Template", tunerSelectedTemplate.c_str())) {
        for (auto& [name, defs] : allDefs) {
            bool selected = (name == tunerSelectedTemplate);
            if (ImGui::Selectable(name.c_str(), selected))
                tunerSelectedTemplate = name;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    auto& defs = allDefs[tunerSelectedTemplate];
    bool assetChanged = false;

    for (int i = 0; i < (int)defs.size(); ++i) {
        auto& def = defs[i];
        ImGui::PushID(i);

        ImGui::Spacing();
        // Show supported groups
        std::string groupsLabel = def.supportedGroups.empty() ? "all" : "";
        for (size_t g = 0; g < def.supportedGroups.size(); ++g) {
            if (g > 0) groupsLabel += ", ";
            groupsLabel += def.supportedGroups[g];
        }
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "[ %s ]  type: %s  groups: %s",
                           def.pointId.c_str(), def.type.c_str(), groupsLabel.c_str());
        ImGui::Separator();

        // === Asset-level settings (saved to .voxel) ===
        if (ImGui::TreeNodeEx("Asset Point (saved to .voxel)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Seat Anchor: base reference point on the seat");
            assetChanged |= ImGui::InputFloat3("Seat Anchor##end", &def.localOffset.x, "%.4f");
            ImGui::Spacing();
            ImGui::TextDisabled("Facing direction in radians (0=+Z, pi=-Z, pi/2=+X)");
            assetChanged |= ImGui::InputFloat("Facing Yaw##yaw", &def.facingYaw, 0.0f, 0.0f, "%.4f");
            ImGui::TreePop();
        }

        // === Profile-level settings (per-archetype, saved to JSON) ===
        ImGui::Spacing();
        if (ImGui::TreeNodeEx("Profile Offsets (per-archetype, saved to JSON)", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Get or create the profile for this archetype+template+point
            Core::InteractionProfile profile;
            if (interactionProfileManager) {
                const auto* existing = interactionProfileManager->getProfile(archetype, tunerSelectedTemplate, def.pointId);
                if (existing) {
                    profile = *existing;
                }
            }

            bool profileChanged = false;
            ImGui::TextDisabled("Foot-anchored: character's FEET snap here. Animation moves the hips.");
            ImGui::TextDisabled("Each offset is relative to Seat Anchor.");
            ImGui::Spacing();
            ImGui::TextDisabled("Feet pos during sit-down animation:");
            profileChanged |= ImGui::InputFloat3("SitDown Feet##sd", &profile.sitDownOffset.x, "%.4f");
            ImGui::TextDisabled("Feet pos during sitting-idle loop:");
            profileChanged |= ImGui::InputFloat3("SittingIdle Feet##si", &profile.sittingIdleOffset.x, "%.4f");
            ImGui::TextDisabled("Feet pos during stand-up animation:");
            profileChanged |= ImGui::InputFloat3("StandUp Feet##su", &profile.sitStandUpOffset.x, "%.4f");
            ImGui::Spacing();
            if (ImGui::Button("Copy SitDown -> All##copysitdown")) {
                profile.sittingIdleOffset = profile.sitDownOffset;
                profile.sitStandUpOffset = profile.sitDownOffset;
                profileChanged = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(Sets all three to the same value)");
            ImGui::Spacing();
            profileChanged |= ImGui::SliderFloat("Blend Duration##bd", &profile.sitBlendDuration, 0.0f, 1.0f, "%.2f s");
            ImGui::TextDisabled("Animation crossfade time. Set to 0 for instant clip switch.");
            ImGui::Spacing();
            ImGui::TextDisabled("Quick Y adjust on the Seat Anchor (shifts all states vertically):");
            profileChanged |= ImGui::InputFloat("Seat Height Offset##sh", &profile.seatHeightOffset, 0.0f, 0.0f, "%.4f");

            if (profileChanged && interactionProfileManager) {
                interactionProfileManager->setProfile(archetype, tunerSelectedTemplate, def.pointId, profile);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    if (assetChanged)
        placedObjectManager->recomputeAllInteractionPoints();

    ImGui::Spacing();
    ImGui::Separator();

    // Save buttons
    if (ImGui::Button("Save Asset (.voxel)")) {
        if (objectTemplateManager &&
            objectTemplateManager->saveInteractionDefs(tunerSelectedTemplate, defs)) {
            LOG_INFO("Application", "Saved asset interaction points for '{}' to template file.", tunerSelectedTemplate);
        } else {
            LOG_ERROR("Application", "Failed to save asset interaction points for '{}'.", tunerSelectedTemplate);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Profile (.json)")) {
        if (interactionProfileManager &&
            interactionProfileManager->saveArchetype(archetype)) {
            LOG_INFO("Application", "Saved interaction profiles for archetype '{}'.", archetype);
        } else {
            LOG_ERROR("Application", "Failed to save interaction profiles for archetype '{}'.", archetype);
        }
    }

    // --- Debug visualization: draw markers when character is sitting ---
    if (animatedCharacter && animatedCharacter->isSitting() && raycastVisualizer) {
        auto drawCross = [this](const glm::vec3& pos, const glm::vec3& color, float size = 0.15f) {
            raycastVisualizer->addLine(pos - glm::vec3(size, 0, 0), pos + glm::vec3(size, 0, 0), color);
            raycastVisualizer->addLine(pos - glm::vec3(0, size, 0), pos + glm::vec3(0, size, 0), color);
            raycastVisualizer->addLine(pos - glm::vec3(0, 0, size), pos + glm::vec3(0, 0, size), color);
        };

        // Green: seat anchor position
        drawCross(animatedCharacter->getSeatSurfacePos(), glm::vec3(0.0f, 1.0f, 0.0f), 0.2f);
        // Yellow: current worldPosition (feet)
        drawCross(animatedCharacter->getPosition(), glm::vec3(1.0f, 1.0f, 0.0f), 0.15f);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Green cross = seat anchor");
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Yellow cross = character feet (worldPosition)");
    }

    ImGui::End();
}

void Application::renderTemplateSpawner() {
    if (!showTemplateSpawner || !objectTemplateManager) return;

    ImGui::SetNextWindowSize(ImVec2(320, 260), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Template Spawner", &showTemplateSpawner)) {
        ImGui::End();
        return;
    }

    // Template selector combo
    auto templateNames = objectTemplateManager->getTemplateNames();
    if (!templateNames.empty()) {
        if (spawnerTemplateIdx >= static_cast<int>(templateNames.size()))
            spawnerTemplateIdx = 0;
        if (ImGui::BeginCombo("Template", templateNames[spawnerTemplateIdx].c_str())) {
            for (int i = 0; i < static_cast<int>(templateNames.size()); ++i) {
                bool selected = (spawnerTemplateIdx == i);
                if (ImGui::Selectable(templateNames[i].c_str(), selected))
                    spawnerTemplateIdx = i;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    // Position inputs
    ImGui::InputFloat3("Position", spawnerPos);

    // Fill from crosshair button
    if (ImGui::Button("From Crosshair")) {
        if (voxelInteractionSystem && voxelInteractionSystem->hasHoveredCube()) {
            const auto& loc = voxelInteractionSystem->getCurrentHoveredLocation();
            glm::vec3 pos = glm::vec3(loc.worldPos) + loc.hitNormal;
            spawnerPos[0] = pos.x;
            spawnerPos[1] = pos.y;
            spawnerPos[2] = pos.z;
        } else if (camera) {
            glm::vec3 pos = camera->getPosition() + camera->getFront() * 5.0f;
            spawnerPos[0] = pos.x;
            spawnerPos[1] = pos.y;
            spawnerPos[2] = pos.z;
        }
    }

    // Rotation
    ImGui::SliderInt("Rotation", &spawnerRotation, 0, 270);
    // Snap to nearest 90
    spawnerRotation = (spawnerRotation / 90) * 90;

    ImGui::Separator();
    if (ImGui::Button("Spawn", ImVec2(-1, 30)) && !templateNames.empty()) {
        const std::string& name = templateNames[spawnerTemplateIdx];
        glm::ivec3 pos((int)spawnerPos[0], (int)spawnerPos[1], (int)spawnerPos[2]);
        if (placedObjectManager) {
            // Route through PlacedObjectManager so the object is tracked, shows in World Outliner, and persists
            std::string objId = placedObjectManager->placeTemplate(name, pos, spawnerRotation);
            LOG_INFO_FMT("TemplateSpawner", "Placed '" << name << "' as '" << objId << "' at ("
                         << pos.x << "," << pos.y << "," << pos.z << ") rot=" << spawnerRotation);
            // Persist to DB so objects survive engine restart
            if (chunkManager) {
                auto* ws = chunkManager->m_streamingManager.getWorldStorage();
                if (ws) placedObjectManager->saveToDb(ws->getDb());
            }
        } else {
            objectTemplateManager->spawnTemplate(name, glm::vec3(pos), true, spawnerRotation);
            LOG_INFO_FMT("TemplateSpawner", "Spawned '" << name << "' (untracked) at ("
                         << pos.x << "," << pos.y << "," << pos.z << ") rot=" << spawnerRotation);
        }
    }

    ImGui::End();
}

void Application::renderClickActions() {
    if (!showClickActions) return;

    ImGui::SetNextWindowSize(ImVec2(300, 280), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Click Actions", &showClickActions)) {
        ImGui::End();
        return;
    }

    // Target mode selector (redundant with arrow keys)
    if (voxelInteractionSystem) {
        int mode = static_cast<int>(voxelInteractionSystem->getTargetMode());
        const char* modeNames[] = {"Cube", "Subcube", "Microcube"};
        if (ImGui::Combo("Target Mode", &mode, modeNames, 3)) {
            voxelInteractionSystem->setTargetMode(static_cast<TargetMode>(mode));
        }
    }

    ImGui::Separator();
    ImGui::Text("Furniture Physics");

    // Furniture hit force
    if (voxelInteractionSystem) {
        ImGui::SliderFloat("Hit Force", &voxelInteractionSystem->furnitureHitForce, 0.0f, 20.0f, "%.1f");
        ImGui::SetItemTooltip("Impulse applied when clicking furniture (0 = activate only, no push)");
    }

    if (dynamicFurnitureManager) {
        int active = static_cast<int>(dynamicFurnitureManager->activeCount());
        ImGui::Text("Active: %d / %d", active, Core::DynamicFurnitureManager::MAX_DYNAMIC_FURNITURE);

        // Reset all active furniture to original positions
        if (active > 0) {
            if (ImGui::Button("Reset All to Original", ImVec2(-1, 0))) {
                std::vector<std::string> ids;
                for (const auto& [id, _] : dynamicFurnitureManager->getActiveObjects())
                    ids.push_back(id);
                for (const auto& id : ids)
                    dynamicFurnitureManager->deactivate(id, true);
            }
        }
    }

    ImGui::End();
}

void Application::renderCharacterCustomizer() {
    if (!showCharacterCustomizer) return;

    ImGui::SetNextWindowSize(ImVec2(340, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Character Customizer", &showCharacterCustomizer)) {
        ImGui::End();
        return;
    }

    // Build list of customizable characters: player + all NPCs
    std::vector<std::string> targets;
    if (animatedCharacter) targets.push_back("Player");
    if (npcManager) {
        auto names = npcManager->getAllNPCNames();
        targets.insert(targets.end(), names.begin(), names.end());
    }

    if (targets.empty()) {
        ImGui::Text("No characters available.");
        ImGui::End();
        return;
    }

    // Target selector
    if (customizerSelectedNPC.empty() || std::find(targets.begin(), targets.end(), customizerSelectedNPC) == targets.end()) {
        customizerSelectedNPC = targets[0];
    }

    if (ImGui::BeginCombo("Character", customizerSelectedNPC.c_str())) {
        for (auto& t : targets) {
            bool selected = (t == customizerSelectedNPC);
            if (ImGui::Selectable(t.c_str(), selected)) {
                customizerSelectedNPC = t;
            }
        }
        ImGui::EndCombo();
    }

    // Get the target character
    Scene::AnimatedVoxelCharacter* target = nullptr;
    if (customizerSelectedNPC == "Player") {
        target = animatedCharacter;
    } else if (npcManager) {
        auto* npc = npcManager->getNPC(customizerSelectedNPC);
        if (npc) target = npc->getAnimatedCharacter();
    }

    if (!target) {
        ImGui::Text("Character not found.");
        ImGui::End();
        return;
    }

    // Get a mutable copy of appearance
    Scene::CharacterAppearance app = target->getAppearance();
    bool changed = false;
    bool colorOnly = false;

    ImGui::Separator();
    ImGui::Text("Proportions");

    changed |= ImGui::SliderFloat("Height",    &app.heightScale,          0.4f, 1.6f, "%.2f");
    changed |= ImGui::SliderFloat("Bulk",      &app.bulkScale,            0.5f, 1.8f, "%.2f");
    changed |= ImGui::SliderFloat("Head Size", &app.headScale,            0.6f, 1.6f, "%.2f");

    ImGui::Separator();
    ImGui::Text("Limb Proportions");

    changed |= ImGui::SliderFloat("Arm Length",      &app.armLengthScale,      0.5f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Leg Length",      &app.legLengthScale,      0.5f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Torso Length",    &app.torsoLengthScale,    0.6f, 1.5f, "%.2f");
    changed |= ImGui::SliderFloat("Shoulder Width",  &app.shoulderWidthScale,  0.5f, 1.6f, "%.2f");

    ImGui::Separator();
    ImGui::Text("Colors");

    glm::vec3 skin(app.skinColor);
    glm::vec3 torso(app.torsoColor);
    glm::vec3 arm(app.armColor);
    glm::vec3 leg(app.legColor);

    if (ImGui::ColorEdit3("Skin",  &skin.x))  { app.skinColor  = glm::vec4(skin, 1.0f);  colorOnly = true; }
    if (ImGui::ColorEdit3("Torso", &torso.x)) { app.torsoColor = glm::vec4(torso, 1.0f); colorOnly = true; }
    if (ImGui::ColorEdit3("Arms",  &arm.x))   { app.armColor   = glm::vec4(arm, 1.0f);   colorOnly = true; }
    if (ImGui::ColorEdit3("Legs",  &leg.x))   { app.legColor   = glm::vec4(leg, 1.0f);   colorOnly = true; }

    ImGui::Separator();

    // Presets
    if (ImGui::Button("Dwarf")) {
        app.heightScale = 0.6f; app.bulkScale = 1.4f; app.headScale = 1.15f;
        app.shoulderWidthScale = 1.35f; app.armLengthScale = 0.85f;
        app.legLengthScale = 0.7f; app.torsoLengthScale = 1.1f;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Giant")) {
        app.heightScale = 1.5f; app.bulkScale = 1.3f; app.headScale = 0.9f;
        app.shoulderWidthScale = 1.2f; app.armLengthScale = 1.1f;
        app.legLengthScale = 1.2f; app.torsoLengthScale = 1.1f;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Child")) {
        app.heightScale = 0.7f; app.bulkScale = 0.75f; app.headScale = 1.25f;
        app.shoulderWidthScale = 0.9f; app.armLengthScale = 0.9f;
        app.legLengthScale = 0.85f; app.torsoLengthScale = 0.9f;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        app = Scene::CharacterAppearance{};
        changed = true;
    }

    // Apply changes
    if (changed) {
        target->rebuildWithAppearance(app);
    } else if (colorOnly) {
        target->setAppearance(app);
        target->recolorFromAppearance();
    }

    ImGui::End();
}

// ============================================================================
// Animated Character inspector panel
// ============================================================================

void Application::renderAnimatedCharPanel() {
    if (!animatedCharacter) return;

    if (!showAnimatedCharPanel) {
        ImGui::SetNextWindowPos(ImVec2(10, 340), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(210, 30), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGuiWindowFlags hint = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                              | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin("##animcharhint", nullptr, hint)) {
            if (ImGui::Button("Animated Char Inspector")) showAnimatedCharPanel = true;
        }
        ImGui::End();
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(360, 620), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, 340), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animated Character Inspector", &showAnimatedCharPanel)) {
        ImGui::End();
        return;
    }

    // --- State & Position ---
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "State: %s",
        animatedCharacter->stateToString(animatedCharacter->getAnimationState()).c_str());

    glm::vec3 pos = animatedCharacter->getPosition();
    ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
    ImGui::Text("Yaw: %.1f deg", glm::degrees(animatedCharacter->getYaw()));

    glm::vec3 vel = animatedCharacter->getControllerVelocity();
    float speed = glm::length(glm::vec2(vel.x, vel.z));
    ImGui::Text("Speed: %.2f m/s  (vel: %.1f, %.1f, %.1f)", speed, vel.x, vel.y, vel.z);

    ImGui::Text("Archetype: %s", animatedCharacter->getArchetype().c_str());
    ImGui::Separator();

    // --- Animation ---
    if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Clip: %s", animatedCharacter->getCurrentClipName().c_str());
        float progress = animatedCharacter->getAnimationProgress();
        float duration = animatedCharacter->getAnimationDuration();
        ImGui::Text("Progress: %.0f%%  (%.2f / %.2f s)", progress * 100.0f,
                     progress * duration, duration);
        ImGui::ProgressBar(progress, ImVec2(-1, 0));

        float bd = animatedCharacter->getBlendDuration();
        if (ImGui::SliderFloat("Blend duration (s)", &bd, 0.0f, 1.0f, "%.2f"))
            animatedCharacter->setBlendDuration(bd);

        float stepH = animatedCharacter->getMaxStepHeight();
        if (ImGui::SliderFloat("Step-up height (m)", &stepH, 0.0f, 1.5f, "%.2f"))
            animatedCharacter->setMaxStepHeight(stepH);

        // Available clips
        const auto& clips = animatedCharacter->getAnimationClips();
        char clipsLabel[64];
        snprintf(clipsLabel, sizeof(clipsLabel), "Clips (%d)", (int)clips.size());
        if (ImGui::TreeNode(clipsLabel)) {
            for (const auto& clip : clips) {
                ImGui::BulletText("%s  dur=%.2fs spd=%.1f",
                    clip.name.c_str(), clip.duration, clip.speed);
            }
            ImGui::TreePop();
        }
    }

    // --- Voxel Model Stats ---
    if (ImGui::CollapsingHeader("Voxel Model", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& model = animatedCharacter->getVoxelModel();
        ImGui::Text("Bone shapes: %d", (int)model.shapes.size());

        int totalVoxels = 0;
        for (const auto& shape : model.shapes) {
            int vx = std::max(1, (int)std::round(shape.size.x));
            int vy = std::max(1, (int)std::round(shape.size.y));
            int vz = std::max(1, (int)std::round(shape.size.z));
            totalVoxels += vx * vy * vz;
        }
        ImGui::Text("Approx total voxels: %d", totalVoxels);

        if (ImGui::TreeNode("Shape details")) {
            const auto& skel = animatedCharacter->getSkeleton();
            for (const auto& shape : model.shapes) {
                const char* name = (shape.boneId >= 0 && shape.boneId < (int)skel.bones.size())
                    ? skel.bones[shape.boneId].name.c_str() : "?";
                ImGui::Text("  [%2d] %s  size(%.1f,%.1f,%.1f) off(%.1f,%.1f,%.1f)",
                    shape.boneId, name,
                    shape.size.x, shape.size.y, shape.size.z,
                    shape.offset.x, shape.offset.y, shape.offset.z);
            }
            ImGui::TreePop();
        }
    }

    // --- Skeleton ---
    if (ImGui::CollapsingHeader("Skeleton")) {
        const auto& skel = animatedCharacter->getSkeleton();
        ImGui::Text("Bones: %d", (int)skel.bones.size());

        for (const auto& bone : skel.bones) {
            const char* parentName = (bone.parentId >= 0 && bone.parentId < (int)skel.bones.size())
                ? skel.bones[bone.parentId].name.c_str() : "(root)";

            char label[128];
            snprintf(label, sizeof(label), "[%2d] %s", bone.id, bone.name.c_str());
            bool open = ImGui::TreeNode(label);
            if (open) {
                ImGui::Text("Parent: [%d] %s", bone.parentId, parentName);
                glm::vec3 lp = bone.localPosition;
                ImGui::Text("Local pos: %.3f, %.3f, %.3f", lp.x, lp.y, lp.z);
                glm::vec3 gp = glm::vec3(bone.globalTransform[3]);
                ImGui::Text("Model pos: %.3f, %.3f, %.3f", gp.x, gp.y, gp.z);
                ImGui::TreePop();
            }
        }
    }

    // --- Attachments ---
    if (animatedCharacter->hasAttachments()) {
        if (ImGui::CollapsingHeader("Attachments")) {
            ImGui::Text("Has active bone attachments");
        }
    }

    // --- Physics ---
    if (ImGui::CollapsingHeader("Physics")) {
        ImGui::Text("Controller half-height: %.3f m", animatedCharacter->getControllerHalfHeight());
        ImGui::Text("Controller half-width: %.3f m", animatedCharacter->getControllerHalfWidth());
        ImGui::Text("Sitting: %s", animatedCharacter->isSitting() ? "Yes" : "No");
    }

    ImGui::End();
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

void Application::toggleGameMenu(const std::string& name) {
    if (renderCoordinator) {
        auto* uiSystem = renderCoordinator->getUISystem();
        if (uiSystem) {
            uiSystem->toggleScreen(name);
            LOG_INFO_FMT("Application", "Menu '" << name << "': " << (uiSystem->isScreenVisible(name) ? "VISIBLE" : "HIDDEN"));
        }
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

void Application::cycleRaycastTargetMode(int direction) {
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
    // Only one control target exists: AnimatedCharacter
    currentControlTarget = ControlTarget::AnimatedCharacter;
    LOG_INFO("Application", "Control is AnimatedCharacter");
}

Scene::AnimatedVoxelCharacter* Application::createAnimatedCharacter(const glm::vec3& pos, const std::string& animFile) {
    auto animatedCharPtr = std::make_unique<Scene::AnimatedVoxelCharacter>(physicsWorld, pos);
    animatedCharacter = animatedCharPtr.get();
    if (animatedCharacter->loadModel(animFile)) {
        // Play default animation if available
        animatedCharacter->playAnimation("idle"); 
        LOG_INFO("Application", "Loaded animated character model: " + animFile);

        // Propagate character archetype to interaction system
        if (interactionManager) {
            interactionManager->setPlayerArchetype(animatedCharacter->getArchetype());
            LOG_INFO("Application", "Set player interaction archetype to '{}'", animatedCharacter->getArchetype());
        }
        // Load interaction profiles for this archetype if not already loaded
        if (interactionProfileManager) {
            interactionProfileManager->loadArchetype(animatedCharacter->getArchetype());
        }
    } else {
        LOG_ERROR("Application", "Failed to load animated character model: " + animFile);
    }
    // Wire voxel collision queries
    if (chunkManager) {
        animatedCharacter->setChunkManager(chunkManager);
    }
    // Wire F5 debug visualizer for segment box display
    if (raycastVisualizer) {
        animatedCharacter->setRaycastVisualizer(raycastVisualizer.get());
    }
    entities.push_back(std::move(animatedCharPtr));
    if (entityRegistry) {
        entityRegistry->registerEntity(animatedCharacter, "animated_" + std::to_string(entities.size()), "animated");
    }
    return animatedCharacter;
}

void Application::setControlTarget(const std::string& targetName) {
    currentControlTarget = ControlTarget::AnimatedCharacter;
    if (camera) {
        camera->setDistanceFromTarget(4.0f);
        if (camera->getMode() == Graphics::CameraMode::Free) {
            camera->setMode(Graphics::CameraMode::ThirdPerson);
        }
    }
    LOG_INFO("Application", "Control target set to: " + targetName);
}

void Application::derezCharacter(float duration) {
    if (!animatedCharacter) {
        LOG_WARN("Application", "Cannot derez: no animated character");
        return;
    }
    if (animatedCharacter->isDerezzing()) {
        LOG_WARN("Application", "Cannot derez: character is already derezzing");
        return;
    }

    LOG_INFO("Application", "Beginning derez on animated character");

    if (gpuParticlePhysics && gpuParticlePhysics->isInitialized()) {
        // GPU path: staggered voxel-by-voxel falling apart
        animatedCharacter->beginDerez(gpuParticlePhysics.get(), duration,
                                      Scene::DerezPattern::Wave);
    } else {
        // Fallback: instant CPU debris if GPU physics not available
        if (chunkManager)
            chunkManager->m_dynamicObjectManager.derezCharacter(animatedCharacter, 1.0f);
        auto it = std::remove_if(entities.begin(), entities.end(),
            [this](const std::unique_ptr<Scene::Entity>& e) {
                return e.get() == animatedCharacter;
            });
        if (it != entities.end()) entities.erase(it, entities.end());
        animatedCharacter = nullptr;
    }

    // Release player control immediately
    if (currentControlTarget == ControlTarget::AnimatedCharacter)
        toggleCharacterControl();
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

    auto* npcChar = createAnimatedCharacter(spawnPos, "resources/animated_characters/humanoid.anim");

    // Generate a unique ID for this NPC
    static int npcCounter = 0;
    std::string npcId = "ai_npc_" + std::to_string(npcCounter++);

    // Register with AI system using guard recipe as default
    if (aiSystem->createAINPC(npcChar, npcId, Core::AssetManager::instance().resolveRecipe("characters/guard.yaml"),
                               "You are a guard NPC in a voxel world. Be helpful but cautious.")) {
        LOG_INFO("Application", "Spawned AI NPC '{}' at ({}, {}, {})", npcId, spawnPos.x, spawnPos.y, spawnPos.z);
    } else {
        LOG_WARN("Application", "AI NPC created but AI registration failed for: {}", npcId);
    }
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
        config.defaultProvider = engineConfig.aiProvider;
        if (!engineConfig.aiModel.empty()) config.defaultModel = engineConfig.aiModel;
        if (aiSystem->initialize(config, /*autoStart=*/true)) {
            LOG_INFO("Application", "AI system started with goose-server (provider={})", engineConfig.aiProvider);
        } else {
            LOG_WARN("Application", "Failed to start AI system");
        }
    }
}

void Application::interactWithNPC() {
    LOG_WARN("Application", "interactWithNPC() called  --  E key pressed");
    if (!interactionManager) { LOG_WARN("Application", "  -> no interactionManager"); return; }

    // If dialogue is already active, advance it instead of starting new interaction
    if (dialogueSystem && dialogueSystem->isActive()) {
        LOG_WARN("Application", "  -> dialogue active, advancing");
        dialogueSystem->advanceDialogue();
        return;
    }

    // If animated character is currently seated, E = stand up
    if (animatedCharacter && animatedCharacter->isSitting()) {
        LOG_WARN("Application", "  -> character sitting, standing up");
        animatedCharacter->standUp();
        if (interactionManager) interactionManager->releaseSeat("player");
        return;
    }

    // Use the current active character as the "player" interactor
    Scene::Entity* playerEntity = animatedCharacter;
    LOG_WARN("Application", "  -> calling tryInteract, playerEntity={}", playerEntity ? "valid" : "null");
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
        // skip world generation from the definition  --  it would overwrite the
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
        subsystems.placedObjectManager = placedObjectManager.get();
        subsystems.gameEventLog     = gameEventLog.get();
        subsystems.locationRegistry = locationRegistry;
        subsystems.camera           = camera;
        subsystems.dialogueSystem   = dialogueSystem.get();
        subsystems.storyEngine      = storyEngine.get();

        subsystems.entitySpawner = [this](const std::string& type, const glm::vec3& pos,
                                          const std::string& animFile) -> Scene::Entity* {
            return createAnimatedCharacter(pos, animFile.empty() ? "resources/animated_characters/humanoid.anim" : animFile);
        };

        // AI NPC registration callback: creates AIController when agencyLevel >= 1
        subsystems.aiRegister = [this](Scene::Entity* entity, const std::string& entityId,
                                        const std::string& npcName, const std::string& personality) {
            if (!aiSystem) {
                LOG_WARN("Application", "Cannot register AI NPC '{}': AI system not initialized", npcName);
                return;
            }
            std::string recipe = Core::AssetManager::instance().resolveRecipe("characters/" + entityId + ".yaml");
            if (recipe.empty()) {
                recipe = Core::AssetManager::instance().resolveRecipe("characters/guard.yaml");
            }
            std::string personalityStr = personality;
            if (personalityStr.empty()) {
                personalityStr = "You are " + npcName + ", an NPC in a voxel world. Stay in character and be conversational.";
            }
            if (aiSystem->createAINPC(entity, entityId, recipe, personalityStr)) {
                LOG_INFO("Application", "Registered AI NPC '{}' (id={}) with AI system", npcName, entityId);
            } else {
                LOG_WARN("Application", "AI NPC '{}' registered but AIController creation failed (server may not be running)", npcName);
            }
        };

        auto result = Core::GameDefinitionLoader::load(gameDef, subsystems);
        if (result.success) {
            LOG_INFO("Application", "Game definition loaded: {} chunks, {} structures, {} NPCs", result.chunksGenerated, result.structuresPlaced, result.npcsSpawned);

            // Build navigation grid for NPC pathfinding
            if (npcManager) {
                npcManager->buildNavGrid();
            }

            // Sync InputManager with camera set by game definition
            if (inputManager && camera) {
                inputManager->setCameraPosition(camera->getPosition());
                inputManager->setYawPitch(camera->getYaw(), camera->getPitch());
            }

            // If a player was spawned, set up character control but keep camera in Free mode
            // (user can switch to ThirdPerson with V)
            if (result.playerSpawned) {
                if (animatedCharacter) {
                    currentControlTarget = ControlTarget::AnimatedCharacter;
                    camera->setDistanceFromTarget(4.0f);
                    LOG_INFO("Application", "Player spawned (AnimatedCharacter) — camera stays Free, press V to follow");
                }
            }
        } else {
            LOG_ERROR("Application", "Failed to load game definition: {}", result.error);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Application", "Error loading game definition '{}': {}", defPath, e.what());
    }
}

// ============================================================================
// Social Simulation Command Handler (extracted to reduce nesting depth)
// ============================================================================
static nlohmann::json handleSocialSimulationCommand(
    const std::string& action,
    const nlohmann::json& params,
    Core::NPCManager* npcManager)
{
    if (!npcManager) {
        return {{"error", "NPCManager not available"}};
    }

    // --- Needs ---
    if (action == "get_npc_needs") {
        std::string name = params.value("name", "");
        if (name.empty()) return {{"error", "NPC name required"}};
        auto* npc = npcManager->getNPC(name);
        if (!npc) return {{"error", "NPC not found: " + name}};
        return {{"success", true}, {"name", name}, {"needs", npc->getNeeds().toJson()}};
    }
    if (action == "set_npc_needs") {
        std::string name = params.value("name", "");
        if (name.empty()) return {{"error", "NPC name required"}};
        auto* npc = npcManager->getNPC(name);
        if (!npc) return {{"error", "NPC not found: " + name}};
        if (params.contains("needs")) {
            npc->getNeeds().fromJson(params["needs"]);
        }
        if (params.contains("type") && params.contains("value")) {
            auto type = AI::needTypeFromString(params["type"]);
            float value = params["value"];
            npc->getNeeds().fulfill(type, value - (npc->getNeeds().getNeed(type) ? npc->getNeeds().getNeed(type)->value : 0.0f));
        }
        return {{"success", true}, {"name", name}};
    }

    // --- Relationships ---
    if (action == "get_npc_relationships") {
        std::string name = params.value("name", "");
        if (name.empty()) {
            return {{"success", true}, {"relationships", npcManager->getRelationships().toJson()}};
        }
        auto rels = npcManager->getRelationships().getRelationshipsFor(name);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [targetId, rel] : rels) {
            arr.push_back({{"target", targetId}, {"trust", rel.trust},
                          {"affection", rel.affection}, {"respect", rel.respect},
                          {"fear", rel.fear}, {"label", rel.label},
                          {"disposition", npcManager->getRelationships().getDisposition(name, targetId)}});
        }
        return {{"success", true}, {"name", name}, {"relationships", arr}};
    }
    if (action == "set_npc_relationship") {
        std::string from = params.value("from", "");
        std::string to = params.value("to", "");
        if (from.empty() || to.empty()) return {{"error", "Both 'from' and 'to' NPC names required"}};
        Story::Relationship rel;
        rel.targetCharacterId = to;
        rel.trust = params.value("trust", 0.0f);
        rel.affection = params.value("affection", 0.0f);
        rel.respect = params.value("respect", 0.0f);
        rel.fear = params.value("fear", 0.0f);
        rel.label = params.value("label", "");
        npcManager->getRelationships().setRelationship(from, to, rel);
        return {{"success", true}, {"from", from}, {"to", to}};
    }
    if (action == "apply_npc_interaction") {
        std::string from = params.value("from", "");
        std::string to = params.value("to", "");
        std::string typeStr = params.value("type", "Greeting");
        float intensity = params.value("intensity", 1.0f);
        if (from.empty() || to.empty()) return {{"error", "Both 'from' and 'to' NPC names required"}};
        auto type = AI::interactionTypeFromString(typeStr);
        npcManager->getRelationships().applyInteraction(from, to, type, intensity);
        float disposition = npcManager->getRelationships().getDisposition(from, to);
        return {{"success", true}, {"from", from}, {"to", to},
                {"interaction", typeStr}, {"newDisposition", disposition}};
    }

    // --- WorldView ---
    if (action == "get_npc_worldview") {
        std::string name = params.value("name", "");
        if (name.empty()) return {{"error", "NPC name required"}};
        auto* npc = npcManager->getNPC(name);
        if (!npc) return {{"error", "NPC not found: " + name}};
        return {{"success", true}, {"name", name},
                {"worldView", npc->getWorldView().toJson()},
                {"contextSummary", npc->getWorldView().buildContextSummary()}};
    }
    if (action == "set_npc_belief") {
        std::string name = params.value("name", "");
        std::string key = params.value("key", "");
        std::string value = params.value("value", "");
        if (name.empty() || key.empty()) return {{"error", "NPC name and belief key required"}};
        auto* npc = npcManager->getNPC(name);
        if (!npc) return {{"error", "NPC not found: " + name}};
        float confidence = params.value("confidence", 1.0f);
        npc->getWorldView().setBelief(key, value, confidence);
        return {{"success", true}, {"name", name}, {"key", key}};
    }
    if (action == "set_npc_opinion") {
        std::string name = params.value("name", "");
        std::string subject = params.value("subject", "");
        if (name.empty() || subject.empty()) return {{"error", "NPC name and opinion subject required"}};
        auto* npc = npcManager->getNPC(name);
        if (!npc) return {{"error", "NPC not found: " + name}};
        float sentiment = params.value("sentiment", 0.0f);
        std::string reason = params.value("reason", "");
        npc->getWorldView().setOpinion(subject, sentiment, reason);
        return {{"success", true}, {"name", name}, {"subject", subject}};
    }

    return {{"error", "Unknown social action: " + action}};
}

// ============================================================================
// Game State Command Handler (extracted to reduce nesting depth - C1061)
// ============================================================================
struct GameStateContext {
    bool& gamePaused;
    std::function<void(bool)> setPaused;
    Core::HealthComponent& playerHealth;
    Core::RespawnSystem& respawnSystem;
    Core::MusicPlaylist& musicPlaylist;
    Core::PlayerProfile& playerProfile;
    Core::ObjectiveTracker& objectiveTracker;
    Core::AudioSystem* audioSystem;
    Graphics::Camera* camera;
    Core::Inventory* inventory;
    ChunkManager* chunkManager;
};

static nlohmann::json handleGameStateCommand(
    const std::string& action,
    const nlohmann::json& params,
    GameStateContext& ctx)
{
    using json = nlohmann::json;

    if (action == "get_pause_state") {
        return {{"paused", ctx.gamePaused}};
    } else if (action == "set_pause_state") {
        bool paused = params.value("paused", !ctx.gamePaused);
        ctx.setPaused(paused);
        return {{"paused", ctx.gamePaused}};
    } else if (action == "get_player_health") {
        json r = ctx.playerHealth.toJson();
        r["respawn"] = ctx.respawnSystem.toJson();
        return r;
    } else if (action == "modify_player_health") {
        std::string act = params.value("action", "");
        if (act == "damage") {
            float amount = params.value("amount", 10.0f);
            float actual = ctx.playerHealth.takeDamage(amount);
            return {{"damaged", actual}, {"health", ctx.playerHealth.toJson()}};
        } else if (act == "heal") {
            float amount = params.value("amount", 10.0f);
            float actual = ctx.playerHealth.heal(amount);
            return {{"healed", actual}, {"health", ctx.playerHealth.toJson()}};
        } else if (act == "kill") {
            ctx.playerHealth.kill();
            return {{"killed", true}, {"health", ctx.playerHealth.toJson()}};
        } else if (act == "revive") {
            float pct = params.value("healthPercent", 1.0f);
            ctx.playerHealth.revive(pct);
            ctx.respawnSystem.respawn();
            return {{"revived", true}, {"health", ctx.playerHealth.toJson()}};
        } else if (act == "set") {
            float hp = params.value("health", ctx.playerHealth.getMaxHealth());
            ctx.playerHealth.setHealth(hp);
            return {{"health", ctx.playerHealth.toJson()}};
        }
        return {{"error", "Unknown health action. Use: damage, heal, kill, revive, set"}};
    } else if (action == "get_respawn_state") {
        return ctx.respawnSystem.toJson();
    } else if (action == "modify_respawn") {
        if (params.contains("force_respawn") && params["force_respawn"].get<bool>()) {
            ctx.respawnSystem.respawn();
            ctx.playerHealth.revive(1.0f);
            return {{"respawned", true}};
        } else if (params.contains("spawn_point")) {
            auto& sp = params["spawn_point"];
            glm::vec3 pos(sp.value("x", 16.0f), sp.value("y", 25.0f), sp.value("z", 16.0f));
            ctx.respawnSystem.setSpawnPoint(pos);
            return {{"spawnPoint", {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}}}};
        } else if (params.contains("respawn_delay")) {
            ctx.respawnSystem.setRespawnDelay(params["respawn_delay"].get<float>());
            return {{"respawnDelay", ctx.respawnSystem.getRespawnDelay()}};
        }
        return ctx.respawnSystem.toJson();
    } else if (action == "get_music_state") {
        return ctx.musicPlaylist.toJson();
    } else if (action == "control_music") {
        std::string act = params.value("action", "");
        if (act == "play") {
            ctx.musicPlaylist.play(ctx.audioSystem);
            return {{"playing", true}, {"music", ctx.musicPlaylist.toJson()}};
        } else if (act == "stop") {
            ctx.musicPlaylist.stop(ctx.audioSystem);
            return {{"playing", false}};
        } else if (act == "next") {
            ctx.musicPlaylist.next(ctx.audioSystem);
            return ctx.musicPlaylist.toJson();
        } else if (act == "add_track") {
            std::string path = params.value("path", "");
            if (!path.empty()) {
                ctx.musicPlaylist.addTrack(path);
                return {{"added", path}, {"music", ctx.musicPlaylist.toJson()}};
            }
            return {{"error", "Missing 'path' for add_track"}};
        } else if (act == "remove_track") {
            std::string path = params.value("path", "");
            ctx.musicPlaylist.removeTrack(path);
            return ctx.musicPlaylist.toJson();
        } else if (act == "clear") {
            ctx.musicPlaylist.clear();
            if (ctx.audioSystem) ctx.audioSystem->stopMusic();
            return {{"cleared", true}};
        } else if (act == "set_volume") {
            float vol = params.value("volume", 0.5f);
            ctx.musicPlaylist.setVolume(vol);
            if (ctx.audioSystem) ctx.audioSystem->setChannelVolume(Core::AudioChannel::Music, vol);
            return {{"volume", vol}};
        } else if (act == "set_mode") {
            std::string mode = params.value("mode", "sequential");
            ctx.musicPlaylist.setMode(mode == "shuffle" ? Core::MusicPlaylist::Mode::Shuffle : Core::MusicPlaylist::Mode::Sequential);
            return {{"mode", mode}};
        }
        return {{"error", "Unknown music action. Use: play, stop, next, add_track, remove_track, clear, set_volume, set_mode"}};
    } else if (action == "save_player_state") {
        if (ctx.camera) {
            ctx.playerProfile.cameraPosition = ctx.camera->getPosition();
            ctx.playerProfile.cameraYaw = ctx.camera->getYaw();
            ctx.playerProfile.cameraPitch = ctx.camera->getPitch();
        }
        ctx.playerProfile.health = ctx.playerHealth.getHealth();
        ctx.playerProfile.maxHealth = ctx.playerHealth.getMaxHealth();
        ctx.playerProfile.spawnPoint = ctx.respawnSystem.getSpawnPoint();
        ctx.playerProfile.deathCount = ctx.respawnSystem.getDeathCount();
        if (ctx.inventory) {
            ctx.playerProfile.inventoryData = ctx.inventory->toJson();
        }
        auto* worldStorage = ctx.chunkManager->m_streamingManager.getWorldStorage();
        if (worldStorage && worldStorage->getDb()) {
            bool ok = ctx.playerProfile.saveToDb(worldStorage->getDb());
            return {{"saved", ok}, {"profile", ctx.playerProfile.toJson()}};
        }
        return {{"error", "No world database available"}};
    } else if (action == "load_player_state") {
        auto* worldStorage = ctx.chunkManager->m_streamingManager.getWorldStorage();
        if (worldStorage && worldStorage->getDb()) {
            bool ok = ctx.playerProfile.loadFromDb(worldStorage->getDb());
            if (ok) {
                if (ctx.camera) {
                    ctx.camera->setPosition(ctx.playerProfile.cameraPosition);
                    ctx.camera->setYaw(ctx.playerProfile.cameraYaw);
                    ctx.camera->setPitch(ctx.playerProfile.cameraPitch);
                }
                ctx.playerHealth.setHealth(ctx.playerProfile.health);
                ctx.respawnSystem.setSpawnPoint(ctx.playerProfile.spawnPoint);
                if (ctx.inventory && !ctx.playerProfile.inventoryData.empty()) {
                    ctx.inventory->fromJson(ctx.playerProfile.inventoryData);
                }
                return {{"loaded", true}, {"profile", ctx.playerProfile.toJson()}};
            }
            return {{"loaded", false}, {"reason", "No saved profile found"}};
        }
        return {{"error", "No world database available"}};
    } else if (action == "get_objectives") {
        return ctx.objectiveTracker.toJson();
    } else if (action == "manage_objectives") {
        std::string act = params.value("action", "");
        if (act == "add") {
            std::string id = params.value("id", "");
            std::string title = params.value("title", "");
            if (id.empty() || title.empty()) {
                return {{"error", "Missing 'id' and 'title' for add"}};
            }
            bool ok = ctx.objectiveTracker.addObjective(id, title,
                params.value("description", ""),
                params.value("category", "main"),
                params.value("priority", 0),
                params.value("hidden", false));
            return {{"added", ok}, {"objectives", ctx.objectiveTracker.toJson()}};
        } else if (act == "complete") {
            std::string id = params.value("id", "");
            bool ok = ctx.objectiveTracker.completeObjective(id);
            return {{"completed", ok}, {"objectives", ctx.objectiveTracker.toJson()}};
        } else if (act == "fail") {
            std::string id = params.value("id", "");
            bool ok = ctx.objectiveTracker.failObjective(id);
            return {{"failed", ok}, {"objectives", ctx.objectiveTracker.toJson()}};
        } else if (act == "remove") {
            std::string id = params.value("id", "");
            bool ok = ctx.objectiveTracker.removeObjective(id);
            return {{"removed", ok}, {"objectives", ctx.objectiveTracker.toJson()}};
        } else if (act == "clear") {
            ctx.objectiveTracker.clear();
            return {{"cleared", true}};
        }
        return {{"error", "Unknown objective action. Use: add, complete, fail, remove, clear"}};
    }
    return {{"error", "Unknown game state action: " + action}};
}

// AI / LLM Command Handler (extracted to reduce nesting depth)
// ============================================================================
static nlohmann::json handleMaterialCommand(
    const std::string& action,
    const nlohmann::json& params,
    Phyxel::Vulkan::VulkanDevice* vulkanDevice)
{
    using json = nlohmann::json;
    auto& reg = Phyxel::Core::MaterialRegistry::instance();

    if (action == "add_material") {
        std::string name = params.value("name", "");
        if (name.empty()) return {{"error", "Material name is required"}};
        if (reg.getMaterialID(name) >= 0) return {{"error", "Material already exists: " + name}};

        Phyxel::Core::MaterialDef def;
        def.name = name;
        def.emissive = params.value("emissive", false);
        def.category = "material";
        if (params.contains("physics")) {
            auto& p = params["physics"];
            def.physics.mass = p.value("mass", 1.0f);
            def.physics.friction = p.value("friction", 0.5f);
            def.physics.restitution = p.value("restitution", 0.3f);
        }
        static const char* faceKeys[6] = {"side_n", "side_s", "side_e", "side_w", "top", "bottom"};
        for (int f = 0; f < 6; f++) {
            def.textures.faceFiles[f] = name + "_" + faceKeys[f] + ".png";
        }
        int id = reg.addMaterial(def);
        if (id >= 0) return {{"success", true}, {"name", name}, {"id", id}};
        return {{"error", "Failed to add material (max capacity?)"}};

    } else if (action == "remove_material") {
        std::string name = params.value("name", "");
        if (name.empty()) return {{"error", "Material name is required"}};
        bool ok = reg.removeMaterial(name);
        json r = {{"success", ok}};
        if (!ok) r["error"] = "Material not found or cannot be removed: " + name;
        return r;

    } else if (action == "save_materials") {
        std::string path = params.value("path", "resources/materials.json");
        bool ok = reg.saveToJson(path);
        return {{"success", ok}, {"path", path}};

    } else if (action == "reload_atlas") {
        auto& atlas = Phyxel::Core::AtlasManager::instance();
        bool ok = atlas.hotReload(vulkanDevice);
        json r = {{"success", ok}};
        if (ok) {
            r["textureCount"] = atlas.getAtlasInfo().textureCount;
            r["atlasSize"] = { atlas.getAtlasInfo().atlasWidth, atlas.getAtlasInfo().atlasHeight };
        }
        return r;
    }
    return {{"error", "Unknown material action: " + action}};
}

static nlohmann::json handleAICommand(
    const std::string& action,
    const nlohmann::json& params,
    AI::AIConversationService* aiService,
    Core::NPCManager* npcManager,
    Core::EntityRegistry* entityRegistry,
    UI::DialogueSystem* dialogueSystem)
{
    if (action == "get_ai_status") {
        nlohmann::json result = {{"success", true}};
        if (aiService) {
            result["configured"] = aiService->isConfigured();
            auto* client = aiService->getLLMClient();
            if (client) {
                const auto& cfg = client->getConfig();
                result["provider"] = cfg.provider;
                result["model"] = cfg.model.empty() ? AI::LLMConfig::getDefaultModel(cfg.provider) : cfg.model;
                auto usage = client->getTokenUsage();
                result["tokenUsage"] = {
                    {"totalInput", usage.totalInput},
                    {"totalOutput", usage.totalOutput},
                    {"totalCalls", usage.totalCalls}
                };
            }
        } else {
            result["configured"] = false;
            result["error"] = "AIConversationService not available";
        }
        return result;
    }

    if (action == "configure_ai") {
        if (!aiService) return {{"error", "AIConversationService not available"}};
        AI::LLMConfig config;
        config.provider = params.value("provider", "anthropic");
        config.model = params.value("model", "");
        config.apiKey = params.value("api_key", "");
        if (params.contains("max_tokens")) config.maxTokens = params["max_tokens"];
        if (params.contains("temperature")) config.temperature = params["temperature"];
        aiService->setLLMConfig(config);
        return {{"success", true}, {"provider", config.provider},
                {"configured", aiService->isConfigured()}};
    }

    if (action == "start_ai_conversation") {
        if (!aiService) return {{"error", "AIConversationService not available"}};
        if (!aiService->isConfigured()) return {{"error", "AI not configured (no API key)"}};
        if (!npcManager) return {{"error", "NPCManager not available"}};
        std::string name = params.value("name", "");
        if (name.empty()) return {{"error", "NPC name required"}};
        auto* npc = npcManager->getNPC(name);
        if (!npc) return {{"error", "NPC not found: " + name}};
        std::string npcId = entityRegistry ? entityRegistry->getEntityId(npc) : name;
        if (aiService->startConversation(npc, npcId, name)) {
            return {{"success", true}, {"name", name}, {"npcId", npcId}};
        }
        return {{"error", "Failed to start AI conversation with " + name}};
    }

    if (action == "send_ai_message") {
        if (!dialogueSystem) return {{"error", "DialogueSystem not available"}};
        std::string message = params.value("message", "");
        if (message.empty()) return {{"error", "Message text required"}};
        dialogueSystem->submitAIInput(message);
        return {{"success", true}, {"message", message}};
    }

    return {{"error", "Unknown AI action: " + action}};
}

// ============================================================================
// Region capture/place helpers for multi-level voxel operations
// Used by snapshot, clipboard, save_template, and move_region commands
// ============================================================================

/// Capture all cubes, subcubes, and microcubes in a region into VoxelEntry list.
static std::vector<Core::VoxelEntry> captureRegionAllLevels(
    ChunkManager* chunkManager,
    const glm::ivec3& minCorner,
    const glm::ivec3& maxCorner)
{
    std::vector<Core::VoxelEntry> voxels;
    for (int ix = minCorner.x; ix <= maxCorner.x; ++ix) {
        for (int iy = minCorner.y; iy <= maxCorner.y; ++iy) {
            for (int iz = minCorner.z; iz <= maxCorner.z; ++iz) {
                glm::ivec3 worldPos(ix, iy, iz);
                auto* chunk = chunkManager->getChunkAt(worldPos);
                if (!chunk) continue;
                glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
                glm::ivec3 relOffset(ix - minCorner.x, iy - minCorner.y, iz - minCorner.z);

                // Cubes
                auto* cube = chunk->getCubeAt(localPos);
                if (cube && cube->isVisible()) {
                    Core::VoxelEntry entry;
                    entry.offset = relOffset;
                    entry.material = cube->getMaterialName();
                    entry.level = Core::SnapshotVoxelLevel::Cube;
                    voxels.push_back(entry);
                }

                // Subcubes
                auto subs = chunk->getSubcubesAt(localPos);
                for (auto* sub : subs) {
                    if (!sub) continue;
                    Core::VoxelEntry entry;
                    entry.offset = relOffset;
                    entry.material = sub->getMaterialName();
                    entry.level = Core::SnapshotVoxelLevel::Subcube;
                    entry.subcubePos = glm::ivec3(sub->getLocalPosition());
                    voxels.push_back(entry);
                }

                // Microcubes (iterate all 27 subcube slots)
                for (int msx = 0; msx < 3; ++msx) {
                    for (int msy = 0; msy < 3; ++msy) {
                        for (int msz = 0; msz < 3; ++msz) {
                            auto micros = chunk->getMicrocubesAt(localPos, glm::ivec3(msx, msy, msz));
                            for (auto* mic : micros) {
                                if (!mic) continue;
                                Core::VoxelEntry entry;
                                entry.offset = relOffset;
                                entry.material = mic->getMaterialName();
                                entry.level = Core::SnapshotVoxelLevel::Microcube;
                                entry.subcubePos = glm::ivec3(msx, msy, msz);
                                entry.microcubePos = glm::ivec3(mic->getMicrocubeLocalPosition());
                                voxels.push_back(entry);
                            }
                        }
                    }
                }
            }
        }
    }
    return voxels;
}

/// Place voxels from VoxelEntry list into the world at a given origin.
/// Returns {placed, failed} counts.
static std::pair<int, int> placeVoxelEntries(
    ChunkManager* chunkManager,
    const std::vector<Core::VoxelEntry>& voxels,
    const glm::ivec3& origin)
{
    int placed = 0, failed = 0;
    for (const auto& v : voxels) {
        glm::ivec3 worldPos = origin + v.offset;
        bool ok = false;
        switch (v.level) {
            case Core::SnapshotVoxelLevel::Cube:
                ok = chunkManager->m_voxelModificationSystem.addCubeWithMaterial(worldPos, v.material);
                break;
            case Core::SnapshotVoxelLevel::Subcube:
                ok = chunkManager->m_voxelModificationSystem.addSubcubeWithMaterial(worldPos, v.subcubePos, v.material);
                break;
            case Core::SnapshotVoxelLevel::Microcube:
                ok = chunkManager->m_voxelModificationSystem.addMicrocubeWithMaterial(worldPos, v.subcubePos, v.microcubePos, v.material);
                break;
        }
        if (ok) ++placed; else ++failed;
    }
    return {placed, failed};
}

/// Push an undo snapshot for a region before a destructive operation.
/// label describes the operation (e.g. "fill_region", "clear_region").
/// Silently skips if region exceeds MAX_VOLUME or managers are null.
static void pushUndoSnapshot(
    ChunkManager* chunkManager,
    Core::SnapshotManager* snapshotManager,
    const glm::ivec3& minCorner,
    const glm::ivec3& maxCorner,
    const std::string& label)
{
    if (!chunkManager || !snapshotManager) return;
    int64_t volume = (int64_t)(maxCorner.x - minCorner.x + 1) *
                     (maxCorner.y - minCorner.y + 1) *
                     (maxCorner.z - minCorner.z + 1);
    if (volume <= 0 || volume > Core::SnapshotManager::MAX_VOLUME) return;

    Core::RegionSnapshot snap;
    snap.name = label;
    snap.min = minCorner;
    snap.max = maxCorner;
    snap.size = maxCorner - minCorner + glm::ivec3(1);
    snap.totalVolume = volume;
    snap.createdAt = std::chrono::system_clock::now();
    snap.voxels = captureRegionAllLevels(chunkManager, minCorner, maxCorner);
    snapshotManager->pushUndo(std::move(snap));
}

// ============================================================================
// HTTP API Command Processor
// Called once per frame from update() to handle commands from the API server.
// ============================================================================

// Helper: handle AI inspection, location & schedule commands (extracted to avoid nesting depth limit)
static bool handlePlacedObjectHierarchyCommands(
    const Core::APICommand& cmd,
    nlohmann::json& response,
    Core::PlacedObjectManager* placedObjectManager)
{
    if (cmd.action == "set_parent_object") {
        if (!placedObjectManager) { response = {{"error", "PlacedObjectManager not available"}}; return true; }
        std::string id = cmd.params.value("id", "");
        if (id.empty()) { response = {{"error", "Missing 'id' parameter"}}; return true; }
        std::string parentId = cmd.params.value("parent_id", "");
        bool ok = placedObjectManager->setParent(id, parentId);
        response = {{"success", ok}, {"id", id}, {"parent_id", parentId}};
        return true;
    }
    if (cmd.action == "get_children_objects") {
        if (!placedObjectManager) { response = {{"error", "PlacedObjectManager not available"}}; return true; }
        std::string id = cmd.params.value("id", "");
        auto children = placedObjectManager->getChildren(id);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& child : children) arr.push_back(child.toJson());
        response = {{"children", arr}, {"count", children.size()}, {"parent_id", id}};
        return true;
    }
    if (cmd.action == "get_object_tree") {
        if (!placedObjectManager) { response = {{"error", "PlacedObjectManager not available"}}; return true; }
        std::string id = cmd.params.value("id", "");
        if (id.empty()) { response = {{"error", "Missing 'id' parameter"}}; return true; }
        auto tree = placedObjectManager->getTree(id);
        response = tree.is_null() ? nlohmann::json({{"error", "Object not found"}}) : tree;
        return true;
    }
    return false;
}

static bool handleAIInspectionCommands(
    const Core::APICommand& cmd,
    nlohmann::json& response,
    Core::NPCManager* npcManager,
    Core::LocationRegistry* locationRegistry)
{
    if (cmd.action == "get_npc_blackboard") {
        if (!npcManager) { response = {{"error", "NPCManager not available"}}; return true; }
        std::string name = cmd.params.value("name", "");
        if (name.empty()) { response = {{"error", "NPC name required"}}; return true; }
        auto* npc = npcManager->getNPC(name);
        if (!npc) { response = {{"error", "NPC not found: " + name}}; return true; }
        auto* btBehavior = dynamic_cast<Scene::BehaviorTreeBehavior*>(npc->getBehavior());
        if (!btBehavior) { response = {{"error", "NPC does not have a BehaviorTree behavior"}}; return true; }
        response = {{"success", true}, {"name", name}, {"blackboard", btBehavior->getBlackboard().toJson()}};
        return true;
    }
    if (cmd.action == "get_npc_perception") {
        if (!npcManager) { response = {{"error", "NPCManager not available"}}; return true; }
        std::string name = cmd.params.value("name", "");
        if (name.empty()) { response = {{"error", "NPC name required"}}; return true; }
        auto* npc = npcManager->getNPC(name);
        if (!npc) { response = {{"error", "NPC not found: " + name}}; return true; }
        auto* btBehavior = dynamic_cast<Scene::BehaviorTreeBehavior*>(npc->getBehavior());
        if (!btBehavior) { response = {{"error", "NPC does not have a BehaviorTree behavior"}}; return true; }
        response = {{"success", true}, {"name", name}, {"perception", btBehavior->getPerception().toJson()}};
        return true;
    }
    if (cmd.action == "set_npc_blackboard") {
        if (!npcManager) { response = {{"error", "NPCManager not available"}}; return true; }
        std::string name = cmd.params.value("name", "");
        std::string key = cmd.params.value("key", "");
        if (name.empty() || key.empty()) { response = {{"error", "NPC name and key required"}}; return true; }
        auto* npc = npcManager->getNPC(name);
        if (!npc) { response = {{"error", "NPC not found: " + name}}; return true; }
        auto* btBehavior = dynamic_cast<Scene::BehaviorTreeBehavior*>(npc->getBehavior());
        if (!btBehavior) { response = {{"error", "NPC does not have a BehaviorTree behavior"}}; return true; }
        auto& bb = btBehavior->getBlackboard();
        if (cmd.params.contains("value")) {
            auto& val = cmd.params["value"];
            if (val.is_boolean()) bb.set(key, val.get<bool>());
            else if (val.is_number_integer()) bb.set(key, val.get<int>());
            else if (val.is_number_float()) bb.set(key, val.get<float>());
            else if (val.is_string()) bb.set(key, val.get<std::string>());
            else if (val.is_object() && val.contains("x")) {
                bb.set(key, glm::vec3(val.value("x", 0.0f), val.value("y", 0.0f), val.value("z", 0.0f)));
            }
        }
        response = {{"success", true}, {"name", name}, {"key", key}};
        return true;
    }
    if (cmd.action == "get_locations") {
        if (!locationRegistry) { response = {{"error", "LocationRegistry not available"}}; return true; }
        response = locationRegistry->toJson();
        return true;
    }
    if (cmd.action == "add_location") {
        if (!locationRegistry) { response = {{"error", "LocationRegistry not available"}}; return true; }
        auto loc = Core::Location::fromJson(cmd.params);
        locationRegistry->addLocation(loc);
        response = {{"success", true}, {"id", loc.id}, {"name", loc.name}};
        return true;
    }
    if (cmd.action == "remove_location") {
        if (!locationRegistry) { response = {{"error", "LocationRegistry not available"}}; return true; }
        std::string id = cmd.params.value("id", "");
        if (id.empty()) { response = {{"error", "Location id required"}}; return true; }
        locationRegistry->removeLocation(id);
        response = {{"success", true}, {"id", id}};
        return true;
    }
    if (cmd.action == "get_npc_schedule") {
        if (!npcManager) { response = {{"error", "NPCManager not available"}}; return true; }
        std::string name = cmd.params.value("name", "");
        if (name.empty()) { response = {{"error", "NPC name required"}}; return true; }
        auto* npc = npcManager->getNPC(name);
        if (!npc) { response = {{"error", "NPC not found: " + name}}; return true; }
        auto* scheduled = dynamic_cast<Scene::ScheduledBehavior*>(npc->getBehavior());
        if (!scheduled) { response = {{"error", "NPC does not have a Scheduled behavior"}}; return true; }
        response = {{"name", name}, {"schedule", scheduled->getSchedule().toJson()}};
        return true;
    }
    if (cmd.action == "set_npc_schedule") {
        if (!npcManager) { response = {{"error", "NPCManager not available"}}; return true; }
        std::string name = cmd.params.value("name", "");
        if (name.empty()) { response = {{"error", "NPC name required"}}; return true; }
        auto* npc = npcManager->getNPC(name);
        if (!npc) { response = {{"error", "NPC not found: " + name}}; return true; }
        auto* scheduled = dynamic_cast<Scene::ScheduledBehavior*>(npc->getBehavior());
        if (!scheduled) { response = {{"error", "NPC does not have a Scheduled behavior"}}; return true; }
        if (cmd.params.contains("schedule")) {
            AI::Schedule sched = AI::Schedule::fromJson(cmd.params["schedule"]);
            scheduled->setSchedule(sched);
        } else if (cmd.params.contains("role")) {
            std::string role = cmd.params["role"];
            scheduled->setSchedule(AI::Schedule::forRole(role));
        }
        response = {{"success", true}, {"name", name}};
        return true;
    }
    return false;
}

// Helper: handle navgrid/movement query commands (extracted to avoid nesting depth limit)
static bool handleNavGridQueryCommand(
    const Core::APICommand& cmd,
    nlohmann::json& response,
    Core::NPCManager* npcManager,
    Core::EntityRegistry* entityRegistry)
{
    if (cmd.action == "navgrid_cell") {
        if (!npcManager || !npcManager->getNavGrid()) {
            response = {{"error", "NavGrid not available"}};
        } else {
            int x = cmd.params.value("x", 0);
            int z = cmd.params.value("z", 0);
            const auto* cell = npcManager->getNavGrid()->getCell(x, z);
            if (cell) {
                response = {{"x", cell->x}, {"z", cell->z},
                            {"walkable", cell->walkable},
                            {"surfaceY", cell->surfaceY},
                            {"nearWall", cell->nearWall}};
            } else {
                response = {{"x", x}, {"z", z}, {"walkable", false},
                            {"message", "Cell not in grid"}};
            }
        }
        return true;

    } else if (cmd.action == "navgrid_path") {
        if (!npcManager || !npcManager->getPathfinder()) {
            response = {{"error", "Pathfinder not available"}};
        } else {
            int x1 = cmd.params.value("x1", 0);
            int z1 = cmd.params.value("z1", 0);
            int x2 = cmd.params.value("x2", 0);
            int z2 = cmd.params.value("z2", 0);
            auto result = npcManager->getPathfinder()->findPath(
                glm::vec3(x1 + 0.5f, 0.0f, z1 + 0.5f),
                glm::vec3(x2 + 0.5f, 0.0f, z2 + 0.5f));
            nlohmann::json waypoints = nlohmann::json::array();
            for (const auto& wp : result.waypoints) {
                waypoints.push_back({{"x", wp.x}, {"y", wp.y}, {"z", wp.z}});
            }
            response = {{"found", result.found},
                        {"waypoints", waypoints},
                        {"nodesExpanded", result.nodesExpanded}};
        }
        return true;

    } else if (cmd.action == "entity_movement_state") {
        std::string id = cmd.params.value("id", "");
        if (id.empty() || !entityRegistry) {
            response = {{"error", "Entity ID required"}};
        } else {
            auto* entity = entityRegistry->getEntity(id);
            if (!entity) {
                response = {{"error", "Entity not found: " + id}};
            } else {
                glm::vec3 pos = entity->getPosition();
                response = {{"id", id},
                            {"position", {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}}}};

                auto* animChar = dynamic_cast<Scene::AnimatedVoxelCharacter*>(entity);
                if (animChar) {
                    response["animationState"] = animChar->stateToString(animChar->getAnimationState());
                    response["clipName"] = animChar->getCurrentClipName();
                    response["yaw"] = animChar->getYaw();
                    glm::vec3 forward = animChar->getForwardDirection();
                    response["forward"] = {{"x", forward.x}, {"y", forward.y}, {"z", forward.z}};
                }
            }
        }
        return true;
    }

    return false;
}

// Helper: handle debug dynamic spawn commands (Bullet cubes / GPU particles)
static bool handleDebugDynamicSpawnCommand(
    const Core::APICommand& cmd,
    nlohmann::json& response,
    ChunkManager* chunkManager,
    GpuParticlePhysics* gpuParticles)
{
    if (cmd.action == "spawn_bullet_cube") {
        float x = cmd.params.value("x", 0.0f);
        float y = cmd.params.value("y", 20.0f);
        float z = cmd.params.value("z", 0.0f);
        std::string material = cmd.params.value("material", "Stone");
        float scale = cmd.params.value("scale", 1.0f);
        float lifetime = cmd.params.value("lifetime", 30.0f);
        int count = std::clamp(cmd.params.value("count", 1), 1, 300);
        glm::vec3 vel(0.0f);
        if (cmd.params.contains("velocity")) {
            vel.x = cmd.params["velocity"].value("x", 0.0f);
            vel.y = cmd.params["velocity"].value("y", 0.0f);
            vel.z = cmd.params["velocity"].value("z", 0.0f);
        }
        if (!chunkManager || !chunkManager->physicsWorld) {
            response = {{"error", "ChunkManager or PhysicsWorld not available"}};
            return true;
        }
        glm::vec3 cubeSize(scale);
        float spacing = scale * 1.1f;
        int gridSize = static_cast<int>(std::ceil(std::cbrt(static_cast<float>(count))));
        int spawned = 0;
        for (int i = 0; i < count; ++i) {
            int gx = i % gridSize;
            int gy = (i / gridSize) % gridSize;
            int gz = i / (gridSize * gridSize);
            glm::vec3 pos(x + gx * spacing, y + gy * spacing, z + gz * spacing);
            glm::vec3 center = pos + glm::vec3(scale * 0.5f);
            auto cube = std::make_unique<Cube>(glm::ivec3(pos), material);
            if (auto* vw = chunkManager->physicsWorld->getVoxelWorld()) {
                auto* body = vw->createVoxelBody(center, cubeSize * 0.5f, 1.0f);
                if (body) body->linearVelocity = vel;
                cube->setVoxelBody(body);
            }
            cube->setPhysicsPosition(center);
            cube->setDynamicScale(cubeSize);
            cube->setLifetime(lifetime);
            cube->breakApart();
            chunkManager->m_dynamicObjectManager.addGlobalDynamicCube(std::move(cube));
            ++spawned;
        }
        response = {{"success", true}, {"spawned", spawned}, {"system", "voxel"},
                    {"scale", scale}, {"position", {{"x", x}, {"y", y}, {"z", z}}}};
        return true;
    }
    if (cmd.action == "spawn_gpu_particle") {
        float x = cmd.params.value("x", 0.0f);
        float y = cmd.params.value("y", 20.0f);
        float z = cmd.params.value("z", 0.0f);
        std::string material = cmd.params.value("material", "Stone");
        float scale = cmd.params.value("scale", 1.0f);
        float lifetime = cmd.params.value("lifetime", 30.0f);
        int count = std::clamp(cmd.params.value("count", 1), 1, 2000);
        glm::vec3 vel(0.0f);
        if (cmd.params.contains("velocity")) {
            vel.x = cmd.params["velocity"].value("x", 0.0f);
            vel.y = cmd.params["velocity"].value("y", 0.0f);
            vel.z = cmd.params["velocity"].value("z", 0.0f);
        }
        if (!gpuParticles || !gpuParticles->isInitialized()) {
            response = {{"error", "GPU particle physics not available"}};
            return true;
        }
        float spacing = scale * 1.1f;
        int gridSize = static_cast<int>(std::ceil(std::cbrt(static_cast<float>(count))));
        int spawned = 0;
        for (int i = 0; i < count; ++i) {
            int gx = i % gridSize;
            int gy = (i / gridSize) % gridSize;
            int gz = i / (gridSize * gridSize);
            GpuParticlePhysics::SpawnParams sp;
            sp.position = glm::vec3(x + gx * spacing, y + gy * spacing, z + gz * spacing);
            sp.velocity = vel;
            sp.materialName = material;
            sp.scale = glm::vec3(scale);
            sp.lifetime = lifetime;
            gpuParticles->queueSpawn(sp);
            ++spawned;
        }
        response = {{"success", true}, {"spawned", spawned}, {"system", "gpu"},
                    {"position", {{"x", x}, {"y", y}, {"z", z}}}};
        return true;
    }
    if (cmd.action == "spawn_voxel_body") {
        float x = cmd.params.value("x", 0.0f);
        float y = cmd.params.value("y", 20.0f);
        float z = cmd.params.value("z", 0.0f);
        float scale = cmd.params.value("scale", 1.0f);
        float mass = cmd.params.value("mass", 1.0f);
        float restitution = cmd.params.value("restitution", 0.2f);
        float friction = cmd.params.value("friction", 0.6f);
        float lifetime = cmd.params.value("lifetime", std::numeric_limits<float>::max());
        int count = std::clamp(cmd.params.value("count", 1), 1, 5000);
        glm::vec3 vel(0.0f);
        if (cmd.params.contains("velocity")) {
            vel.x = cmd.params["velocity"].value("x", 0.0f);
            vel.y = cmd.params["velocity"].value("y", 0.0f);
            vel.z = cmd.params["velocity"].value("z", 0.0f);
        }
        if (!chunkManager || !chunkManager->physicsWorld ||
            !chunkManager->physicsWorld->getVoxelWorld()) {
            response = {{"error", "VoxelDynamicsWorld not available"}};
            return true;
        }
        auto* voxelWorld = chunkManager->physicsWorld->getVoxelWorld();
        glm::vec3 halfExtents(scale * 0.5f);
        float spacing = scale * 1.1f;
        int gridSize = static_cast<int>(std::ceil(std::cbrt(static_cast<float>(count))));
        int spawned = 0;
        for (int i = 0; i < count; ++i) {
            int gx = i % gridSize;
            int gy = (i / gridSize) % gridSize;
            int gz = i / (gridSize * gridSize);
            glm::vec3 pos(x + gx * spacing, y + gy * spacing, z + gz * spacing);
            auto* body = voxelWorld->createVoxelBody(pos, halfExtents, mass, restitution, friction);
            if (body) {
                body->linearVelocity = vel;
                body->lifetime = lifetime;
                ++spawned;
            }
        }
        response = {{"success", true}, {"spawned", spawned}, {"system", "voxel"},
                    {"body_count", static_cast<int>(voxelWorld->getBodyCount())},
                    {"position", {{"x", x}, {"y", y}, {"z", z}}}};
        return true;
    }
    if (cmd.action == "clear_voxel_bodies") {
        if (!chunkManager || !chunkManager->physicsWorld ||
            !chunkManager->physicsWorld->getVoxelWorld()) {
            response = {{"error", "VoxelDynamicsWorld not available"}};
            return true;
        }
        auto* voxelWorld = chunkManager->physicsWorld->getVoxelWorld();
        size_t cleared = voxelWorld->getBodyCount();
        voxelWorld->removeAllBodies();
        response = {{"success", true}, {"cleared", cleared}};
        return true;
    }
    if (cmd.action == "clear_dynamics") {
        size_t bulletCleared = 0;
        uint32_t gpuCleared = 0;
        if (chunkManager) {
            bulletCleared = chunkManager->m_dynamicObjectManager.getActiveBulletCount();
            chunkManager->m_dynamicObjectManager.clearAllGlobalDynamicCubes();
            chunkManager->m_dynamicObjectManager.clearAllGlobalDynamicSubcubes();
            chunkManager->m_dynamicObjectManager.clearAllGlobalDynamicMicrocubes();
        }
        if (gpuParticles && gpuParticles->isInitialized()) {
            gpuCleared = gpuParticles->getActiveParticleCount();
            gpuParticles->despawnAll();
        }
        response = {{"success", true},
                    {"bullet_cleared", bulletCleared},
                    {"gpu_cleared", gpuCleared}};
        return true;
    }
    return false;
}

// Helper: handle subcube/microcube API commands (extracted to avoid nesting depth limit)
static bool handleSubcubeMicrocubeCommand(
    const Core::APICommand& cmd,
    nlohmann::json& response,
    ChunkManager* chunkManager,
    Core::NPCManager* npcManager)
{
    if (cmd.action == "remove_subcube") {
        int x = cmd.params.value("x", 0), y = cmd.params.value("y", 0), z = cmd.params.value("z", 0);
        int sx = cmd.params.value("sx", 0), sy = cmd.params.value("sy", 0), sz = cmd.params.value("sz", 0);
        if (!chunkManager) { response = {{"error", "ChunkManager not available"}}; return true; }
        bool ok = chunkManager->m_voxelModificationSystem.removeSubcube(
            glm::ivec3(x, y, z), glm::ivec3(sx, sy, sz));
        response = {{"success", ok}, {"position", {{"x", x}, {"y", y}, {"z", z}}},
                    {"subcube", {{"sx", sx}, {"sy", sy}, {"sz", sz}}}};
        if (ok && npcManager) npcManager->onVoxelChanged(glm::ivec3(x, y, z));
        return true;
    }
    if (cmd.action == "place_microcube") {
        int x = cmd.params.value("x", 0), y = cmd.params.value("y", 0), z = cmd.params.value("z", 0);
        int sx = cmd.params.value("sx", 0), sy = cmd.params.value("sy", 0), sz = cmd.params.value("sz", 0);
        int mx = cmd.params.value("mx", 0), my = cmd.params.value("my", 0), mz = cmd.params.value("mz", 0);
        if (!chunkManager) { response = {{"error", "ChunkManager not available"}}; return true; }
        std::string material = cmd.params.value("material", "Default");
        bool ok = chunkManager->m_voxelModificationSystem.addMicrocubeWithMaterial(
            glm::ivec3(x, y, z), glm::ivec3(sx, sy, sz), glm::ivec3(mx, my, mz), material);
        response = {{"success", ok}, {"position", {{"x", x}, {"y", y}, {"z", z}}},
                    {"subcube", {{"sx", sx}, {"sy", sy}, {"sz", sz}}},
                    {"microcube", {{"mx", mx}, {"my", my}, {"mz", mz}}}};
        if (ok && npcManager) npcManager->onVoxelChanged(glm::ivec3(x, y, z));
        return true;
    }
    if (cmd.action == "remove_microcube") {
        int x = cmd.params.value("x", 0), y = cmd.params.value("y", 0), z = cmd.params.value("z", 0);
        int sx = cmd.params.value("sx", 0), sy = cmd.params.value("sy", 0), sz = cmd.params.value("sz", 0);
        int mx = cmd.params.value("mx", 0), my = cmd.params.value("my", 0), mz = cmd.params.value("mz", 0);
        if (!chunkManager) { response = {{"error", "ChunkManager not available"}}; return true; }
        bool ok = chunkManager->m_voxelModificationSystem.removeMicrocube(
            glm::ivec3(x, y, z), glm::ivec3(sx, sy, sz), glm::ivec3(mx, my, mz));
        response = {{"success", ok}, {"position", {{"x", x}, {"y", y}, {"z", z}}},
                    {"subcube", {{"sx", sx}, {"sy", sy}, {"sz", sz}}},
                    {"microcube", {{"mx", mx}, {"my", my}, {"mz", mz}}}};
        if (ok && npcManager) npcManager->onVoxelChanged(glm::ivec3(x, y, z));
        return true;
    }
    if (cmd.action == "place_subcubes_batch") {
        if (!chunkManager) { response = {{"error", "ChunkManager not available"}}; return true; }
        if (!cmd.params.contains("subcubes") || !cmd.params["subcubes"].is_array()) {
            response = {{"error", "Missing 'subcubes' array"}}; return true;
        }
        auto& items = cmd.params["subcubes"];
        int placed = 0, failed = 0;
        glm::ivec3 bmin(INT_MAX), bmax(INT_MIN);
        for (auto& item : items) {
            int x = item.value("x", 0), y = item.value("y", 0), z = item.value("z", 0);
            int sx = item.value("sx", 0), sy = item.value("sy", 0), sz = item.value("sz", 0);
            std::string mat = item.value("material", "Default");
            bool ok = chunkManager->m_voxelModificationSystem.addSubcubeWithMaterial(
                glm::ivec3(x, y, z), glm::ivec3(sx, sy, sz), mat);
            if (ok) { ++placed; bmin = glm::min(bmin, glm::ivec3(x,y,z)); bmax = glm::max(bmax, glm::ivec3(x,y,z)); }
            else ++failed;
        }
        if (npcManager && placed > 0) npcManager->onRegionChanged(bmin, bmax);
        response = {{"success", true}, {"placed", placed}, {"failed", failed}, {"total", items.size()}};
        return true;
    }
    if (cmd.action == "place_microcubes_batch") {
        if (!chunkManager) { response = {{"error", "ChunkManager not available"}}; return true; }
        if (!cmd.params.contains("microcubes") || !cmd.params["microcubes"].is_array()) {
            response = {{"error", "Missing 'microcubes' array"}}; return true;
        }
        auto& items = cmd.params["microcubes"];
        int placed = 0, failed = 0;
        glm::ivec3 bmin(INT_MAX), bmax(INT_MIN);
        for (auto& item : items) {
            int x = item.value("x", 0), y = item.value("y", 0), z = item.value("z", 0);
            int sx = item.value("sx", 0), sy = item.value("sy", 0), sz = item.value("sz", 0);
            int mx = item.value("mx", 0), my = item.value("my", 0), mz = item.value("mz", 0);
            std::string mat = item.value("material", "Default");
            bool ok = chunkManager->m_voxelModificationSystem.addMicrocubeWithMaterial(
                glm::ivec3(x, y, z), glm::ivec3(sx, sy, sz), glm::ivec3(mx, my, mz), mat);
            if (ok) { ++placed; bmin = glm::min(bmin, glm::ivec3(x,y,z)); bmax = glm::max(bmax, glm::ivec3(x,y,z)); }
            else ++failed;
        }
        if (npcManager && placed > 0) npcManager->onRegionChanged(bmin, bmax);
        response = {{"success", true}, {"placed", placed}, {"failed", failed}, {"total", items.size()}};
        return true;
    }
    return false; // not handled
}

// Helper: handle placed-object CRUD commands (extracted to reduce nesting depth)
static bool handlePlacedObjectCommand(
    const Core::APICommand& cmd,
    nlohmann::json& response,
    Core::PlacedObjectManager* placedObjectManager,
    ChunkManager* chunkManager,
    Core::SnapshotManager* snapshotManager)
{
    if (cmd.action == "list_placed_objects") {
        if (!placedObjectManager) {
            response = {{"error", "PlacedObjectManager not available"}};
        } else {
            auto objects = placedObjectManager->list();
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& obj : objects) {
                arr.push_back(obj.toJson());
            }
            response = {{"objects", arr}, {"count", objects.size()}};
        }
        return true;

    } else if (cmd.action == "get_placed_object") {
        if (!placedObjectManager) {
            response = {{"error", "PlacedObjectManager not available"}};
        } else {
            std::string id = cmd.params.value("id", "");
            if (id.empty()) {
                response = {{"error", "Missing 'id' parameter"}};
            } else {
                const auto* obj = placedObjectManager->get(id);
                if (!obj) {
                    response = {{"error", "Object not found"}, {"id", id}};
                } else {
                    response = obj->toJson();
                }
            }
        }
        return true;

    } else if (cmd.action == "remove_placed_object") {
        if (!placedObjectManager) {
            response = {{"error", "PlacedObjectManager not available"}};
        } else {
            std::string id = cmd.params.value("id", "");
            if (id.empty()) {
                response = {{"error", "Missing 'id' parameter"}};
            } else {
                const auto* obj = placedObjectManager->get(id);
                if (obj && chunkManager && snapshotManager) {
                    pushUndoSnapshot(chunkManager, snapshotManager,
                                     obj->boundingMin, obj->boundingMax,
                                     "remove_placed_object:" + id);
                }
                bool ok = placedObjectManager->remove(id);
                if (ok && chunkManager) {
                    auto* ws = chunkManager->m_streamingManager.getWorldStorage();
                    if (ws) placedObjectManager->saveToDb(ws->getDb());
                }
                response = {{"success", ok}, {"id", id}};
            }
        }
        return true;

    } else if (cmd.action == "move_placed_object") {
        if (!placedObjectManager) {
            response = {{"error", "PlacedObjectManager not available"}};
        } else {
            std::string id = cmd.params.value("id", "");
            if (id.empty() || !cmd.params.contains("position")) {
                response = {{"error", "Missing 'id' or 'position' parameter"}};
            } else {
                int nx = cmd.params["position"].value("x", 0);
                int ny = cmd.params["position"].value("y", 0);
                int nz = cmd.params["position"].value("z", 0);

                const auto* obj = placedObjectManager->get(id);
                if (obj && chunkManager && snapshotManager) {
                    pushUndoSnapshot(chunkManager, snapshotManager,
                                     obj->boundingMin, obj->boundingMax,
                                     "move_placed_object:" + id);
                }

                bool ok = placedObjectManager->move(id, glm::ivec3(nx, ny, nz));
                if (ok && chunkManager) {
                    auto* ws = chunkManager->m_streamingManager.getWorldStorage();
                    if (ws) placedObjectManager->saveToDb(ws->getDb());
                }
                response = {{"success", ok}, {"id", id},
                            {"position", {{"x", nx}, {"y", ny}, {"z", nz}}}};
            }
        }
        return true;

    } else if (cmd.action == "rotate_placed_object") {
        if (!placedObjectManager) {
            response = {{"error", "PlacedObjectManager not available"}};
        } else {
            std::string id = cmd.params.value("id", "");
            int newRotation = cmd.params.value("rotation", -1);
            if (id.empty() || newRotation < 0) {
                response = {{"error", "Missing 'id' or 'rotation' parameter"}};
            } else {
                const auto* obj = placedObjectManager->get(id);
                if (obj && chunkManager && snapshotManager) {
                    pushUndoSnapshot(chunkManager, snapshotManager,
                                     obj->boundingMin, obj->boundingMax,
                                     "rotate_placed_object:" + id);
                }
                bool ok = placedObjectManager->rotate(id, newRotation);
                if (ok && chunkManager) {
                    auto* ws = chunkManager->m_streamingManager.getWorldStorage();
                    if (ws) placedObjectManager->saveToDb(ws->getDb());
                }
                response = {{"success", ok}, {"id", id}, {"rotation", newRotation}};
            }
        }
        return true;

    } else if (cmd.action == "get_objects_at") {
        if (!placedObjectManager) {
            response = {{"error", "PlacedObjectManager not available"}};
        } else {
            int x = cmd.params.value("x", 0);
            int y = cmd.params.value("y", 0);
            int z = cmd.params.value("z", 0);
            auto ids = placedObjectManager->getAt(glm::ivec3(x, y, z));
            response = {{"object_ids", ids}, {"count", ids.size()}};
        }
        return true;
    }

    return false; // not a placed-object command
}

// Helper: handle dynamic furniture commands (extracted to reduce nesting depth)
static bool handleDynamicFurnitureCommand(
    const Core::APICommand& cmd,
    nlohmann::json& response,
    Core::DynamicFurnitureManager* dynamicFurnitureManager)
{
    if (cmd.action == "activate_furniture") {
        if (!dynamicFurnitureManager) {
            response = {{"error", "DynamicFurnitureManager not available"}};
        } else {
            std::string id = cmd.params.value("id", "");
            if (id.empty()) {
                response = {{"error", "Missing 'id' parameter"}};
            } else {
                glm::vec3 impulse(
                    cmd.params.value("impulse_x", 0.0f),
                    cmd.params.value("impulse_y", 0.0f),
                    cmd.params.value("impulse_z", 0.0f)
                );
                glm::vec3 contact(
                    cmd.params.value("contact_x", 0.0f),
                    cmd.params.value("contact_y", 0.0f),
                    cmd.params.value("contact_z", 0.0f)
                );
                bool ok = dynamicFurnitureManager->activate(id, impulse, contact);
                response = {{"success", ok}, {"id", id}};
            }
        }
        return true;

    } else if (cmd.action == "deactivate_furniture") {
        if (!dynamicFurnitureManager) {
            response = {{"error", "DynamicFurnitureManager not available"}};
        } else {
            std::string id = cmd.params.value("id", "");
            bool restoreOriginal = cmd.params.value("restore_original", false);
            if (id.empty()) {
                response = {{"error", "Missing 'id' parameter"}};
            } else {
                bool ok = dynamicFurnitureManager->deactivate(id, restoreOriginal);
                response = {{"success", ok}, {"id", id}};
            }
        }
        return true;

    } else if (cmd.action == "list_dynamic_furniture") {
        if (!dynamicFurnitureManager) {
            response = {{"error", "DynamicFurnitureManager not available"}};
        } else {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& [id, obj] : dynamicFurnitureManager->getActiveObjects()) {
                nlohmann::json entry;
                entry["id"] = id;
                entry["template"] = obj.templateName;
                entry["mass"] = obj.totalMass;
                entry["sleep_timer"] = obj.sleepTimer;
                entry["is_grabbed"] = obj.isGrabbed;
                glm::vec3 pos(obj.currentTransform[3]);
                entry["position"] = {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}};
                arr.push_back(entry);
            }
            response = {{"objects", arr}, {"count", dynamicFurnitureManager->activeCount()}};
        }
        return true;

    } else if (cmd.action == "shatter_furniture") {
        if (!dynamicFurnitureManager) {
            response = {{"error", "DynamicFurnitureManager not available"}};
        } else {
            std::string id = cmd.params.value("id", "");
            if (id.empty()) {
                response = {{"error", "Missing 'id' parameter"}};
            } else {
                float force = cmd.params.value("force", 100.0f);
                glm::vec3 contact(
                    cmd.params.value("contact_x", 0.0f),
                    cmd.params.value("contact_y", 0.0f),
                    cmd.params.value("contact_z", 0.0f)
                );
                int fragments = dynamicFurnitureManager->shatter(id, force, contact);
                response = {{"success", fragments > 0}, {"id", id}, {"fragments", fragments}};
            }
        }
        return true;
    }

    return false; // not a furniture command
}

// Helper: handle door management API commands (extracted to avoid nesting depth limit)
static bool handleDoorCommand(
    const Core::APICommand& cmd,
    nlohmann::json& response,
    Core::DoorManager* doorManager,
    Core::PlacedObjectManager* placedObjectManager)
{
    if (cmd.action == "register_door") {
        if (!doorManager) {
            response = {{"error", "DoorManager not available"}};
        } else if (!cmd.params.contains("placed_object_id") || !cmd.params.contains("template_name")) {
            response = {{"error", "Missing 'placed_object_id' or 'template_name'"}};
        } else {
            std::string poId = cmd.params.value("placed_object_id", "");
            std::string tmplName = cmd.params.value("template_name", "");
            int baseRot = cmd.params.value("base_rotation", 0);
            float openAngle = cmd.params.value("open_angle", 90.0f);
            float swingSpeed = cmd.params.value("swing_speed", 120.0f);
            int   thickness  = cmd.params.value("thickness", 2);

            glm::vec3 hingePos(0.0f);
            if (cmd.params.contains("hinge")) {
                hingePos.x = cmd.params["hinge"].value("x", 0.0f);
                hingePos.y = cmd.params["hinge"].value("y", 0.0f);
                hingePos.z = cmd.params["hinge"].value("z", 0.0f);
            } else if (placedObjectManager) {
                const auto* obj = placedObjectManager->get(poId);
                if (obj) {
                    hingePos = glm::vec3(obj->position);
                }
            }

            bool ok = doorManager->registerDoor(poId, tmplName, hingePos, baseRot, openAngle, swingSpeed, thickness);
            response = {{"success", ok}, {"placed_object_id", poId}};
        }
        return true;
    }

    if (cmd.action == "toggle_door") {
        if (!doorManager) {
            response = {{"error", "DoorManager not available"}};
        } else if (!cmd.params.contains("placed_object_id")) {
            response = {{"error", "Missing 'placed_object_id'"}};
        } else {
            std::string poId = cmd.params.value("placed_object_id", "");
            doorManager->toggle(poId);
            const auto& doors = doorManager->getDoors();
            auto it = doors.find(poId);
            if (it != doors.end()) {
                response = {{"success", true}, {"placed_object_id", poId},
                            {"is_open", it->second.isOpen},
                            {"current_angle", it->second.currentAngle},
                            {"target_angle", it->second.targetAngle}};
            } else {
                response = {{"error", "Door not found: " + poId}};
            }
        }
        return true;
    }

    if (cmd.action == "open_door") {
        if (!doorManager) {
            response = {{"error", "DoorManager not available"}};
        } else if (!cmd.params.contains("placed_object_id")) {
            response = {{"error", "Missing 'placed_object_id'"}};
        } else {
            std::string poId = cmd.params.value("placed_object_id", "");
            doorManager->open(poId);
            response = {{"success", true}, {"placed_object_id", poId}};
        }
        return true;
    }

    if (cmd.action == "close_door") {
        if (!doorManager) {
            response = {{"error", "DoorManager not available"}};
        } else if (!cmd.params.contains("placed_object_id")) {
            response = {{"error", "Missing 'placed_object_id'"}};
        } else {
            std::string poId = cmd.params.value("placed_object_id", "");
            doorManager->close(poId);
            response = {{"success", true}, {"placed_object_id", poId}};
        }
        return true;
    }

    if (cmd.action == "list_doors") {
        if (!doorManager) {
            response = {{"error", "DoorManager not available"}};
        } else {
            const auto& doors = doorManager->getDoors();
            nlohmann::json doorList = nlohmann::json::array();
            for (const auto& [id, state] : doors) {
                doorList.push_back({
                    {"placed_object_id", id},
                    {"is_open", state.isOpen},
                    {"locked", state.locked},
                    {"current_angle", state.currentAngle},
                    {"open_angle", state.openAngle},
                    {"base_rotation", state.baseRotation},
                    {"swing_speed", state.swingSpeed},
                    {"settled", state.settled},
                    {"hinge", {{"x", state.worldHingePos.x}, {"y", state.worldHingePos.y}, {"z", state.worldHingePos.z}}}
                });
            }
            response = {{"doors", doorList}, {"count", doors.size()}};
        }
        return true;
    }

    if (cmd.action == "set_door_lock") {
        if (!doorManager) {
            response = {{"error", "DoorManager not available"}};
        } else if (!cmd.params.contains("placed_object_id")) {
            response = {{"error", "Missing 'placed_object_id'"}};
        } else {
            std::string poId = cmd.params.value("placed_object_id", "");
            bool locked = cmd.params.value("locked", true);
            std::string keyItemId = cmd.params.value("key_item_id", "");
            doorManager->setLocked(poId, locked, keyItemId);
            response = {{"success", true}, {"placed_object_id", poId}, {"locked", locked}};
        }
        return true;
    }

    if (cmd.action == "unregister_door") {
        if (!doorManager) {
            response = {{"error", "DoorManager not available"}};
        } else if (!cmd.params.contains("placed_object_id")) {
            response = {{"error", "Missing 'placed_object_id'"}};
        } else {
            std::string poId = cmd.params.value("placed_object_id", "");
            doorManager->unregisterDoor(poId);
            response = {{"success", true}, {"placed_object_id", poId}};
        }
        return true;
    }

    return false; // not a door command
}

void Application::processAPICommands() {
    if (!apiCommandQueue || !apiCommandQueue->hasPending()) return;

    std::vector<Core::APICommand> commands;
    apiCommandQueue->drainCommands(commands);

    for (auto& cmd : commands) {
        nlohmann::json response;
        try {
            // Handle debug dynamic spawn commands early (avoids nesting depth limit)
            if (handleDebugDynamicSpawnCommand(cmd, response, chunkManager,
                    gpuParticlePhysics.get())) {
                if (cmd.onComplete) cmd.onComplete(response);
                continue;
            }

            // Handle subcube/microcube commands via helper (avoids nesting depth limit)
            if (handleSubcubeMicrocubeCommand(cmd, response, chunkManager, npcManager.get())) {
                // handled  --  skip to promise fulfillment below
            } else if (cmd.action == "spawn_entity") {
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

                if (type == "physics" || type == "spider" || type == "animated") {
                    std::string animFile = cmd.params.value("animFile", "resources/animated_characters/humanoid.anim");
                    spawned = createAnimatedCharacter(glm::vec3(x, y, z), animFile);
                    entityType = "animated";
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
                    if (ok && npcManager) {
                        npcManager->onVoxelChanged(glm::ivec3(x, y, z));
                    }
                } else {
                    response = {{"error", "ChunkManager not available"}};
                }

            } else if (cmd.action == "place_subcube") {
                int x = cmd.params.value("x", 0);
                int y = cmd.params.value("y", 0);
                int z = cmd.params.value("z", 0);
                int sx = cmd.params.value("sx", 0);
                int sy = cmd.params.value("sy", 0);
                int sz = cmd.params.value("sz", 0);
                if (chunkManager) {
                    std::string material = cmd.params.value("material", "Default");
                    LOG_DEBUG("API", "place_subcube: world(%d,%d,%d) sub(%d,%d,%d) mat=%s",
                              x, y, z, sx, sy, sz, material.c_str());
                    bool ok = chunkManager->m_voxelModificationSystem.addSubcubeWithMaterial(
                        glm::ivec3(x, y, z), glm::ivec3(sx, sy, sz), material);
                    LOG_DEBUG("API", "place_subcube result: %s", ok ? "true" : "false");
                    response = {{"success", ok},
                                {"position", {{"x", x}, {"y", y}, {"z", z}}},
                                {"subcube", {{"sx", sx}, {"sy", sy}, {"sz", sz}}}};
                    if (ok && npcManager) {
                        npcManager->onVoxelChanged(glm::ivec3(x, y, z));
                    }
                } else {
                    LOG_ERROR("API", "place_subcube: ChunkManager not available!");
                    response = {{"error", "ChunkManager not available"}};
                }

            } else if (cmd.action == "fill_region") {
                // Fill a 3D box with voxels  --  batched per-chunk for performance
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
                        // Auto-snapshot for undo
                        pushUndoSnapshot(chunkManager, snapshotManager.get(),
                                         glm::ivec3(minX, minY, minZ), glm::ivec3(maxX, maxY, maxZ),
                                         "fill_region");
                        int placed = 0, failed = 0;

                        // Group positions by chunk for batch placement
                        std::unordered_map<glm::ivec3, std::vector<glm::ivec3>, ChunkCoordHash> chunkBatches;
                        for (int ix = minX; ix <= maxX; ++ix) {
                            for (int iy = minY; iy <= maxY; ++iy) {
                                for (int iz = minZ; iz <= maxZ; ++iz) {
                                    if (hollow &&
                                        ix > minX && ix < maxX &&
                                        iy > minY && iy < maxY &&
                                        iz > minZ && iz < maxZ) {
                                        continue;
                                    }
                                    glm::ivec3 worldPos(ix, iy, iz);
                                    glm::ivec3 cc = ChunkManager::worldToChunkCoord(worldPos);
                                    glm::ivec3 lp = ChunkManager::worldToLocalCoord(worldPos);
                                    chunkBatches[cc].push_back(lp);
                                }
                            }
                        }

                        for (auto& [cc, positions] : chunkBatches) {
                            // Ensure chunk exists
                            if (!chunkManager->getChunkAtCoord(cc)) {
                                glm::ivec3 origin = cc * 32;
                                chunkManager->createChunk(origin, false);
                            }
                            Chunk* chunk = chunkManager->getChunkAtCoord(cc);
                            if (!chunk) { failed += static_cast<int>(positions.size()); continue; }
                            int batchPlaced = chunk->addCubesBatch(positions, material);
                            placed += batchPlaced;
                            failed += static_cast<int>(positions.size()) - batchPlaced;
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
                        if (placed > 0 && npcManager) {
                            npcManager->buildNavGrid();
                        }
                        // Update GPU occupancy grid for filled region
                        if (placed > 0 && gpuParticlePhysics) {
                            for (int ix = minX; ix <= maxX; ++ix) {
                                for (int iy = minY; iy <= maxY; ++iy) {
                                    for (int iz = minZ; iz <= maxZ; ++iz) {
                                        if (hollow &&
                                            ix > minX && ix < maxX &&
                                            iy > minY && iy < maxY &&
                                            iz > minZ && iz < maxZ) {
                                            continue;
                                        }
                                        gpuParticlePhysics->setOccupied(ix, iy, iz, true);
                                    }
                                }
                            }
                        }
                    }
                }

            } else if (cmd.action == "list_materials") {
                auto& reg = Phyxel::Core::MaterialRegistry::instance();
                const auto& materials = reg.getAllMaterials();
                nlohmann::json matArr = nlohmann::json::array();
                for (const auto& mat : materials) {
                    nlohmann::json entry = {
                        {"name", mat.name},
                        {"id", reg.getMaterialID(mat.name)},
                        {"emissive", mat.emissive}
                    };
                    if (mat.hasPhysics()) {
                        const auto& p = mat.physics;
                        entry["mass"] = p.mass;
                        entry["friction"] = p.friction;
                        entry["restitution"] = p.restitution;
                        entry["colorTint"] = {{"r", p.colorTint.r}, {"g", p.colorTint.g}, {"b", p.colorTint.b}};
                        entry["metallic"] = p.metallic;
                        entry["roughness"] = p.roughness;
                    }
                    matArr.push_back(entry);
                }
                response = {{"materials", matArr}, {"count", materials.size()}};

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
                // Clear (remove) all voxels in a 3D box  --  batched per-chunk for performance
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
                        // Auto-snapshot for undo
                        pushUndoSnapshot(chunkManager, snapshotManager.get(),
                                         glm::ivec3(minX, minY, minZ), glm::ivec3(maxX, maxY, maxZ),
                                         "clear_region");
                        int removed = 0, skipped = 0;

                        // Group positions by chunk for batch removal
                        std::unordered_map<glm::ivec3, std::vector<glm::ivec3>, ChunkCoordHash> chunkBatches;
                        for (int ix = minX; ix <= maxX; ++ix) {
                            for (int iy = minY; iy <= maxY; ++iy) {
                                for (int iz = minZ; iz <= maxZ; ++iz) {
                                    glm::ivec3 worldPos(ix, iy, iz);
                                    glm::ivec3 cc = ChunkManager::worldToChunkCoord(worldPos);
                                    glm::ivec3 lp = ChunkManager::worldToLocalCoord(worldPos);
                                    chunkBatches[cc].push_back(lp);
                                }
                            }
                        }

                        for (auto& [cc, positions] : chunkBatches) {
                            Chunk* chunk = chunkManager->getChunkAtCoord(cc);
                            if (!chunk) { skipped += static_cast<int>(positions.size()); continue; }

                            // If clearing all 32k positions, use fast path
                            if (positions.size() >= 32 * 32 * 32) {
                                // Count existing cubes before clearing
                                int existing = 0;
                                for (const auto& p : positions) {
                                    if (chunk->getCubeAt(p)) ++existing;
                                }
                                chunk->clearAll();
                                removed += existing;
                                skipped += static_cast<int>(positions.size()) - existing;
                            } else {
                                int batchRemoved = chunk->removeCubesBatch(positions);
                                removed += batchRemoved;
                                // Also clear subcubes/microcubes at each position
                                int subRemoved = 0;
                                for (const auto& p : positions) {
                                    if (chunk->clearSubdivisionAt(p)) ++subRemoved;
                                }
                                removed += subRemoved;
                                skipped += static_cast<int>(positions.size()) - batchRemoved - subRemoved;
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
                        if (removed > 0 && npcManager) {
                            npcManager->buildNavGrid();
                        }
                        // Update GPU occupancy grid for cleared region
                        if (removed > 0 && gpuParticlePhysics) {
                            for (int ix = minX; ix <= maxX; ++ix)
                                for (int iy = minY; iy <= maxY; ++iy)
                                    for (int iz = minZ; iz <= maxZ; ++iz)
                                        gpuParticlePhysics->setOccupied(ix, iy, iz, false);
                        }
                    }
                }

            } else if (cmd.action == "clear_chunk") {
                // Instantly clear all voxels in a chunk (bulk operation)
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else {
                    int cx = cmd.params.value("x", 0);
                    int cy = cmd.params.value("y", 0);
                    int cz = cmd.params.value("z", 0);
                    glm::ivec3 cc(cx, cy, cz);
                    bool ok = chunkManager->clearChunk(cc);
                    if (ok) {
                        response = {{"success", true}, {"chunk", {{"x", cx}, {"y", cy}, {"z", cz}}}};
                        if (gameEventLog) {
                            gameEventLog->emit("chunk_cleared", {{"chunk", {{"x", cx}, {"y", cy}, {"z", cz}}}});
                        }
                        // Update GPU occupancy grid for entire cleared chunk
                        if (gpuParticlePhysics) {
                            glm::ivec3 origin = cc * 32;
                            for (int lx = 0; lx < 32; ++lx)
                                for (int ly = 0; ly < 32; ++ly)
                                    for (int lz = 0; lz < 32; ++lz)
                                        gpuParticlePhysics->setOccupied(origin.x + lx, origin.y + ly, origin.z + lz, false);
                        }
                    } else {
                        response = {{"error", "Chunk not found"}, {"chunk", {{"x", cx}, {"y", cy}, {"z", cz}}}};
                    }
                }

            } else if (cmd.action == "rebuild_physics") {
                // Force rebuild physics collision for all or specific chunks
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else {
                    int rebuilt = 0;
                    if (cmd.params.contains("chunk")) {
                        int cx = cmd.params["chunk"].value("x", 0);
                        int cy = cmd.params["chunk"].value("y", 0);
                        int cz = cmd.params["chunk"].value("z", 0);
                        Chunk* chunk = chunkManager->getChunkAtCoord(glm::ivec3(cx, cy, cz));
                        if (chunk) { chunk->forcePhysicsRebuild(); rebuilt = 1; }
                    } else {
                        for (auto& chunk : chunkManager->chunks) {
                            if (chunk) { chunk->forcePhysicsRebuild(); ++rebuilt; }
                        }
                    }
                    response = {{"success", true}, {"chunks_rebuilt", rebuilt}};
                }

            } else if (cmd.action == "clear_all_entities") {
                // Remove all entities from the world
                if (!entityRegistry) {
                    response = {{"error", "EntityRegistry not available"}};
                } else {
                    int count = static_cast<int>(entityRegistry->getAllIds().size());
                    entityRegistry->clear();
                    entities.clear();
                
                    animatedCharacter = nullptr;
                    response = {{"success", true}, {"removed", count}};
                    if (gameEventLog) {
                        gameEventLog->emit("all_entities_cleared", {{"removed", count}});
                    }
                }

            } else if (cmd.action == "reload_game_definition") {
                // Destructive reload: clear entities, NPCs, story, then load fresh
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else {
                    // Clear entities
                    if (entityRegistry) {
                        entityRegistry->clear();
                    }
                    entities.clear();
                
                    animatedCharacter = nullptr;

                    // Clear NPC / dialogue / story subsystems
                    if (npcManager) {
                        auto names = npcManager->getAllNPCNames();
                        for (const auto& n : names) npcManager->removeNPC(n);
                    }
                    if (dialogueSystem) dialogueSystem->endConversation();
                    if (storyEngine) {
                        auto charIds = storyEngine->getCharacterIds();
                        for (const auto& cid : charIds) storyEngine->removeCharacter(cid);
                    }

                    // Load the game definition
                    std::string jsonStr = cmd.params.value("definition", "");
                    if (jsonStr.empty()) {
                        response = {{"error", "No definition provided"}};
                    } else {
                        try {
                            auto defJson = nlohmann::json::parse(jsonStr);

                            Core::GameSubsystems subsystems;
                            subsystems.chunkManager = chunkManager;
                            subsystems.npcManager = npcManager.get();
                            subsystems.entityRegistry = entityRegistry.get();
                            subsystems.templateManager = objectTemplateManager.get();
                            subsystems.placedObjectManager = placedObjectManager.get();
                            subsystems.gameEventLog = gameEventLog.get();
                            subsystems.locationRegistry = locationRegistry;
                            subsystems.camera = camera;
                            subsystems.dialogueSystem = dialogueSystem.get();
                            subsystems.storyEngine = storyEngine.get();
                            subsystems.entitySpawner = [this](const std::string& type, const glm::vec3& pos,
                                                               const std::string& animFile) -> Scene::Entity* {
                                return createAnimatedCharacter(pos, animFile.empty() ? "resources/animated_characters/humanoid.anim" : animFile);
                            };
                            subsystems.aiRegister = [this](Scene::Entity* entity, const std::string& entityId,
                                                            const std::string& npcName, const std::string& personality) {
                                if (!aiSystem) return;
                                std::string recipe = Core::AssetManager::instance().resolveRecipe("characters/" + entityId + ".yaml");
                                if (recipe.empty()) recipe = Core::AssetManager::instance().resolveRecipe("characters/guard.yaml");
                                std::string p = personality.empty() ? "You are " + npcName + ", an NPC in a voxel world." : personality;
                                aiSystem->createAINPC(entity, entityId, recipe, p);
                            };

                            auto loadResult = Core::GameDefinitionLoader::load(defJson, subsystems);
                            response = loadResult.toJson();
                            response["reloaded"] = true;

                            // Switch to third-person if player was spawned
                            if (loadResult.playerSpawned && camera) {
                                camera->setMode(Graphics::CameraMode::ThirdPerson);
                                if (animatedCharacter) {
                                    currentControlTarget = ControlTarget::AnimatedCharacter;
                                    camera->setDistanceFromTarget(4.0f);
                                }
                            }
                        } catch (const std::exception& ex) {
                            response = {{"error", ex.what()}};
                        }
                    }
                }

            } else if (cmd.action == "get_terrain_height") {
                // Query the surface Y coordinate at a given (x, z)
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else {
                    int qx = cmd.params.value("x", 0);
                    int qz = cmd.params.value("z", 0);
                    int maxY = cmd.params.value("max_y", 255);
                    int minY = cmd.params.value("min_y", 0);

                    int surfaceY = -1;
                    for (int y = maxY; y >= minY; --y) {
                        if (chunkManager->hasVoxelAt(glm::ivec3(qx, y, qz))) {
                            surfaceY = y;
                            break;
                        }
                    }
                    if (surfaceY >= 0) {
                        response = {{"x", qx}, {"z", qz}, {"surface_y", surfaceY}, {"spawn_y", surfaceY + 1}};
                    } else {
                        response = {{"x", qx}, {"z", qz}, {"surface_y", nullptr}, {"spawn_y", nullptr},
                                    {"message", "No terrain found"}};
                    }
                }

            } else if (cmd.action == "save_world") {
                // Re-staticize all dynamic furniture before saving
                if (dynamicFurnitureManager) {
                    dynamicFurnitureManager->deactivateAll();
                }
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
                        // Also persist placed objects
                        if (placedObjectManager) {
                            auto* ws = chunkManager->m_streamingManager.getWorldStorage();
                            if (ws) {
                                placedObjectManager->saveToDb(ws->getDb());
                            }
                        }
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

            } else if (cmd.action == "equip_item") {
                std::string entityId = cmd.params.value("entityId", "");
                std::string itemId = cmd.params.value("itemId", "");
                if (entityId.empty() || itemId.empty()) {
                    response = {{"error", "entityId and itemId required"}};
                } else if (!entityRegistry) {
                    response = {{"error", "No entity registry"}};
                } else {
                    auto* entity = entityRegistry->getEntity(entityId);
                    if (!entity) {
                        response = {{"error", "Entity not found: " + entityId}};
                    } else {
                        auto* npc = dynamic_cast<Scene::NPCEntity*>(entity);
                        if (!npc) {
                            response = {{"error", "Entity is not an NPC: " + entityId}};
                        } else {
                            auto* def = Core::ItemRegistry::instance().getItem(itemId);
                            if (!def) {
                                response = {{"error", "Item not found: " + itemId}};
                            } else {
                                bool ok = npc->getEquipment().equip(*def);
                                if (ok) {
                                    // Attach weapon visual to right_hand bone
                                    auto* animChar = npc->getAnimatedCharacter();
                                    if (animChar && def->equipSlot == Core::EquipSlot::MainHand) {
                                        animChar->attachToBone("right_hand",
                                            glm::vec3(0.15f, 0.4f, 0.15f),
                                            glm::vec3(0.0f, 0.2f, 0.0f),
                                            glm::vec4(0.7f, 0.7f, 0.8f, 1.0f),
                                            itemId);
                                    }
                                    response = {{"success", true}, {"entityId", entityId}, {"itemId", itemId},
                                                {"slot", Core::equipSlotToString(def->equipSlot)}};
                                    if (gameEventLog) {
                                        gameEventLog->emit("item_equipped", {
                                            {"entityId", entityId}, {"itemId", itemId},
                                            {"slot", Core::equipSlotToString(def->equipSlot)}
                                        });
                                    }
                                } else {
                                    response = {{"error", "Cannot equip item (wrong type or slot)"}};
                                }
                            }
                        }
                    }
                }

            } else if (cmd.action == "unequip_item") {
                std::string entityId = cmd.params.value("entityId", "");
                std::string slotStr = cmd.params.value("slot", "");
                if (entityId.empty() || slotStr.empty()) {
                    response = {{"error", "entityId and slot required"}};
                } else if (!entityRegistry) {
                    response = {{"error", "No entity registry"}};
                } else {
                    auto* entity = entityRegistry->getEntity(entityId);
                    if (!entity) {
                        response = {{"error", "Entity not found: " + entityId}};
                    } else {
                        auto* npc = dynamic_cast<Scene::NPCEntity*>(entity);
                        if (!npc) {
                            response = {{"error", "Entity is not an NPC: " + entityId}};
                        } else {
                            Core::EquipSlot slot = Core::equipSlotFromString(slotStr);
                            if (slot == Core::EquipSlot::None) {
                                response = {{"error", "Invalid slot: " + slotStr}};
                            } else {
                                auto removedItem = npc->getEquipment().unequip(slot);
                                if (removedItem) {
                                    // Remove weapon visual
                                    auto* animChar = npc->getAnimatedCharacter();
                                    if (animChar && slot == Core::EquipSlot::MainHand) {
                                        animChar->detachAll();
                                    }
                                    response = {{"success", true}, {"entityId", entityId},
                                                {"slot", slotStr}, {"removedItemId", *removedItem}};
                                    if (gameEventLog) {
                                        gameEventLog->emit("item_unequipped", {
                                            {"entityId", entityId}, {"slot", slotStr},
                                            {"itemId", *removedItem}
                                        });
                                    }
                                } else {
                                    response = {{"error", "No item in slot: " + slotStr}};
                                }
                            }
                        }
                    }
                }

            } else if (cmd.action == "combat_attack") {
                std::string attackerId = cmd.params.value("attackerId", "");
                if (attackerId.empty() || !entityRegistry) {
                    response = {{"error", "attackerId required"}};
                } else {
                    auto* entity = entityRegistry->getEntity(attackerId);
                    if (!entity) {
                        response = {{"error", "Entity not found: " + attackerId}};
                    } else {
                        Core::CombatSystem::AttackParams params;
                        params.attackerId = attackerId;
                        params.attackerPos = entity->getPosition();
                        // Use entity forward direction if available, else use params
                        if (cmd.params.contains("forward")) {
                            params.attackerForward = glm::vec3(
                                cmd.params["forward"].value("x", 0.0f),
                                cmd.params["forward"].value("y", 0.0f),
                                cmd.params["forward"].value("z", -1.0f));
                        } else {
                            params.attackerForward = glm::vec3(0, 0, -1);
                        }

                        // Get weapon stats from equipment if NPC
                        auto* npc = dynamic_cast<Scene::NPCEntity*>(entity);
                        if (npc) {
                            params.damage = npc->getEquipment().getTotalDamage();
                            params.reach = npc->getEquipment().getTotalReach();
                        } else {
                            params.damage = cmd.params.value("damage", 10.0f);
                            params.reach = cmd.params.value("reach", 1.5f);
                        }
                        params.coneAngleDeg = cmd.params.value("coneAngle", 90.0f);
                        params.knockbackForce = cmd.params.value("knockback", 2.0f);

                        auto events = combatSystem->performAttack(params, *entityRegistry);
                        nlohmann::json hits = nlohmann::json::array();
                        for (const auto& e : events) {
                            hits.push_back(e.toJson());
                            if (gameEventLog) {
                                gameEventLog->emit("combat_damage", e.toJson());
                            }
                        }
                        response = {{"success", true}, {"attackerId", attackerId},
                                    {"hitCount", events.size()}, {"hits", hits}};
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
                    if (ok && npcManager) {
                        npcManager->onVoxelChanged(glm::ivec3(x, y, z));
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
                    // Auto-snapshot: compute bounding box of all voxels
                    if (snapshotManager) {
                        glm::ivec3 bmin(INT_MAX), bmax(INT_MIN);
                        for (const auto& v : cmd.params["voxels"]) {
                            glm::ivec3 p(v.value("x", 0), v.value("y", 0), v.value("z", 0));
                            bmin = glm::min(bmin, p);
                            bmax = glm::max(bmax, p);
                        }
                        if (bmin.x <= bmax.x) {
                            pushUndoSnapshot(chunkManager, snapshotManager.get(), bmin, bmax, "place_voxels_batch");
                        }
                    }
                    int placed = 0;
                    int failed = 0;
                    glm::ivec3 batchMin(INT_MAX), batchMax(INT_MIN);
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
                        if (ok) {
                            ++placed;
                            batchMin = glm::min(batchMin, glm::ivec3(x, y, z));
                            batchMax = glm::max(batchMax, glm::ivec3(x, y, z));
                        } else ++failed;
                    }
                    if (npcManager && placed > 0) {
                        npcManager->onRegionChanged(batchMin, batchMax);
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
                    const bool isStatic = true;
                    int rotation = cmd.params.value("rotation", 0);

                    // Auto-snapshot the affected region before spawning
                    if (isStatic && chunkManager && snapshotManager) {
                        const auto* tmpl = objectTemplateManager->getTemplate(name);
                        if (tmpl && !tmpl->cubes.empty()) {
                            glm::ivec3 tmin(INT_MAX), tmax(INT_MIN);
                            for (const auto& c : tmpl->cubes) {
                                tmin = glm::min(tmin, c.relativePos);
                                tmax = glm::max(tmax, c.relativePos);
                            }
                            for (const auto& s : tmpl->subcubes) {
                                tmin = glm::min(tmin, s.parentRelativePos);
                                tmax = glm::max(tmax, s.parentRelativePos);
                            }
                            for (const auto& m : tmpl->microcubes) {
                                tmin = glm::min(tmin, m.parentRelativePos);
                                tmax = glm::max(tmax, m.parentRelativePos);
                            }
                            glm::ivec3 origin(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z));
                            pushUndoSnapshot(chunkManager, snapshotManager.get(),
                                             origin + tmin, origin + tmax,
                                             "spawn_template:" + name);
                        }
                    }

                    // Route static placements through PlacedObjectManager for tracking
                    if (isStatic && placedObjectManager) {
                        glm::ivec3 pos(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z));
                        std::string parentId = cmd.params.value("parent_id", "");
                        std::string objectId = placedObjectManager->placeTemplate(name, pos, rotation, parentId);
                        bool ok = !objectId.empty();
                        // Immediately persist placed_objects so the record survives engine restarts
                        // without requiring a separate save_world call
                        if (ok && chunkManager) {
                            auto* ws = chunkManager->m_streamingManager.getWorldStorage();
                            if (ws) placedObjectManager->saveToDb(ws->getDb());
                        }
                        response = {{"success", ok}, {"template", name},
                                    {"object_id", objectId},
                                    {"position", {{"x", x}, {"y", y}, {"z", z}}},
                                    {"rotation", rotation}};
                    } else {
                        bool ok = objectTemplateManager->spawnTemplate(name, glm::vec3(x, y, z), isStatic, rotation);
                        response = {{"success", ok}, {"template", name},
                                    {"position", {{"x", x}, {"y", y}, {"z", z}}},
                                    {"rotation", rotation}};
                    }

                    // Rebuild NavGrid for the affected region
                    if (npcManager && isStatic) {
                        const auto* tmpl = objectTemplateManager->getTemplate(name);
                        if (tmpl) {
                            glm::ivec3 tmin(INT_MAX), tmax(INT_MIN);
                            for (const auto& c : tmpl->cubes) {
                                tmin = glm::min(tmin, c.relativePos);
                                tmax = glm::max(tmax, c.relativePos);
                            }
                            for (const auto& s : tmpl->subcubes) {
                                tmin = glm::min(tmin, s.parentRelativePos);
                                tmax = glm::max(tmax, s.parentRelativePos);
                            }
                            for (const auto& m : tmpl->microcubes) {
                                tmin = glm::min(tmin, m.parentRelativePos);
                                tmax = glm::max(tmax, m.parentRelativePos);
                            }
                            glm::ivec3 origin(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z));
                            npcManager->onRegionChanged(origin + tmin, origin + tmax);
                        }
                    }
                }

            } else if (cmd.action == "list_templates") {
                if (objectTemplateManager) {
                    auto names = objectTemplateManager->getTemplateNames();
                    response = {{"templates", names}};
                } else {
                    response = {{"templates", nlohmann::json::array()}};
                }

            } else if (cmd.action == "build_structure") {
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else if (!cmd.params.contains("type")) {
                    response = {{"error", "Missing 'type' parameter"}};
                } else {
                    auto structure = Core::StructureGenerator::generateFromJson(cmd.params);
                    if (structure.voxels.empty()) {
                        response = {{"error", "Failed to generate structure (unknown type or invalid params)"}};
                    } else {
                        // Auto-snapshot for undo: compute bounding box from generated voxels
                        if (snapshotManager) {
                            glm::ivec3 smin(INT_MAX), smax(INT_MIN);
                            for (const auto& v : structure.voxels) {
                                smin = glm::min(smin, v.position);
                                smax = glm::max(smax, v.position);
                            }
                            pushUndoSnapshot(chunkManager, snapshotManager.get(), smin, smax,
                                             "build_structure:" + cmd.params.value("type", "unknown"));
                        }
                        auto placement = Core::StructureGenerator::place(chunkManager, structure);

                        // Auto-register locations
                        nlohmann::json locationsJson = nlohmann::json::array();
                        if (locationRegistry) {
                            for (auto& loc : placement.locations) {
                                if (loc.id.empty()) {
                                    std::string stype = cmd.params.value("type", "structure");
                                    loc.id = stype + "_" + std::to_string(static_cast<int>(loc.position.x)) +
                                             "_" + std::to_string(static_cast<int>(loc.position.z));
                                }
                                Core::Location regLoc;
                                regLoc.id = loc.id;
                                regLoc.name = loc.name;
                                regLoc.position = loc.position;
                                regLoc.radius = loc.radius;
                                regLoc.type = loc.type;
                                locationRegistry->addLocation(regLoc);
                                locationsJson.push_back({
                                    {"id", loc.id}, {"name", loc.name},
                                    {"position", {{"x", loc.position.x}, {"y", loc.position.y}, {"z", loc.position.z}}},
                                    {"radius", loc.radius}, {"type", Core::Location::typeToString(loc.type)}
                                });
                            }
                        }

                        response = {{"success", true}, {"placed", placement.placed},
                                    {"failed", placement.failed}, {"voxels_generated", structure.voxels.size()},
                                    {"locations", locationsJson}};

                        // Register with PlacedObjectManager for tracking
                        if (placedObjectManager) {
                            glm::ivec3 smin(INT_MAX), smax(INT_MIN);
                            for (const auto& v : structure.voxels) {
                                smin = glm::min(smin, v.position);
                                smax = glm::max(smax, v.position);
                            }
                            int posX = cmd.params.contains("position") ? cmd.params["position"].value("x", smin.x) : smin.x;
                            int posY = cmd.params.contains("position") ? cmd.params["position"].value("y", smin.y) : smin.y;
                            int posZ = cmd.params.contains("position") ? cmd.params["position"].value("z", smin.z) : smin.z;
                            std::string parentId = cmd.params.value("parent_id", "");
                            std::string objectId = placedObjectManager->registerStructure(
                                cmd.params.value("type", "structure"),
                                glm::ivec3(posX, posY, posZ), 0, smin, smax, parentId);
                            response["object_id"] = objectId;
                            // Immediately persist so the structure record survives restarts
                            if (!objectId.empty() && chunkManager) {
                                auto* ws = chunkManager->m_streamingManager.getWorldStorage();
                                if (ws) placedObjectManager->saveToDb(ws->getDb());
                            }

                            // Rebuild NavGrid for the affected region
                            if (npcManager) {
                                npcManager->onRegionChanged(smin, smax);
                            }
                        }
                    }
                }

            } else if (cmd.action == "list_structure_types") {
                response = {{"types", Core::StructureGenerator::getStructureTypes()}};

            } else if (handleDoorCommand(cmd, response, doorManager.get(), placedObjectManager.get())) {
                // Handled by door helper

            } else if (cmd.action == "query_interaction") {
                // Debug: return current InteractionManager state
                std::string doorObjId, doorPtId, promptText;
                bool hasNPC = false;
                float cooldown = 0.0f;
                bool hasDoorCb = false;
                bool imguiWantsKb = ImGui::GetIO().WantCaptureKeyboard;
                if (interactionManager) {
                    doorObjId = interactionManager->getNearestDoorObjId();
                    doorPtId = interactionManager->getNearestDoorPtId();
                    hasNPC = (interactionManager->getNearestInteractableNPC() != nullptr);
                    hasDoorCb = interactionManager->hasDoorCallback();
                    cooldown = interactionManager->getCooldownTimer();
                    promptText = interactionManager->getActivePromptText();
                }
                response = {
                    {"success", true},
                    {"door_in_range", !doorObjId.empty()},
                    {"door_obj_id", doorObjId},
                    {"door_pt_id", doorPtId},
                    {"door_callback_set", hasDoorCb},
                    {"cooldown", cooldown},
                    {"npc_in_range", hasNPC},
                    {"seat_in_range", interactionManager && interactionManager->isSeatInRange()},
                    {"prompt_text", promptText},
                    {"imgui_wants_keyboard", imguiWantsKb},
                    {"has_interaction_manager", interactionManager != nullptr},
                    {"current_control_target", static_cast<int>(currentControlTarget)},
                    {"has_animated_char", animatedCharacter != nullptr},
                    {"game_paused", gamePaused}
                };

            } else if (cmd.action == "trigger_interact") {
                // Debug: trigger interactWithNPC() from API (bypasses keyboard)
                interactWithNPC();
                response = {{"success", true}, {"triggered", true}};

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
                            snap.voxels = captureRegionAllLevels(chunkManager, snap.min, snap.max);
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
                            // Clear the original region first (cubes + subcubes/microcubes)
                            int cleared = 0;
                            {
                                std::unordered_map<glm::ivec3, std::vector<glm::ivec3>, ChunkCoordHash> chunkBatches;
                                for (int ix = snap->min.x; ix <= snap->max.x; ++ix)
                                    for (int iy = snap->min.y; iy <= snap->max.y; ++iy)
                                        for (int iz = snap->min.z; iz <= snap->max.z; ++iz) {
                                            glm::ivec3 wp(ix, iy, iz);
                                            chunkBatches[ChunkManager::worldToChunkCoord(wp)]
                                                .push_back(ChunkManager::worldToLocalCoord(wp));
                                        }
                                for (auto& [cc, positions] : chunkBatches) {
                                    Chunk* chunk = chunkManager->getChunkAtCoord(cc);
                                    if (!chunk) continue;
                                    cleared += chunk->removeCubesBatch(positions);
                                    for (const auto& p : positions)
                                        if (chunk->clearSubdivisionAt(p)) ++cleared;
                                }
                            }
                            // Restore voxels from snapshot
                            auto [placed, failed] = placeVoxelEntries(chunkManager, snap->voxels, snap->min);
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
            // UNDO / REDO
            // ================================================================
            } else if (cmd.action == "undo") {
                if (!chunkManager || !snapshotManager) {
                    response = {{"error", "ChunkManager or SnapshotManager not available"}};
                } else if (!snapshotManager->canUndo()) {
                    response = {{"error", "Nothing to undo"}, {"undo_depth", 0}};
                } else {
                    // 1. Pop the "before" snapshot from the undo stack
                    Core::RegionSnapshot before = snapshotManager->popUndo();

                    // 2. Capture current state of that region for redo
                    Core::RegionSnapshot current;
                    current.name = before.name;
                    current.min = before.min;
                    current.max = before.max;
                    current.size = before.size;
                    current.totalVolume = before.totalVolume;
                    current.createdAt = std::chrono::system_clock::now();
                    current.voxels = captureRegionAllLevels(chunkManager, current.min, current.max);

                    // 3. Clear the region
                    {
                        std::unordered_map<glm::ivec3, std::vector<glm::ivec3>, ChunkCoordHash> chunkBatches;
                        for (int ix = before.min.x; ix <= before.max.x; ++ix)
                            for (int iy = before.min.y; iy <= before.max.y; ++iy)
                                for (int iz = before.min.z; iz <= before.max.z; ++iz) {
                                    glm::ivec3 wp(ix, iy, iz);
                                    chunkBatches[ChunkManager::worldToChunkCoord(wp)]
                                        .push_back(ChunkManager::worldToLocalCoord(wp));
                                }
                        for (auto& [cc, positions] : chunkBatches) {
                            Chunk* chunk = chunkManager->getChunkAtCoord(cc);
                            if (!chunk) continue;
                            chunk->removeCubesBatch(positions);
                            for (const auto& p : positions)
                                chunk->clearSubdivisionAt(p);
                        }
                    }

                    // 4. Restore the "before" state
                    auto [placed, failed] = placeVoxelEntries(chunkManager, before.voxels, before.min);

                    // 5. Push current state to redo (manual push since popUndo already locked/unlocked)
                    snapshotManager->pushRedo(std::move(current));

                    response = {{"success", true}, {"operation", before.name},
                                {"restored_voxels", placed}, {"failed", failed},
                                {"undo_depth", snapshotManager->undoDepth()},
                                {"redo_depth", snapshotManager->redoDepth()}};
                    if (gameEventLog) {
                        gameEventLog->emit("undo", {{"operation", before.name}, {"restored", placed}});
                    }
                }

            } else if (cmd.action == "redo") {
                if (!chunkManager || !snapshotManager) {
                    response = {{"error", "ChunkManager or SnapshotManager not available"}};
                } else if (!snapshotManager->canRedo()) {
                    response = {{"error", "Nothing to redo"}, {"redo_depth", 0}};
                } else {
                    Core::RegionSnapshot redoSnap = snapshotManager->popRedo();

                    // Capture current state for undo
                    Core::RegionSnapshot current;
                    current.name = redoSnap.name;
                    current.min = redoSnap.min;
                    current.max = redoSnap.max;
                    current.size = redoSnap.size;
                    current.totalVolume = redoSnap.totalVolume;
                    current.createdAt = std::chrono::system_clock::now();
                    current.voxels = captureRegionAllLevels(chunkManager, current.min, current.max);

                    // Clear the region
                    {
                        std::unordered_map<glm::ivec3, std::vector<glm::ivec3>, ChunkCoordHash> chunkBatches;
                        for (int ix = redoSnap.min.x; ix <= redoSnap.max.x; ++ix)
                            for (int iy = redoSnap.min.y; iy <= redoSnap.max.y; ++iy)
                                for (int iz = redoSnap.min.z; iz <= redoSnap.max.z; ++iz) {
                                    glm::ivec3 wp(ix, iy, iz);
                                    chunkBatches[ChunkManager::worldToChunkCoord(wp)]
                                        .push_back(ChunkManager::worldToLocalCoord(wp));
                                }
                        for (auto& [cc, positions] : chunkBatches) {
                            Chunk* chunk = chunkManager->getChunkAtCoord(cc);
                            if (!chunk) continue;
                            chunk->removeCubesBatch(positions);
                            for (const auto& p : positions)
                                chunk->clearSubdivisionAt(p);
                        }
                    }

                    // Restore the redo state
                    auto [placed, failed] = placeVoxelEntries(chunkManager, redoSnap.voxels, redoSnap.min);

                    // Push current to undo (without clearing redo  --  special push)
                    snapshotManager->pushUndoOnly(std::move(current));

                    response = {{"success", true}, {"operation", redoSnap.name},
                                {"restored_voxels", placed}, {"failed", failed},
                                {"undo_depth", snapshotManager->undoDepth()},
                                {"redo_depth", snapshotManager->redoDepth()}};
                    if (gameEventLog) {
                        gameEventLog->emit("redo", {{"operation", redoSnap.name}, {"restored", placed}});
                    }
                }

            } else if (cmd.action == "get_undo_status") {
                if (!snapshotManager) {
                    response = {{"error", "SnapshotManager not available"}};
                } else {
                    auto undoEntries = snapshotManager->listUndoStack();
                    auto redoEntries = snapshotManager->listRedoStack();
                    nlohmann::json undoArr = nlohmann::json::array();
                    for (const auto& e : undoEntries) {
                        undoArr.push_back({{"label", e.label},
                                           {"min", {{"x", e.min.x}, {"y", e.min.y}, {"z", e.min.z}}},
                                           {"max", {{"x", e.max.x}, {"y", e.max.y}, {"z", e.max.z}}},
                                           {"voxel_count", e.voxelCount}});
                    }
                    nlohmann::json redoArr = nlohmann::json::array();
                    for (const auto& e : redoEntries) {
                        redoArr.push_back({{"label", e.label},
                                           {"min", {{"x", e.min.x}, {"y", e.min.y}, {"z", e.min.z}}},
                                           {"max", {{"x", e.max.x}, {"y", e.max.y}, {"z", e.max.z}}},
                                           {"voxel_count", e.voxelCount}});
                    }
                    response = {{"can_undo", snapshotManager->canUndo()},
                                {"can_redo", snapshotManager->canRedo()},
                                {"undo_depth", snapshotManager->undoDepth()},
                                {"redo_depth", snapshotManager->redoDepth()},
                                {"undo_stack", undoArr},
                                {"redo_stack", redoArr}};
                }

            // ================================================================
            // PLACED OBJECT COMMANDS (extracted to reduce nesting depth)
            // ================================================================
            } else if (handlePlacedObjectCommand(cmd, response,
                           placedObjectManager.get(), chunkManager, snapshotManager.get())) {
                // handled

            } else if (handlePlacedObjectHierarchyCommands(cmd, response, placedObjectManager.get())) {
                // handled

            // ================================================================
            // DYNAMIC FURNITURE COMMANDS (extracted to reduce nesting depth)
            // ================================================================
            } else if (handleDynamicFurnitureCommand(cmd, response, dynamicFurnitureManager.get())) {
                // handled

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
                        clip.voxels = captureRegionAllLevels(chunkManager, clip.min, clip.max);
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
                    response = {{"error", "Clipboard is empty  --  use copy_region first"}};
                } else {
                    int px = cmd.params.value("x", 0);
                    int py = cmd.params.value("y", 0);
                    int pz = cmd.params.value("z", 0);
                    int rotate = cmd.params.value("rotate", 0);

                    // Make a copy so we can rotate without affecting stored clipboard
                    Core::RegionSnapshot clip = *snapshotManager->getClipboard();

                    // Apply rotation (90Â° increments around Y)
                    int rotSteps = ((rotate % 360) + 360) % 360 / 90;
                    for (int r = 0; r < rotSteps; ++r) {
                        Core::SnapshotManager::rotateY90(clip);
                    }

                    auto [placed, failed] = placeVoxelEntries(chunkManager, clip.voxels, glm::ivec3(px, py, pz));
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
            // MOVE REGION (capture + clear + rotate + paste at new position)
            // ================================================================
            } else if (cmd.action == "move_region") {
                if (!chunkManager) {
                    response = {{"error", "ChunkManager not available"}};
                } else {
                    // Source region
                    int x1 = cmd.params.value("x1", 0), y1 = cmd.params.value("y1", 0), z1 = cmd.params.value("z1", 0);
                    int x2 = cmd.params.value("x2", 0), y2 = cmd.params.value("y2", 0), z2 = cmd.params.value("z2", 0);
                    int minX = std::min(x1, x2), maxX = std::max(x1, x2);
                    int minY = std::min(y1, y2), maxY = std::max(y1, y2);
                    int minZ = std::min(z1, z2), maxZ = std::max(z1, z2);

                    // Destination and rotation
                    int dx = cmd.params.value("dx", minX), dy = cmd.params.value("dy", minY), dz = cmd.params.value("dz", minZ);
                    int rotate = cmd.params.value("rotate", 0);

                    int64_t volume = (int64_t)(maxX - minX + 1) * (maxY - minY + 1) * (maxZ - minZ + 1);
                    if (volume > 100000) {
                        response = {{"error", "Region too large"}, {"volume", volume}, {"max_volume", 100000}};
                    } else {
                        glm::ivec3 minCorner(minX, minY, minZ), maxCorner(maxX, maxY, maxZ);

                        // 1. Capture all voxels
                        auto voxels = captureRegionAllLevels(chunkManager, minCorner, maxCorner);

                        // 2. Clear source region (all levels)
                        int cleared = 0;
                        {
                            std::unordered_map<glm::ivec3, std::vector<glm::ivec3>, ChunkCoordHash> chunkBatches;
                            for (int ix = minX; ix <= maxX; ++ix)
                                for (int iy = minY; iy <= maxY; ++iy)
                                    for (int iz = minZ; iz <= maxZ; ++iz) {
                                        glm::ivec3 wp(ix, iy, iz);
                                        chunkBatches[ChunkManager::worldToChunkCoord(wp)]
                                            .push_back(ChunkManager::worldToLocalCoord(wp));
                                    }
                            for (auto& [cc, positions] : chunkBatches) {
                                Chunk* chunk = chunkManager->getChunkAtCoord(cc);
                                if (!chunk) continue;
                                cleared += chunk->removeCubesBatch(positions);
                                for (const auto& p : positions)
                                    if (chunk->clearSubdivisionAt(p)) ++cleared;
                            }
                        }

                        // 3. Apply rotation
                        int rotSteps = ((rotate % 360) + 360) % 360 / 90;
                        if (rotSteps > 0) {
                            Core::RegionSnapshot snap;
                            snap.min = minCorner;
                            snap.size = maxCorner - minCorner + glm::ivec3(1);
                            snap.voxels = std::move(voxels);
                            for (int r = 0; r < rotSteps; ++r) {
                                Core::SnapshotManager::rotateY90(snap);
                            }
                            voxels = std::move(snap.voxels);
                        }

                        // 4. Place at destination
                        auto [placed, failed] = placeVoxelEntries(chunkManager, voxels, glm::ivec3(dx, dy, dz));

                        response = {{"success", true}, {"captured", (int)voxels.size()},
                                    {"cleared", cleared}, {"placed", placed}, {"failed", failed},
                                    {"from", {{"x", minX}, {"y", minY}, {"z", minZ}}},
                                    {"to", {{"x", maxX}, {"y", maxY}, {"z", maxZ}}},
                                    {"destination", {{"x", dx}, {"y", dy}, {"z", dz}}},
                                    {"rotation", rotate}};
                        if (gameEventLog) {
                            gameEventLog->emit("region_moved", {
                                {"placed", placed}, {"cleared", cleared},
                                {"from", {{"x", minX}, {"y", minY}, {"z", minZ}}},
                                {"destination", {{"x", dx}, {"y", dy}, {"z", dz}}},
                                {"rotation", rotate}});
                        }
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
                        if (generated > 0 && npcManager) {
                            npcManager->buildNavGrid();
                        }
                        // Rebuild entire GPU occupancy grid after world generation
                        if (generated > 0) {
                            chunkManager->rebuildOccupancyFromChunks();
                        }
                    }
                }
                done_generate:;

            // ================================================================
            // SAVE TEMPLATE (region â†’ .txt file)
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
                            // Capture all voxel levels
                            glm::ivec3 minCorner(minX, minY, minZ), maxCorner(maxX, maxY, maxZ);
                            auto voxels = captureRegionAllLevels(chunkManager, minCorner, maxCorner);

                            // Build template lines
                            std::vector<std::string> lines;
                            lines.push_back("# Template: " + name);
                            lines.push_back("# Generated from region (" +
                                std::to_string(minX) + "," + std::to_string(minY) + "," + std::to_string(minZ) + ") to (" +
                                std::to_string(maxX) + "," + std::to_string(maxY) + "," + std::to_string(maxZ) + ")");

                            int cubeCount = 0, subcubeCount = 0, microcubeCount = 0;
                            for (const auto& v : voxels) {
                                std::string mat = v.material.empty() ? "Default" : v.material;
                                switch (v.level) {
                                    case Core::SnapshotVoxelLevel::Cube:
                                        lines.push_back("C " + std::to_string(v.offset.x) + " " +
                                            std::to_string(v.offset.y) + " " + std::to_string(v.offset.z) + " " + mat);
                                        ++cubeCount;
                                        break;
                                    case Core::SnapshotVoxelLevel::Subcube:
                                        lines.push_back("S " + std::to_string(v.offset.x) + " " +
                                            std::to_string(v.offset.y) + " " + std::to_string(v.offset.z) + " " +
                                            std::to_string(v.subcubePos.x) + " " + std::to_string(v.subcubePos.y) + " " +
                                            std::to_string(v.subcubePos.z) + " " + mat);
                                        ++subcubeCount;
                                        break;
                                    case Core::SnapshotVoxelLevel::Microcube:
                                        lines.push_back("M " + std::to_string(v.offset.x) + " " +
                                            std::to_string(v.offset.y) + " " + std::to_string(v.offset.z) + " " +
                                            std::to_string(v.subcubePos.x) + " " + std::to_string(v.subcubePos.y) + " " +
                                            std::to_string(v.subcubePos.z) + " " +
                                            std::to_string(v.microcubePos.x) + " " + std::to_string(v.microcubePos.y) + " " +
                                            std::to_string(v.microcubePos.z) + " " + mat);
                                        ++microcubeCount;
                                        break;
                                }
                            }

                            // Write to templates dir/<name>.voxel
                            auto& assets = Core::AssetManager::instance();
                            std::string filepath = assets.resolveTemplate(name + ".voxel");
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

                                int totalCount = cubeCount + subcubeCount + microcubeCount;
                                response = {{"success", true}, {"name", name}, {"path", filepath},
                                            {"voxel_count", totalCount}, {"cubes", cubeCount},
                                            {"subcubes", subcubeCount}, {"microcubes", microcubeCount}};
                                LOG_INFO("Application", "Template saved: {} ({} cubes, {} subcubes, {} microcubes)",
                                         name, cubeCount, subcubeCount, microcubeCount);
                                if (gameEventLog) {
                                    gameEventLog->emit("template_saved", {
                                        {"name", name}, {"voxel_count", totalCount}, {"path", filepath}});
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
                        std::string animFile = cmd.params.value("animFile", "resources/animated_characters/humanoid.anim");
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
                        } else if (behaviorStr == "behavior_tree") {
                            behaviorType = Core::NPCBehaviorType::BehaviorTree;
                        } else if (behaviorStr == "scheduled") {
                            behaviorType = Core::NPCBehaviorType::Scheduled;
                        }

                        // Parse optional appearance
                        Scene::CharacterAppearance appearance;
                        std::string role = cmd.params.value("role", "");
                        bool procedural = cmd.params.value("procedural", false);
                        std::string driveModeStr = cmd.params.value("driveMode", "animated");
                        bool physicsDriven = (driveModeStr == "physics");

                        if (cmd.params.contains("appearance")) {
                            appearance = Scene::CharacterAppearance::fromJson(cmd.params["appearance"]);
                        } else if (!role.empty()) {
                            // Detect morphology from anim file name
                            std::string animLower = animFile;
                            std::transform(animLower.begin(), animLower.end(), animLower.begin(), ::tolower);
                            Scene::MorphologyType morph = Scene::MorphologyType::Humanoid;
                            if (animLower.find("wolf") != std::string::npos)
                                morph = Scene::MorphologyType::Quadruped;
                            else if (animLower.find("spider") != std::string::npos)
                                morph = Scene::MorphologyType::Arachnid;
                            else if (animLower.find("dragon") != std::string::npos)
                                morph = Scene::MorphologyType::Dragon;
                            appearance = Scene::CharacterAppearance::generateFromSeed(name, role, morph);
                        }

                        Scene::NPCEntity* npc = nullptr;
                        if (physicsDriven) {
                            if (procedural) {
                                npc = npcManager->spawnPhysicsProceduralNPC(name, animFile, glm::vec3(x, y, z),
                                                                             behaviorType, role, waypoints, walkSpeed, waitTime, appearance);
                            } else {
                                npc = npcManager->spawnPhysicsNPC(name, animFile, glm::vec3(x, y, z),
                                                                    behaviorType, waypoints, walkSpeed, waitTime, appearance);
                            }
                        } else if (procedural) {
                            // Procedural mode: use cached template + unique appearance
                            npc = npcManager->spawnProceduralNPC(name, animFile, glm::vec3(x, y, z),
                                                                  behaviorType, role, waypoints, walkSpeed, waitTime, appearance);
                        } else {
                            npc = npcManager->spawnNPC(name, animFile, glm::vec3(x, y, z),
                                                       behaviorType, waypoints, walkSpeed, waitTime, appearance);
                        }

                        if (npc) {
                            response = {{"success", true}, {"name", name}, {"behavior", behaviorStr},
                                        {"procedural", procedural}, {"role", role}, {"driveMode", driveModeStr},
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
                            } else if (behaviorStr == "behavior_tree") {
                                behavior = std::make_unique<Scene::BehaviorTreeBehavior>();
                            } else if (behaviorStr == "scheduled") {
                                std::string role = cmd.params.value("role", "");
                                auto schedule = role.empty() ? AI::Schedule::defaultSchedule() : AI::Schedule::forRole(role);
                                behavior = std::make_unique<Scene::ScheduledBehavior>(schedule);
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
            } else if (cmd.action == "get_npc_appearance") {
                if (!npcManager) {
                    response = {{"error", "NPCManager not available"}};
                } else {
                    std::string name = cmd.params.value("name", "");
                    if (name.empty()) {
                        response = {{"error", "NPC name required"}};
                    } else {
                        auto* npc = npcManager->getNPC(name);
                        if (!npc) {
                            response = {{"error", "NPC not found: " + name}};
                        } else {
                            auto* character = npc->getAnimatedCharacter();
                            if (!character) {
                                response = {{"error", "NPC has no animated character"}};
                            } else {
                                const auto& app = character->getAppearance();
                                response = {{"success", true}, {"name", name}, {"appearance", app.toJson()}};
                            }
                        }
                    }
                }

            } else if (cmd.action == "set_npc_appearance") {
                if (!npcManager) {
                    response = {{"error", "NPCManager not available"}};
                } else {
                    std::string name = cmd.params.value("name", "");
                    if (name.empty()) {
                        response = {{"error", "NPC name required"}};
                    } else {
                        auto* npc = npcManager->getNPC(name);
                        if (!npc) {
                            response = {{"error", "NPC not found: " + name}};
                        } else {
                            auto* character = npc->getAnimatedCharacter();
                            if (!character) {
                                response = {{"error", "NPC has no animated character"}};
                            } else {
                                // Merge provided fields with current appearance
                                auto currentApp = character->getAppearance();
                                auto newJson = currentApp.toJson();
                                if (cmd.params.contains("appearance")) {
                                    for (auto& [key, val] : cmd.params["appearance"].items()) {
                                        newJson[key] = val;
                                    }
                                }
                                auto newApp = Scene::CharacterAppearance::fromJson(newJson);
                                character->rebuildWithAppearance(newApp);
                                response = {{"success", true}, {"name", name}, {"appearance", newApp.toJson()}};
                            }
                        }
                    }
                }

            } else if (cmd.action == "interact_npc") {
                // Trigger the same interaction callback as pressing E near an NPC
                std::string npcName = cmd.params.value("name", "");
                if (npcName.empty()) {
                    response = {{"error", "Required: 'name' (NPC name)"}};
                } else if (!npcManager) {
                    response = {{"error", "NPCManager not available"}};
                } else {
                    auto* npc = npcManager->getNPC(npcName);
                    if (!npc) {
                        response = {{"error", "NPC not found: " + npcName}};
                    } else if (!interactionManager) {
                        response = {{"error", "InteractionManager not available"}};
                    } else {
                        interactionManager->triggerInteraction(npc);
                        response = {{"success", true}, {"npc", npcName}};
                    }
                }

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
                            // No NPC entity  --  store tree in a member to keep it alive
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
                subsystems.placedObjectManager = placedObjectManager.get();
                subsystems.gameEventLog = gameEventLog.get();
                subsystems.locationRegistry = locationRegistry;
                subsystems.camera = camera;
                subsystems.dialogueSystem = dialogueSystem.get();
                subsystems.storyEngine = storyEngine.get();

                // Entity spawner callback  --  delegates to Application factory methods
                subsystems.entitySpawner = [this](const std::string& type, const glm::vec3& pos,
                                                   const std::string& animFile) -> Scene::Entity* {
                    return createAnimatedCharacter(pos, animFile.empty() ? "resources/animated_characters/humanoid.anim" : animFile);
                };

                subsystems.aiRegister = [this](Scene::Entity* entity, const std::string& entityId,
                                                const std::string& npcName, const std::string& personality) {
                    if (!aiSystem) return;
                    std::string recipe = Core::AssetManager::instance().resolveRecipe("characters/" + entityId + ".yaml");
                    if (recipe.empty()) recipe = Core::AssetManager::instance().resolveRecipe("characters/guard.yaml");
                    std::string p = personality.empty() ? "You are " + npcName + ", an NPC in a voxel world." : personality;
                    aiSystem->createAINPC(entity, entityId, recipe, p);
                };

                auto loadResult = Core::GameDefinitionLoader::load(cmd.params, subsystems);
                response = loadResult.toJson();

                // Rebuild GPU occupancy grid after world generation/placement
                if (loadResult.chunksGenerated > 0 || loadResult.structuresPlaced > 0) {
                    chunkManager->rebuildOccupancyFromChunks();
                }

                // Set up character control but keep camera Free (press V to follow)
                if (loadResult.playerSpawned) {
                    if (animatedCharacter) {
                        currentControlTarget = ControlTarget::AnimatedCharacter;
                        camera->setDistanceFromTarget(4.0f);
                    }
                }

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
                    subsystems.locationRegistry = locationRegistry;
                    subsystems.aiRegister = [this](Scene::Entity* entity, const std::string& entityId,
                                                    const std::string& npcName, const std::string& personality) {
                        if (!aiSystem) return;
                        std::string recipe = Core::AssetManager::instance().resolveRecipe("characters/" + entityId + ".yaml");
                        if (recipe.empty()) recipe = Core::AssetManager::instance().resolveRecipe("characters/guard.yaml");
                        std::string p = personality.empty() ? "You are " + npcName + ", an NPC in a voxel world." : personality;
                        aiSystem->createAINPC(entity, entityId, recipe, p);
                    };

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

                        // Find cmake  --  check common VS 2022 location, then PATH
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
                    if (cmd.params.contains("dayNumber")) {
                        cycle.setDayNumber(cmd.params["dayNumber"].get<int>());
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
                            if (placed > 0) cmFill->rebuildOccupancyFromChunks();
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
                                        glm::ivec3 worldPos(ix, iy, iz);
                                        if (cmClear->removeCubeFast(worldPos)) ++removed;
                                        // Also clear subcubes/microcubes at this position
                                        Chunk* chunk = cmClear->getChunkAtFast(worldPos);
                                        if (chunk) {
                                            glm::ivec3 lp = ChunkManager::worldToLocalCoord(worldPos);
                                            if (chunk->clearSubdivisionAt(lp)) ++removed;
                                        }
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
                            if (removed > 0) cmClear->rebuildOccupancyFromChunks();
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
                            cmGen->rebuildOccupancyFromChunks();
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

            // â"€â"€ Custom UI Menu Management â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€â"€
            } else if (cmd.action == "create_menu") {
                std::string name = cmd.params.value("name", "");
                if (name.empty()) {
                    response = {{"error", "Menu name required"}};
                } else if (!renderCoordinator || !renderCoordinator->getUISystem()) {
                    response = {{"error", "UISystem not initialized"}};
                } else if (!cmd.params.contains("definition")) {
                    response = {{"error", "Menu definition required"}};
                } else {
                    auto panel = UI::MenuDefinition::buildFromJson(cmd.params["definition"]);
                    if (!panel) {
                        response = {{"error", "Failed to build menu from definition"}};
                    } else {
                        renderCoordinator->getUISystem()->addScreen(name, std::move(panel));
                        response = {{"success", true}, {"name", name}};
                    }
                }

            } else if (cmd.action == "show_menu") {
                std::string name = cmd.params.value("name", "");
                if (name.empty()) {
                    response = {{"error", "Menu name required"}};
                } else if (!renderCoordinator || !renderCoordinator->getUISystem()) {
                    response = {{"error", "UISystem not initialized"}};
                } else {
                    renderCoordinator->getUISystem()->showScreen(name);
                    response = {{"success", true}, {"name", name}, {"visible", true}};
                }

            } else if (cmd.action == "hide_menu") {
                std::string name = cmd.params.value("name", "");
                if (name.empty()) {
                    response = {{"error", "Menu name required"}};
                } else if (!renderCoordinator || !renderCoordinator->getUISystem()) {
                    response = {{"error", "UISystem not initialized"}};
                } else {
                    renderCoordinator->getUISystem()->hideScreen(name);
                    response = {{"success", true}, {"name", name}, {"visible", false}};
                }

            } else if (cmd.action == "toggle_menu") {
                std::string name = cmd.params.value("name", "");
                if (name.empty()) {
                    response = {{"error", "Menu name required"}};
                } else if (!renderCoordinator || !renderCoordinator->getUISystem()) {
                    response = {{"error", "UISystem not initialized"}};
                } else {
                    renderCoordinator->getUISystem()->toggleScreen(name);
                    bool visible = renderCoordinator->getUISystem()->isScreenVisible(name);
                    response = {{"success", true}, {"name", name}, {"visible", visible}};
                }

            } else if (cmd.action == "remove_menu") {
                std::string name = cmd.params.value("name", "");
                if (name.empty()) {
                    response = {{"error", "Menu name required"}};
                } else if (!renderCoordinator || !renderCoordinator->getUISystem()) {
                    response = {{"error", "UISystem not initialized"}};
                } else {
                    renderCoordinator->getUISystem()->removeScreen(name);
                    response = {{"success", true}, {"name", name}, {"removed", true}};
                }

            // ================================================================
            // PROJECT MANAGEMENT COMMANDS
            // ================================================================
            } else if (cmd.action == "projects_list") {
                std::string baseDir = Core::ProjectInfo::getDefaultProjectsDir();
                Core::LauncherState state;
                state.load(baseDir);
                auto projects = state.getMergedProjects(baseDir);

                nlohmann::json arr = nlohmann::json::array();
                for (const auto& p : projects) {
                    arr.push_back(p.toJson());
                }
                response = {{"projects", arr}, {"base_dir", baseDir}};

            } else if (cmd.action == "projects_create") {
                std::string name = cmd.params.value("name", "");
                if (name.empty()) {
                    response = {{"error", "Missing 'name' field"}};
                } else {
                    std::string baseDir = Core::ProjectInfo::getDefaultProjectsDir();
                    std::string path = Core::ProjectInfo::scaffoldProject(name, baseDir);
                    if (path.empty()) {
                        response = {{"error", "Failed to create project"}, {"name", name}};
                    } else {
                        response = {{"success", true}, {"name", name}, {"path", path}};
                    }
                }

            } else if (cmd.action == "projects_open") {
                std::string path = cmd.params.value("path", "");
                if (path.empty()) {
                    response = {{"error", "Missing 'path' field"}};
                } else if (!Core::ProjectInfo::isValidProject(path)) {
                    response = {{"error", "Not a valid project (missing engine.json)"},
                                {"path", path}};
                } else {
                    applyProjectSelection(path);

                    // Record in launcher state
                    std::string baseDir = Core::ProjectInfo::getDefaultProjectsDir();
                    Core::LauncherState state;
                    state.load(baseDir);
                    Core::ProjectInfo info;
                    info.path = path;
                    info.name = std::filesystem::path(path).filename().string();
                    state.addRecentProject(info);
                    state.save(baseDir);

                    // Reload world from the new project
                    autoLoadGameDefinition();

                    // Dismiss launcher if it was active
                    if (launcherActive_) {
                        projectLauncher_.reset();
                        launcherActive_ = false;
                    }

                    response = {{"success", true}, {"project_dir", path}};
                }

            // ================================================================
            // SEGMENT BOX DEBUG
            // ================================================================
            // ================================================================
            // ANIMATION CONTROL COMMANDS
            // ================================================================
            } else if (cmd.action == "list_animations") {
                std::string id = cmd.params.value("id", "");
                Scene::AnimatedVoxelCharacter* character = nullptr;

                // Try NPC first (strip "npc_" prefix if present)
                if (npcManager) {
                    std::string npcName = id;
                    if (npcName.substr(0, 4) == "npc_") npcName = npcName.substr(4);
                    auto* npc = npcManager->getNPC(npcName);
                    if (npc) character = npc->getAnimatedCharacter();
                }
                // Fallback to player animated character
                if (!character && (id.empty() || id == "player") && animatedCharacter) {
                    character = animatedCharacter;
                }

                if (!character) {
                    response = {{"error", "No animated character found for: " + id}};
                } else {
                    auto names = character->getAnimationNames();
                    nlohmann::json clipList = nlohmann::json::array();
                    const auto& clips = character->getAnimationClips();
                    for (size_t i = 0; i < clips.size(); ++i) {
                        clipList.push_back({{"index", i}, {"name", clips[i].name},
                                            {"duration", clips[i].duration}, {"speed", clips[i].speed}});
                    }
                    response = {{"success", true}, {"id", id}, {"animations", clipList}};
                }

            } else if (cmd.action == "play_animation") {
                std::string id = cmd.params.value("id", "");
                std::string animName = cmd.params.value("animation", "");
                if (animName.empty()) {
                    response = {{"error", "Animation name required"}};
                } else {
                    Scene::AnimatedVoxelCharacter* character = nullptr;
                    if (npcManager) {
                        std::string npcName = id;
                        if (npcName.substr(0, 4) == "npc_") npcName = npcName.substr(4);
                        auto* npc = npcManager->getNPC(npcName);
                        if (npc) character = npc->getAnimatedCharacter();
                    }
                    if (!character && (id.empty() || id == "player") && animatedCharacter) {
                        character = animatedCharacter;
                    }

                    if (!character) {
                        response = {{"error", "No animated character found for: " + id}};
                    } else {
                        character->setAnimationState(Scene::AnimatedCharacterState::Preview);
                        character->playAnimation(animName);
                        response = {{"success", true}, {"id", id}, {"animation", animName}};
                    }
                }

            } else if (cmd.action == "get_animation_state") {
                std::string id = cmd.params.value("id", "");
                Scene::AnimatedVoxelCharacter* character = nullptr;
                if (npcManager) {
                    std::string npcName = id;
                    if (npcName.substr(0, 4) == "npc_") npcName = npcName.substr(4);
                    auto* npc = npcManager->getNPC(npcName);
                    if (npc) character = npc->getAnimatedCharacter();
                }
                if (!character && (id.empty() || id == "player") && animatedCharacter) {
                    character = animatedCharacter;
                }

                if (!character) {
                    response = {{"error", "No animated character found for: " + id}};
                } else {
                    response = {{"success", true}, {"id", id},
                                {"state", character->stateToString(character->getAnimationState())},
                                {"clip", character->getCurrentClipName()},
                                {"progress", character->getAnimationProgress()},
                                {"duration", character->getAnimationDuration()},
                                {"blendDuration", character->getBlendDuration()}};
                }

            } else if (cmd.action == "set_animation_state") {
                std::string id = cmd.params.value("id", "");
                std::string stateName = cmd.params.value("state", "");
                if (stateName.empty()) {
                    response = {{"error", "State name required"}};
                } else {
                    Scene::AnimatedVoxelCharacter* character = nullptr;
                    if (npcManager) {
                        std::string npcName = id;
                        if (npcName.substr(0, 4) == "npc_") npcName = npcName.substr(4);
                        auto* npc = npcManager->getNPC(npcName);
                        if (npc) character = npc->getAnimatedCharacter();
                    }
                    if (!character && (id.empty() || id == "player") && animatedCharacter) {
                        character = animatedCharacter;
                    }

                    if (!character) {
                        response = {{"error", "No animated character found for: " + id}};
                    } else {
                        auto state = Scene::AnimatedVoxelCharacter::stringToState(stateName);
                        character->setAnimationState(state);
                        response = {{"success", true}, {"id", id}, {"state", stateName}};
                    }
                }

            } else if (cmd.action == "set_blend_duration") {
                std::string id = cmd.params.value("id", "");
                float duration = cmd.params.value("duration", 0.2f);
                Scene::AnimatedVoxelCharacter* character = nullptr;
                if (npcManager) {
                    std::string npcName = id;
                    if (npcName.substr(0, 4) == "npc_") npcName = npcName.substr(4);
                    auto* npc = npcManager->getNPC(npcName);
                    if (npc) character = npc->getAnimatedCharacter();
                }
                if (!character && (id.empty() || id == "player") && animatedCharacter) {
                    character = animatedCharacter;
                }

                if (!character) {
                    response = {{"error", "No animated character found for: " + id}};
                } else {
                    character->setBlendDuration(duration);
                    response = {{"success", true}, {"id", id}, {"blendDuration", duration}};
                }

            } else if (cmd.action == "reload_animation") {
                std::string id = cmd.params.value("id", "");
                std::string animFile = cmd.params.value("animFile", "");
                Scene::AnimatedVoxelCharacter* character = nullptr;
                if (animFile.empty()) { response = {{"error", "animFile path required"}}; }
                else {
                    if (npcManager) {
                        std::string npcName = (id.substr(0, 4) == "npc_") ? id.substr(4) : id;
                        auto* npc = npcManager->getNPC(npcName);
                        if (npc) character = npc->getAnimatedCharacter();
                    }
                    if (!character && (id.empty() || id == "player") && animatedCharacter)
                        character = animatedCharacter;
                    if (!character) { response = {{"error", "No animated character found for: " + id}}; }
                    else {
                        bool ok = character->reloadAnimations(animFile);
                        response = ok ? nlohmann::json({{"success", true}, {"id", id}, {"animFile", animFile},
                                        {"clipCount", (int)character->getAnimationNames().size()}})
                                      : nlohmann::json({{"error", "Failed to reload animations from: " + animFile}});
                    }
                }

            // AI Inspection, Location & Schedule commands (extracted to reduce nesting)
            } else if (handleAIInspectionCommands(cmd, response, npcManager.get(), locationRegistry)) {
                // handled

            // ================================================================
            // Social Simulation (Needs, Relationships, WorldView)
            // ================================================================
            } else if (cmd.action == "get_npc_needs" || cmd.action == "set_npc_needs" ||
                       cmd.action == "get_npc_relationships" || cmd.action == "set_npc_relationship" ||
                       cmd.action == "apply_npc_interaction" || cmd.action == "get_npc_worldview" ||
                       cmd.action == "set_npc_belief" || cmd.action == "set_npc_opinion") {
                response = handleSocialSimulationCommand(cmd.action, cmd.params, npcManager.get());

            // ================================================================
            // GAME STATE (extracted  --  handleGameStateCommand)
            // ================================================================
            } else if (cmd.action == "get_pause_state" || cmd.action == "set_pause_state" ||
                       cmd.action == "get_player_health" || cmd.action == "modify_player_health" ||
                       cmd.action == "get_respawn_state" || cmd.action == "modify_respawn" ||
                       cmd.action == "get_music_state" || cmd.action == "control_music" ||
                       cmd.action == "save_player_state" || cmd.action == "load_player_state" ||
                       cmd.action == "get_objectives" || cmd.action == "manage_objectives") {
                GameStateContext gsCtx{
                    gamePaused,
                    [this](bool p) { setPaused(p); },
                    playerHealth, respawnSystem, musicPlaylist,
                    playerProfile, objectiveTracker,
                    audioSystem, camera, inventory.get(), chunkManager
                };
                response = handleGameStateCommand(cmd.action, cmd.params, gsCtx);

            // ================================================================
            // NavGrid / Movement state queries (extracted to avoid nesting depth)
            // ================================================================
            } else if (handleNavGridQueryCommand(cmd, response, npcManager.get(), entityRegistry.get())) {
                // handled

            // ================================================================
            // AI / LLM
            // ================================================================
            } else if (cmd.action == "get_ai_status" || cmd.action == "configure_ai" ||
                       cmd.action == "start_ai_conversation" || cmd.action == "send_ai_message") {
                response = handleAICommand(cmd.action, cmd.params,
                    aiConversationService.get(), npcManager.get(),
                    entityRegistry.get(), dialogueSystem.get());

            } else if (cmd.action == "add_material" || cmd.action == "remove_material" ||
                       cmd.action == "save_materials" || cmd.action == "reload_atlas") {
                response = handleMaterialCommand(cmd.action, cmd.params, vulkanDevice);

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

// ============================================================================
// MAIN MENU BAR
// ============================================================================

void Application::renderMainMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open File...", "Ctrl+O")) {
                openFileDialog();
            }
#ifdef _WIN32
            if (ImGui::MenuItem("Open Project...")) {
                openProjectDialog();
            }
#endif
            ImGui::Separator();

            // Show current mode info
            if (m_assetEditorMode) {
                ImGui::TextDisabled("Asset Editor: %s", 
                    std::filesystem::path(m_assetEditorFile).filename().string().c_str());
            } else if (m_animEditorMode) {
                ImGui::TextDisabled("Anim Editor: %s", 
                    std::filesystem::path(m_animEditorFile).filename().string().c_str());
            } else if (m_interactionEditorMode) {
                ImGui::TextDisabled("Interaction Editor: %s", 
                    std::filesystem::path(m_interactionEditorFile).filename().string().c_str());
            } else {
                ImGui::TextDisabled("Project Mode");
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                isRunning = false;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            // Dockable panels
            ImGui::MenuItem("Properties", nullptr, &m_showProperties);
            ImGui::MenuItem("World Outliner", nullptr, &m_showWorldOutliner);
            ImGui::MenuItem("Camera", nullptr, &m_showCameraPanel);
#ifdef _WIN32
            ImGui::MenuItem("Terminal", nullptr, &m_showTerminal);
#endif
            ImGui::Separator();

            // Debug & diagnostic panels (toggled by hotkeys too)
            ImGui::MenuItem("Performance Overlay", "F1", &showPerformanceOverlay);
            if (ImGui::MenuItem("Profiler", "F7")) toggleProfiler();
            if (ImGui::MenuItem("Debug Rendering", "F4")) toggleDebugRendering();
            if (ImGui::MenuItem("Raycast Visualization", "F5")) toggleRaycastVisualization();
            if (ImGui::MenuItem("Lighting Controls", "F6")) toggleLightingControls();
            ImGui::Separator();

            // Tool panels
            ImGui::MenuItem("Character Customizer", "F11", &showCharacterCustomizer);
            ImGui::MenuItem("Interaction Tuner", "F12", &showInteractionTuner);
            ImGui::MenuItem("Template Spawner", nullptr, &showTemplateSpawner);
            ImGui::MenuItem("Texture Editor", nullptr, &m_showTextureEditor);
            ImGui::MenuItem("Click Actions", nullptr, &showClickActions);
            ImGui::Separator();

            if (ImGui::MenuItem("Reset Layout")) {
                m_needsLayoutReset = true;
            }

            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
}

void Application::renderStatusBar() {
    const float barHeight = 24.0f;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - barHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, barHeight));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));

    if (ImGui::Begin("##StatusBar", nullptr, flags)) {
        // Cursor position
        if (voxelInteractionSystem && voxelInteractionSystem->hasHoveredCube()) {
            const auto& loc = voxelInteractionSystem->getCurrentHoveredLocation();
            ImGui::Text("Cursor: (%d, %d, %d)", loc.worldPos.x, loc.worldPos.y, loc.worldPos.z);
        } else {
            ImGui::TextDisabled("Cursor: --");
        }

        ImGui::SameLine(0, 20);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 20);

        // Voxel mode
        if (voxelInteractionSystem) {
            auto mode = voxelInteractionSystem->getTargetMode();
            const char* modeName = (mode == TargetMode::Cube) ? "CUBE" :
                                   (mode == TargetMode::Subcube) ? "SUBCUBE" : "MICROCUBE";
            ImGui::Text("Mode: %s", modeName);
        }

        ImGui::SameLine(0, 20);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 20);

        // FPS
        ImGui::Text("FPS: %.0f", ImGui::GetIO().Framerate);

        ImGui::SameLine(0, 20);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 20);

        // Entity count
        if (entityRegistry) {
            ImGui::Text("Entities: %zu", entityRegistry->getAllIds().size());
        }

        ImGui::SameLine(0, 20);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0, 20);

        // Camera position
        if (camera) {
            auto pos = camera->getPosition();
            ImGui::Text("Camera: (%.0f, %.0f, %.0f)", pos.x, pos.y, pos.z);
        }
    }
    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

#ifdef _WIN32
void Application::openFileDialog() {
    // Use Windows native file dialog
    OPENFILENAMEA ofn = {};
    char szFile[MAX_PATH] = {};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;  // No parent window handle (GLFW doesn't expose HWND directly here)
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = 
        "All Supported Files\0*.anim;*.voxel;*.txt;*.json\0"
        "Animation Files (*.anim)\0*.anim\0"
        "Template Files (*.voxel;*.txt)\0*.voxel;*.txt\0"
        "Game Definition (*.json)\0*.json\0"
        "All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        // Defer to next frame — can't tear down scene mid-render
        m_pendingOpenFile = std::string(szFile);
    }
}

void Application::openProjectDialog() {
    // Use modern IFileDialog (Vista+) to pick a folder
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool comOwned = SUCCEEDED(hrInit);

    IFileOpenDialog* pDialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IFileOpenDialog, reinterpret_cast<void**>(&pDialog));
    if (FAILED(hr) || !pDialog) {
        LOG_WARN("Application", "Failed to create folder picker dialog");
        if (comOwned) CoUninitialize();
        return;
    }

    // Set folder-pick mode
    DWORD options = 0;
    pDialog->GetOptions(&options);
    pDialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
    pDialog->SetTitle(L"Open Phyxel Project");

    // Default to the projects directory
    std::string defaultDir = Core::ProjectInfo::getDefaultProjectsDir();
    if (std::filesystem::exists(defaultDir)) {
        IShellItem* pFolder = nullptr;
        int wlen = MultiByteToWideChar(CP_UTF8, 0, defaultDir.c_str(), -1, nullptr, 0);
        std::wstring wDir(wlen - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, defaultDir.c_str(), -1, wDir.data(), wlen);
        hr = SHCreateItemFromParsingName(wDir.c_str(), nullptr, IID_IShellItem,
                                          reinterpret_cast<void**>(&pFolder));
        if (SUCCEEDED(hr) && pFolder) {
            pDialog->SetDefaultFolder(pFolder);
            pFolder->Release();
        }
    }

    hr = pDialog->Show(nullptr);
    if (SUCCEEDED(hr)) {
        IShellItem* pItem = nullptr;
        hr = pDialog->GetResult(&pItem);
        if (SUCCEEDED(hr) && pItem) {
            PWSTR pszPath = nullptr;
            hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath);
            if (SUCCEEDED(hr) && pszPath) {
                // Convert wide string to UTF-8
                int len = WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, nullptr, 0, nullptr, nullptr);
                std::string path(len - 1, '\0');
                WideCharToMultiByte(CP_UTF8, 0, pszPath, -1, path.data(), len, nullptr, nullptr);
                CoTaskMemFree(pszPath);

                // Validate it looks like a project (has engine.json, game.json, or worlds/ dir)
                namespace fs = std::filesystem;
                bool valid = fs::exists(fs::path(path) / "engine.json") ||
                             fs::exists(fs::path(path) / "game.json") ||
                             fs::exists(fs::path(path) / "worlds");
                if (valid) {
                    m_pendingOpenProject = path;
                } else {
                    LOG_WARN("Application", "Selected folder '{}' does not appear to be a Phyxel project "
                             "(no engine.json, game.json, or worlds/ found)", path);
                    MessageBoxA(nullptr,
                        "The selected folder does not appear to be a Phyxel project.\n\n"
                        "A project folder should contain engine.json, game.json, or a worlds/ directory.",
                        "Not a Project", MB_OK | MB_ICONWARNING);
                }
            }
            pItem->Release();
        }
    }
    pDialog->Release();
    if (comOwned) CoUninitialize();
}
#else
void Application::openFileDialog() {
    LOG_WARN("Application", "File dialog not implemented on this platform. Use command-line flags.");
}
void Application::openProjectDialog() {
    LOG_WARN("Application", "Project dialog not implemented on this platform. Use --project flag.");
}
#endif

void Application::resetEditorScene() {
    // Reset editor-mode-specific state
    m_assetEditorMode = false;
    m_assetEditorFile.clear();
    m_assetRefCharVisible = false;
    m_assetRefChar = nullptr;
    m_assetEditorMaterial = "Wood";

    m_animEditorMode = false;
    m_animEditorFile.clear();
    m_animEditorChar = nullptr;
    m_animEditorSelectedBone = -1;
    m_animEditorBoneScale.clear();
    m_animEditorBodyBones.clear();
    m_animEditorAnimIdx = 0;

    m_interactionEditorMode = false;
    m_interactionEditorFile.clear();
    m_interactionEditorCharFile.clear();
    m_ieChar = nullptr;
    m_ieSelectedPoint = 0;
    m_iePreviewState = InteractionPreviewState::None;
    m_ieBodyBones.clear();
    m_ieBoneScale.clear();

    // Null out raw entity pointers BEFORE clearing the owning vector,
    // so nothing in the main loop dereferences dangling pointers.
    animatedCharacter = nullptr;

    currentControlTarget = ControlTarget::AnimatedCharacter;

    // Clean up entities
    if (entityRegistry) entityRegistry->clear();
    entities.clear();

    // Clean up NPC manager
    if (npcManager) {
        auto names = npcManager->getAllNPCNames();
        for (auto& n : names) npcManager->removeNPC(n);
    }

    // Clean up placed objects
    if (placedObjectManager) placedObjectManager->clear();

    // Wait for GPU before destroying chunk Vulkan buffers
    if (vulkanDevice) vkDeviceWaitIdle(vulkanDevice->getDevice());

    // Clean up chunk data
    if (chunkManager) chunkManager->cleanup();
}

void Application::switchToEditorMode(const std::string& filePath) {
    namespace fs = std::filesystem;
    fs::path p(filePath);
    std::string ext = p.extension().string();

    // Convert extension to lowercase for comparison
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    LOG_INFO("Application", "Opening file: {} (ext: {})", filePath, ext);

    if (ext == ".anim") {
        // Anim editor mode
        resetEditorScene();
        m_animEditorMode = true;
        m_animEditorFile = filePath;
        initAnimEditorScene();
        LOG_INFO("Application", "Switched to Anim Editor mode: {}", p.filename().string());

    } else if (ext == ".voxel" || ext == ".txt") {
        // Check if it's an interaction file (has interaction point definitions)
        // by scanning first few lines for "INTERACT" keyword
        bool isInteraction = false;
        std::ifstream f(filePath);
        if (f.is_open()) {
            std::string line;
            int lineCount = 0;
            while (std::getline(f, line) && lineCount < 200) {
                if (line.find("INTERACT") != std::string::npos) {
                    isInteraction = true;
                    break;
                }
                lineCount++;
            }
        }

        resetEditorScene();
        if (isInteraction) {
            m_interactionEditorMode = true;
            m_interactionEditorFile = filePath;
            m_interactionEditorCharFile = "";
            initInteractionEditorScene();
            LOG_INFO("Application", "Switched to Interaction Editor mode: {}", p.filename().string());
        } else {
            m_assetEditorMode = true;
            m_assetEditorFile = filePath;
            initAssetEditorScene();
            LOG_INFO("Application", "Switched to Asset Editor mode: {}", p.filename().string());
        }

    } else if (ext == ".json") {
        // Check if this is an interaction profile JSON
        // These live in resources/interactions/ and have an "archetype" + "profiles" structure.
        // The profile keys are template names (e.g. "test_chair") — open the first one
        // in the interaction editor.
        bool handled = false;
        try {
            std::ifstream jf(filePath);
            if (jf.is_open()) {
                std::string content((std::istreambuf_iterator<char>(jf)),
                                     std::istreambuf_iterator<char>());
                // Quick check for interaction profile structure
                if (content.find("\"archetype\"") != std::string::npos &&
                    content.find("\"profiles\"") != std::string::npos) {
                    // Parse to find the first template name in profiles
                    // Simple: find the template .voxel file referenced by profiles keys
                    auto profilesPos = content.find("\"profiles\"");
                    if (profilesPos != std::string::npos) {
                        // Find the first key after "profiles": {
                        auto bracePos = content.find('{', profilesPos + 10);
                        if (bracePos != std::string::npos) {
                            auto keyStart = content.find('"', bracePos + 1);
                            if (keyStart != std::string::npos) {
                                auto keyEnd = content.find('"', keyStart + 1);
                                if (keyEnd != std::string::npos) {
                                    std::string templateName = content.substr(keyStart + 1, keyEnd - keyStart - 1);
                                    // Resolve template file path (.voxel preferred, .txt fallback)
                                    fs::path templatePath = fs::path("resources/templates") / (templateName + ".voxel");
                                    if (!fs::exists(templatePath)) {
                                        templatePath = fs::path("resources/templates") / (templateName + ".txt");
                                    }
                                    if (!fs::exists(templatePath)) {
                                        // Try absolute from workspace
                                        templatePath = fs::current_path() / "resources" / "templates" / (templateName + ".voxel");
                                    }
                                    if (!fs::exists(templatePath)) {
                                        templatePath = fs::current_path() / "resources" / "templates" / (templateName + ".txt");
                                    }
                                    if (fs::exists(templatePath)) {
                                        resetEditorScene();
                                        m_interactionEditorMode = true;
                                        m_interactionEditorFile = templatePath.string();
                                        m_interactionEditorCharFile = "";
                                        initInteractionEditorScene();
                                        LOG_INFO("Application", "Opened interaction profile '{}' -> template '{}' in Interaction Editor",
                                                 p.filename().string(), templateName);
                                        handled = true;
                                    } else {
                                        LOG_WARN("Application", "Interaction profile references template '{}' but template file not found at {}",
                                                 templateName, templatePath.string());
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } catch (...) {
            LOG_WARN("Application", "Failed to parse JSON file: {}", filePath);
        }

        if (!handled) {
            LOG_WARN("Application", "Cannot open '{}' — JSON files must be interaction profiles (with archetype + profiles keys) "
                     "to open in the editor. Use File > Open to select a .voxel template or .anim file instead.",
                     p.filename().string());
        }

    } else {
        LOG_WARN("Application", "Unsupported file type '{}'. Supported: .anim (Anim Editor), .voxel/.txt (Asset/Interaction Editor), "
                 ".json (interaction profiles)", ext);
    }
}

// ============================================================================
// ASSET EDITOR MODE
// ============================================================================

void Application::initAssetEditorScene() {
    if (!chunkManager) return;

    LOG_INFO("Application", "Asset Editor: initializing scene for '{}'", m_assetEditorFile);

    // Clear the default world that WorldInitializer loaded from worlds/default.db.
    // initialize() runs before this function, so the default DB is already in memory.
    // run() hasn't started yet so there are no in-flight GPU render operations.
    chunkManager->cleanup();

    // Create the chunk that will hold the floor (origin 0,0,0 covers Y=0..31)
    chunkManager->createChunk(glm::ivec3(0, 0, 0), false);

    // Build a clean flat platform: Y=15 floor, 32Ã—32 slab of Stone
    for (int x = 0; x < 32; ++x) {
        for (int z = 0; z < 32; ++z) {
            chunkManager->m_voxelModificationSystem.addCubeWithMaterial(
                glm::ivec3(x, 15, z), "Stone");
        }
    }

    // Rebuild faces and voxel maps now that the floor is placed
    chunkManager->rebuildAllChunkFaces();
    chunkManager->initializeAllChunkVoxelMaps();

    // Spawn the template being edited at the asset origin
    // Protect the floor from being broken
    if (voxelInteractionSystem) {
        voxelInteractionSystem->setMinBreakableY(15);
    }

    if (objectTemplateManager && !m_assetEditorFile.empty()) {
        namespace fs = std::filesystem;
        // Load the template file, then spawn by its stem name
        objectTemplateManager->loadTemplate(m_assetEditorFile);
        std::string stemName = fs::path(m_assetEditorFile).stem().string();
        objectTemplateManager->spawnTemplate(stemName,
            glm::vec3(m_assetTemplateOrigin), true /*static*/);
    }

    // Position camera with a clear view of the editing area
    if (camera) {
        camera->setPosition(glm::vec3(20.0f, 22.0f, 10.0f));
        camera->setYaw(-135.0f);
        camera->setPitch(-25.0f);
        camera->setMode(Graphics::CameraMode::Free);
    }
    if (inputManager) {
        inputManager->setCameraPosition(glm::vec3(20.0f, 22.0f, 10.0f));
        inputManager->setYawPitch(-135.0f, -25.0f);
    }

    LOG_INFO("Application", "Asset Editor: scene ready. H = toggle reference char, Ctrl+S = save.");
}

void Application::renderAssetEditorUI() {
    if (!m_assetEditorMode) return;

    // Derive a display name from the file path
    namespace fs = std::filesystem;
    std::string displayName = fs::path(m_assetEditorFile).stem().string();

    if (!ImGui::Begin("Asset Editor", nullptr)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "ASSET EDITOR");
    ImGui::Separator();
    ImGui::TextWrapped("%s", displayName.c_str());
    ImGui::Spacing();

    // Material palette
    ImGui::Text("Material:");
    const char* materials[] = {
        "Wood","Stone","Metal","Glass","Rubber","Ice","Cork","glow","Leaf","Default"
    };
    for (int i = 0; i < 10; ++i) {
        bool selected = (m_assetEditorMaterial == materials[i]);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
        if (ImGui::SmallButton(materials[i])) {
            m_assetEditorMaterial = materials[i];
        }
        if (selected) ImGui::PopStyleColor();
        if (i % 3 != 2) ImGui::SameLine();
    }
    ImGui::Spacing();

    // Reference character toggle
    const char* refLabel = m_assetRefCharVisible ? "Hide Ref Char [H]" : "Show Ref Char [H]";
    if (ImGui::Button(refLabel, ImVec2(-1, 0))) {
        // Toggle via the same logic as the H-key path
        if (m_assetRefCharVisible) {
            // Remove from entities
            entities.erase(
                std::remove_if(entities.begin(), entities.end(),
                    [this](const std::unique_ptr<Scene::Entity>& e) {
                        return e.get() == m_assetRefChar;
                    }),
                entities.end());
            m_assetRefChar = nullptr;
            m_assetRefCharVisible = false;
        } else {
            auto& assets = Core::AssetManager::instance();
            std::string animPath = assets.resolveAnimatedChar("humanoid.anim");
            glm::vec3 refPos(m_assetTemplateOrigin.x + 4, m_assetTemplateOrigin.y, m_assetTemplateOrigin.z);
            m_assetRefChar = createAnimatedCharacter(refPos, animPath);
            if (m_assetRefChar) {
                m_assetRefChar->playAnimation("idle");
                m_assetRefCharVisible = true;
            }
        }
    }
    ImGui::Spacing();

    // Save button
    if (ImGui::Button("Save Template [Ctrl+S]", ImVec2(-1, 0))) {
        saveAssetTemplate();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "H: toggle ref char");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Ctrl+S: save template");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "C/Ctrl+C/Alt+C: place");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "LMB: break voxel");

    ImGui::End();
}

void Application::saveAssetTemplate() {
    if (!chunkManager) return;

    // Determine bounding box: scan a generous area around the template origin
    // Use a 16x16x16 region centred on the origin (excluding the floor at Y=15)
    constexpr int HALF = 8;
    glm::ivec3 minCorner(
        m_assetTemplateOrigin.x - HALF,
        m_assetTemplateOrigin.y,
        m_assetTemplateOrigin.z - HALF);
    glm::ivec3 maxCorner(
        m_assetTemplateOrigin.x + HALF,
        m_assetTemplateOrigin.y + 16,
        m_assetTemplateOrigin.z + HALF);

    auto voxels = captureRegionAllLevels(chunkManager, minCorner, maxCorner);

    if (voxels.empty()) {
        LOG_WARN("Application", "Asset Editor: nothing to save in region");
        return;
    }

    namespace fs = std::filesystem;
    std::string stemName = fs::path(m_assetEditorFile).stem().string();

    std::vector<std::string> lines;
    lines.push_back("# Template: " + stemName);
    lines.push_back("# Saved by Asset Editor");

    int cubeCount = 0, subcubeCount = 0, microcubeCount = 0;
    for (const auto& v : voxels) {
        std::string mat = v.material.empty() ? "Default" : v.material;
        switch (v.level) {
            case Core::SnapshotVoxelLevel::Cube:
                lines.push_back("C " + std::to_string(v.offset.x) + " " +
                    std::to_string(v.offset.y) + " " + std::to_string(v.offset.z) + " " + mat);
                ++cubeCount;
                break;
            case Core::SnapshotVoxelLevel::Subcube:
                lines.push_back("S " + std::to_string(v.offset.x) + " " +
                    std::to_string(v.offset.y) + " " + std::to_string(v.offset.z) + " " +
                    std::to_string(v.subcubePos.x) + " " + std::to_string(v.subcubePos.y) + " " +
                    std::to_string(v.subcubePos.z) + " " + mat);
                ++subcubeCount;
                break;
            case Core::SnapshotVoxelLevel::Microcube:
                lines.push_back("M " + std::to_string(v.offset.x) + " " +
                    std::to_string(v.offset.y) + " " + std::to_string(v.offset.z) + " " +
                    std::to_string(v.subcubePos.x) + " " + std::to_string(v.subcubePos.y) + " " +
                    std::to_string(v.subcubePos.z) + " " +
                    std::to_string(v.microcubePos.x) + " " + std::to_string(v.microcubePos.y) + " " +
                    std::to_string(v.microcubePos.z) + " " + mat);
                ++microcubeCount;
                break;
        }
    }

    std::ofstream file(m_assetEditorFile);
    if (file.is_open()) {
        for (const auto& line : lines) file << line << "\n";
        file.close();

        // Hot-reload into ObjectTemplateManager
        if (objectTemplateManager) objectTemplateManager->loadTemplate(m_assetEditorFile);

        LOG_INFO("Application", "Asset Editor: saved '{}' ({} cubes, {} subcubes, {} microcubes)",
                 m_assetEditorFile, cubeCount, subcubeCount, microcubeCount);
    } else {
        LOG_ERROR("Application", "Asset Editor: could not write to '{}'", m_assetEditorFile);
    }
}

// ============================================================================
// ANIM EDITOR MODE
// ============================================================================

void Application::initAnimEditorScene() {
    if (!chunkManager) return;

    LOG_INFO("Application", "Anim Editor: initializing scene for '{}'", m_animEditorFile);

    // Clear any world loaded by EngineRuntime startup
    chunkManager->cleanup();

    // Create one chunk and lay a Stone floor at Y=15
    chunkManager->createChunk(glm::ivec3(0, 0, 0), false);
    for (int x = 0; x < 32; ++x) {
        for (int z = 0; z < 32; ++z) {
            chunkManager->m_voxelModificationSystem.addCubeWithMaterial(glm::ivec3(x, 15, z), "Stone");
        }
    }
    chunkManager->rebuildAllChunkFaces();
    chunkManager->initializeAllChunkVoxelMaps();

    // Spawn the animated character from the .anim file
    glm::vec3 spawnPos(16.0f, 16.0f, 16.0f);
    auto* ch = createAnimatedCharacter(spawnPos, m_animEditorFile);
    if (ch) {
        m_animEditorChar = ch;
        ch->setAnimationState(Scene::AnimatedCharacterState::Preview);

        // Build the list of "body" bones for the editor panel (skip finger/toe bones)
        m_animEditorBodyBones.clear();
        const auto& skel = ch->getSkeleton();
        // Keywords that indicate a body-relevant bone
        auto isBodyBone = [](const std::string& name) -> bool {
            static const std::vector<std::string> include_kw = {
                "Hips","Spine","Neck","Head","Shoulder","Arm","ForeArm","Hand",
                "UpLeg","Leg","Foot"
            };
            static const std::vector<std::string> exclude_kw = {
                "Thumb","Index","Middle","Ring","Pinky","Toe","Top_End","top_end","_end","_End"
            };
            for (const auto& kw : exclude_kw) {
                if (name.find(kw) != std::string::npos) return false;
            }
            for (const auto& kw : include_kw) {
                if (name.find(kw) != std::string::npos) return true;
            }
            return false;
        };
        for (const auto& bone : skel.bones) {
            if (isBodyBone(bone.name)) {
                m_animEditorBodyBones.push_back({bone.id, bone.name});
            }
        }

        LOG_INFO("Application", "Anim Editor: loaded {} body bones from skeleton",
                 m_animEditorBodyBones.size());
    } else {
        LOG_WARN("Application", "Anim Editor: failed to load character from '{}'", m_animEditorFile);
    }

    // Position camera for a good view
    if (camera) {
        camera->setPosition(glm::vec3(20.0f, 22.0f, 25.0f));
        camera->setYaw(-135.0f);
        camera->setPitch(-20.0f);
    }
    LOG_INFO("Application", "Anim Editor: scene ready. Ctrl+S = save model changes.");
}

void Application::renderAnimEditorUI() {
    if (!m_animEditorMode || !m_animEditorChar) return;

    namespace fs = std::filesystem;
    std::string displayName = fs::path(m_animEditorFile).filename().string();

    ImGui::Begin(("Anim Editor: " + displayName + "###AnimEditor").c_str());

    // Animation preview selector
    // ---- Animation list with preview + rename ----
    ImGui::Text("Animations (%d):", (int)m_animEditorChar->getAnimationNames().size());
    ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f), "Click=preview  Dbl-click=rename");

    const auto& animNames = m_animEditorChar->getAnimationNames();
    if (!animNames.empty()) {
        if (m_animEditorAnimIdx < 0) m_animEditorAnimIdx = 0;
        if (m_animEditorAnimIdx >= (int)animNames.size())
            m_animEditorAnimIdx = (int)animNames.size() - 1;

        ImGui::BeginChild("AnimList", ImVec2(0, 160), true);
        for (int i = 0; i < (int)animNames.size(); ++i) {
            ImGui::PushID(i);

            if (m_animEditorRenamingIdx == i) {
                // Inline rename text field
                ImGui::SetNextItemWidth(-1);
                bool commit = ImGui::InputText("##rename", m_animEditorRenameBuffer,
                                               sizeof(m_animEditorRenameBuffer),
                                               ImGuiInputTextFlags_EnterReturnsTrue |
                                               ImGuiInputTextFlags_AutoSelectAll);
                if (ImGui::IsItemDeactivated()) {
                    // Enter or focus lost  --  commit if non-empty and changed
                    std::string newName(m_animEditorRenameBuffer);
                    if (!newName.empty() && newName != animNames[i]) {
                        renameAnimationInFile(animNames[i], newName);
                    }
                    m_animEditorRenamingIdx = -1;
                }
                if (commit && ImGui::IsItemActive()) {
                    // Already handled by IsItemDeactivated
                }
                // ESC cancels
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    m_animEditorRenamingIdx = -1;
                }
            } else {
                bool selected = (i == m_animEditorAnimIdx);
                if (ImGui::Selectable(animNames[i].c_str(), selected,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    m_animEditorAnimIdx = i;
                    m_animEditorChar->playAnimation(animNames[i]);

                    if (ImGui::IsMouseDoubleClicked(0)) {
                        // Start rename
                        m_animEditorRenamingIdx = i;
                        strncpy(m_animEditorRenameBuffer, animNames[i].c_str(),
                                sizeof(m_animEditorRenameBuffer) - 1);
                        m_animEditorRenameBuffer[sizeof(m_animEditorRenameBuffer) - 1] = '\0';
                        ImGui::SetKeyboardFocusHere(-1);
                    }
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (ImGui::Button("< Prev")) {
            m_animEditorAnimIdx = (m_animEditorAnimIdx - 1 + (int)animNames.size()) % (int)animNames.size();
            m_animEditorChar->playAnimation(animNames[m_animEditorAnimIdx]);
        }
        ImGui::SameLine();
        if (ImGui::Button("Next >")) {
            m_animEditorAnimIdx = (m_animEditorAnimIdx + 1) % (int)animNames.size();
            m_animEditorChar->playAnimation(animNames[m_animEditorAnimIdx]);
        }
    }

    ImGui::Separator();
    ImGui::Text("Bone Scales:");
    ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f), "(drag slider to resize bone)");
    ImGui::Spacing();

    // Bone list with scale sliders
    const Phyxel::VoxelModel& model = m_animEditorChar->getVoxelModel();

    // Build a quick lookup: boneId -> total box count for display
    std::map<int,int> boneBoxCount;
    for (const auto& shape : model.shapes) {
        boneBoxCount[shape.boneId]++;
    }

    ImGui::BeginChild("BoneList", ImVec2(0, 350), true);
    for (auto& [boneId, boneName] : m_animEditorBodyBones) {
        bool selected = (m_animEditorSelectedBone == boneId);

        // Get/init scale for this bone
        if (m_animEditorBoneScale.find(boneId) == m_animEditorBoneScale.end()) {
            m_animEditorBoneScale[boneId] = 1.0f;
        }
        float& scale = m_animEditorBoneScale[boneId];

        // Short label
        std::string label = boneName;
        if (label.size() > 20) label = label.substr(0, 20);

        ImGui::PushID(boneId);
        if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(120, 0))) {
            m_animEditorSelectedBone = boneId;
        }
        ImGui::SameLine(125);
        ImGui::SetNextItemWidth(100.0f);
        bool changed = ImGui::SliderFloat("##s", &scale, 0.1f, 3.0f, "%.2f");
        if (changed) {
            // Apply scale to all shapes for this bone
            Phyxel::VoxelModel newModel = m_animEditorChar->getVoxelModel();
            // We need to know the "base" size. We apply scale relative to original.
            // Store original on first change by reading from character.
            // Simple approach: track last applied scale and adjust incrementally.
            // For now, apply absolute scale relative to stored original model shapes.
            // Re-read original each time from the loaded file (via originalVoxelModel_
            // which we exposed via getVoxelModel returning voxelModel - post-scale).
            //
            // The simplest correct approach: maintain our own copy of the original model.
            // We'll use a lazy-initialized "original model" cache.
            static std::map<int, Phyxel::VoxelModel> s_originalModels;
            auto charKey = (intptr_t)m_animEditorChar;
            if (s_originalModels.find(charKey) == s_originalModels.end()) {
                s_originalModels[charKey] = m_animEditorChar->getVoxelModel();
            }
            const Phyxel::VoxelModel& origModel = s_originalModels[charKey];

            for (auto& shape : newModel.shapes) {
                if (shape.boneId == boneId) {
                    // Find matching shape in original
                    for (const auto& origShape : origModel.shapes) {
                        if (origShape.boneId == boneId &&
                            origShape.offset == shape.offset) {
                            shape.size = origShape.size * scale;
                            break;
                        }
                    }
                }
            }
            m_animEditorChar->setVoxelModel(newModel);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("Reset All Scales")) {
        m_animEditorBoneScale.clear();
        // Re-load original model from file
        Phyxel::AnimationSystem animSys;
        Phyxel::Skeleton skel;
        std::vector<Phyxel::AnimationClip> clips;
        Phyxel::VoxelModel origModel;
        if (animSys.loadFromFile(m_animEditorFile, skel, clips, origModel)) {
            m_animEditorChar->setVoxelModel(origModel);
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("Save Model [Ctrl+S]", ImVec2(-1, 0))) {
        saveAnimModel();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f), "Ctrl+S: save");
    ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f), "V: toggle camera mode");

    ImGui::End();
}

void Application::saveAnimModel() {
    if (!m_animEditorChar) return;

    // Load the full .anim file to get skeleton + animations
    Phyxel::AnimationSystem animSys;
    Phyxel::Skeleton skel;
    std::vector<Phyxel::AnimationClip> clips;
    Phyxel::VoxelModel originalModel;
    if (!animSys.loadFromFile(m_animEditorFile, skel, clips, originalModel)) {
        LOG_ERROR("Application", "Anim Editor: failed to read '{}' for save", m_animEditorFile);
        return;
    }

    // Get the modified model from the character
    const Phyxel::VoxelModel& modifiedModel = m_animEditorChar->getVoxelModel();

    // Read the raw .anim file lines
    std::ifstream inFile(m_animEditorFile);
    if (!inFile.is_open()) {
        LOG_ERROR("Application", "Anim Editor: cannot open '{}' for reading", m_animEditorFile);
        return;
    }
    std::vector<std::string> fileLines;
    std::string line;
    while (std::getline(inFile, line)) {
        fileLines.push_back(line);
    }
    inFile.close();

    // Find and replace the MODEL section
    // MODEL section format:
    //   MODEL
    //   BoxCount N
    //   Box boneId sx sy sz ox oy oz
    //   ...
    std::vector<std::string> outLines;
    bool inModel = false;
    bool modelDone = false;
    bool modelWritten = false;

    for (size_t i = 0; i < fileLines.size(); ++i) {
        const std::string& fl = fileLines[i];
        std::string trimmed = fl;
        // Trim leading whitespace
        size_t start = trimmed.find_first_not_of(" \t");
        if (start != std::string::npos) trimmed = trimmed.substr(start);

        if (!inModel && !modelDone && trimmed == "MODEL") {
            inModel = true;
            outLines.push_back(fl); // Keep "MODEL" line
            continue;
        }

        if (inModel && !modelWritten) {
            // Skip BoxCount line and all Box lines
            if (trimmed.rfind("BoxCount", 0) == 0) {
                // Replace with updated count
                outLines.push_back("BoxCount " + std::to_string(modifiedModel.shapes.size()));
                continue;
            }
            if (trimmed.rfind("Box ", 0) == 0) {
                // Skip original boxes; we'll write new ones after the last Box line
                // Peek ahead: if next line is NOT a Box line, flush the new model
                bool nextIsBox = false;
                if (i + 1 < fileLines.size()) {
                    std::string next = fileLines[i + 1];
                    size_t ns = next.find_first_not_of(" \t");
                    if (ns != std::string::npos) next = next.substr(ns);
                    nextIsBox = (next.rfind("Box ", 0) == 0);
                }
                if (!nextIsBox) {
                    // Write all modified shapes
                    for (const auto& shape : modifiedModel.shapes) {
                        outLines.push_back("Box " +
                            std::to_string(shape.boneId) + " " +
                            std::to_string(shape.size.x) + " " +
                            std::to_string(shape.size.y) + " " +
                            std::to_string(shape.size.z) + " " +
                            std::to_string(shape.offset.x) + " " +
                            std::to_string(shape.offset.y) + " " +
                            std::to_string(shape.offset.z));
                    }
                    modelWritten = true;
                    inModel = false;
                    modelDone = true;
                }
                // Either way, don't copy the original Box line
                continue;
            }
            // If we hit a non-Box/non-BoxCount line while in model section,
            // we must have missed writing (empty model edge case)
            if (!modelWritten) {
                for (const auto& shape : modifiedModel.shapes) {
                    outLines.push_back("Box " +
                        std::to_string(shape.boneId) + " " +
                        std::to_string(shape.size.x) + " " +
                        std::to_string(shape.size.y) + " " +
                        std::to_string(shape.size.z) + " " +
                        std::to_string(shape.offset.x) + " " +
                        std::to_string(shape.offset.y) + " " +
                        std::to_string(shape.offset.z));
                }
                modelWritten = true;
                inModel = false;
                modelDone = true;
            }
            outLines.push_back(fl);
            continue;
        }

        outLines.push_back(fl);
    }

    // Write back to file
    std::ofstream outFile(m_animEditorFile);
    if (!outFile.is_open()) {
        LOG_ERROR("Application", "Anim Editor: cannot write to '{}'", m_animEditorFile);
        return;
    }
    for (const auto& ol : outLines) {
        outFile << ol << "\n";
    }
    outFile.close();

    LOG_INFO("Application", "Anim Editor: saved {} shapes to '{}'",
             modifiedModel.shapes.size(), m_animEditorFile);
}

void Application::renameAnimationInFile(const std::string& oldName, const std::string& newName) {
    if (oldName.empty() || newName.empty() || oldName == newName) return;

    // Read file
    std::ifstream inFile(m_animEditorFile);
    if (!inFile.is_open()) {
        LOG_ERROR("Application", "Anim Editor: cannot open '{}' for rename", m_animEditorFile);
        return;
    }
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(inFile, line)) lines.push_back(line);
    inFile.close();

    // Replace "ANIMATION <oldName>" lines (exact match after trimming)
    int replacements = 0;
    for (auto& fl : lines) {
        std::string trimmed = fl;
        size_t start = trimmed.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        trimmed = trimmed.substr(start);
        if (trimmed == "ANIMATION " + oldName) {
            fl = "ANIMATION " + newName;
            ++replacements;
        }
    }

    if (replacements == 0) {
        LOG_WARN("Application", "Anim Editor: rename found no match for '{}'", oldName);
        return;
    }

    // Write back
    std::ofstream outFile(m_animEditorFile);
    if (!outFile.is_open()) {
        LOG_ERROR("Application", "Anim Editor: cannot write '{}' for rename", m_animEditorFile);
        return;
    }
    for (const auto& fl : lines) outFile << fl << "\n";
    outFile.close();

    LOG_INFO("Application", "Anim Editor: renamed '{}' -> '{}' ({} occurrence(s))",
             oldName, newName, replacements);

    // Hot-reload so the in-memory clip name updates immediately
    if (m_animEditorChar) {
        m_animEditorChar->reloadAnimations(m_animEditorFile);
        // Keep previewing the renamed clip
        m_animEditorChar->playAnimation(newName);
        // Re-clamp index in case vector size changed (it won't, but be safe)
        const auto& names = m_animEditorChar->getAnimationNames();
        m_animEditorAnimIdx = 0;
        for (int i = 0; i < (int)names.size(); ++i) {
            if (names[i] == newName) { m_animEditorAnimIdx = i; break; }
        }
    }
}

// ============================================================================
// INTERACTION EDITOR MODE  (--interaction-editor <file> [--character <file>])
// ============================================================================

void Application::initInteractionEditorScene() {
    if (!chunkManager) return;

    LOG_INFO("Application", "Interaction Editor: initializing scene for '{}'", m_interactionEditorFile);

    // Clear the default world
    chunkManager->cleanup();

    // Create one chunk with a Stone floor at Y=15
    chunkManager->createChunk(glm::ivec3(0, 0, 0), false);
    for (int x = 0; x < 32; ++x) {
        for (int z = 0; z < 32; ++z) {
            chunkManager->m_voxelModificationSystem.addCubeWithMaterial(
                glm::ivec3(x, 15, z), "Stone");
        }
    }
    chunkManager->rebuildAllChunkFaces();
    chunkManager->initializeAllChunkVoxelMaps();

    // Protect floor from breaking
    if (voxelInteractionSystem) {
        voxelInteractionSystem->setMinBreakableY(15);
    }

    // Spawn the asset template at the origin
    if (objectTemplateManager && !m_interactionEditorFile.empty()) {
        namespace fs = std::filesystem;
        objectTemplateManager->loadTemplate(m_interactionEditorFile);
        std::string stemName = fs::path(m_interactionEditorFile).stem().string();
        objectTemplateManager->spawnTemplate(stemName,
            glm::vec3(m_ieAssetOrigin), true /*static*/);
    }

    // Spawn the character next to the asset
    std::string charFile = m_interactionEditorCharFile;
    if (charFile.empty()) {
        auto& assets = Core::AssetManager::instance();
        charFile = assets.resolveAnimatedChar("humanoid.anim");
    }
    m_ieCharRestPos = glm::vec3(m_ieAssetOrigin.x + 4, m_ieAssetOrigin.y, m_ieAssetOrigin.z);
    m_ieChar = createAnimatedCharacter(m_ieCharRestPos, charFile);
    if (m_ieChar) {
        m_ieChar->playAnimation("idle");

        // Build body bones list for bone scale sliders (same filter as anim editor)
        m_ieBodyBones.clear();
        const auto& skel = m_ieChar->getSkeleton();
        auto isBodyBone = [](const std::string& name) -> bool {
            static const std::vector<std::string> include_kw = {
                "Hips","Spine","Neck","Head","Shoulder","Arm","ForeArm","Hand",
                "UpLeg","Leg","Foot"
            };
            static const std::vector<std::string> exclude_kw = {
                "Thumb","Index","Middle","Ring","Pinky","Toe","Top_End","top_end","_end","_End"
            };
            for (const auto& kw : exclude_kw)
                if (name.find(kw) != std::string::npos) return false;
            for (const auto& kw : include_kw)
                if (name.find(kw) != std::string::npos) return true;
            return false;
        };
        for (const auto& bone : skel.bones) {
            if (isBodyBone(bone.name))
                m_ieBodyBones.push_back({bone.id, bone.name});
        }

        // Initialize archetype buffer for editing
        std::string arch = m_ieChar->getArchetype();
        strncpy(m_ieArchetypeBuf, arch.c_str(), sizeof(m_ieArchetypeBuf) - 1);
        m_ieArchetypeBuf[sizeof(m_ieArchetypeBuf) - 1] = '\0';
    }

    // Set up interaction profiles
    if (interactionProfileManager && m_ieChar) {
        std::string basePath = "resources/interactions/";
        interactionProfileManager->setBasePath(basePath);
        m_ieProfileArchetype = m_ieChar->getArchetype();
        interactionProfileManager->loadArchetype(m_ieProfileArchetype);
    }

    // Position camera with an elevated overview of the editing area
    if (camera) {
        camera->setPosition(glm::vec3(20.0f, 24.0f, 8.0f));
        camera->setYaw(-135.0f);
        camera->setPitch(-30.0f);
        camera->setMode(Graphics::CameraMode::Free);
    }
    if (inputManager) {
        inputManager->setCameraPosition(glm::vec3(20.0f, 24.0f, 8.0f));
        inputManager->setYawPitch(-135.0f, -30.0f);
    }

    LOG_INFO("Application", "Interaction Editor: scene ready. Ctrl+S = save profile.");
}

void Application::ieStartPreview(bool autoPlay) {
    if (!m_ieChar || !placedObjectManager) return;

    auto& allDefs = placedObjectManager->getMutableTemplateDefs();
    namespace fs = std::filesystem;
    std::string stemName = fs::path(m_interactionEditorFile).stem().string();
    auto it = allDefs.find(stemName);
    if (it == allDefs.end() || m_ieSelectedPoint >= (int)it->second.size()) return;

    auto& def = it->second[m_ieSelectedPoint];

    // Compute world-space seat anchor from asset origin + local offset
    glm::vec3 seatAnchor = glm::vec3(m_ieAssetOrigin) + def.localOffset;

    // Get profile offsets
    std::string archetype = m_ieChar->getArchetype();
    glm::vec3 sitDown{0.0f}, sittingIdle{0.0f}, sitStandUp{0.0f};
    float blendDur = 0.0f, heightOff = 0.0f;

    if (interactionProfileManager) {
        const auto* profile = interactionProfileManager->getProfile(
            archetype, stemName, def.pointId);
        if (profile) {
            sitDown     = profile->sitDownOffset;
            sittingIdle = profile->sittingIdleOffset;
            sitStandUp  = profile->sitStandUpOffset;
            blendDur    = profile->sitBlendDuration;
            heightOff   = profile->seatHeightOffset;
        }
    }

    m_ieChar->sitAt(seatAnchor, def.facingYaw,
                    sitDown, sittingIdle, sitStandUp, blendDur, heightOff);

    m_iePreviewState = InteractionPreviewState::SittingDown;
    m_ieAutoPreview = autoPlay;
    m_ieAutoTimer = 0.0f;

    LOG_INFO("Application", "Interaction Editor: preview started (auto={})", autoPlay);
}

void Application::ieStopPreview() {
    if (!m_ieChar) return;

    // Force reset: if sitting, stand up immediately by resetting state
    if (m_ieChar->isSitting()) {
        m_ieChar->standUp();
    }

    m_iePreviewState = InteractionPreviewState::None;
    m_ieAutoPreview = false;
    m_ieAutoTimer = 0.0f;

    LOG_INFO("Application", "Interaction Editor: preview reset");
}

void Application::ieSaveAnimModel() {
    if (!m_ieChar) return;

    // Resolve .anim file path
    std::string animFile = m_interactionEditorCharFile;
    if (animFile.empty())
        animFile = Core::AssetManager::instance().resolveAnimatedChar("humanoid.anim");

    // Load the full .anim file
    Phyxel::AnimationSystem animSys;
    Phyxel::Skeleton skel;
    std::vector<Phyxel::AnimationClip> clips;
    Phyxel::VoxelModel originalModel;
    if (!animSys.loadFromFile(animFile, skel, clips, originalModel)) {
        LOG_ERROR("Application", "IE: failed to read '{}' for save", animFile);
        return;
    }

    const Phyxel::VoxelModel& modifiedModel = m_ieChar->getVoxelModel();

    // Read the raw .anim file lines
    std::ifstream inFile(animFile);
    if (!inFile.is_open()) {
        LOG_ERROR("Application", "IE: cannot open '{}' for reading", animFile);
        return;
    }
    std::vector<std::string> fileLines;
    std::string line;
    while (std::getline(inFile, line))
        fileLines.push_back(line);
    inFile.close();

    // Update archetype header if present
    std::string newArchetype(m_ieArchetypeBuf);
    bool archetypeUpdated = false;

    // Find MODEL section and replace boxes; also update archetype header
    std::vector<std::string> outLines;
    bool inModel = false;
    bool modelDone = false;
    bool modelWritten = false;

    for (size_t i = 0; i < fileLines.size(); ++i) {
        const std::string& fl = fileLines[i];
        std::string trimmed = fl;
        size_t start = trimmed.find_first_not_of(" \t");
        if (start != std::string::npos) trimmed = trimmed.substr(start);

        // Update archetype comment line
        if (!archetypeUpdated && trimmed.rfind("# archetype:", 0) == 0) {
            outLines.push_back("# archetype: " + newArchetype);
            archetypeUpdated = true;
            continue;
        }

        if (!inModel && !modelDone && trimmed == "MODEL") {
            inModel = true;
            outLines.push_back(fl);
            continue;
        }

        if (inModel && !modelWritten) {
            if (trimmed.rfind("BoxCount", 0) == 0) {
                outLines.push_back("BoxCount " + std::to_string(modifiedModel.shapes.size()));
                continue;
            }
            if (trimmed.rfind("Box ", 0) == 0) {
                bool nextIsBox = false;
                if (i + 1 < fileLines.size()) {
                    std::string next = fileLines[i + 1];
                    size_t ns = next.find_first_not_of(" \t");
                    if (ns != std::string::npos) next = next.substr(ns);
                    nextIsBox = (next.rfind("Box ", 0) == 0);
                }
                if (!nextIsBox) {
                    for (const auto& shape : modifiedModel.shapes) {
                        outLines.push_back("Box " +
                            std::to_string(shape.boneId) + " " +
                            std::to_string(shape.size.x) + " " +
                            std::to_string(shape.size.y) + " " +
                            std::to_string(shape.size.z) + " " +
                            std::to_string(shape.offset.x) + " " +
                            std::to_string(shape.offset.y) + " " +
                            std::to_string(shape.offset.z));
                    }
                    modelWritten = true;
                    inModel = false;
                    modelDone = true;
                }
                continue;
            }
            if (!modelWritten) {
                for (const auto& shape : modifiedModel.shapes) {
                    outLines.push_back("Box " +
                        std::to_string(shape.boneId) + " " +
                        std::to_string(shape.size.x) + " " +
                        std::to_string(shape.size.y) + " " +
                        std::to_string(shape.size.z) + " " +
                        std::to_string(shape.offset.x) + " " +
                        std::to_string(shape.offset.y) + " " +
                        std::to_string(shape.offset.z));
                }
                modelWritten = true;
                inModel = false;
                modelDone = true;
            }
            outLines.push_back(fl);
            continue;
        }

        outLines.push_back(fl);
    }

    // Write back
    std::ofstream outFile(animFile);
    if (!outFile.is_open()) {
        LOG_ERROR("Application", "IE: cannot write to '{}'", animFile);
        return;
    }
    for (const auto& ol : outLines)
        outFile << ol << "\n";
    outFile.close();

    LOG_INFO("Application", "IE: saved {} shapes to '{}'",
             modifiedModel.shapes.size(), animFile);
}

void Application::ieSaveAssetTemplate() {
    if (!chunkManager) return;

    namespace fs = std::filesystem;
    std::string assetName = fs::path(m_interactionEditorFile).stem().string();

    // Scan region around asset origin (same as asset editor: 16Ã—16Ã—16 above floor)
    constexpr int HALF = 8;
    glm::ivec3 minCorner(
        m_ieAssetOrigin.x - HALF,
        m_ieAssetOrigin.y,
        m_ieAssetOrigin.z - HALF);
    glm::ivec3 maxCorner(
        m_ieAssetOrigin.x + HALF,
        m_ieAssetOrigin.y + 16,
        m_ieAssetOrigin.z + HALF);

    auto voxels = captureRegionAllLevels(chunkManager, minCorner, maxCorner);
    if (voxels.empty()) {
        LOG_WARN("Application", "IE: nothing to save in region");
        return;
    }

    std::vector<std::string> lines;
    lines.push_back("# Template: " + assetName);
    lines.push_back("# Saved by Interaction Editor");

    // Write interaction point defs as metadata
    auto& allDefs = placedObjectManager->getMutableTemplateDefs();
    auto defIt = allDefs.find(assetName);
    if (defIt != allDefs.end()) {
        for (const auto& def : defIt->second) {
            std::string groupsStr = "*";
            if (!def.supportedGroups.empty()) {
                groupsStr.clear();
                for (size_t i = 0; i < def.supportedGroups.size(); ++i) {
                    if (i > 0) groupsStr += ",";
                    groupsStr += def.supportedGroups[i];
                }
            }
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                "# interaction_point: %s %s %.4f %.4f %.4f %.6f %s",
                def.pointId.c_str(), def.type.c_str(),
                def.localOffset.x, def.localOffset.y, def.localOffset.z,
                def.facingYaw, groupsStr.c_str());
            lines.push_back(buf);
        }
    }

    int cubeCount = 0, subcubeCount = 0, microcubeCount = 0;
    for (const auto& v : voxels) {
        std::string mat = v.material.empty() ? "Default" : v.material;
        switch (v.level) {
            case Core::SnapshotVoxelLevel::Cube:
                lines.push_back("C " + std::to_string(v.offset.x) + " " +
                    std::to_string(v.offset.y) + " " + std::to_string(v.offset.z) + " " + mat);
                ++cubeCount;
                break;
            case Core::SnapshotVoxelLevel::Subcube:
                lines.push_back("S " + std::to_string(v.offset.x) + " " +
                    std::to_string(v.offset.y) + " " + std::to_string(v.offset.z) + " " +
                    std::to_string(v.subcubePos.x) + " " + std::to_string(v.subcubePos.y) + " " +
                    std::to_string(v.subcubePos.z) + " " + mat);
                ++subcubeCount;
                break;
            case Core::SnapshotVoxelLevel::Microcube:
                lines.push_back("M " + std::to_string(v.offset.x) + " " +
                    std::to_string(v.offset.y) + " " + std::to_string(v.offset.z) + " " +
                    std::to_string(v.subcubePos.x) + " " + std::to_string(v.subcubePos.y) + " " +
                    std::to_string(v.subcubePos.z) + " " +
                    std::to_string(v.microcubePos.x) + " " + std::to_string(v.microcubePos.y) + " " +
                    std::to_string(v.microcubePos.z) + " " + mat);
                ++microcubeCount;
                break;
        }
    }

    std::ofstream file(m_interactionEditorFile);
    if (file.is_open()) {
        for (const auto& line : lines) file << line << "\n";
        file.close();
        if (objectTemplateManager) objectTemplateManager->loadTemplate(m_interactionEditorFile);
        LOG_INFO("Application", "IE: saved full template '{}' ({} cubes, {} subcubes, {} microcubes)",
                 m_interactionEditorFile, cubeCount, subcubeCount, microcubeCount);
    } else {
        LOG_ERROR("Application", "IE: could not write to '{}'", m_interactionEditorFile);
    }
}

void Application::renderInteractionEditorUI() {
    if (!m_interactionEditorMode || !m_ieChar) return;

    namespace fs = std::filesystem;
    std::string assetName = fs::path(m_interactionEditorFile).stem().string();
    std::string charName = m_interactionEditorCharFile.empty()
        ? "humanoid.anim"
        : fs::path(m_interactionEditorCharFile).filename().string();
    std::string archetype = m_ieProfileArchetype;

    if (!ImGui::Begin("Interaction Editor", nullptr)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "INTERACTION EDITOR");
    ImGui::Separator();
    ImGui::Text("Asset: %s", assetName.c_str());
    ImGui::Text("Character: %s", charName.c_str());
    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "Char Archetype: %s", m_ieChar->getArchetype().c_str());

    // === Profile selector: load any archetype JSON ===
    {
        namespace fs = std::filesystem;
        std::vector<std::string> profileNames;
        std::string interactionsDir = "resources/interactions/";
        if (fs::is_directory(interactionsDir)) {
            for (auto& entry : fs::directory_iterator(interactionsDir)) {
                if (entry.path().extension() == ".json") {
                    profileNames.push_back(entry.path().stem().string());
                }
            }
        }
        // Ensure current archetype is in the list
        bool found = false;
        for (const auto& n : profileNames) {
            if (n == archetype) { found = true; break; }
        }
        if (!found) profileNames.insert(profileNames.begin(), archetype);
        std::sort(profileNames.begin(), profileNames.end());

        ImGui::Text("Profile:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##ieProfileSelect", archetype.c_str())) {
            for (const auto& name : profileNames) {
                bool selected = (name == archetype);
                if (ImGui::Selectable(name.c_str(), selected)) {
                    if (name != archetype) {
                        m_ieProfileArchetype = name;
                        archetype = name;
                        if (interactionProfileManager)
                            interactionProfileManager->loadArchetype(name);
                        LOG_INFO("Application", "Interaction Editor: loaded profile '{}'", name);
                    }
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Spacing();

    // === Material palette ===
    ImGui::Text("Material:");
    const char* ieMaterials[] = {
        "Wood","Stone","Metal","Glass","Rubber","Ice","Cork","glow","Leaf","Default"
    };
    for (int i = 0; i < 10; ++i) {
        bool selected = (m_ieMaterial == ieMaterials[i]);
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 1.0f, 1.0f));
        if (ImGui::SmallButton(ieMaterials[i])) {
            m_ieMaterial = ieMaterials[i];
        }
        if (selected) ImGui::PopStyleColor();
        if (i % 3 != 2) ImGui::SameLine();
    }
    ImGui::Spacing();

    // Get interaction point defs for this asset
    auto& allDefs = placedObjectManager->getMutableTemplateDefs();
    auto defIt = allDefs.find(assetName);
    bool hasDefs = (defIt != allDefs.end() && !defIt->second.empty());

    // === Add Point UI (always visible) ===
    ImGui::Separator();
    if (ImGui::TreeNode("Add Interaction Point")) {
        ImGui::SetNextItemWidth(120);
        ImGui::InputText("Point ID##newpt", m_ieNewPointId, sizeof(m_ieNewPointId));
        ImGui::SameLine();
        if (ImGui::Button("Add##addpt")) {
            std::string newId(m_ieNewPointId);
            if (!newId.empty()) {
                Core::InteractionPointDef newDef;
                newDef.pointId = newId;
                newDef.type = "seat";
                newDef.localOffset = glm::vec3(0.5f, 1.0f, 0.5f);
                newDef.facingYaw = 3.14159265f;
                allDefs[assetName].push_back(newDef);
                m_ieSelectedPoint = (int)allDefs[assetName].size() - 1;
                m_ieNewPointId[0] = '\0';
                placedObjectManager->recomputeAllInteractionPoints();
                hasDefs = true;
                defIt = allDefs.find(assetName);
                LOG_INFO("Application", "IE: added interaction point '{}'", newId);
            }
        }
        ImGui::TreePop();
    }

    if (!hasDefs) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "No interaction points defined.");
        ImGui::End();
        return;
    }

    auto& defs = defIt->second;
    if (m_ieSelectedPoint >= (int)defs.size()) m_ieSelectedPoint = 0;

    // Point selector
    ImGui::Separator();
    if (ImGui::BeginCombo("Point", defs[m_ieSelectedPoint].pointId.c_str())) {
        for (int i = 0; i < (int)defs.size(); ++i) {
            bool selected = (i == m_ieSelectedPoint);
            std::string label = defs[i].pointId + " (" + defs[i].type + ")";
            if (ImGui::Selectable(label.c_str(), selected)) {
                m_ieSelectedPoint = i;
                // Reset preview when switching points
                if (m_iePreviewState != InteractionPreviewState::None)
                    ieStopPreview();
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // Remove point button
    if (defs.size() > 1) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            std::string removedId = defs[m_ieSelectedPoint].pointId;
            defs.erase(defs.begin() + m_ieSelectedPoint);
            if (m_ieSelectedPoint >= (int)defs.size())
                m_ieSelectedPoint = (int)defs.size() - 1;
            placedObjectManager->recomputeAllInteractionPoints();
            LOG_INFO("Application", "IE: removed interaction point '{}'", removedId);
        }
    }

    auto& def = defs[m_ieSelectedPoint];

    // === Supported groups editor ===
    if (ImGui::TreeNode("Supported Groups")) {
        if (def.supportedGroups.empty()) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "(all archetypes)");
        }
        int removeIdx = -1;
        for (int g = 0; g < (int)def.supportedGroups.size(); ++g) {
            ImGui::PushID(g);
            ImGui::BulletText("%s", def.supportedGroups[g].c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("X##rmg")) {
                removeIdx = g;
            }
            ImGui::PopID();
        }
        if (removeIdx >= 0) {
            def.supportedGroups.erase(def.supportedGroups.begin() + removeIdx);
        }
        // Add group
        ImGui::SetNextItemWidth(120);
        ImGui::InputText("##newgrp", m_ieNewGroupBuf, sizeof(m_ieNewGroupBuf));
        ImGui::SameLine();
        if (ImGui::SmallButton("Add Group")) {
            std::string grp(m_ieNewGroupBuf);
            if (!grp.empty()) {
                // Avoid duplicates
                bool exists = false;
                for (const auto& g : def.supportedGroups)
                    if (g == grp) { exists = true; break; }
                if (!exists) {
                    def.supportedGroups.push_back(grp);
                    m_ieNewGroupBuf[0] = '\0';
                }
            }
        }
        ImGui::TreePop();
    } else {
        // Show compact groups summary when closed
        std::string groupsLabel = def.supportedGroups.empty() ? "all" : "";
        for (size_t g = 0; g < def.supportedGroups.size(); ++g) {
            if (g > 0) groupsLabel += ", ";
            groupsLabel += def.supportedGroups[g];
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "[%s]", groupsLabel.c_str());
    }

    // === Asset-level settings ===
    ImGui::Spacing();
    bool assetChanged = false;
    if (ImGui::TreeNodeEx("Asset Point (saved to .voxel)", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Type selector
        const char* types[] = {"seat", "bed", "counter", "door_handle", "pickup", "ledge", "switch"};
        int typeIdx = 0;
        for (int t = 0; t < 7; ++t) {
            if (def.type == types[t]) { typeIdx = t; break; }
        }
        if (ImGui::Combo("Type##pttype", &typeIdx, types, 7)) {
            def.type = types[typeIdx];
            assetChanged = true;
        }

        assetChanged |= ImGui::InputFloat3("Seat Anchor", &def.localOffset.x, "%.4f");
        assetChanged |= ImGui::InputFloat("Facing Yaw", &def.facingYaw, 0.0f, 0.0f, "%.4f");
        ImGui::TreePop();
    }

    if (assetChanged)
        placedObjectManager->recomputeAllInteractionPoints();

    // === Animation validation ===
    auto iType = def.interactionType();
    auto requiredAnims = Core::requiredAnimationsForType(iType);
    if (!requiredAnims.empty() && m_ieChar) {
        std::vector<std::string> charAnims = m_ieChar->getAnimationNames();
        std::vector<std::string> missing;
        for (const auto& req : requiredAnims) {
            bool found = false;
            for (const auto& ca : charAnims)
                if (ca == req) { found = true; break; }
            if (!found) missing.push_back(req);
        }
        if (!missing.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Missing animations:");
            for (const auto& m : missing)
                ImGui::BulletText("%s", m.c_str());
        } else {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "All required animations present.");
        }
    }

    // === Profile-level settings (type-appropriate) ===
    ImGui::Spacing();
    if (ImGui::TreeNodeEx("Profile Offsets (per-archetype)", ImGuiTreeNodeFlags_DefaultOpen)) {
        Core::InteractionProfile profile;
        if (interactionProfileManager) {
            const auto* existing = interactionProfileManager->getProfile(archetype, assetName, def.pointId);
            if (existing) profile = *existing;
        }

        bool profileChanged = false;

        switch (iType) {
            case Core::ObjectInteractionType::Seat:
            case Core::ObjectInteractionType::Counter:
                ImGui::TextDisabled("Feet snap pos relative to Seat Anchor:");
                profileChanged |= ImGui::InputFloat3("SitDown##sd", &profile.sitDownOffset.x, "%.4f");
                profileChanged |= ImGui::InputFloat3("SittingIdle##si", &profile.sittingIdleOffset.x, "%.4f");
                profileChanged |= ImGui::InputFloat3("StandUp##su", &profile.sitStandUpOffset.x, "%.4f");
                ImGui::Spacing();
                if (ImGui::Button("Copy SitDown -> All")) {
                    profile.sittingIdleOffset = profile.sitDownOffset;
                    profile.sitStandUpOffset = profile.sitDownOffset;
                    profileChanged = true;
                }
                ImGui::Spacing();
                profileChanged |= ImGui::SliderFloat("Blend##bd", &profile.sitBlendDuration, 0.0f, 1.0f, "%.2f s");
                profileChanged |= ImGui::InputFloat("Height Offset##sh", &profile.seatHeightOffset, 0.0f, 0.0f, "%.4f");
                break;

            case Core::ObjectInteractionType::Bed:
                ImGui::TextDisabled("Lying position offsets:");
                profileChanged |= ImGui::InputFloat3("LieDown##ld", &profile.sitDownOffset.x, "%.4f");
                profileChanged |= ImGui::InputFloat3("Sleeping##sl", &profile.sittingIdleOffset.x, "%.4f");
                profileChanged |= ImGui::InputFloat3("WakeUp##wu", &profile.sitStandUpOffset.x, "%.4f");
                ImGui::Spacing();
                profileChanged |= ImGui::SliderFloat("Blend##bd", &profile.sitBlendDuration, 0.0f, 1.0f, "%.2f s");
                profileChanged |= ImGui::InputFloat("Height Offset##sh", &profile.seatHeightOffset, 0.0f, 0.0f, "%.4f");
                break;

            case Core::ObjectInteractionType::DoorHandle:
            case Core::ObjectInteractionType::Switch:
                ImGui::TextDisabled("Hand position offset:");
                profileChanged |= ImGui::InputFloat3("Reach##reach", &profile.sitDownOffset.x, "%.4f");
                ImGui::Spacing();
                profileChanged |= ImGui::SliderFloat("Blend##bd", &profile.sitBlendDuration, 0.0f, 1.0f, "%.2f s");
                break;

            case Core::ObjectInteractionType::Pickup:
                ImGui::TextDisabled("Grab position offset:");
                profileChanged |= ImGui::InputFloat3("Grab##grab", &profile.sitDownOffset.x, "%.4f");
                ImGui::Spacing();
                profileChanged |= ImGui::SliderFloat("Blend##bd", &profile.sitBlendDuration, 0.0f, 1.0f, "%.2f s");
                break;

            case Core::ObjectInteractionType::Ledge:
                ImGui::TextDisabled("Grab/hang/climb offsets:");
                profileChanged |= ImGui::InputFloat3("Grab##lgr", &profile.sitDownOffset.x, "%.4f");
                profileChanged |= ImGui::InputFloat3("Hang##lhg", &profile.sittingIdleOffset.x, "%.4f");
                profileChanged |= ImGui::InputFloat3("Climb##lcl", &profile.sitStandUpOffset.x, "%.4f");
                ImGui::Spacing();
                profileChanged |= ImGui::SliderFloat("Blend##bd", &profile.sitBlendDuration, 0.0f, 1.0f, "%.2f s");
                break;

            default:
                ImGui::TextDisabled("Generic offsets (no type-specific schema):");
                profileChanged |= ImGui::InputFloat3("Offset A##oa", &profile.sitDownOffset.x, "%.4f");
                profileChanged |= ImGui::InputFloat3("Offset B##ob", &profile.sittingIdleOffset.x, "%.4f");
                profileChanged |= ImGui::InputFloat3("Offset C##oc", &profile.sitStandUpOffset.x, "%.4f");
                ImGui::Spacing();
                profileChanged |= ImGui::SliderFloat("Blend##bd", &profile.sitBlendDuration, 0.0f, 1.0f, "%.2f s");
                profileChanged |= ImGui::InputFloat("Height Offset##sh", &profile.seatHeightOffset, 0.0f, 0.0f, "%.4f");
                break;
        }

        if (profileChanged && interactionProfileManager) {
            interactionProfileManager->setProfile(archetype, assetName, def.pointId, profile);
        }
        ImGui::TreePop();
    }

    // === Preview controls ===
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::TreeNodeEx("Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
        // State display
        const char* stateStr = "Idle";
        switch (m_iePreviewState) {
            case InteractionPreviewState::SittingDown: stateStr = "Sitting Down..."; break;
            case InteractionPreviewState::SittingIdle: stateStr = "Seated Idle"; break;
            case InteractionPreviewState::AutoIdle:    stateStr = "Seated (auto)..."; break;
            case InteractionPreviewState::StandingUp:  stateStr = "Standing Up..."; break;
            default: stateStr = "Standing"; break;
        }
        ImGui::Text("State: %s", stateStr);
        ImGui::Spacing();

        // Auto preview button
        bool previewing = (m_iePreviewState != InteractionPreviewState::None);
        if (!previewing) {
            if (ImGui::Button("Auto Preview", ImVec2(-1, 0))) {
                ieStartPreview(true);
            }
        } else {
            if (ImGui::Button("Reset", ImVec2(-1, 0))) {
                ieStopPreview();
            }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Manual:");
        // Manual step buttons
        bool canSit = (m_iePreviewState == InteractionPreviewState::None);
        bool canStand = (m_iePreviewState == InteractionPreviewState::SittingIdle ||
                         m_iePreviewState == InteractionPreviewState::AutoIdle);
        if (!canSit) ImGui::BeginDisabled();
        if (ImGui::Button("Sit Down", ImVec2(135, 0))) {
            ieStartPreview(false);
        }
        if (!canSit) ImGui::EndDisabled();
        ImGui::SameLine();
        if (!canStand) ImGui::BeginDisabled();
        if (ImGui::Button("Stand Up", ImVec2(135, 0))) {
            if (m_ieChar) m_ieChar->standUp();
            m_iePreviewState = InteractionPreviewState::StandingUp;
        }
        if (!canStand) ImGui::EndDisabled();

        ImGui::TreePop();
    }

    // === Character modification ===
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::TreeNode("Character")) {
        // Archetype name editor
        ImGui::Text("Archetype:");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##archetype", m_ieArchetypeBuf, sizeof(m_ieArchetypeBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::string newArch(m_ieArchetypeBuf);
            if (!newArch.empty() && newArch != m_ieChar->getArchetype()) {
                // Save current profiles under old archetype first
                if (interactionProfileManager)
                    interactionProfileManager->saveArchetype(archetype);
                // Update character archetype
                m_ieChar->setArchetype(newArch);
                if (interactionManager)
                    interactionManager->setPlayerArchetype(newArch);
                // Update profile archetype to match
                m_ieProfileArchetype = newArch;
                archetype = newArch;
                // Load profiles for new archetype (or create empty)
                if (interactionProfileManager)
                    interactionProfileManager->loadArchetype(newArch);
                LOG_INFO("Application", "Interaction Editor: archetype changed to '{}'", newArch);
            }
        }
        ImGui::TextDisabled("Press Enter to apply.");

        // Bone scale sliders
        ImGui::Spacing();
        ImGui::Text("Bone Scales:");
        ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f), "(drag slider to resize bone)");

        const Phyxel::VoxelModel& model = m_ieChar->getVoxelModel();

        ImGui::BeginChild("IEBoneList", ImVec2(0, 250), true);
        for (auto& [boneId, boneName] : m_ieBodyBones) {
            bool selected = (m_ieSelectedBone == boneId);

            if (m_ieBoneScale.find(boneId) == m_ieBoneScale.end())
                m_ieBoneScale[boneId] = 1.0f;
            float& scale = m_ieBoneScale[boneId];

            std::string label = boneName;
            if (label.size() > 18) label = label.substr(0, 18);

            ImGui::PushID(boneId);
            if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(110, 0)))
                m_ieSelectedBone = boneId;
            ImGui::SameLine(115);
            ImGui::SetNextItemWidth(90.0f);
            bool changed = ImGui::SliderFloat("##s", &scale, 0.1f, 3.0f, "%.2f");
            if (changed) {
                Phyxel::VoxelModel newModel = m_ieChar->getVoxelModel();
                static std::map<intptr_t, Phyxel::VoxelModel> s_ieOrigModels;
                auto charKey = (intptr_t)m_ieChar;
                if (s_ieOrigModels.find(charKey) == s_ieOrigModels.end())
                    s_ieOrigModels[charKey] = m_ieChar->getVoxelModel();
                const Phyxel::VoxelModel& origModel = s_ieOrigModels[charKey];

                for (auto& shape : newModel.shapes) {
                    if (shape.boneId == boneId) {
                        for (const auto& origShape : origModel.shapes) {
                            if (origShape.boneId == boneId && origShape.offset == shape.offset) {
                                shape.size = origShape.size * scale;
                                break;
                            }
                        }
                    }
                }
                m_ieChar->setVoxelModel(newModel);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (ImGui::Button("Reset Scales")) {
            m_ieBoneScale.clear();
            Phyxel::AnimationSystem animSys;
            Phyxel::Skeleton skel;
            std::vector<Phyxel::AnimationClip> clips;
            Phyxel::VoxelModel origModel;
            std::string animFile = m_interactionEditorCharFile.empty()
                ? Core::AssetManager::instance().resolveAnimatedChar("humanoid.anim")
                : m_interactionEditorCharFile;
            if (animSys.loadFromFile(animFile, skel, clips, origModel))
                m_ieChar->setVoxelModel(origModel);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save .anim")) {
            ieSaveAnimModel();
        }

        ImGui::TreePop();
    }

    // === Save buttons ===
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Save Profile (.json) [Ctrl+S]", ImVec2(-1, 0))) {
        if (interactionProfileManager) {
            interactionProfileManager->saveArchetype(m_ieProfileArchetype);
            LOG_INFO("Application", "Interaction Editor: saved profile for '{}'", m_ieProfileArchetype);
        }
    }
    if (ImGui::Button("Save Interaction Defs (.txt)", ImVec2(-1, 0))) {
        if (objectTemplateManager) {
            objectTemplateManager->saveInteractionDefs(assetName, defs);
            LOG_INFO("Application", "Interaction Editor: saved asset defs for '{}'", assetName);
        }
    }
    if (ImGui::Button("Save Full Template (.voxel)", ImVec2(-1, 0))) {
        ieSaveAssetTemplate();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Ctrl+S: save profile");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "C/Ctrl+C/Alt+C: place voxel");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "LMB: break voxel");
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "V: toggle camera mode");

    ImGui::End();
}

// ============================================================================
// DOCKING / VIEWPORT
// ============================================================================

void Application::renderDockableViewport() {
    // Register or re-register the offscreen texture when image view changes (resize)
    if (renderCoordinator) {
        VkImageView imageView = renderCoordinator->getViewportImageView();
        VkSampler sampler = renderCoordinator->getViewportSampler();
        if (imageView != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE && imageView != m_viewportLastImageView) {
            if (m_viewportTextureId != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(m_viewportTextureId);
            }
            m_viewportTextureId = ImGui_ImplVulkan_AddTexture(
                sampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            m_viewportLastImageView = imageView;
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    bool viewportOpen = true;
    if (ImGui::Begin("Viewport", &viewportOpen, ImGuiWindowFlags_NoNav)) {
        ImVec2 size = ImGui::GetContentRegionAvail();

        if (m_viewportTextureId != VK_NULL_HANDLE && size.x > 0 && size.y > 0) {
            // Record content origin (screen coords of the image top-left)
            ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
            m_viewportPosX = cursorScreenPos.x;
            m_viewportPosY = cursorScreenPos.y;
            m_viewportSizeW = size.x;
            m_viewportSizeH = size.y;
            ImGui::Image((ImTextureID)m_viewportTextureId, size);
        }
        m_viewportHovered = ImGui::IsWindowHovered();
        m_viewportFocused = ImGui::IsWindowFocused();
    } else {
        m_viewportHovered = false;
        m_viewportFocused = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Application::setupDefaultDockLayout(unsigned int dockSpaceId) {
    if (m_dockLayoutInitialized) return;
    m_dockLayoutInitialized = true;

    // Only set up if no saved layout exists (imgui.ini will override on subsequent runs)
    ImGui::DockBuilderRemoveNode(dockSpaceId);
    ImGui::DockBuilderAddNode(dockSpaceId, ImGuiDockNodeFlags_DockSpace);
    
    int w = windowManager->getWidth();
    int h = windowManager->getHeight();
    ImGui::DockBuilderSetNodeSize(dockSpaceId, ImVec2(static_cast<float>(w), static_cast<float>(h)));

    // Split: center = viewport, bottom = console
    ImGuiID dockCenter = dockSpaceId;
    ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.25f, nullptr, &dockCenter);
    ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Right, 0.25f, nullptr, &dockCenter);
    ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Left, 0.20f, nullptr, &dockCenter);

    ImGui::DockBuilderDockWindow("Viewport", dockCenter);
    ImGui::DockBuilderDockWindow("Scripting Console", dockBottom);
    ImGui::DockBuilderDockWindow("Terminal", dockBottom);
    ImGui::DockBuilderDockWindow("Properties", dockRight);
    ImGui::DockBuilderDockWindow("Camera", dockRight);
    ImGui::DockBuilderDockWindow("Asset Editor", dockRight);
    ImGui::DockBuilderDockWindow("###AnimEditor", dockRight);
    ImGui::DockBuilderDockWindow("Interaction Editor", dockRight);
    ImGui::DockBuilderDockWindow("World Outliner", dockLeft);
    ImGui::DockBuilderDockWindow("Performance Profiler", dockRight);
    ImGui::DockBuilderDockWindow("Performance Overlay", dockRight);

    ImGui::DockBuilderFinish(dockSpaceId);
}

} // namespace Phyxel
