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
#include "core/DynamicCube.h"
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

Application::Application() 
    : windowManager(nullptr)
    , isRunning(false)
    , deltaTime(0.0f)
    , frameCount(0)
    , performanceProfiler(std::make_unique<PerformanceProfiler>())
    , performanceMonitor(std::make_unique<Utils::PerformanceMonitor>())
    , imguiRenderer(std::make_unique<UI::ImGuiRenderer>())
    , inputManager(std::make_unique<Input::InputManager>())
    , forceSystem(std::make_unique<ForceSystem>())
    , mouseVelocityTracker(std::make_unique<MouseVelocityTracker>()) {
    
    // Initialize profiling
    lastFrameTime = 0.0;
    fpsTimer = 0.0;
}

Application::~Application() {
    cleanup();
}

bool Application::initialize() {
    // Initialize logging system first
    Utils::Logger::loadConfig("logging.ini"); // Load config if exists
    LOG_INFO("Application", "Initializing VulkanCube Application...");
    LOG_INFO("Application", "Logging system initialized (check logging.ini for configuration)");

    // Initialize window first
    if (!initializeWindow()) {
        LOG_ERROR("Application", "Failed to initialize window!");
        return false;
    }

    // Create core components
    vulkanDevice = std::make_unique<Vulkan::VulkanDevice>();
    renderPipeline = std::make_unique<Vulkan::RenderPipeline>(*vulkanDevice);
    dynamicRenderPipeline = std::make_unique<Vulkan::RenderPipeline>(*vulkanDevice);
    sceneManager = std::make_unique<Scene::SceneManager>();
    physicsWorld = std::make_unique<Physics::PhysicsWorld>();
    timer = std::make_unique<Timer>();
    chunkManager = std::make_unique<ChunkManager>();

    // Initialize subsystems
    if (!initializeVulkan()) {
        LOG_ERROR("Application", "Failed to initialize Vulkan!");
        return false;
    }

    // Load shaders for pipelines (after Vulkan is initialized)
    if (!renderPipeline->loadShaders("shaders/cube.vert.spv", "shaders/cube.frag.spv")) {
        LOG_ERROR("Application", "Failed to load static pipeline shaders!");
        return false;
    }
    
    if (!dynamicRenderPipeline->loadShaders("shaders/dynamic_subcube.vert.spv", "shaders/cube.frag.spv")) {
        LOG_ERROR("Application", "Failed to load dynamic pipeline shaders!");
        return false;
    }

    // Initialize texture atlas system
    if (!initializeTextureAtlas()) {
        LOG_ERROR("Application", "Failed to initialize texture atlas!");
        return false;
    }

    // Initialize chunk manager after Vulkan is ready
    chunkManager->initialize(vulkanDevice->getDevice(), vulkanDevice->getPhysicalDevice());
    
    // Set physics world for proper cleanup of dynamic objects
    chunkManager->setPhysicsWorld(physicsWorld.get());
    
    // Initialize world storage for persistent chunks
    if (!chunkManager->initializeWorldStorage("worlds/default.db")) {
        LOG_WARN("Application", "Failed to initialize world storage. Using temporary chunks.");
    }
    
    // Create chunks
    //auto origins = MultiChunkDemo::createLinearChunks(10);
    //auto origins = MultiChunkDemo::createGridChunks(5, 5);
    auto origins = MultiChunkDemo::create3DGridChunks(2, 2, 2);
    
    // Debug: Print chunk origins
    LOG_DEBUG("Application", "=== CHUNK ORIGINS ===");
    for (size_t i = 0; i < origins.size(); ++i) {
        LOG_DEBUG_FMT("Application", "Chunk " << i << " origin: (" << origins[i].x << "," << origins[i].y << "," << origins[i].z << ")");
    }
    LOG_DEBUG("Application", "=====================");
    
    // Load chunks from database or generate if they don't exist
    for (const auto& origin : origins) {
        glm::ivec3 chunkCoord = chunkManager->worldToChunkCoord(origin);
        chunkManager->generateOrLoadChunk(chunkCoord);
    }
    
    // Rebuild faces for all chunks AFTER all are loaded (critical for cross-chunk culling)
    chunkManager->rebuildAllChunkFaces();
    
    // Initialize hash maps for optimized O(1) hover detection
    chunkManager->initializeAllChunkVoxelMaps();
    
    // Calculate face culling optimization after chunk creation (DISABLED for now)
    // chunkManager->calculateChunkFaceCulling();
    
    // Cross-chunk occlusion culling is now handled in rebuildAllChunkFaces()
    // chunkManager->performOcclusionCulling();
    
    LOG_INFO_FMT("Application", "Created " << chunkManager->chunks.size() << " chunks for testing");

    // if (!initializeScene()) {
    //     std::cerr << "Failed to initialize scene!" << std::endl;
    //     return false;
    // }

    // Initialize input manager and register actions
    if (!inputManager->initialize(windowManager->getHandle())) {
        LOG_ERROR("Application", "Failed to initialize input manager!");
        return false;
    }
    
    // Set up initial camera position and orientation
    inputManager->setCameraPosition(glm::vec3(50.0f, 50.0f, 50.0f));
    glm::vec3 lookAt = glm::normalize(glm::vec3(16.0f, 16.0f, 16.0f) - glm::vec3(50.0f, 50.0f, 50.0f));
    inputManager->setCameraFront(lookAt);
    inputManager->setYawPitch(-135.0f, -30.0f);
    
    // Register mouse position callback for mouse velocity tracker
    inputManager->setMousePositionCallback([this](double x, double y) {
        mouseVelocityTracker->updatePosition(x, y);
    });
    
    // Register all input actions
    initializeInputActions();

    if (!initializePhysics()) {
        LOG_ERROR("Application", "Failed to initialize physics!");
        return false;
    }

    // Create physics bodies for all chunks AFTER physics world is initialized
    // Each chunk will be a compound shape made from individual cube collision boxes
    LOG_INFO("Application", "Creating chunk physics bodies...");
    for (auto& chunk : chunkManager->chunks) {
        chunk->setPhysicsWorld(physicsWorld.get());
        chunk->createChunkPhysicsBody();
    }

    if (!loadAssets()) {
        LOG_ERROR("Application", "Failed to load assets!");
        return false;
    }

    // Initialize ImGui after Vulkan is fully set up
    if (!imguiRenderer->initialize(windowManager->getHandle(), vulkanDevice.get(), renderPipeline.get())) {
        LOG_ERROR("Application", "Failed to initialize ImGui!");
        return false;
    }

    // Initialize VoxelInteractionSystem after all dependencies are created
    voxelInteractionSystem = std::make_unique<VoxelInteractionSystem>(
        chunkManager.get(),
        physicsWorld.get(),
        mouseVelocityTracker.get(),
        windowManager.get(),
        forceSystem.get()
    );
    LOG_INFO("Application", "VoxelInteractionSystem initialized successfully!");

    // Initialize RenderCoordinator after all rendering dependencies are created
    renderCoordinator = std::make_unique<Graphics::RenderCoordinator>(
        vulkanDevice.get(),
        renderPipeline.get(),
        dynamicRenderPipeline.get(),
        imguiRenderer.get(),
        windowManager.get(),
        inputManager.get(),
        chunkManager.get(),
        performanceMonitor.get(),
        performanceProfiler.get()
    );
    renderCoordinator->setMaxChunkRenderDistance(maxChunkRenderDistance);
    renderCoordinator->setChunkInclusionDistance(chunkInclusionDistance);
    LOG_INFO("Application", "RenderCoordinator initialized successfully!");

    timer->start();
    LOG_INFO("Application", "Application initialized successfully!");
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
        
        {
            ScopedTimer renderTimer(*performanceProfiler, "render");
            // Pass frameStartTime to RenderCoordinator
            renderCoordinator->setFrameStartTime(frameStartTime);
            render();
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
        imguiRenderer->renderForceSystemDebug(
            debugFlags.showForceSystemDebug,
            forceSystem.get(),
            mouseVelocityTracker.get(),
            voxelInteractionSystem->hasHoveredCube(),
            voxelInteractionSystem->hasHoveredCube() ? glm::vec3(voxelInteractionSystem->getCurrentHoveredLocation().worldPos) : glm::vec3(0.0f),
            debugFlags.manualForceValue
        );
        
        // Check if distances were changed by UI
        if (currentRenderDistance != maxChunkRenderDistance) {
            setRenderDistance(currentRenderDistance);
        }
        if (currentChunkInclusionDistance != chunkInclusionDistance) {
            setChunkInclusionDistance(currentChunkInclusionDistance);
        }
        
        // End ImGui frame
        imguiRenderer->endFrame();
        
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
    }
    
    LOG_INFO("Application", "Application shutting down...");
}

