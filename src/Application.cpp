#include "Application.h"
#include "scene/VoxelInteractionSystem.h"
#include "utils/FileUtils.h"
#include "utils/Math.h"
#include "utils/PerformanceProfiler.h"
#include "utils/PerformanceMonitor.h"
#include "utils/Frustum.h"
#include "utils/Logger.h"
#include "utils/CoordinateUtils.h"
#include "examples/MultiChunkDemo.h"
#include "core/Chunk.h"
#include "physics/Material.h"
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

namespace VulkanCube {

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
    // Start with yaw = 90.0f to face towards the world (positive Z) instead of away
    camera = std::make_unique<Graphics::Camera>(glm::vec3(50.0f, 50.0f, 50.0f), glm::vec3(0.0f, 1.0f, 0.0f), 90.0f);
    camera->setMode(Graphics::CameraMode::ThirdPerson);

    // Create Player
    // Spawn high up to avoid falling through world before chunks load
    auto playerPtr = std::make_unique<Scene::Player>(physicsWorld.get(), inputManager.get(), camera.get(), glm::vec3(20, 50, 20));
    player = playerPtr.get();
    entities.push_back(std::move(playerPtr));

    // Create Enemy
    auto enemyPtr = std::make_unique<Scene::Enemy>(physicsWorld.get(), glm::vec3(25, 50, 25));
    enemyPtr->setTarget(player);
    entities.push_back(std::move(enemyPtr));
    
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

    m_initialized = true;
    return true;
}

void Application::run() {
    LOG_INFO("Application", "Starting VulkanCube application...");
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

        // Reset mouse delta at the end of the frame
        if (inputManager) {
            inputManager->resetMouseDelta();
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
    
    if (vulkanDevice) {
        vulkanDevice->cleanup();
    }
    
    if (physicsWorld) {
        physicsWorld->cleanup();
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
    this->deltaTime = deltaTime;
    
    // Update scripting system
    if (scriptingSystem) {
        scriptingSystem->update(deltaTime);
    }

    // Update entities
    for (auto& entity : entities) {
        entity->update(deltaTime);
    }

    // Camera sync is now handled by Player::update()
    
    // Input is processed in handleInput() which is called from run() loop
    // Update input controller logic (e.g. previews)
    if (inputController) {
        inputController->update(deltaTime);
    }

    // Update object template manager (for sequential spawning)
    if (objectTemplateManager) {
        objectTemplateManager->update(deltaTime);
    }
    
    // Update mouse hover detection via VoxelInteractionSystem
    if (voxelInteractionSystem) {
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
    }
    
    // Update chunks that have been modified (for hover color changes, etc.)
    // OPTIMIZED: Only update chunks that have actually changed
    if (chunkManager) {
        chunkManager->updateDirtyChunks();
        chunkManager->updateGlobalDynamicSubcubes(deltaTime);  // Update global dynamic subcube lifetimes
        chunkManager->updateGlobalDynamicCubes(deltaTime);     // Update global dynamic cube lifetimes
        chunkManager->updateGlobalDynamicMicrocubes(deltaTime); // Update global dynamic microcube lifetimes
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
    
    // Update dynamic subcube positions from physics bodies
    if (chunkManager) {
        // Update global dynamic subcubes (all dynamic subcubes are now global)
        chunkManager->updateGlobalDynamicSubcubePositions();
        // Update global dynamic cubes
        chunkManager->updateGlobalDynamicCubePositions();
        // Update global dynamic microcubes
        chunkManager->updateGlobalDynamicMicrocubePositions();
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
    if (camera && inputManager) {
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
        } else {
            newMode = Graphics::CameraMode::FirstPerson;
            LOG_INFO("Application", "Switched to First Person Camera");
        }
        
        camera->setMode(newMode);
    }
}

} // namespace VulkanCube
