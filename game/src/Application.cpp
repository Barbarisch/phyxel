#include "Application.h"
#include "scene/VoxelInteractionSystem.h"
#include "scene/PhysicsCharacter.h"
#include "scene/SpiderCharacter.h"
#include "scene/AnimatedVoxelCharacter.h"
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
#include "physics/Material.h"
#include <imgui.h>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstring>
#include <limits>
#include <random>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
 * 1. Core systems (window, input, profiling) - initialized here with make_unique
 * 2. Graphics/physics systems - initialized in initialize() method
 * 3. World setup - delegated to WorldInitializer
 * 
 * OWNERSHIP PATTERN:
 * Application owns all major subsystems via unique_ptr.
 * Subsystems receive raw pointers to dependencies (non-owning).
 * This creates clear ownership hierarchy and prevents circular dependencies.
 * 
 * PROFILING SYSTEMS:
 * - PerformanceProfiler: Detailed timing breakdown (frame sections)
 * - PerformanceMonitor: High-level metrics (FPS, frame time)
 * Both are created early for startup profiling.
 */
Application::Application() 
    : windowManager(nullptr)     // Created in initialize() after subsystems ready
    , isRunning(false)            // Set to true in run() method
    , deltaTime(0.0f)             // Updated each frame
    , frameCount(0)               // Incremented each frame
    , performanceProfiler(std::make_unique<PerformanceProfiler>())
    , performanceMonitor(std::make_unique<Utils::PerformanceMonitor>())
    , imguiRenderer(std::make_unique<UI::ImGuiRenderer>())
    , inputManager(std::make_unique<Input::InputManager>())
    , forceSystem(std::make_unique<ForceSystem>())
    , mouseVelocityTracker(std::make_unique<MouseVelocityTracker>()) {
    
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
 * INITIALIZATION SEQUENCE:
 * 1. Create core components (window, vulkan, physics, timing)
 * 2. Create WorldInitializer with ALL dependencies injected
 * 3. Delegate world setup to WorldInitializer (chunks, camera, etc.)
 * 4. Create VoxelInteractionSystem (requires initialized world)
 * 5. Create RenderCoordinator (requires initialized rendering pipeline)
 * 6. Register input actions (keyboard/mouse bindings)
 * 
 * DEPENDENCY INJECTION PATTERN:
 * All subsystems receive dependencies via constructor (raw pointers).
 * Application retains ownership (unique_ptr), subsystems just reference.
 * This makes testing easier (can inject mocks) and clarifies data flow.
 * 
 * WHY WorldInitializer?
 * Extracted from monolithic Application::initialize() to separate concerns:
 * - Application: High-level coordination and ownership
 * - WorldInitializer: Detailed world setup (chunks, camera, materials)
 * Reduces Application.cpp size and improves maintainability.
 * 
 * @return true if initialization successful, false on error
 */
bool Application::initialize() {
    // STEP 1: CREATE CORE COMPONENTS
    // These are the foundational systems that other subsystems depend on
    windowManager = std::make_unique<UI::WindowManager>();                    // GLFW window and input handling
    vulkanDevice = std::make_unique<Vulkan::VulkanDevice>();                  // Vulkan instance, device, swapchain
    renderPipeline = std::make_unique<Vulkan::RenderPipeline>(*vulkanDevice); // Static voxel rendering pipeline
    dynamicRenderPipeline = std::make_unique<Vulkan::RenderPipeline>(*vulkanDevice); // Dynamic physics-enabled pipeline
    physicsWorld = std::make_unique<Physics::PhysicsWorld>();                 // Bullet Physics simulation
    timer = std::make_unique<Timer>();                                        // High-precision timing
    chunkManager = std::make_unique<ChunkManager>();                          // Multi-chunk world manager
    audioSystem = std::make_unique<Core::AudioSystem>();                      // Audio System

    if (!audioSystem->initialize()) {
        LOG_ERROR("Application", "Failed to initialize AudioSystem");
        // We continue even if audio fails, it's not critical
    }

    // STEP 2: CREATE WorldInitializer WITH DEPENDENCY INJECTION
    // WorldInitializer handles complex world setup (chunks, camera, materials, pipelines)
    // We inject ALL dependencies it needs via constructor (Dependency Injection pattern)
    // 
    // INJECTED DEPENDENCIES (13 subsystems):
    // - windowManager: Window/input for camera controls and rendering
    // - inputManager: Keyboard/mouse input processing
    // - vulkanDevice: GPU device and resource management
    // - renderPipeline: Static voxel rendering (grid-aligned cubes)
    // - dynamicRenderPipeline: Dynamic voxel rendering (physics-enabled)
    // - physicsWorld: Bullet Physics simulation
    // - timer: High-precision frame timing
    // - chunkManager: Multi-chunk world coordination
    // - forceSystem: Physics force application system
    // - mouseVelocityTracker: Mouse velocity for throwing objects
    // - performanceProfiler: Detailed timing breakdown
    // - performanceMonitor: High-level FPS/frame time metrics
    // - imguiRenderer: ImGui UI rendering
    worldInitializer = std::make_unique<Core::WorldInitializer>(
        windowManager.get(),
        inputManager.get(),
        vulkanDevice.get(),
        renderPipeline.get(),
        dynamicRenderPipeline.get(),
        physicsWorld.get(),
        timer.get(),
        chunkManager.get(),
        forceSystem.get(),
        mouseVelocityTracker.get(),
        performanceProfiler.get(),
        performanceMonitor.get(),
        imguiRenderer.get()
    );

    // Check World Storage status
#ifdef ENABLE_WORLD_STORAGE
    LOG_INFO("Application", "World Storage is ENABLED (SQLite3 support active)");
#else
    LOG_WARN("Application", "World Storage is DISABLED (SQLite3 not found or disabled)");
#endif

    // Configure render distances (controls how many chunks are visible)
    worldInitializer->setMaxChunkRenderDistance(maxChunkRenderDistance);
    worldInitializer->setChunkInclusionDistance(chunkInclusionDistance);

    // STEP 3: DELEGATE WORLD INITIALIZATION
    // WorldInitializer.initialize() handles:
    // - Window creation and Vulkan surface setup
    // - Shader compilation and pipeline configuration
    // - Camera initialization with sensible defaults
    // - Chunk creation and voxel population
    // - Physics material configuration
    // - ImGui setup for debug UI
    if (!worldInitializer->initialize()) {
        LOG_ERROR("Application", "WorldInitializer failed!");
        return false;
    }

    // Initialize last camera position to avoid velocity spike
    lastCameraPos = inputManager->getCameraPosition();

    // STEP 4: CREATE VoxelInteractionSystem (DEPENDS ON INITIALIZED WORLD)
    // This system handles mouse picking and voxel manipulation (breaking, subdividing)
    // Must be created AFTER WorldInitializer because it needs:
    // - Initialized ChunkManager with chunks
    // - Configured PhysicsWorld
    // - Ready WindowManager for screen-to-world ray conversion
    voxelInteractionSystem = std::make_unique<VoxelInteractionSystem>(
        chunkManager.get(),
        physicsWorld.get(),
        mouseVelocityTracker.get(),
        windowManager.get(),
        forceSystem.get(),
        audioSystem.get()
    );
    LOG_INFO("Application", "VoxelInteractionSystem initialized successfully!");

    // STEP 4.5: CREATE ObjectTemplateManager
    objectTemplateManager = std::make_unique<ObjectTemplateManager>(
        chunkManager.get(),
        &chunkManager->m_dynamicObjectManager
    );
    objectTemplateManager->loadTemplates("resources/templates");
    LOG_INFO("Application", "ObjectTemplateManager initialized successfully!");

    // STEP 5: CREATE RaycastVisualizer (DEBUG VISUALIZATION)
    // This system visualizes raycast operations for debugging
    raycastVisualizer = std::make_unique<RaycastVisualizer>();
    raycastVisualizer->initialize(vulkanDevice.get());
    LOG_INFO("Application", "RaycastVisualizer initialized successfully!");

    // STEP 5.5: INITIALIZE ENTITIES
    // Create Camera
    // Position camera to see the spider and physics character
    // Spider is at (35, 50, 35), PhysicsCharacter at (30, 50, 30)
    // Place camera at (45, 55, 45) looking back at them
    camera = std::make_unique<Graphics::Camera>(glm::vec3(45.0f, 55.0f, 45.0f), glm::vec3(0.0f, 1.0f, 0.0f), -135.0f, -30.0f);
    camera->setMode(Graphics::CameraMode::Free);
    
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
        vulkanDevice.get(),
        renderPipeline.get(),
        dynamicRenderPipeline.get(),
        imguiRenderer.get(),
        windowManager.get(),
        inputManager.get(),
        camera.get(),
        chunkManager.get(),
        performanceMonitor.get(),
        performanceProfiler.get(),
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
        inputManager.get(),
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
    apiServer = std::make_unique<Core::EngineAPIServer>(apiCommandQueue.get(), 8090);

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

    // Start the API server
    if (apiServer->start()) {
        LOG_INFO("Application", "Engine HTTP API available at http://localhost:8090/api/status");
    } else {
        LOG_WARN("Application", "Failed to start HTTP API server (non-critical)");
    }

    m_initialized = true;
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
            timer.get(),
            performanceProfiler.get(),
            performanceMonitor->getCurrentFrameTiming(),
            performanceMonitor->getDetailedTimings(),
            physicsWorld.get(),
            inputManager->getCameraPosition(),
            frameCount,
            currentRenderDistance,         // Pass by reference to allow UI modification
            currentChunkInclusionDistance  // Pass by reference to allow UI modification
        );
        
        // Render Force System Debug overlay
        auto& flags = inputController->getDebugFlags();
        imguiRenderer->renderForceSystemDebug(
            flags.showForceSystemDebug,
            forceSystem.get(),
            mouseVelocityTracker.get(),
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
                // performanceMonitor->printProfilingInfo(fpsFrameCount, inputManager.get());
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
            // performanceMonitor->printPerformanceStats(timer.get(), timing, chunkManager.get(), physicsWorld.get());
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
    
    // Cleanup ImGui first (requires Vulkan device to still be active)
    if (imguiRenderer) {
        imguiRenderer->cleanup();
    }

    // Shutdown HTTP API server first (background thread must stop before systems go away)
    if (apiServer) {
        apiServer->stop();
        apiServer.reset();
    }
    apiCommandQueue.reset();
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
    
    if (renderPipeline) {
        renderPipeline->cleanup();
    }
    
    if (dynamicRenderPipeline) {
        dynamicRenderPipeline->cleanup();
    }
    
    // Cleanup chunk manager before Vulkan device
    if (chunkManager) {
        // Save only dirty chunks to database before cleanup for efficiency
        LOG_INFO("Application", "Saving modified chunks to database...");
        bool saveSuccess = chunkManager->saveDirtyChunks();
        if (saveSuccess) {
            LOG_INFO("Application", "Successfully saved dirty chunks to database");
        } else {
            LOG_ERROR("Application", "Failed to save dirty chunks to database");
        }
        chunkManager->cleanup();
    }
    
    // Reset render coordinator and voxel interaction system before Vulkan cleanup
    // These hold Vulkan resources that must be destroyed before the device
    renderCoordinator.reset();
    voxelInteractionSystem.reset();
    
    // Clear entities BEFORE physics cleanup — they hold raw pointers to physics bodies
    entities.clear();
    player = nullptr;
    physicsCharacter = nullptr;
    spiderCharacter = nullptr;
    animatedCharacter = nullptr;

    if (vulkanDevice) {
        vulkanDevice->cleanup();
    }
    
    if (physicsWorld) {
        physicsWorld->cleanup();
    }

    // Cleanup audio system
    if (audioSystem) {
        audioSystem.reset();
    }
    
    // Cleanup window
    windowManager.reset();  // Calls WindowManager destructor which handles cleanup
    
    // Reset unique_ptrs
    renderPipeline.reset();
    dynamicRenderPipeline.reset();
    vulkanDevice.reset();
    physicsWorld.reset();
    timer.reset();
    imguiRenderer.reset();
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
    auto physicsCharPtr = std::make_unique<Scene::PhysicsCharacter>(physicsWorld.get(), inputManager.get(), camera.get(), pos);
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
    auto spiderPtr = std::make_unique<Scene::SpiderCharacter>(physicsWorld.get(), pos);
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
    auto animatedCharPtr = std::make_unique<Scene::AnimatedVoxelCharacter>(physicsWorld.get(), pos);
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

    auto npc = std::make_unique<Scene::PhysicsCharacter>(physicsWorld.get(), inputManager.get(), camera.get(), spawnPos);
    npc->debugColor = glm::vec4(0.2f, 0.8f, 0.2f, 1.0f); // Green tint for AI NPCs
    npc->setControlActive(false); // AI-controlled, not player-controlled

    auto* rawPtr = npc.get();

    // Generate a unique ID for this NPC
    static int npcCounter = 0;
    std::string npcId = "ai_npc_" + std::to_string(npcCounter++);

    // Register with AI system using guard recipe as default
    if (aiSystem->createAINPC(rawPtr, npcId, "resources/recipes/characters/guard.yaml",
                               "You are a guard NPC in a voxel world. Be helpful but cautious.")) {
        LOG_INFO("Application", "Spawned AI NPC '" << npcId << "' at (" 
                 << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << ")");
    } else {
        LOG_WARN("Application", "AI NPC created but AI registration failed for: " << npcId);
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
                    }
                }

            } else if (cmd.action == "remove_voxel") {
                int x = cmd.params.value("x", 0);
                int y = cmd.params.value("y", 0);
                int z = cmd.params.value("z", 0);
                if (chunkManager) {
                    bool ok = chunkManager->removeCube(glm::ivec3(x, y, z));
                    response = {{"success", ok}, {"position", {{"x", x}, {"y", y}, {"z", z}}}};
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