void Application::cleanup() {
    // Cleanup ImGui first (requires Vulkan device to still be active)
    if (imguiRenderer) {
        imguiRenderer->cleanup();
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
        chunkManager->saveDirtyChunks();
        chunkManager->cleanup();
    }
    
    if (vulkanDevice) {
        vulkanDevice->cleanup();
    }
    
    if (sceneManager) {
        sceneManager->cleanup();
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
    sceneManager.reset();
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

bool Application::initializeVulkan() {
    // Create Vulkan instance first
    if (!vulkanDevice->createInstance()) {
        LOG_ERROR("Application", "Failed to create Vulkan instance!");
        return false;
    }

    // Setup debug messenger
    if (!vulkanDevice->setupDebugMessenger()) {
        LOG_ERROR("Application", "Failed to setup debug messenger!");
        return false;
    }

    // Create Vulkan surface with the window (must be done before device selection)
    if (!vulkanDevice->createSurface(windowManager->getHandle())) {
        LOG_ERROR("Application", "Failed to create Vulkan surface!");
        return false;
    }

    // Now pick physical device and create logical device
    if (!vulkanDevice->pickPhysicalDevice()) {
        LOG_ERROR("Application", "Failed to pick physical device!");
        return false;
    }

    if (!vulkanDevice->createLogicalDevice()) {
        LOG_ERROR("Application", "Failed to create logical device!");
        return false;
    }

    // Create swapchain
    if (!vulkanDevice->createSwapChain(windowManager->getWidth(), windowManager->getHeight())) {
        LOG_ERROR("Application", "Failed to create swapchain!");
        return false;
    }

    // Create render pass
    if (!renderPipeline->createRenderPass()) {
        LOG_ERROR("Application", "Failed to create render pass!");
        return false;
    }

    // Create depth resources
    if (!vulkanDevice->createDepthResources()) {
        LOG_ERROR("Application", "Failed to create depth resources!");
        return false;
    }

    // Create framebuffers
    if (!vulkanDevice->createFramebuffers(renderPipeline->getRenderPass())) {
        LOG_ERROR("Application", "Failed to create framebuffers!");
        return false;
    }

    // Load shaders for static pipeline
    if (!renderPipeline->loadShaders("shaders/cube.vert.spv", "shaders/cube.frag.spv")) {
        LOG_ERROR("Application", "Failed to load static graphics shaders!");
        return false;
    }

    // Load shaders for dynamic subcube pipeline
    if (!dynamicRenderPipeline->loadShaders("shaders/dynamic_subcube.vert.spv", "shaders/cube.frag.spv")) {
        LOG_ERROR("Application", "Failed to load dynamic subcube graphics shaders!");
        return false;
    }

    // TODO: Compute shader functionality for frustum culling (experimental/incomplete)
    /*
    if (!renderPipeline->loadComputeShader("shaders/frustum_cull.comp.spv")) {
        LOG_ERROR("Application", "Failed to load compute shader!");
        return false;
    }
    */

    // Create descriptor set layout before graphics pipeline
    if (!vulkanDevice->createDescriptorSetLayout()) {
        LOG_ERROR("Application", "Failed to create descriptor set layout!");
        return false;
    }

    if (!renderPipeline->createGraphicsPipeline()) {
        LOG_ERROR("Application", "Failed to create static graphics pipeline!");
        return false;
    }

    if (!dynamicRenderPipeline->createGraphicsPipelineForDynamicSubcubes()) {
        LOG_ERROR("Application", "Failed to create dynamic graphics pipeline!");
        return false;
    }

    // TODO: Compute pipeline functionality (experimental/incomplete)
    /*
    if (!renderPipeline->createComputePipeline()) {
        return false;
    }
    */

    // Note: Compute descriptor sets will be created after scene initialization
    // when we have actual AABB data to bind

    // Create command buffers
    if (!vulkanDevice->createCommandBuffers()) {
        LOG_ERROR("Application", "Failed to create command buffers!");
        return false;
    }

    // Create rendering buffers
    if (!vulkanDevice->createVertexBuffer()) {
        LOG_ERROR("Application", "Failed to create vertex buffer!");
        return false;
    }

    if (!vulkanDevice->createIndexBuffer()) {
        LOG_ERROR("Application", "Failed to create index buffer!");
        return false;
    }

    if (!vulkanDevice->createInstanceBuffer()) {
        LOG_ERROR("Application", "Failed to create instance buffer!");
        return false;
    }

    // Create dynamic subcube buffer (support up to 1000 dynamic subcubes)
    if (!vulkanDevice->createDynamicSubcubeBuffer(1000)) {
        LOG_ERROR("Application", "Failed to create dynamic subcube buffer!");
        return false;
    }

    // TODO: Frustum culling buffers functionality (experimental/incomplete)
    /*
    // Create frustum culling buffers (support up to 35,000 instances)
    if (!vulkanDevice->createFrustumCullingBuffers(35000)) {
        LOG_ERROR("Application", "Failed to create frustum culling buffers!");
        return false;
    }
    */

    // Create dynamic subcube buffer (support up to 1000 dynamic subcubes)
    if (!vulkanDevice->createDynamicSubcubeBuffer(1000)) {
        LOG_ERROR("Application", "Failed to create dynamic subcube buffer!");
        return false;
    }

    if (!vulkanDevice->createUniformBuffers()) {
        LOG_ERROR("Application", "Failed to create uniform buffers!");
        return false;
    }

    if (!vulkanDevice->createDescriptorPool()) {
        LOG_ERROR("Application", "Failed to create descriptor pool!");
        return false;
    }

    // TODO: Compute descriptor pool functionality (experimental/incomplete)
    /*
    if (!vulkanDevice->createComputeDescriptorPool()) {
        LOG_ERROR("Application", "Failed to create compute descriptor pool!");
        return false;
    }
    */

    if (!vulkanDevice->createDescriptorSets()) {
        LOG_ERROR("Application", "Failed to create descriptor sets!");
        return false;
    }

    // Create synchronization objects
    if (!vulkanDevice->createSyncObjects()) {
        LOG_ERROR("Application", "Failed to create sync objects!");
        return false;
    }

    LOG_INFO("Application", "Vulkan subsystem initialized successfully");
    return true;
}

bool Application::initializeTextureAtlas() {
    LOG_INFO("Application", "Initializing texture atlas system...");
    
    // Load the texture atlas
    if (!vulkanDevice->loadTextureAtlas("resources/textures/cube_atlas.png")) {
        LOG_ERROR("Application", "Failed to load texture atlas!");
        return false;
    }
    
    // Create the texture sampler
    if (!vulkanDevice->createTextureAtlasSampler()) {
        LOG_ERROR("Application", "Failed to create texture atlas sampler!");
        return false;
    }
    
    // Update descriptor sets with texture binding
    vulkanDevice->updateDescriptorSetsWithTexture();
    
    LOG_INFO("Application", "Texture atlas system initialized successfully");
    return true;
}

bool Application::initializeScene() {
    // Scene initialization now handled by ChunkManager
    // No need for separate scene manager initialization

    LOG_INFO("Application", "Scene subsystem initialized successfully");
    return true;
}

bool Application::initializePhysics() {
    if (!physicsWorld->initialize()) {
        return false;
    }

    LOG_INFO("Application", "Physics subsystem initialized successfully (static cubes excluded)");
    return true;
}

bool Application::loadAssets() {
    // Load textures, models, etc.
    // For now, just return true as we have basic cube rendering
    
    LOG_INFO("Application", "Assets loaded successfully");
    return true;
}

void Application::update(float deltaTime) {
    this->deltaTime = deltaTime;
    
    // Input is processed in handleInput() which is called from run() loop
    
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
        voxelInteractionSystem->setDebugFlags(
            debugFlags.hoverDetection,
            debugFlags.disableBreakingForces,
            debugFlags.showForceSystemDebug,
            debugFlags.manualForceValue
        );
    }
    
    // Update chunks that have been modified (for hover color changes, etc.)
    // OPTIMIZED: Only update chunks that have actually changed
    if (chunkManager) {
        chunkManager->updateDirtyChunks();
        chunkManager->updateGlobalDynamicSubcubes(deltaTime);  // Update global dynamic subcube lifetimes
        chunkManager->updateGlobalDynamicCubes(deltaTime);     // Update global dynamic cube lifetimes
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
    }
    
    static int frameCount = 0;
    if (frameCount % 60 == 0) { // Log every 60 frames
        //std::cout << "[PHYSICS STEP] Using FIXED timestep: " << (1.0f/60.0f) << "s (60Hz), frame deltaTime: " << deltaTime << std::endl;
    }
    frameCount++;

    // Physics is handled by ChunkManager for dynamic objects
    // No need for separate scene manager physics sync
    
    // NOTE: Frustum culling is now handled in renderStaticGeometry()

    // Update view and projection matrices for Vulkan rendering
    glm::vec3 cameraPos = inputManager->getCameraPosition();
    glm::vec3 cameraFront = inputManager->getCameraFront();
    glm::vec3 cameraUp = inputManager->getCameraUp();
    
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
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

bool Application::initializeWindow() {
    LOG_INFO("Application", "Initializing window");
    
    windowManager = std::make_unique<UI::WindowManager>();
    
    if (!windowManager->initialize(1200, 720, "Phyxel - Voxel Physics Engine")) {
        LOG_ERROR("Application", "Failed to initialize window manager");
        return false;
    }
    
    // Register resize callback
    windowManager->setResizeCallback([this](int w, int h) {
        LOG_DEBUG("Application", "Window resized to {}x{}", w, h);
        projectionMatrixNeedsUpdate = true;
    });
    
    // Set GLFW window user pointer for callbacks (Application*, not WindowManager*)
    glfwSetWindowUserPointer(windowManager->getHandle(), this);
    
    LOG_INFO("Application", "Window initialization complete");
    return true;
}

void Application::initializeInputActions() {
    // Keyboard actions
    
    // ESC - Exit application
    inputManager->registerAction(GLFW_KEY_ESCAPE, "Exit", [this]() {
        LOG_INFO("Application", "ESC pressed - requesting shutdown");
        glfwSetWindowShouldClose(windowManager->getHandle(), true);
    });
    
    // F1 - Toggle performance overlay
    inputManager->registerAction(GLFW_KEY_F1, "Toggle Performance Overlay", [this]() {
        togglePerformanceOverlay();
    });
    
    // F2 - Save world (placeholder - functionality removed)
    inputManager->registerAction(GLFW_KEY_F2, "Save World", [this]() {
        LOG_INFO("Application", "World save functionality not yet implemented in refactored code");
    });
    
    // F3 - Toggle force debug visualization
    inputManager->registerAction(GLFW_KEY_F3, "Toggle Force Debug", [this]() {
        debugFlags.showForceSystemDebug = !debugFlags.showForceSystemDebug;
        LOG_INFO("Application", std::string("Force debug visualization: ") + 
                 (debugFlags.showForceSystemDebug ? "ENABLED" : "DISABLED"));
    });
    
    // T - Test camera positions
    inputManager->registerAction(GLFW_KEY_T, "Test Camera Positions", [this]() {
        static int cameraPosition = 0;
        cameraPosition = (cameraPosition + 1) % 3;
        
        switch(cameraPosition) {
            case 0:
                inputManager->setCameraPosition(glm::vec3(50.0f, 50.0f, 50.0f));
                LOG_INFO("Application", "Camera: Far view");
                break;
            case 1:
                inputManager->setCameraPosition(glm::vec3(20.0f, 20.0f, 20.0f));
                LOG_INFO("Application", "Camera: Medium view");
                break;
            case 2:
                inputManager->setCameraPosition(glm::vec3(10.0f, 10.0f, 10.0f));
                LOG_INFO("Application", "Camera: Close view");
                break;
        }
    });
    
    // G - Spawn dynamic subcube (placeholder - functionality removed)
    inputManager->registerAction(GLFW_KEY_G, "Spawn Dynamic Subcube", [this]() {
        glm::vec3 spawnPos = inputManager->getCameraPosition() + inputManager->getCameraFront() * 5.0f;
        LOG_INFO_FMT("Application", "Dynamic spawn at " << spawnPos.x 
                     << ", " << spawnPos.y << ", " << spawnPos.z << " not yet implemented");
    });
    
    // C - Place cube (placeholder - functionality removed)
    inputManager->registerAction(GLFW_KEY_C, "Place Cube", [this]() {
        glm::vec3 cubePos = inputManager->getCameraPosition() + inputManager->getCameraFront() * 3.0f;
        glm::ivec3 worldPos = glm::ivec3(cubePos);
        glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(worldPos);
        glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
        
        LOG_INFO_FMT("Application", "Cube placement at world " << worldPos.x 
                     << ", " << worldPos.y << ", " << worldPos.z << " not yet implemented");
    });
    
    // P - Debug coordinates at camera position
    inputManager->registerAction(GLFW_KEY_P, "Debug Coordinates", [this]() {
        glm::vec3 pos = inputManager->getCameraPosition();
        glm::ivec3 worldPos = glm::ivec3(pos);
        glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(worldPos);
        glm::ivec3 localPos = Utils::CoordinateUtils::worldToLocalCoord(worldPos);
        
        LOG_INFO_FMT("Application", "Camera World Position: " << pos.x << ", " << pos.y << ", " << pos.z);
        LOG_INFO_FMT("Application", "World Coord (int): " << worldPos.x << ", " << worldPos.y << ", " << worldPos.z);
        LOG_INFO_FMT("Application", "Chunk Coord: " << chunkCoord.x << ", " << chunkCoord.y << ", " << chunkCoord.z);
        LOG_INFO_FMT("Application", "Local Coord: " << localPos.x << ", " << localPos.y << ", " << localPos.z);
    });
    
    // O - Toggle breaking forces
    inputManager->registerAction(GLFW_KEY_O, "Toggle Breaking Forces", [this]() {
        debugFlags.disableBreakingForces = !debugFlags.disableBreakingForces;
        LOG_INFO("Application", std::string("Breaking forces: ") + 
                 (debugFlags.disableBreakingForces ? "DISABLED" : "ENABLED"));
    });
    
    // Mouse actions
    
    // Left click - Break cube/subcube
    inputManager->registerMouseAction(GLFW_MOUSE_BUTTON_LEFT, 0, "Break Cube", [this]() {
        // Check if we're hovering over a subcube or regular cube
        if (voxelInteractionSystem->hasHoveredCube()) {
            if (voxelInteractionSystem->getCurrentHoveredLocation().isSubcube) {
                // Break subcube with physics
                voxelInteractionSystem->breakHoveredSubcube();
            } else {
                // Break regular cube into dynamic cube with physics
                voxelInteractionSystem->breakHoveredCube(inputManager->getCameraPosition());
            }
        }
    });
    
    // Ctrl + Left click - Subdivide cube
    inputManager->registerMouseAction(GLFW_MOUSE_BUTTON_LEFT, GLFW_MOD_CONTROL, "Subdivide Cube", [this]() {
        voxelInteractionSystem->subdivideHoveredCube();
    });
    
    // Middle click - Subdivide cube
    inputManager->registerMouseAction(GLFW_MOUSE_BUTTON_MIDDLE, 0, "Subdivide Cube (Middle)", [this]() {
        voxelInteractionSystem->subdivideHoveredCube();
    });
    
    LOG_INFO("Application", "Input actions registered successfully");
}

void Application::togglePerformanceOverlay() {
    showPerformanceOverlay = !showPerformanceOverlay;
    LOG_INFO_FMT("Application", "Performance Overlay: " << (showPerformanceOverlay ? "ENABLED" : "DISABLED"));
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

} // namespace VulkanCube
