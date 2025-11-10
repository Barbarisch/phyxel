#include "Application.h"
#include "utils/FileUtils.h"
#include "utils/Math.h"
#include "utils/PerformanceProfiler.h"
#include "utils/Frustum.h"
#include "utils/Logger.h"
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
    : window(nullptr)
    , isRunning(false)
    , windowWidth(1200)
    , windowHeight(720)
    , windowTitle("VulkanCube - Refactored")
    , deltaTime(0.0f)
    , frameCount(0)
    , cameraPos(50.0f, 50.0f, 50.0f)  // Position camera outside the 32x32x32 grid
    , cameraFront(glm::normalize(glm::vec3(16.0f, 16.0f, 16.0f) - glm::vec3(50.0f, 50.0f, 50.0f)))  // Look at center of cube grid
    , cameraUp(0.0f, 1.0f, 0.0f)
    , yaw(-135.0f)
    , pitch(-30.0f)
    , lastX(windowWidth / 2.0f)
    , lastY(windowHeight / 2.0f)
    , firstMouse(true)
    , mouseCaptured(false)
    , currentMouseX(0.0)
    , currentMouseY(0.0)
    , lastHoveredCube(-1)
    , lastVisibleInstances(0)
    , lastCulledInstances(0)
    , performanceProfiler(std::make_unique<PerformanceProfiler>())
    , imguiRenderer(std::make_unique<UI::ImGuiRenderer>())
    , forceSystem(std::make_unique<ForceSystem>())
    , mouseVelocityTracker(std::make_unique<MouseVelocityTracker>()) {
    
    // Initialize frame timing
    frameTiming.cpuFrameTime = 0.0;
    frameTiming.gpuFrameTime = 0.0;
    frameTiming.vertexCount = 0;
    frameTiming.drawCalls = 0;
    frameTiming.culledInstances = 0;
    frameTiming.visibleInstances = 0;
    
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

    // Initialize camera after window is created
    initializeCamera();

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
    if (!imguiRenderer->initialize(window, vulkanDevice.get(), renderPipeline.get())) {
        LOG_ERROR("Application", "Failed to initialize ImGui!");
        return false;
    }

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
    
    while (isRunning && !glfwWindowShouldClose(window)) {
        frameStartTime = std::chrono::high_resolution_clock::now();
        
        // Start new frame profiling
        performanceProfiler->startFrame();
        
        double currentTime = glfwGetTime();
        deltaTime = currentTime - lastFrameTime;
        lastFrameTime = currentTime;
        
        // Poll GLFW events
        glfwPollEvents();
        
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
            frameTiming,
            detailedTimings,
            physicsWorld.get(),
            cameraPos,
            frameCount,
            currentRenderDistance,         // Pass by reference to allow UI modification
            currentChunkInclusionDistance  // Pass by reference to allow UI modification
        );
        
        // Render Force System Debug overlay
        imguiRenderer->renderForceSystemDebug(
            debugFlags.showForceSystemDebug,
            forceSystem.get(),
            mouseVelocityTracker.get(),
            hasHoveredCube,
            hasHoveredCube ? glm::vec3(currentHoveredLocation.worldPos) : glm::vec3(0.0f),
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
        
        // Profile the frame (legacy system)
        FrameTiming timing = profileFrame();
        frameTimings.push_back(timing);
        
        // Keep only last 60 frames for analysis
        if (frameTimings.size() > 60) {
            frameTimings.erase(frameTimings.begin());
        }
        
        updateFrameTiming();
        frameCount++;
        fpsFrameCount++;
        
        // Print detailed performance stats every second (only when overlay is disabled)
        if (currentTime - fpsTimer >= 1.0) {
            if (!showPerformanceOverlay) {
                // Temporarily disabled performance reports
                // printProfilingInfo(fpsFrameCount);
                // printDetailedTimings();
                
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
            //printPerformanceStats();
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
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
    
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
    windowWidth = width;
    windowHeight = height;
    projectionMatrixNeedsUpdate = true; // Invalidate cached projection matrix
}

void Application::setTitle(const std::string& title) {
    windowTitle = title;
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
    if (!vulkanDevice->createSurface(window)) {
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
    if (!vulkanDevice->createSwapChain(windowWidth, windowHeight)) {
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
    
    // Process input
    processInput();
    
    // Update mouse hover detection
    updateMouseHover();
    
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
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 proj = glm::perspective(
        glm::radians(45.0f), 
        static_cast<float>(windowWidth) / static_cast<float>(windowHeight), 
        0.1f, 
        maxChunkRenderDistance  // Use configurable render distance
    );
    proj[1][1] *= -1; // Flip Y for Vulkan
    
    // Store matrices for use in render functions
    cachedViewMatrix = view;
    cachedProjectionMatrix = proj;
}

void Application::render() {
    drawFrame();
}

size_t Application::renderStaticGeometry() {
    // Render static cubes and static subcubes using the standard pipeline
    // Bind graphics pipeline
    renderPipeline->bindGraphicsPipeline(vulkanDevice->getCommandBuffer(currentFrame));
    
    size_t renderedChunks = 0;
    
    // Draw indexed cubes using chunk manager with proper culling
    if (chunkManager && !chunkManager->chunks.empty()) {
        
        // LEVEL 1: Distance-based culling (sphere of influence)
        // LEVEL 2: Frustum culling (camera view)
        std::vector<size_t> visibleChunkIndices;
        
        for (size_t i = 0; i < chunkManager->chunks.size(); ++i) {
            const Chunk* chunk = chunkManager->chunks[i].get();
            
            // Skip chunks with no static faces (already optimized)
            if (chunk->getNumInstances() == 0) continue;
            
            // Get chunk bounding box
            glm::vec3 minBounds = chunk->getMinBounds();
            glm::vec3 maxBounds = chunk->getMaxBounds();
            glm::vec3 chunkCenter = (minBounds + maxBounds) * 0.5f;
            
            // LEVEL 1: Chunk inclusion distance culling (broader range for chunk loading)
            float distanceToCamera = glm::length(chunkCenter - cameraPos);
            if (distanceToCamera > chunkInclusionDistance) {
                continue; // Skip chunk - too far away even for loading
            }
            
            // LEVEL 2: Frustum culling (uses actual render distance)
            // Create AABB for frustum testing
            Utils::AABB chunkAABB(minBounds, maxBounds);
            
            // Extract frustum from current view/projection matrices (uses maxChunkRenderDistance)
            glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
            glm::mat4 proj = cachedProjectionMatrix;
            glm::mat4 viewProjection = proj * view;
            
            Utils::Frustum cameraFrustum;
            cameraFrustum.extractFromMatrix(viewProjection);
            
            // Test chunk against frustum (this uses the shorter render distance in projection matrix)
            if (!cameraFrustum.intersects(chunkAABB)) {
                continue; // Skip chunk - not visible in camera view
            }
            
            // Chunk passed both distance and frustum culling
            visibleChunkIndices.push_back(i);
        }
        
        // Render only the visible chunks
        for (size_t chunkIndex : visibleChunkIndices) {
            const Chunk* chunk = chunkManager->chunks[chunkIndex].get();
            
            // Bind this chunk's instance buffer
            VkBuffer instanceBuffers[] = {chunk->getInstanceBuffer()};
            VkDeviceSize instanceOffsets[] = {0};
            vkCmdBindVertexBuffers(vulkanDevice->getCommandBuffer(currentFrame), 1, 1, instanceBuffers, instanceOffsets);
            
            // Set chunk origin as push constants for world positioning
            glm::ivec3 worldOrigin = chunk->getWorldOrigin();
            glm::vec3 chunkBaseOffset(worldOrigin.x, worldOrigin.y, worldOrigin.z);
            vulkanDevice->pushConstants(currentFrame, renderPipeline->getGraphicsLayout(), chunkBaseOffset);
            
            // Draw this chunk's static geometry
            // LEVEL 3: Face culling already applied (only visible faces in buffer)
            vulkanDevice->drawIndexed(currentFrame, 36, chunk->getNumInstances());
            renderedChunks++;
        }
    }
    
    return renderedChunks;
}

void Application::renderDynamicSubcubes() {
    // Get all global dynamic subcube faces from ChunkManager
    // All dynamic subcubes (both G key spawned and broken from chunks) are now global
    const auto& allDynamicSubcubeFaces = chunkManager->getGlobalDynamicSubcubeFaces();
    
    // Debug output for dynamic object count
    static int debugFrameCount = 0;
    if (debugFrameCount % 60 == 0) { // Every 60 frames
        size_t subcubeCount = chunkManager->getGlobalDynamicSubcubeCount();
        size_t cubeCount = chunkManager->getGlobalDynamicCubeCount();
        // std::cout << "[DEBUG] Dynamic objects: " << subcubeCount << " subcubes, " << cubeCount 
        //           << " cubes, " << allDynamicSubcubeFaces.size() << " total faces" << std::endl;
    }
    debugFrameCount++;
    
    // Only render if we have dynamic subcube faces
    if (!allDynamicSubcubeFaces.empty()) {
        // Update dynamic subcube buffer
        vulkanDevice->updateDynamicSubcubeBuffer(allDynamicSubcubeFaces);
        
        // Bind dynamic render pipeline
        vkCmdBindPipeline(vulkanDevice->getCommandBuffer(currentFrame), 
                         VK_PIPELINE_BIND_POINT_GRAPHICS, 
                         dynamicRenderPipeline->getGraphicsPipeline());
        
        // Bind vertex and dynamic instance buffers
        vulkanDevice->bindDynamicSubcubeBuffer(currentFrame);
        vulkanDevice->bindIndexBuffer(currentFrame);
        
        // Bind descriptor sets for dynamic pipeline
        vulkanDevice->bindDescriptorSets(currentFrame, dynamicRenderPipeline->getGraphicsLayout());
        
        // No push constants needed for dynamic subcubes (world position is in instance data)
        // Draw all dynamic subcubes (6 indices per quad face)
        vulkanDevice->drawIndexed(currentFrame, 6, static_cast<uint32_t>(allDynamicSubcubeFaces.size()));
    }
}

void Application::drawFrame() {
    // Check if we need to recreate swapchain due to window resize
    if (vulkanDevice->getFramebufferResized()) {
        if (!vulkanDevice->recreateSwapChain(windowWidth, windowHeight, renderPipeline->getRenderPass())) {
            return; // Try again next frame
        }
    }

    // Wait for previous frame
    vulkanDevice->waitForFence(currentFrame);
    vulkanDevice->resetFence(currentFrame);

    // Acquire next image
    uint32_t imageIndex;
    VkResult result = vulkanDevice->acquireNextImage(currentFrame, &imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain is out of date, recreate it
        if (!vulkanDevice->recreateSwapChain(windowWidth, windowHeight, renderPipeline->getRenderPass())) {
            return; // Try again next frame
        }
        return; // Skip this frame and try again
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("Application", "Failed to acquire swapchain image!");
        return;
    }

    // ChunkManager handles its own data management - no instance buffer needed
    // Static chunk geometry is pre-built and doesn't change unless modified
    
    // Get chunk statistics for rendering
    auto chunkStats = chunkManager->getPerformanceStats();
    
    // Prepare uniform buffer data (optimized)
    auto uboStart = std::chrono::high_resolution_clock::now();
    
    // Cache projection matrix - only recalculate on window resize
    if (projectionMatrixNeedsUpdate) {
        cachedProjectionMatrix = glm::perspective(
            glm::radians(45.0f), 
            (float)windowWidth / (float)windowHeight, 
            0.1f, 
            maxChunkRenderDistance  // Use configurable render distance
        );
        cachedProjectionMatrix[1][1] *= -1; // Flip Y for Vulkan
        projectionMatrixNeedsUpdate = false;
    }

    // Use cached matrices from update()
    glm::mat4 view = cachedViewMatrix;
    glm::mat4 proj = cachedProjectionMatrix;
    auto uboEnd = std::chrono::high_resolution_clock::now();
    
    // Update uniform buffer with camera matrices
    auto uniformUploadStart = std::chrono::high_resolution_clock::now();
    
    // Track memory bandwidth for uniform buffer update
    size_t uniformBufferSize = sizeof(glm::mat4) * 2 + sizeof(uint32_t); // view + proj + cubeCount
    performanceProfiler->recordMemoryTransfer(uniformBufferSize);
    
    vulkanDevice->updateUniformBuffer(currentFrame, view, proj, static_cast<uint32_t>(chunkStats.totalCubes));
    auto uniformUploadEnd = std::chrono::high_resolution_clock::now();

    // Record command buffer
    auto recordStart = std::chrono::high_resolution_clock::now();
    vulkanDevice->resetCommandBuffer(currentFrame);
    vulkanDevice->beginCommandBuffer(currentFrame);
    
    // TODO: GPU frustum culling functionality (experimental/incomplete)
    /*
    // Perform GPU frustum culling BEFORE rendering
    auto frustumCullingStart = std::chrono::high_resolution_clock::now();
    vulkanDevice->dispatchFrustumCulling(currentFrame, renderPipeline.get(), static_cast<uint32_t>(cubeCount));
    auto frustumCullingEnd = std::chrono::high_resolution_clock::now();
    
    // Download GPU visibility results for performance stats (every 10 frames)
    static int cullStatsCounter = 0;
    if (++cullStatsCounter >= 10) {
        cullStatsCounter = 0;
        
        std::vector<uint32_t> visibilityResults = vulkanDevice->downloadVisibilityResults(static_cast<uint32_t>(cubeCount));
        
        // Calculate statistics
        uint32_t totalObjects = static_cast<uint32_t>(visibilityResults.size());
        uint32_t visibleObjects = 0;
        for (uint32_t visible : visibilityResults) {
            if (visible) visibleObjects++;
        }
        uint32_t culledObjects = totalObjects - visibleObjects;
        
        // Calculate GPU compute time in milliseconds
        double frustumCullingTimeMs = std::chrono::duration<double, std::milli>(
            frustumCullingEnd - frustumCullingStart
        ).count();
        
        // Record actual GPU frustum culling statistics
        // std::cout << "[DEBUG] Performance stats: Total=" << totalObjects 
        //           << ", Visible=" << visibleObjects 
        //           << ", Culled=" << culledObjects 
        //           << " (" << (100.0f * culledObjects / totalObjects) << "% culled)"
        //           << ", Time=" << frustumCullingTimeMs << "ms" << std::endl;
        // std::cout << "[DEBUG] Camera pos: (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")" << std::endl;
        
        // Store results for UI display
        lastVisibleInstances = visibleObjects;
        lastCulledInstances = culledObjects;
        
        performanceProfiler->recordFrustumCulling(totalObjects, culledObjects, frustumCullingTimeMs);
    }
    */
    
    // Record occlusion culling statistics from chunk manager
    if (chunkManager && !chunkManager->chunks.empty()) {
        // Get occlusion stats from chunk manager
        auto chunkStats = chunkManager->getPerformanceStats();
        frameTiming.fullyOccludedCubes = static_cast<int>(chunkStats.fullyOccludedCubes);
        frameTiming.partiallyOccludedCubes = static_cast<int>(chunkStats.partiallyOccludedCubes);
        frameTiming.totalHiddenFaces = static_cast<int>(chunkStats.totalHiddenFaces);
        frameTiming.occlusionCulledInstances = static_cast<int>(chunkStats.fullyOccludedCubes);
        frameTiming.faceCulledFaces = static_cast<int>(chunkStats.totalHiddenFaces);
    } else {
        // No chunks available
        frameTiming.fullyOccludedCubes = 0;
        frameTiming.partiallyOccludedCubes = 0;
        frameTiming.totalHiddenFaces = 0;
        frameTiming.occlusionCulledInstances = 0;
        frameTiming.faceCulledFaces = 0;
    }
    
    // Begin render pass
    vulkanDevice->beginRenderPass(currentFrame, imageIndex, renderPipeline->getRenderPass());
    
    // Bind graphics pipeline
    renderPipeline->bindGraphicsPipeline(vulkanDevice->getCommandBuffer(currentFrame));
    
    // Set viewport (required for dynamic viewport)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(windowWidth);
    viewport.height = static_cast<float>(windowHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(vulkanDevice->getCommandBuffer(currentFrame), 0, 1, &viewport);
    
    // Set scissor (required for dynamic scissor)
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight)};
    vkCmdSetScissor(vulkanDevice->getCommandBuffer(currentFrame), 0, 1, &scissor);
    
    // Bind vertex and instance buffers
    vulkanDevice->bindVertexBuffers(currentFrame);
    vulkanDevice->bindIndexBuffer(currentFrame);
    
    // Bind descriptor sets (uniform buffers)
    vulkanDevice->bindDescriptorSets(currentFrame, renderPipeline->getGraphicsLayout());
    
    // Draw using dual rendering system
    if (chunkManager && !chunkManager->chunks.empty()) {
        // Render static geometry first and capture how many chunks were actually rendered
        size_t actuallyRenderedChunks = renderStaticGeometry();
        
        // Render dynamic subcubes with separate pipeline
        renderDynamicSubcubes();
        
        // Get accurate performance statistics from chunk manager
        auto chunkStats = chunkManager->getPerformanceStats();
        
        // Update frame timing with chunk-based statistics using ACTUAL rendered chunks
        frameTiming.drawCalls = static_cast<int>(actuallyRenderedChunks);  // Only chunks that passed culling
        frameTiming.vertexCount = static_cast<int>(chunkStats.totalVertices);
        frameTiming.visibleInstances = static_cast<int>(chunkStats.totalCubes);
        frameTiming.fullyOccludedCubes = static_cast<int>(chunkStats.fullyOccludedCubes);
        frameTiming.partiallyOccludedCubes = static_cast<int>(chunkStats.partiallyOccludedCubes);
        frameTiming.totalHiddenFaces = static_cast<int>(chunkStats.totalHiddenFaces);
        frameTiming.faceCulledFaces = static_cast<int>(chunkStats.totalHiddenFaces);
        frameTiming.occlusionCulledInstances = static_cast<int>(chunkStats.fullyOccludedCubes);
        
        // Optional: Add culling statistics debug output
        static size_t lastRenderedChunks = 0;
        if (actuallyRenderedChunks != lastRenderedChunks) {
            LOG_DEBUG_FMT("Application", "[CULLING] Total chunks: " << chunkManager->chunks.size() 
                      << ", Rendered chunks: " << actuallyRenderedChunks 
                      << " (Culled: " << (chunkManager->chunks.size() - actuallyRenderedChunks) << ")");
            lastRenderedChunks = actuallyRenderedChunks;
        }
    } else {
        // No chunks available - render nothing
        frameTiming.drawCalls = 0;
        frameTiming.vertexCount = 0;
    }
    
    // Render ImGui on top
    imguiRenderer->render(currentFrame, imageIndex);
    
    // End render pass
    vulkanDevice->endRenderPass(currentFrame);
    vulkanDevice->endCommandBuffer(currentFrame);
    auto recordEnd = std::chrono::high_resolution_clock::now();

    // Submit command buffer
    auto submitStart = std::chrono::high_resolution_clock::now();
    if (!vulkanDevice->submitCommandBuffer(currentFrame)) {
        LOG_ERROR("Application", "Failed to submit command buffer!");
        return;
    }
    auto submitEnd = std::chrono::high_resolution_clock::now();

    // Present frame
    auto presentStart = std::chrono::high_resolution_clock::now();
    VkResult presentResult = vulkanDevice->presentFrame(imageIndex, currentFrame);
    
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || vulkanDevice->getFramebufferResized()) {
        // Recreate swapchain on next frame
        vulkanDevice->setFramebufferResized(true);
    } else if (presentResult != VK_SUCCESS) {
        LOG_ERROR("Application", "Failed to present frame!");
        return;
    }
    auto presentEnd = std::chrono::high_resolution_clock::now();

    currentFrame = (currentFrame + 1) % 2; // MAX_FRAMES_IN_FLIGHT = 2
    
    // Note: frameTiming statistics are now set in the chunk rendering section above
    // This includes: drawCalls, vertexCount, visibleInstances, culledInstances, etc.
    
    // Use GPU culling results if available for frustum culling statistics
    if (lastVisibleInstances + lastCulledInstances > 0) {
        frameTiming.frustumCulledInstances = static_cast<int>(lastCulledInstances);
    } else {
        frameTiming.frustumCulledInstances = 0;
    }
    
    // Record detailed timing
    auto frameEnd = std::chrono::high_resolution_clock::now();
    DetailedFrameTiming detailedTiming;
    detailedTiming.totalFrameTime = std::chrono::duration<double, std::milli>(frameEnd - frameStartTime).count();
    detailedTiming.physicsTime = 0.0; // Physics timing integrated into main loop
    detailedTiming.mousePickTime = 0.0; // Not implemented yet
    detailedTiming.uboFillTime = std::chrono::duration<double, std::milli>(uboEnd - uboStart).count();
    detailedTiming.instanceUpdateTime = 0.0; // ChunkManager handles its own data
    detailedTiming.drawCmdUpdateTime = 0.0; // Not separate in our implementation
    detailedTiming.uniformUploadTime = std::chrono::duration<double, std::milli>(uniformUploadEnd - uniformUploadStart).count();
    detailedTiming.occlusionCullingTime = 0.0; // Occlusion culling is done once at scene creation, not per-frame
    detailedTiming.commandRecordTime = std::chrono::duration<double, std::milli>(recordEnd - recordStart).count();
    detailedTiming.gpuSubmitTime = std::chrono::duration<double, std::milli>(submitEnd - submitStart).count();
    detailedTiming.presentTime = std::chrono::duration<double, std::milli>(presentEnd - presentStart).count();
    
    detailedTimings.push_back(detailedTiming);
    if (detailedTimings.size() > 60) {
        detailedTimings.erase(detailedTimings.begin());
    }
}

void Application::handleInput() {
    // Process keyboard and mouse input
    processInput();
}

void Application::updateFrameTiming() {
    frameTiming.cpuFrameTime = deltaTime * 1000.0; // Convert to milliseconds
    frameTiming.gpuFrameTime = deltaTime * 1000.0; // Placeholder
}

void Application::printPerformanceStats() {
    float fps = timer->getFPS();
    auto chunkStats = chunkManager->getPerformanceStats();
    
    LOG_INFO("Performance", "Performance Stats:");
    LOG_INFO_FMT("Performance", "  FPS: " << fps);
    LOG_INFO_FMT("Performance", "  CPU Frame Time: " << frameTiming.cpuFrameTime << "ms");
    LOG_INFO_FMT("Performance", "  GPU Frame Time: " << frameTiming.gpuFrameTime << "ms");
    LOG_INFO_FMT("Performance", "  Vertices: " << frameTiming.vertexCount);
    LOG_INFO_FMT("Performance", "  Draw Calls: " << frameTiming.drawCalls);
    LOG_INFO_FMT("Performance", "  Visible Instances: " << frameTiming.visibleInstances);
    LOG_INFO_FMT("Performance", "  Culled Instances: " << frameTiming.culledInstances);
    LOG_INFO_FMT("Performance", "  Total Chunks: " << chunkManager->chunks.size());
    LOG_INFO_FMT("Performance", "  Total Cubes: " << chunkStats.totalCubes);
    LOG_INFO_FMT("Performance", "  Physics Bodies: " << physicsWorld->getRigidBodyCount());
    LOG_INFO("Performance", "---");
}



bool Application::initializeWindow() {
    // Initialize GLFW
    if (!glfwInit()) {
        LOG_ERROR("Application", "Failed to initialize GLFW!");
        return false;
    }

    // Configure GLFW for Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);  // Enable window resizing

    // Create window
    window = glfwCreateWindow(windowWidth, windowHeight, windowTitle.c_str(), nullptr, nullptr);
    if (!window) {
        LOG_ERROR("Application", "Failed to create GLFW window!");
        glfwTerminate();
        return false;
    }

    // Set up window resize callback
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);

    LOG_INFO("Application", "Window initialized successfully");
    return true;
}

void Application::initializeCamera() {
    // Set up mouse callbacks
    glfwSetWindowUserPointer(window, this);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    
    // Keep cursor visible and free - no mouse capture
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    
    LOG_INFO("Application", "Camera controls initialized - hold right mouse button and drag to look around");
}

void Application::mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    Application* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
    
    // Always track mouse position for hover detection
    app->currentMouseX = xpos;
    app->currentMouseY = ypos;
    
    // Update mouse velocity tracker for force calculations
    app->mouseVelocityTracker->updatePosition(xpos, ypos);
    
    // Only process mouse movement for camera if right mouse button is held down
    if (!app->mouseCaptured) {
        app->lastX = xpos;
        app->lastY = ypos;
        return;
    }
    
    if (app->firstMouse) {
        app->lastX = xpos;
        app->lastY = ypos;
        app->firstMouse = false;
        return;
    }

    float xoffset = xpos - app->lastX;
    float yoffset = app->lastY - ypos;  // Reversed since y-coordinates go from bottom to top

    app->lastX = xpos;
    app->lastY = ypos;

    float sensitivity = 0.3f; // Increased sensitivity for more responsive movement
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    app->yaw += xoffset;
    app->pitch += yoffset;

    // Debug output to show mouse movement is being processed
    static int debugCounter = 0;
    if (++debugCounter % 10 == 0) { // Print every 10th movement to avoid spam
        LOG_TRACE_FMT("Application", "Camera look: yaw=" << std::fixed << std::setprecision(1) 
                  << app->yaw << "° pitch=" << app->pitch << "°");
    }

    // Constrain pitch
    if (app->pitch > 89.0f) app->pitch = 89.0f;
    if (app->pitch < -89.0f) app->pitch = -89.0f;

    // Update camera front vector
    glm::vec3 front;
    front.x = cos(glm::radians(app->yaw)) * cos(glm::radians(app->pitch));
    front.y = sin(glm::radians(app->pitch));
    front.z = sin(glm::radians(app->yaw)) * cos(glm::radians(app->pitch));
    app->cameraFront = glm::normalize(front);
}

void Application::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    Application* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
    
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            // Start camera rotation mode
            app->mouseCaptured = true;
            app->firstMouse = true; // Reset first mouse to avoid jump
            LOG_DEBUG("Application", "*** RIGHT MOUSE PRESSED - CAMERA LOOK MODE ENABLED ***");
        } else if (action == GLFW_RELEASE) {
            // Stop camera rotation mode
            app->mouseCaptured = false;
            LOG_DEBUG("Application", "*** RIGHT MOUSE RELEASED - CAMERA LOOK MODE DISABLED ***");
        }
    }
    
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        // Reset mouse velocity tracking for force calculation
        app->mouseVelocityTracker->reset();
        
        // Check for Ctrl modifier for subdivision
        if (mods & GLFW_MOD_CONTROL) {
            // Ctrl+Left Click: Subdivide cube into 27 subcubes
            app->subdivideHoveredCube();
        } else {
            // Left Click: Break objects with physics using force system
            if (app->hasHoveredCube && app->currentHoveredLocation.isSubcube) {
                // Break subcube with physics
                app->breakHoveredSubcube();  // ✅ NEW: Dedicated subcube breaking function
            } else {
                // Break regular cube into dynamic cube with physics (REVERTED TO WORKING VERSION)
                app->breakHoveredCube();
            }
        }
    }
    
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        // Middle Click: Subdivide cube into 27 subcubes
        app->subdivideHoveredCube();
    }
}

void Application::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    Application* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
    
    // Update window dimensions
    app->windowWidth = width;
    app->windowHeight = height;
    
    // Mark swapchain as needing recreation
    if (app->vulkanDevice) {
        app->vulkanDevice->setFramebufferResized(true);
    }
    
    // Update projection matrix
    app->projectionMatrixNeedsUpdate = true;
    
    LOG_INFO_FMT("Application", "Window resized to " << width << "x" << height);
}

void Application::processInput() {
    // Exit on ESC
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
    
    // Toggle performance overlay with F1
    static bool f1Pressed = false;
    if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_PRESS && !f1Pressed) {
        togglePerformanceOverlay();
        f1Pressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_F1) == GLFW_RELEASE) {
        f1Pressed = false;
    }
    
    // Save world to database with F2
    static bool f2Pressed = false;
    if (glfwGetKey(window, GLFW_KEY_F2) == GLFW_PRESS && !f2Pressed) {
        if (chunkManager) {
            LOG_INFO("Application", "Manual save triggered - saving world to database...");
            chunkManager->saveAllChunks();
            LOG_INFO("Application", "World saved successfully!");
        }
        f2Pressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_F2) == GLFW_RELEASE) {
        f2Pressed = false;
    }
    
    // Toggle force system debug overlay with F3
    static bool f3Pressed = false;
    if (glfwGetKey(window, GLFW_KEY_F3) == GLFW_PRESS && !f3Pressed) {
        debugFlags.showForceSystemDebug = !debugFlags.showForceSystemDebug;
        LOG_DEBUG_FMT("Application", "[DEBUG] Force System Debug overlay " 
                  << (debugFlags.showForceSystemDebug ? "ENABLED" : "DISABLED"));
        f3Pressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_F3) == GLFW_RELEASE) {
        f3Pressed = false;
    }
    
    // Test frustum culling positions with T key
    static bool tPressed = false;
    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS && !tPressed) {
        static int testPosition = 0;
        testPosition = (testPosition + 1) % 4;
        
        switch (testPosition) {
            case 0: // Outside view (default)
                cameraPos = glm::vec3(50.0f, 50.0f, 50.0f);
                cameraFront = glm::normalize(glm::vec3(16.0f, 16.0f, 16.0f) - cameraPos);
                LOG_DEBUG("Application", "[DEBUG] Test position 1: Outside view (should see most cubes)");
                break;
            case 1: // Inside grid, looking at corner
                cameraPos = glm::vec3(16.0f, 16.0f, 16.0f);
                cameraFront = glm::normalize(glm::vec3(31.0f, 31.0f, 31.0f) - cameraPos);
                LOG_DEBUG("Application", "[DEBUG] Test position 2: Inside grid, corner view (should cull ~50%)");
                break;
            case 2: // Ground level, looking up
                cameraPos = glm::vec3(16.0f, -5.0f, 16.0f);
                cameraFront = glm::normalize(glm::vec3(16.0f, 16.0f, 16.0f) - cameraPos);
                LOG_DEBUG("Application", "[DEBUG] Test position 3: Ground level (should cull most cubes)");
                break;
            case 3: // Edge view
                cameraPos = glm::vec3(-10.0f, 16.0f, 16.0f);
                cameraFront = glm::normalize(glm::vec3(31.0f, 16.0f, 16.0f) - cameraPos);
                LOG_DEBUG("Application", "[DEBUG] Test position 4: Edge view (should cull ~50%)");
                break;
        }
        tPressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_T) == GLFW_RELEASE) {
        tPressed = false;
    }
    
    // Spawn dynamic subcube above chunks with G key
    static bool gPressed = false;
    if (glfwGetKey(window, GLFW_KEY_G) == GLFW_PRESS && !gPressed) {
        spawnTestDynamicSubcube();
        gPressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_G) == GLFW_RELEASE) {
        gPressed = false;
    }
    
    // Place cube with C key (test cube placement system)
    static bool cPressed = false;
    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS && !cPressed) {
        placeNewCube();
        cPressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_C) == GLFW_RELEASE) {
        cPressed = false;
    }
    
    // Debug coordinate system with P key (simplified)
    static bool pPressed = false;
    if (glfwGetKey(window, GLFW_KEY_P) == GLFW_PRESS && !pPressed) {
        LOG_DEBUG("Application", "[COORD DEBUG] Simple coordinate test:");
        LOG_DEBUG_FMT("Application", "[COORD DEBUG] Camera position: (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")");
        LOG_DEBUG_FMT("Application", "[COORD DEBUG] Current hovered cube: " << (currentHoveredLocation.chunk ? "Found" : "None"));
        if (currentHoveredLocation.chunk) {
            LOG_DEBUG_FMT("Application", "[COORD DEBUG] Hovered world pos: (" 
                      << currentHoveredLocation.worldPos.x << ", " 
                      << currentHoveredLocation.worldPos.y << ", " 
                      << currentHoveredLocation.worldPos.z << ")");
        }
        pPressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_P) == GLFW_RELEASE) {
        pPressed = false;
    }
    
    // Toggle debug no-forces mode with O key
    static bool oPressed = false;
    if (glfwGetKey(window, GLFW_KEY_O) == GLFW_PRESS && !oPressed) {
        debugFlags.disableBreakingForces = !debugFlags.disableBreakingForces;
        LOG_DEBUG_FMT("Application", "[DEBUG] Breaking forces " << (debugFlags.disableBreakingForces ? "DISABLED" : "ENABLED") 
                  << " - cubes will spawn " << (debugFlags.disableBreakingForces ? "without impulse" : "with normal impulse"));
        oPressed = true;
    } else if (glfwGetKey(window, GLFW_KEY_O) == GLFW_RELEASE) {
        oPressed = false;
    }
    
    // Camera movement with WASD (always available)
    float cameraSpeed = 5.0f * deltaTime;

    // WASD movement
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) {
        cameraPos += cameraSpeed * cameraFront;
    }
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
        cameraPos -= cameraSpeed * cameraFront;
    }

    glm::vec3 right = glm::normalize(glm::cross(cameraFront, cameraUp));
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
        cameraPos -= right * cameraSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
        cameraPos += right * cameraSpeed;
    }
        
    // Vertical movement with Space and Shift
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
        cameraPos += cameraUp * cameraSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) {
        cameraPos -= cameraUp * cameraSpeed;
    }
}

void Application::spawnTestDynamicSubcube() {
    LOG_DEBUG("Application", "=== SPAWNING TEST DYNAMIC SUBCUBE ===");
    
    // Spawn above the center of the 2x2x2 chunk grid
    // Chunk grid spans from (0,0,0) to (64,64,64) with Y being UP
    // Center is (32,32,32), top surface is at Y=64
    // Spawn clearly above at center: (32,80,32) - well above all chunks in Y
    glm::vec3 spawnPosition = glm::vec3(32.0f, 80.0f, 32.0f);
    
    LOG_DEBUG_FMT("Application", "Spawn position: (" << spawnPosition.x << ", " << spawnPosition.y << ", " << spawnPosition.z << ")");
    LOG_DEBUG_FMT("Application", "Camera position: (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")");
    
    // Create a new dynamic subcube with raw pointer (matching existing pattern)
    Subcube* dynamicSubcube = new Subcube(spawnPosition, glm::vec3(1.0f, 0.0f, 0.0f)); // Red color
    
    // Create physics body for the dynamic subcube (match visual size)
    glm::vec3 subcubeSize = glm::vec3(1.0f / 3.0f); // Match visual subcube size
    
    LOG_DEBUG_FMT("Application", "[PHYSICS DEBUG] G-spawning subcube at world pos (" 
              << spawnPosition.x << "," << spawnPosition.y << "," << spawnPosition.z 
              << ") with physics size " << subcubeSize.x);
    
    btRigidBody* rigidBody = physicsWorld->createCube(spawnPosition, subcubeSize, 0.5f); // 0.5kg mass
    if (rigidBody) {
        dynamicSubcube->setRigidBody(rigidBody);
        
        // Add random angular velocity for tumbling effect
        btVector3 randomAngularVelocity(
            (rand() / float(RAND_MAX) - 0.5f) * 10.0f,  // Random angular velocity -5 to +5 rad/s
            (rand() / float(RAND_MAX) - 0.5f) * 10.0f,
            (rand() / float(RAND_MAX) - 0.5f) * 10.0f
        );
        rigidBody->setAngularVelocity(randomAngularVelocity);
        
        // IMPORTANT: Set initial physics position to match the physics body
        dynamicSubcube->setPhysicsPosition(spawnPosition);
        
        LOG_DEBUG_FMT("Application", "Created physics body for dynamic subcube with angular velocity: (" 
                  << randomAngularVelocity.x() << ", " << randomAngularVelocity.y() << ", " << randomAngularVelocity.z() << ")");
    } else {
        LOG_ERROR("Application", "Failed to create physics body for dynamic subcube");
    }
    
    // Add to global dynamic subcube management (not tied to any specific chunk)
    if (chunkManager) {
        chunkManager->addGlobalDynamicSubcube(std::unique_ptr<Subcube>(dynamicSubcube));
        LOG_DEBUG("Application", "Added dynamic subcube to global management");
        LOG_DEBUG("Application", "Press G again to spawn another subcube!");
    } else {
        LOG_ERROR("Application", "No chunk manager available for global dynamic subcube management");
    }
}

void Application::placeNewCube() {
    LOG_DEBUG("Application", "=== ATTEMPTING TO PLACE NEW CUBE ===");
    
    // Check if we have a valid hovered cube with face information
    if (!hasHoveredCube || !currentHoveredLocation.isValid()) {
        LOG_DEBUG("Application", "[CUBE PLACE] No hovered cube - aim at a cube face first");
        return;
    }
    
    // Check if face information is available
    if (currentHoveredLocation.hitFace < 0) {
        LOG_DEBUG("Application", "[CUBE PLACE] No face information available - face detection may have failed");
        return;
    }
    
    // Calculate where to place the new cube (adjacent to the hit face)
    glm::ivec3 placementPos = currentHoveredLocation.getAdjacentPlacementPosition();
    
    LOG_DEBUG_FMT("Application", "[CUBE PLACE] Hovering cube at world pos (" << currentHoveredLocation.worldPos.x 
              << "," << currentHoveredLocation.worldPos.y << "," << currentHoveredLocation.worldPos.z << ")");
    LOG_DEBUG_FMT("Application", "[CUBE PLACE] Hit face: " << currentHoveredLocation.hitFace << " (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z)");
    LOG_DEBUG_FMT("Application", "[CUBE PLACE] Attempting to place cube at world pos (" << placementPos.x 
              << "," << placementPos.y << "," << placementPos.z << ")");
    
    // Check if placement position is within the loaded world bounds
    // For a 2x2x2 chunk grid, world spans from (0,0,0) to (63,63,63)
    if (placementPos.x < 0 || placementPos.x >= 64 ||
        placementPos.y < 0 || placementPos.y >= 64 ||
        placementPos.z < 0 || placementPos.z >= 64) {
        LOG_WARN_FMT("Application", "[CUBE PLACE] Placement position (" << placementPos.x << "," << placementPos.y << "," << placementPos.z 
                  << ") is outside loaded world bounds (0,0,0) to (63,63,63)");
        return;
    }
    
    // Check if placement position is valid (not colliding with existing cube)
    glm::ivec3 placementChunkCoord = ChunkManager::worldToChunkCoord(placementPos);
    glm::ivec3 placementLocalPos = ChunkManager::worldToLocalCoord(placementPos);
    
    Chunk* placementChunk = chunkManager->getChunkAtCoord(placementChunkCoord);
    if (!placementChunk) {
        LOG_WARN("Application", "[CUBE PLACE] No chunk found at placement position - may be outside loaded area");
        return;
    }
    
    // Check if position is already occupied
    Cube* existingCube = placementChunk->getCubeAt(placementLocalPos);
    if (existingCube && existingCube->isVisible()) {
        LOG_DEBUG("Application", "[CUBE PLACE] Position already occupied by existing cube");
        return;
    }
    
    // Place the new cube with a random color
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);
    glm::vec3 newCubeColor = glm::vec3(
        colorDist(gen),
        colorDist(gen),
        colorDist(gen)
    );
    
    if (placementChunk->addCube(placementLocalPos, newCubeColor)) {
        // Mark the chunk as dirty in ChunkManager to trigger visual updates
        chunkManager->markChunkDirty(placementChunk);
        
        LOG_INFO_FMT("Application", "[CUBE PLACE] Successfully placed cube at world pos (" << placementPos.x 
                  << "," << placementPos.y << "," << placementPos.z << ")");
        LOG_DEBUG_FMT("Application", "[CUBE PLACE] Chunk (" << placementChunkCoord.x << "," << placementChunkCoord.y 
                  << "," << placementChunkCoord.z << ") marked dirty for visual update");
    } else {
        LOG_ERROR("Application", "[CUBE PLACE] Failed to place cube");
    }
}

FrameTiming Application::profileFrame() {
    auto frameEnd = std::chrono::high_resolution_clock::now();
    
    FrameTiming timing;
    timing.cpuFrameTime = std::chrono::duration<double, std::milli>(frameEnd - frameStartTime).count();
    timing.gpuFrameTime = timing.cpuFrameTime; // Placeholder - actual GPU timing would need GPU queries
    
    // Use chunk manager statistics if available (multi-chunk system)
    auto chunkStats = chunkManager->getPerformanceStats();
    timing.vertexCount = static_cast<int>(chunkStats.totalVertices);
    timing.drawCalls = static_cast<int>(chunkManager->chunks.size()); // One call per chunk
    timing.visibleInstances = static_cast<int>(chunkStats.totalCubes);
    timing.culledInstances = 0; // Will be updated during rendering with actual culling data
    
    return timing;
}

void Application::printProfilingInfo(int fps) {
    if (frameTimings.empty()) return;
    
    // Calculate averages
    double avgCpuTime = 0.0;
    int avgVertices = 0;
    int avgVisible = 0;
    int avgCulled = 0;
    
    for (const auto& timing : frameTimings) {
        avgCpuTime += timing.cpuFrameTime;
        avgVertices += timing.vertexCount;
        avgVisible += timing.visibleInstances;
        avgCulled += timing.culledInstances;
    }
    
    int samples = frameTimings.size();
    avgCpuTime /= samples;
    avgVertices /= samples;
    avgVisible /= samples;
    avgCulled /= samples;
    
    LOG_INFO("Performance", "\n=== FRAME PROFILING ===");
    LOG_INFO_FMT("Performance", "FPS: " << fps);
    LOG_INFO_FMT("Performance", "Avg CPU Frame Time: " << std::fixed << std::setprecision(2) << avgCpuTime << "ms");
    LOG_INFO_FMT("Performance", "Avg Vertices/Frame: " << avgVertices);
    LOG_INFO_FMT("Performance", "Avg Visible Cubes: " << avgVisible);
    LOG_INFO_FMT("Performance", "Avg Culled Cubes: " << avgCulled);
    
    if (avgVisible + avgCulled > 0) {
        LOG_INFO_FMT("Performance", "Culling Efficiency: " << std::fixed << std::setprecision(1) 
                  << (100.0 * avgCulled / (avgVisible + avgCulled)) << "%");
    }
    
    LOG_INFO_FMT("Performance", "Camera Position: (" << std::fixed << std::setprecision(1) 
              << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")");
    LOG_INFO("Performance", "======================");
}

void Application::printDetailedTimings() {
    if (detailedTimings.empty()) return;
    
    // Calculate averages
    DetailedFrameTiming avg = {};
    for (const auto& t : detailedTimings) {
        avg.totalFrameTime += t.totalFrameTime;
        avg.physicsTime += t.physicsTime;
        avg.mousePickTime += t.mousePickTime;
        avg.uboFillTime += t.uboFillTime;
        avg.instanceUpdateTime += t.instanceUpdateTime;
        avg.drawCmdUpdateTime += t.drawCmdUpdateTime;
        avg.uniformUploadTime += t.uniformUploadTime;
        avg.commandRecordTime += t.commandRecordTime;
        avg.gpuSubmitTime += t.gpuSubmitTime;
        avg.presentTime += t.presentTime;
    }
    
    int samples = detailedTimings.size();
    avg.totalFrameTime /= samples;
    avg.physicsTime /= samples;
    avg.mousePickTime /= samples;
    avg.uboFillTime /= samples;
    avg.instanceUpdateTime /= samples;
    avg.drawCmdUpdateTime /= samples;
    avg.uniformUploadTime /= samples;
    avg.commandRecordTime /= samples;
    avg.gpuSubmitTime /= samples;
    avg.presentTime /= samples;
    
    LOG_DEBUG("Performance", "\n=== DETAILED FRAME TIMING ===");
    LOG_DEBUG_FMT("Performance", "Total Frame Time:    " << std::fixed << std::setprecision(2) << avg.totalFrameTime << "ms");
    LOG_DEBUG_FMT("Performance", "  Physics:           " << std::fixed << std::setprecision(2) << avg.physicsTime << "ms");
    LOG_DEBUG_FMT("Performance", "  UBO Fill:          " << std::fixed << std::setprecision(2) << avg.uboFillTime << "ms");
    LOG_DEBUG_FMT("Performance", "  Instance Update:   " << std::fixed << std::setprecision(2) << avg.instanceUpdateTime << "ms");
    LOG_DEBUG_FMT("Performance", "  Uniform Upload:    " << std::fixed << std::setprecision(2) << avg.uniformUploadTime << "ms");
    LOG_DEBUG_FMT("Performance", "  Command Record:    " << std::fixed << std::setprecision(2) << avg.commandRecordTime << "ms");
    LOG_DEBUG_FMT("Performance", "  GPU Submit:        " << std::fixed << std::setprecision(2) << avg.gpuSubmitTime << "ms");
    LOG_DEBUG_FMT("Performance", "  Present:           " << std::fixed << std::setprecision(2) << avg.presentTime << "ms");
    LOG_DEBUG_FMT("Performance", "  Mouse Pick:        " << std::fixed << std::setprecision(2) << avg.mousePickTime << "ms");
    
    // Calculate percentage breakdown
    double total = avg.totalFrameTime;
    if (total > 0) {
        LOG_DEBUG("Performance", "\nTiming Breakdown:");
        LOG_DEBUG_FMT("Performance", "  Physics:         " << std::fixed << std::setprecision(1) << (avg.physicsTime / total * 100.0) << "%");
        LOG_DEBUG_FMT("Performance", "  UBO Fill:        " << std::fixed << std::setprecision(1) << (avg.uboFillTime / total * 100.0) << "%");
        LOG_DEBUG_FMT("Performance", "  Instance Update: " << std::fixed << std::setprecision(1) << (avg.instanceUpdateTime / total * 100.0) << "%");
        LOG_DEBUG_FMT("Performance", "  Uniform Upload:  " << std::fixed << std::setprecision(1) << (avg.uniformUploadTime / total * 100.0) << "%");
        LOG_DEBUG_FMT("Performance", "  Command Record:  " << std::fixed << std::setprecision(1) << (avg.commandRecordTime / total * 100.0) << "%");
        LOG_DEBUG_FMT("Performance", "  GPU Submit:      " << std::fixed << std::setprecision(1) << (avg.gpuSubmitTime / total * 100.0) << "%");
        LOG_DEBUG_FMT("Performance", "  Present:         " << std::fixed << std::setprecision(1) << (avg.presentTime / total * 100.0) << "%");
    }
    LOG_DEBUG("Performance", "============================");
}

void Application::updateMouseHover() {
    // Skip hover detection if right mouse button is pressed (camera mode)
    if (mouseCaptured) {
        return;
    }
    
    // Performance timing
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Create ray from mouse position
    glm::vec3 rayDirection = screenToWorldRay(currentMouseX, currentMouseY);
    
    // NEW: Use optimized O(1) VoxelLocation-based hover detection
    VoxelLocation voxelLocation = pickVoxelOptimized(cameraPos, rayDirection);
    
    // Convert to CubeLocation for backward compatibility with existing hover system
    CubeLocation hoveredLocation = voxelLocationToCubeLocation(voxelLocation);
    
    // Performance timing
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    hoverDetectionTimeMs = duration.count() / 1000.0;
    
    // Update rolling average
    hoverDetectionSamples++;
    avgHoverDetectionTimeMs = (avgHoverDetectionTimeMs * (hoverDetectionSamples - 1) + hoverDetectionTimeMs) / hoverDetectionSamples;
    
    // Log performance periodically (every 300 frames to avoid spam)
    if (hoverDetectionSamples % 300 == 0) {
        LOG_TRACE_FMT("Performance", "[HOVER PERF] Current: " << std::fixed << std::setprecision(3) 
                  << hoverDetectionTimeMs << "ms, Average: " << avgHoverDetectionTimeMs 
                  << "ms (over " << hoverDetectionSamples << " samples)");
    }
    
    // Convert location to a simple index for tracking (use a hash or simple approach)
    int hoveredCube = -1;
    if (hoveredLocation.isValid()) {
        // Enhanced hash for tracking: include subcube position for proper subcube hover detection
        // Use smaller multipliers to avoid integer overflow for subcube coordinates
        hoveredCube = hoveredLocation.worldPos.x + hoveredLocation.worldPos.y * 1000 + hoveredLocation.worldPos.z * 1000000;
        
        // If it's a subcube, include subcube position in hash to distinguish between subcubes of same parent
        // Use much smaller multipliers to prevent integer overflow (subcube coords are 0-2)
        if (hoveredLocation.isSubcube) {
            hoveredCube += hoveredLocation.subcubePos.x * 27 + 
                          hoveredLocation.subcubePos.y * 9 + 
                          hoveredLocation.subcubePos.z * 3;
        }
    }
    
    // Update hover state if changed
    if (hoveredCube != lastHoveredCube) {
        if (lastHoveredCube >= 0) {
            clearHoveredCubeInChunksOptimized();
        }
        
        if (hoveredCube >= 0) {
            setHoveredCubeInChunksOptimized(hoveredLocation);
        }
        
        lastHoveredCube = hoveredCube;
    }
}

glm::vec3 Application::screenToWorldRay(double mouseX, double mouseY) const {
    // Convert mouse coordinates to normalized device coordinates
    float x = (2.0f * mouseX) / windowWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / windowHeight; // Flip Y coordinate
    
    // Create clip space coordinates - flip Y again for Vulkan coordinate system
    glm::vec4 rayClip = glm::vec4(x, -y, -1.0f, 1.0f);
    
    // Convert to eye space
    glm::mat4 proj = glm::perspective(
        glm::radians(45.0f), 
        (float)windowWidth / (float)windowHeight, 
        0.1f, 
        200.0f
    );
    proj[1][1] *= -1; // Flip Y for Vulkan
    
    glm::vec4 rayEye = glm::inverse(proj) * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
    
    // Convert to world space
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::vec3 rayWorld = glm::vec3(glm::inverse(view) * rayEye);
    
    return glm::normalize(rayWorld);
}

// =============================================================================
// REMOVED: findSubcubeHit method (dead code cleanup)
// Functionality replaced by resolveSubcubeInVoxel in optimized system
// =============================================================================

// Note: Removed corrupt duplicate method definition that had wrong body

// Note: Removed corrupt duplicate method definition (had wrong body from old findSubcubeHit)
// The correct implementations start below:

Application::CubeLocation Application::findExistingSubcubeHit(Chunk* chunk, const glm::ivec3& localPos, const glm::ivec3& cubeWorldPos, const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const {
    // Get all existing subcubes at this cube position
    std::vector<Subcube*> existingSubcubes = chunk->getSubcubesAt(localPos);
    if (existingSubcubes.empty()) {
        return CubeLocation(); // No subcubes exist
    }
    
    // Test ray intersection against each existing subcube's bounding box
    float closestDistance = std::numeric_limits<float>::max();
    Subcube* closestSubcube = nullptr;
    
    for (Subcube* subcube : existingSubcubes) {
        if (!subcube || !subcube->isVisible()) continue;
        
        glm::ivec3 subcubeLocalPos = subcube->getLocalPosition();
        
        // Calculate subcube's world bounding box (each subcube is 1/3 scale within the parent cube)
        float subcubeSize = 1.0f / 3.0f;
        glm::vec3 subcubeMin = glm::vec3(cubeWorldPos) + glm::vec3(subcubeLocalPos) * subcubeSize;
        glm::vec3 subcubeMax = subcubeMin + glm::vec3(subcubeSize);
        
        // Ray-AABB intersection test
        glm::vec3 invDir = 1.0f / rayDirection;
        glm::vec3 t1 = (subcubeMin - rayOrigin) * invDir;
        glm::vec3 t2 = (subcubeMax - rayOrigin) * invDir;
        
        glm::vec3 tMin = glm::min(t1, t2);
        glm::vec3 tMax = glm::max(t1, t2);
        
        float tNear = glm::max(glm::max(tMin.x, tMin.y), tMin.z);
        float tFar = glm::min(glm::min(tMax.x, tMax.y), tMax.z);
        
        // Check if ray intersects this subcube and if it's the closest so far
        if (tNear <= tFar && tFar >= 0.0f && tNear < closestDistance) {
            closestDistance = tNear;
            closestSubcube = subcube;
        }
    }
    
    // Return the closest subcube hit, if any
    if (closestSubcube) {
        return CubeLocation(chunk, localPos, cubeWorldPos, closestSubcube->getLocalPosition());
    }
    
    return CubeLocation(); // No subcube intersected
}

void Application::setHoveredCubeInChunksOptimized(const CubeLocation& location) {
    if (!location.isValid()) return;
    
    // Debug: Show what type of location we're hovering
    LOG_DEBUG_FMT("HoverDetection", "Hovering - isSubcube: " << location.isSubcube 
              << ", world pos: (" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z << ")"
              << (location.isSubcube ? ", subcube pos: (" + std::to_string(location.subcubePos.x) + "," + 
                  std::to_string(location.subcubePos.y) + "," + std::to_string(location.subcubePos.z) + ")" : ""));
    
    // Clear previous hover
    clearHoveredCubeInChunksOptimized();
    
    // Handle subcube vs regular cube hover differently
    if (location.isSubcube) {
        // Hovering over a specific subcube
        Chunk* chunk = location.chunk;
        std::vector<Subcube*> subcubes = chunk->getSubcubesAt(location.localPos);
        if (subcubes.empty()) return;
        
        Subcube* subcube = chunk->getSubcubeAt(location.localPos, location.subcubePos);
        if (!subcube) return;
        
        // Store original subcube color and location for later restoration
        originalHoveredColor = subcube->getOriginalColor(); // Use original color, not current color
        currentHoveredLocation = location;
        hasHoveredCube = true;
        
        LOG_DEBUG_FMT("HoverDetection", "Setting hover at world pos: (" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                  << ") subcube: (" << location.subcubePos.x << "," << location.subcubePos.y << "," << location.subcubePos.z 
                  << ") original color: (" << originalHoveredColor.x << "," << originalHoveredColor.y << "," << originalHoveredColor.z << ")");
        
        // Set hover color (lighten the original color for subtle highlighting)
        glm::vec3 hoverColor = calculateLighterColor(originalHoveredColor);
        
        // Use efficient subcube color update
        if (chunkManager) {
            chunkManager->setSubcubeColorEfficient(location.worldPos, location.subcubePos, hoverColor);
        }
    } else {
        // Hovering over a regular cube (not subdivided)
        Cube* cube = location.chunk->getCubeAt(location.localPos);
        if (!cube) return;
        
        // CRITICAL: Don't apply hover to subdivided cubes (they should be handled as individual subcubes)
        if (!cube->isVisible()) {
            LOG_TRACE_FMT("Application", "[CUBE HOVER] Skipping hover on hidden/subdivided cube at world pos: (" 
                      << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z << ")");
            return;
        }
        
        // Store original color and location for later restoration
        originalHoveredColor = cube->getColor();
        currentHoveredLocation = location;
        hasHoveredCube = true;
        
        LOG_TRACE_FMT("Application", "[CUBE HOVER] Setting hover at world pos: (" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                  << ") original color: (" << originalHoveredColor.x << "," << originalHoveredColor.y << "," << originalHoveredColor.z << ")");
        
        // Set hover color (lighten the original color for subtle highlighting)
        glm::vec3 hoverColor = calculateLighterColor(originalHoveredColor);
        
        // Use efficient partial update instead of marking chunk dirty
        if (chunkManager) {
            chunkManager->setCubeColorEfficient(location.worldPos, hoverColor);
        }
    }
}

void Application::clearHoveredCubeInChunksOptimized() {
    if (hasHoveredCube && currentHoveredLocation.isValid()) {
        // Restore original color based on whether it's a subcube or regular cube
        if (chunkManager) {
            if (currentHoveredLocation.isSubcube) {
                // Restore subcube color
                chunkManager->setSubcubeColorEfficient(currentHoveredLocation.worldPos, currentHoveredLocation.subcubePos, originalHoveredColor);
                LOG_TRACE_FMT("Application", "[SUBCUBE HOVER] Cleared hover for subcube at world pos: (" << currentHoveredLocation.worldPos.x << "," << currentHoveredLocation.worldPos.y << "," << currentHoveredLocation.worldPos.z 
                          << ") subcube: (" << currentHoveredLocation.subcubePos.x << "," << currentHoveredLocation.subcubePos.y << "," << currentHoveredLocation.subcubePos.z << ")");
            } else {
                // Restore regular cube color
                chunkManager->setCubeColorEfficient(currentHoveredLocation.worldPos, originalHoveredColor);
                LOG_TRACE_FMT("Application", "[CUBE HOVER] Cleared hover for cube at world pos: (" << currentHoveredLocation.worldPos.x << "," << currentHoveredLocation.worldPos.y << "," << currentHoveredLocation.worldPos.z << ")");
            }
        }
        
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation(); // Reset to invalid state
    }
}

void Application::removeHoveredCube() {
    // Check if we have a valid hovered cube
    if (!hasHoveredCube || !currentHoveredLocation.isValid()) {
        LOG_DEBUG("Application", "[CUBE REMOVAL] No cube is currently being hovered - cannot remove");
        return;
    }
    
    // Get the chunk and remove the cube using the stored location
    Chunk* chunk = currentHoveredLocation.chunk;
    if (!chunk) {
        LOG_ERROR("Application", "[CUBE REMOVAL] ERROR: Invalid chunk pointer");
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation();
        return;
    }
    
    bool removed = false;
    
    if (currentHoveredLocation.isSubcube) {
        // Remove a specific subcube
        removed = chunk->removeSubcube(currentHoveredLocation.localPos, currentHoveredLocation.subcubePos);
        if (removed) {
            LOG_DEBUG_FMT("Application", "[SUBCUBE REMOVAL] Successfully removed subcube at world pos: (" 
                      << currentHoveredLocation.worldPos.x << "," 
                      << currentHoveredLocation.worldPos.y << "," 
                      << currentHoveredLocation.worldPos.z << ") subcube: ("
                      << currentHoveredLocation.subcubePos.x << ","
                      << currentHoveredLocation.subcubePos.y << ","
                      << currentHoveredLocation.subcubePos.z << ")");
                      
            // Check if this was the last subcube - if so, restore the parent cube
            auto remainingSubcubes = chunk->getSubcubesAt(currentHoveredLocation.localPos);
            if (remainingSubcubes.empty()) {
                // No more subcubes left, restore the parent cube
                Cube* parentCube = chunk->getCubeAt(currentHoveredLocation.localPos);
                if (parentCube) {
                    parentCube->show(); // Make parent cube visible again
                    LOG_DEBUG("Application", "[CUBE RESTORATION] Restored parent cube as no subcubes remain");
                }
            }
        }
    } else {
        // Remove a regular cube (this will also remove all its subcubes if subdivided)
        Chunk* chunk = currentHoveredLocation.chunk;
        std::vector<Subcube*> subcubes = chunk->getSubcubesAt(currentHoveredLocation.localPos);
        if (!subcubes.empty()) {
            // First clear all subcubes
            chunk->clearSubdivisionAt(currentHoveredLocation.localPos);
            LOG_DEBUG("Application", "[SUBDIVISION REMOVAL] Cleared all subcubes for cube removal");
        }
        
        // Remove the cube itself
        removed = chunk->removeCube(currentHoveredLocation.localPos);
        if (removed) {
            LOG_DEBUG_FMT("Application", "[CUBE REMOVAL] Successfully removed cube at world pos: (" 
                      << currentHoveredLocation.worldPos.x << "," 
                      << currentHoveredLocation.worldPos.y << "," 
                      << currentHoveredLocation.worldPos.z << ")");
        }
    }
    
    if (removed) {
        // No need to mark chunk dirty - removal methods now immediately update GPU buffer
        
        // Clear hover state since the object no longer exists
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation();
        lastHoveredCube = -1; // Reset the hover tracking
        
    } else {
        LOG_WARN("Application", "[REMOVAL] WARNING: Failed to remove object - it may not exist");
    }
}

void Application::subdivideHoveredCube() {
    // Check if we have a valid hovered cube
    if (!hasHoveredCube || !currentHoveredLocation.isValid()) {
        LOG_DEBUG("Application", "[CUBE SUBDIVISION] No cube is currently being hovered - cannot subdivide");
        return;
    }

    // Only subdivide regular cubes (not subcubes)
    if (currentHoveredLocation.isSubcube) {
        LOG_DEBUG("Application", "[CUBE SUBDIVISION] Cannot subdivide individual subcubes - use left click to break subcubes");
        return;
    }

    // Get the chunk using the stored location
    Chunk* chunk = currentHoveredLocation.chunk;
    if (!chunk) {
        LOG_ERROR("Application", "[CUBE SUBDIVISION] ERROR: Invalid chunk pointer");
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation();
        return;
    }

    // Check if cube is already subdivided
    if (chunk->getSubcubesAt(currentHoveredLocation.localPos).size() > 0) {
        LOG_DEBUG_FMT("Application", "[CUBE SUBDIVISION] Cube at world pos (" 
                  << currentHoveredLocation.worldPos.x << "," 
                  << currentHoveredLocation.worldPos.y << "," 
                  << currentHoveredLocation.worldPos.z << ") is already subdivided");
        return;
    }

    // Subdivide the cube into 27 static subcubes
    bool subdivided = chunk->subdivideAt(currentHoveredLocation.localPos);
    if (subdivided) {
        LOG_INFO_FMT("Application", "[CUBE SUBDIVISION] Successfully subdivided cube at world pos: (" 
                  << currentHoveredLocation.worldPos.x << "," 
                  << currentHoveredLocation.worldPos.y << "," 
                  << currentHoveredLocation.worldPos.z << ") into 27 static subcubes");
    } else {
        LOG_WARN("Application", "[CUBE SUBDIVISION] WARNING: Failed to subdivide cube - cube may not exist");
    }

    // Use efficient selective update instead of marking entire chunk dirty
    if (chunkManager) {
        chunkManager->updateAfterCubeSubdivision(currentHoveredLocation.worldPos);
    }

    // Clear hover state
    hasHoveredCube = false;
    currentHoveredLocation = CubeLocation();
    lastHoveredCube = -1;
}

void Application::breakHoveredSubcube() {
    // Check if we have a valid hovered subcube
    if (!hasHoveredCube || !currentHoveredLocation.isValid()) {
        LOG_DEBUG("Application", "[SUBCUBE BREAKING] No subcube is currently being hovered - cannot break");
        return;
    }

    // Only break subcubes (not regular cubes)
    if (!currentHoveredLocation.isSubcube) {
        LOG_DEBUG("Application", "[SUBCUBE BREAKING] Hovered object is not a subcube - use left click to break regular cubes");
        return;
    }

    // Get the chunk using the stored location
    Chunk* chunk = currentHoveredLocation.chunk;
    if (!chunk) {
        LOG_ERROR("Application", "[SUBCUBE BREAKING] ERROR: Invalid chunk pointer");
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation();
        return;
    }

    LOG_DEBUG("Application", "[SUBCUBE BREAKING] Breaking subcube without forces (gentle removal)");
    
    // Break subcube WITHOUT any impulse forces (as requested)
    glm::vec3 noForce(0.0f, 0.0f, 0.0f); // No forces applied
    
    bool broken = chunk->breakSubcube(currentHoveredLocation.localPos, currentHoveredLocation.subcubePos, 
                                     physicsWorld.get(), chunkManager.get(), noForce);
    if (broken) {
        LOG_INFO_FMT("Application", "[SUBCUBE BREAKING] Successfully broke subcube (no forces) and transferred to global system at world pos: (" 
                  << currentHoveredLocation.worldPos.x << "," 
                  << currentHoveredLocation.worldPos.y << "," 
                  << currentHoveredLocation.worldPos.z << ") subcube: ("
                  << currentHoveredLocation.subcubePos.x << ","
                  << currentHoveredLocation.subcubePos.y << ","
                  << currentHoveredLocation.subcubePos.z << ")");
                  
        // Use efficient selective update for subcube breaking
        if (chunkManager) {
            chunkManager->updateAfterSubcubeBreak(currentHoveredLocation.worldPos, currentHoveredLocation.subcubePos);
        }
    } else {
        LOG_WARN("Application", "[SUBCUBE BREAKING] WARNING: Failed to break subcube");
    }

    // Clear hover state
    hasHoveredCube = false;
    currentHoveredLocation = CubeLocation();
    lastHoveredCube = -1;
}

void Application::breakHoveredCube() {
    // Check if we have a valid hovered cube
    if (!hasHoveredCube || !currentHoveredLocation.isValid()) {
        LOG_DEBUG("Application", "[CUBE BREAKING] No cube is currently being hovered - cannot break");
        return;
    }
    
    // Only break regular cubes (not subcubes)
    if (currentHoveredLocation.isSubcube) {
        LOG_DEBUG("Application", "[CUBE BREAKING] Cannot break individual subcubes - use left click to break subcubes");
        return;
    }
    
    // Get the chunk using the stored location
    Chunk* chunk = currentHoveredLocation.chunk;
    if (!chunk) {
        LOG_ERROR("Application", "[CUBE BREAKING] ERROR: Invalid chunk pointer");
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation();
        return;
    }
    
    // Check if cube exists and is not already subdivided
    if (!chunk->getCubeAt(currentHoveredLocation.localPos)) {
        LOG_DEBUG("Application", "[CUBE BREAKING] No cube exists at this location");
        return;
    }
    
    if (chunk->getSubcubesAt(currentHoveredLocation.localPos).size() > 0) {
        LOG_DEBUG("Application", "[CUBE BREAKING] Cube is already subdivided - cannot break subdivided cubes");
        return;
    }
    
    // Calculate impulse force away from the camera direction
    glm::vec3 impulseForce(0.0f, 2.0f, 0.0f); // Default upward force
    glm::vec3 cubeWorldPos = glm::vec3(currentHoveredLocation.worldPos); // Declare outside conditional blocks
    
    if (debugFlags.showForceSystemDebug && debugFlags.manualForceValue > 0.0f) {
        // Use manual force value from debug UI
        glm::vec3 forceDirection = normalize(cubeWorldPos - cameraPos);
        
        // Scale the manual force value appropriately (convert from UI units to physics units)
        float scaledForce = debugFlags.manualForceValue * 0.01f; // Scale down for reasonable physics
        
        // Mix directional force with upward force for interesting breakage
        impulseForce = forceDirection * scaledForce * 0.6f + glm::vec3(0.0f, scaledForce * 0.4f, 0.0f);
        
        LOG_DEBUG_FMT("Application", "[PHYSICS] Using manual force from debug UI: " << debugFlags.manualForceValue 
                  << "N (scaled: " << scaledForce << ") resulting impulse: (" 
                  << impulseForce.x << "," << impulseForce.y << "," << impulseForce.z << ")");
    } else {
        // Use original automatic force calculation
        glm::vec3 forceDirection = normalize(cubeWorldPos - cameraPos);
        
        // Mix upward force with outward force for interesting breakage (gentler forces)
        impulseForce = forceDirection * 1.5f + glm::vec3(0.0f, 2.5f, 0.0f); // Reduced from 4.0f and 6.0f
        
        LOG_DEBUG_FMT("Application", "[PHYSICS] Using automatic breakage force: (" 
                  << impulseForce.x << "," << impulseForce.y << "," << impulseForce.z << ")");
    }
    
    // Get the cube's original color before removing it
    const Cube* originalCube = chunk->getCubeAt(currentHoveredLocation.localPos);
    glm::vec3 originalColor = glm::vec3(0.8f, 0.6f, 0.4f); // Default color fallback
    if (originalCube) {
        originalColor = originalCube->getOriginalColor(); // Use original color, not hover-affected color
        LOG_DEBUG_FMT("Application", "[COLOR] Preserving original cube color: (" 
                  << originalColor.x << "," << originalColor.y << "," << originalColor.z << ")");
    }
    
    // Remove the cube from the chunk
    bool removed = chunk->removeCube(currentHoveredLocation.localPos);
    if (!removed) {
        LOG_WARN("Application", "[CUBE BREAKING] WARNING: Failed to remove cube from chunk");
        return;
    }
    
    // NEW: Use incremental collision updates instead of expensive full rebuild
    // This prevents the +1.0 X-axis offset caused by collision recovery against the static compound shape
    chunk->updateChunkPhysicsBody(); // This will process incremental updates
    
    // Create a dynamic cube at the EXACT original position
    // TEST COORDINATE SYSTEM OFFSET - Based on user observation of consistent X/Z axis offset
    // Try different offset patterns to isolate the issue
    glm::vec3 cubeCornerPos = cubeWorldPos; // Corner position from raycast
    
    // COORDINATE SYSTEM FIX - Convert from corner-based to center-based coordinates
    // Static cubes: render from corner (0,0,0 to 1,1,1 face offset)
    // Dynamic cubes: render from center (-0.5,-0.5,-0.5 to 0.5,0.5,0.5 rotated offset)
    // To visually align: dynamic center = static corner + 0.5
    glm::vec3 physicsCenterPos = cubeCornerPos + glm::vec3(0.5f, 0.5f, 0.5f);
    
    LOG_TRACE("Application", "[POSITION DEBUG] ===== COORDINATE SYSTEM CONVERSION =====");
    LOG_TRACE_FMT("Application", "[POSITION DEBUG] Static cube corner: (" 
              << cubeWorldPos.x << ", " << cubeWorldPos.y << ", " << cubeWorldPos.z << ")");
    LOG_TRACE_FMT("Application", "[POSITION DEBUG] Dynamic cube center (corner + 0.5): (" 
              << physicsCenterPos.x << ", " << physicsCenterPos.y << ", " << physicsCenterPos.z << ")");
    LOG_TRACE("Application", "[POSITION DEBUG] COORDINATE FIX: Converting from corner-based to center-based rendering");
    
    // TODO: Add UI to select material - for now cycle through materials based on position
    std::vector<std::string> materials = {"Wood", "Metal", "Glass", "Rubber", "Stone", "Ice", "Cork"};
    int materialIndex = (abs(static_cast<int>(cubeWorldPos.x) + static_cast<int>(cubeWorldPos.z))) % materials.size();
    std::string selectedMaterial = materials[materialIndex];
    
    auto dynamicCube = std::make_unique<DynamicCube>(cubeCornerPos, originalColor, selectedMaterial);
    
    // Create physics body for the dynamic cube using breakaway function (with shrunk collision)
    glm::vec3 cubeSize(1.0f); // Full cube size
    btRigidBody* rigidBody = physicsWorld->createBreakawaCube(physicsCenterPos, cubeSize, selectedMaterial);
    dynamicCube->setRigidBody(rigidBody);
    
    // Check where the physics body actually ended up
    if (rigidBody) {
        btTransform transform = rigidBody->getWorldTransform();
        btVector3 physicsPos = transform.getOrigin();
        LOG_TRACE_FMT("Application", "[POSITION DEBUG] Physics body created at: (" 
                  << physicsPos.x() << ", " << physicsPos.y() << ", " << physicsPos.z() << ")");
        LOG_TRACE_FMT("Application", "[POSITION DEBUG] Position difference from intended: (" 
                  << (physicsPos.x() - physicsCenterPos.x) << ", " 
                  << (physicsPos.y() - physicsCenterPos.y) << ", " 
                  << (physicsPos.z() - physicsCenterPos.z) << ")");
    }
    
    // Get material properties for impulse scaling
    static Physics::MaterialManager materialManager;
    const auto& material = materialManager.getMaterial(selectedMaterial);
    
    // Apply material-specific impulse scaling (reduced for gentler breaking)
    float impulseScale = material.breakForceMultiplier * 0.4f; // Reduce to 40% of original force
    impulseForce *= impulseScale;
    
    LOG_DEBUG_FMT("Application", "[MATERIAL] Breaking cube with '" << selectedMaterial << "' material (reduced impulse scale: " 
              << impulseScale << ")");
    
    // Set initial physics position to the exact center position
    dynamicCube->setPhysicsPosition(physicsCenterPos);
    
    // COMPREHENSIVE POSITION TRACKING - Track exact coordinates through entire pipeline
    glm::vec3 renderingPosition = dynamicCube->getPhysicsPosition();
    LOG_TRACE("Application", "[POSITION TRACK] ===== DYNAMIC CUBE POSITION TRACKING =====");
    LOG_TRACE_FMT("Application", "[POSITION TRACK] 1. Initial spawn position set: (" 
              << physicsCenterPos.x << ", " << physicsCenterPos.y << ", " << physicsCenterPos.z << ")");
    LOG_TRACE_FMT("Application", "[POSITION TRACK] 2. Rendering position stored: (" 
              << renderingPosition.x << ", " << renderingPosition.y << ", " << renderingPosition.z << ")");
    LOG_TRACE_FMT("Application", "[POSITION TRACK] 3. Position match: " << (renderingPosition == physicsCenterPos ? "YES" : "NO"));
    
    // Track actual physics body position
    if (rigidBody) {
        btTransform transform = rigidBody->getWorldTransform();
        btVector3 physicsBodyPos = transform.getOrigin();
        LOG_TRACE_FMT("Application", "[POSITION TRACK] 4. Physics body actual position: (" 
                  << physicsBodyPos.x() << ", " << physicsBodyPos.y() << ", " << physicsBodyPos.z() << ")");
        LOG_TRACE_FMT("Application", "[POSITION TRACK] 5. Physics vs rendering diff: (" 
                  << (physicsBodyPos.x() - renderingPosition.x) << ", " 
                  << (physicsBodyPos.y() - renderingPosition.y) << ", " 
                  << (physicsBodyPos.z() - renderingPosition.z) << ")");
    }
    
    // Apply initial impulse force to make it "break" away (RE-ENABLED)
    // Apply forces now that collision issues are resolved
    if (rigidBody && glm::length(impulseForce) > 0.0f && !debugFlags.disableBreakingForces) {
        btVector3 btImpulse(impulseForce.x, impulseForce.y, impulseForce.z);
        rigidBody->applyCentralImpulse(btImpulse);
        
        // Add random angular velocity for tumbling effect (reduced intensity for realism)
        btVector3 angularVelocity(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f, // Reduced from 6.0f for more realistic tumbling
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f
        );
        rigidBody->setAngularVelocity(angularVelocity);
        
        // Enable gravity for natural falling behavior
        rigidBody->setGravity(btVector3(0, -9.81f, 0)); // Standard gravity
        
        LOG_DEBUG_FMT("Application", "[PHYSICS] Applied impulse (" << impulseForce.x << "," << impulseForce.y << "," << impulseForce.z 
                  << ") and angular velocity (" << angularVelocity.x() << "," << angularVelocity.y() << "," << angularVelocity.z() << ") with gravity enabled");
    } else if (rigidBody) {
        // If forces are disabled or no impulse, still enable gravity for natural falling
        rigidBody->setGravity(btVector3(0, -9.81f, 0)); // Standard gravity
        LOG_DEBUG("Application", "[PHYSICS] No impulse applied - enabled gravity only for natural falling");
    }
    
    // Final position check after everything is set up
    if (rigidBody) {
        btTransform finalTransform = rigidBody->getWorldTransform();
        btVector3 finalPos = finalTransform.getOrigin();
        LOG_TRACE_FMT("Application", "[POSITION DEBUG] Final physics position: (" 
                  << finalPos.x() << ", " << finalPos.y() << ", " << finalPos.z() << ")");
        LOG_TRACE("Application", "[POSITION DEBUG] ===== END POSITION ANALYSIS =====");
    }
    
    // Mark as broken
    dynamicCube->breakApart();
    
    // Add to global dynamic cubes system
    chunkManager->addGlobalDynamicCube(std::move(dynamicCube));
    
    // Use efficient selective update instead of marking entire chunk dirty
    chunkManager->updateAfterCubeBreak(currentHoveredLocation.worldPos);
    
    LOG_INFO_FMT("Application", "[CUBE BREAKING] Successfully broke cube at world pos: (" 
              << currentHoveredLocation.worldPos.x << "," 
              << currentHoveredLocation.worldPos.y << "," 
              << currentHoveredLocation.worldPos.z << ") into dynamic cube");
    
    // Clear hover state
    hasHoveredCube = false;
    currentHoveredLocation = CubeLocation();
    lastHoveredCube = -1;
}

void Application::breakHoveredCubeWithForce() {
    // Check if we have a valid hovered cube
    if (!hasHoveredCube || !currentHoveredLocation.isValid()) {
        LOG_DEBUG("Application", "[FORCE BREAKING] No cube is currently being hovered - cannot break");
        return;
    }
    
    // Only break regular cubes (not subcubes) with force system
    if (currentHoveredLocation.isSubcube) {
        LOG_DEBUG("Application", "[FORCE BREAKING] Cannot break individual subcubes with force - use left click to break subcubes");
        return;
    }
    
    // Get the chunk using the stored location
    Chunk* chunk = currentHoveredLocation.chunk;
    if (!chunk) {
        LOG_ERROR("Application", "[FORCE BREAKING] ERROR: Invalid chunk pointer");
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation();
        return;
    }
    
    // Check if cube exists and is not already subdivided
    if (!chunk->getCubeAt(currentHoveredLocation.localPos)) {
        LOG_DEBUG("Application", "[FORCE BREAKING] No cube exists at this location");
        return;
    }
    
    if (chunk->getSubcubesAt(currentHoveredLocation.localPos).size() > 0) {
        LOG_DEBUG("Application", "[FORCE BREAKING] Cube is already subdivided - cannot break subdivided cubes");
        return;
    }
    
    // Calculate ray for force direction
    glm::vec3 rayOrigin = cameraPos;
    glm::vec3 rayDirection = screenToWorldRay(currentMouseX, currentMouseY);
    
    // Use hit point from current hovered location or calculate from cube center
    glm::vec3 hitPoint = currentHoveredLocation.hitPoint;
    if (glm::length(hitPoint) < 0.1f) { // No valid hit point stored
        hitPoint = glm::vec3(currentHoveredLocation.worldPos) + glm::vec3(0.5f); // Use cube center
    }
    
    // Get current mouse velocity
    glm::vec2 mouseVelocity = mouseVelocityTracker->getVelocity();
    
    LOG_DEBUG_FMT("Application", "[FORCE BREAKING] Mouse velocity: (" << mouseVelocity.x << "," << mouseVelocity.y 
              << ") speed: " << mouseVelocityTracker->getSpeed());
    
    // Calculate force using force system
    ForceSystem::ClickForce clickForce = forceSystem->calculateClickForce(
        mouseVelocity, rayOrigin, rayDirection, hitPoint);
    
    // Propagate force through the chunk system
    ForceSystem::PropagationResult result = forceSystem->propagateForce(
        clickForce, currentHoveredLocation.worldPos, chunkManager.get());
    
    // Debug print the propagation results
    forceSystem->debugPrintPropagation(result);
    
    // Break all cubes that should break according to the force propagation
    for (const glm::ivec3& worldPos : result.brokenCubes) {
        breakCubeAtPosition(worldPos);
    }
    
    LOG_INFO_FMT("Application", "[FORCE BREAKING] Successfully broke " << result.brokenCubes.size() 
              << " cubes using force propagation system");
    
    // Clear hover state
    hasHoveredCube = false;
    currentHoveredLocation = CubeLocation();
    lastHoveredCube = -1;
}

// Helper method to break a single cube at a specific world position
void Application::breakCubeAtPosition(const glm::ivec3& worldPos) {
    Chunk* chunk = chunkManager->getChunkAt(worldPos);
    if (!chunk) {
        LOG_WARN_FMT("Application", "[FORCE BREAKING] No chunk found for position (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
        return;
    }
    
    glm::ivec3 localPos = ChunkManager::worldToLocalCoord(worldPos);
    const Cube* originalCube = chunk->getCubeAt(localPos);
    if (!originalCube) {
        LOG_WARN_FMT("Application", "[FORCE BREAKING] No cube found at position (" 
                  << worldPos.x << "," << worldPos.y << "," << worldPos.z << ")");
        return;
    }
    
    // Get the cube's original color before removing it
    glm::vec3 originalColor = originalCube->getOriginalColor();
    
    // Remove cube from chunk
    bool removed = chunk->removeCube(localPos);
    if (!removed) {
        LOG_WARN("Application", "[FORCE BREAKING] Failed to remove cube from chunk");
        return;
    }
    
    // Create dynamic cube for physics
    glm::vec3 cubeCornerPos = glm::vec3(worldPos); // Corner position for rendering
    glm::vec3 physicsCenterPos = cubeCornerPos + glm::vec3(0.5f); // Center position for physics
    
    // Select material based on position (same logic as original)
    std::vector<std::string> materials = {"stone", "wood", "metal", "ice"};
    int materialIndex = (abs(worldPos.x) + abs(worldPos.z)) % materials.size();
    std::string selectedMaterial = materials[materialIndex];
    
    auto dynamicCube = std::make_unique<DynamicCube>(cubeCornerPos, originalColor, selectedMaterial);
    
    // Create physics body for the dynamic cube
    glm::vec3 cubeSize(1.0f); // Full cube size
    btRigidBody* rigidBody = physicsWorld->createBreakawaCube(physicsCenterPos, cubeSize, selectedMaterial);
    dynamicCube->setRigidBody(rigidBody);
    
    // Apply a small random force for natural breaking effect
    if (rigidBody && !debugFlags.disableBreakingForces) {
        glm::vec3 randomForce(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f,  // Random X force
            100.0f + (static_cast<float>(rand()) / RAND_MAX) * 100.0f, // Upward + random Y force
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 200.0f   // Random Z force
        );
        
        btVector3 btImpulse(randomForce.x, randomForce.y, randomForce.z);
        rigidBody->applyCentralImpulse(btImpulse);
        
        // Add random angular velocity for tumbling effect
        btVector3 angularVelocity(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f
        );
        rigidBody->setAngularVelocity(angularVelocity);
        
        // Enable gravity
        rigidBody->setGravity(btVector3(0, -9.81f, 0));
    }
    
    // Mark as broken and add to global system
    dynamicCube->breakApart();
    chunkManager->addGlobalDynamicCube(std::move(dynamicCube));
    
    // Update affected chunks
    chunkManager->updateAfterCubeBreak(worldPos);
}

// =============================================================================
// NEW: VoxelLocation-based O(1) hover detection system
// =============================================================================

VoxelLocation Application::pickVoxelOptimized(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const {
    if (!chunkManager) {
        return VoxelLocation(); // Invalid location
    }
    
    // DDA algorithm for efficient voxel traversal (same as before)
    float maxDistance = 200.0f;
    glm::ivec3 voxel = glm::ivec3(glm::floor(rayOrigin));
    
    glm::ivec3 step = glm::ivec3(
        rayDirection.x > 0 ? 1 : (rayDirection.x < 0 ? -1 : 0),
        rayDirection.y > 0 ? 1 : (rayDirection.y < 0 ? -1 : 0),
        rayDirection.z > 0 ? 1 : (rayDirection.z < 0 ? -1 : 0)
    );
    
    glm::vec3 deltaDist = glm::vec3(
        rayDirection.x != 0 ? glm::abs(1.0f / rayDirection.x) : std::numeric_limits<float>::max(),
        rayDirection.y != 0 ? glm::abs(1.0f / rayDirection.y) : std::numeric_limits<float>::max(),
        rayDirection.z != 0 ? glm::abs(1.0f / rayDirection.z) : std::numeric_limits<float>::max()
    );
    
    glm::vec3 sideDist;
    if (rayDirection.x < 0) {
        sideDist.x = (rayOrigin.x - voxel.x) * deltaDist.x;
    } else {
        sideDist.x = (voxel.x + 1.0f - rayOrigin.x) * deltaDist.x;
    }
    if (rayDirection.y < 0) {
        sideDist.y = (rayOrigin.y - voxel.y) * deltaDist.y;
    } else {
        sideDist.y = (voxel.y + 1.0f - rayOrigin.y) * deltaDist.y;
    }
    if (rayDirection.z < 0) {
        sideDist.z = (rayOrigin.z - voxel.z) * deltaDist.z;
    } else {
        sideDist.z = (voxel.z + 1.0f - rayOrigin.z) * deltaDist.z;
    }
    
    int maxSteps = 500;
    int lastStepAxis = -1;
    
    for (int step_count = 0; step_count < maxSteps; ++step_count) {
        // NEW: O(1) voxel resolution using the VoxelLocation system
        VoxelLocation location = chunkManager->resolveGlobalPosition(voxel);
        
        if (location.isValid()) {
            // Calculate face information from DDA step
            int hitFace = -1;
            glm::vec3 hitNormal(0);
            
            if (lastStepAxis >= 0) {
                if (lastStepAxis == 0) { // X-axis step
                    hitFace = (step.x > 0) ? 1 : 0;
                    hitNormal = (step.x > 0) ? glm::vec3(-1,0,0) : glm::vec3(1,0,0);
                } else if (lastStepAxis == 1) { // Y-axis step
                    hitFace = (step.y > 0) ? 3 : 2;
                    hitNormal = (step.y > 0) ? glm::vec3(0,-1,0) : glm::vec3(0,1,0);
                } else if (lastStepAxis == 2) { // Z-axis step
                    hitFace = (step.z > 0) ? 5 : 4;
                    hitNormal = (step.z > 0) ? glm::vec3(0,0,-1) : glm::vec3(0,0,1);
                }
            }
            
            location.hitFace = hitFace;
            location.hitNormal = hitNormal;
            
            // If subdivided, resolve specific subcube
            if (location.type == VoxelLocation::SUBDIVIDED) {
                return resolveSubcubeInVoxel(rayOrigin, rayDirection, location);
            } else {
                return location; // Regular cube
            }
        }
        
        // DDA step (same as before)
        if (sideDist.x < sideDist.y && sideDist.x < sideDist.z) {
            lastStepAxis = 0;
            sideDist.x += deltaDist.x;
            voxel.x += step.x;
        } else if (sideDist.y < sideDist.z) {
            lastStepAxis = 1;
            sideDist.y += deltaDist.y;
            voxel.y += step.y;
        } else {
            lastStepAxis = 2;
            sideDist.z += deltaDist.z;
            voxel.z += step.z;
        }
        
        // Distance check
        glm::vec3 currentPos = glm::vec3(voxel);
        if (glm::length(currentPos - rayOrigin) > maxDistance) {
            break;
        }
    }
    
    return VoxelLocation(); // No voxel found
}

VoxelLocation Application::resolveSubcubeInVoxel(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const VoxelLocation& voxelHit) const {
    // Get all existing subcubes at this voxel position
    std::vector<Subcube*> existingSubcubes = voxelHit.chunk->getSubcubesAt(voxelHit.localPos);
    if (existingSubcubes.empty()) {
        return VoxelLocation(); // No subcubes exist at all
    }
    
    // Test ray intersection against each existing subcube's bounding box
    float closestDistance = std::numeric_limits<float>::max();
    Subcube* closestSubcube = nullptr;
    
    for (Subcube* subcube : existingSubcubes) {
        if (!subcube || !subcube->isVisible()) continue;
        
        glm::ivec3 subcubeLocalPos = subcube->getLocalPosition();
        
        // Calculate subcube's world bounding box (each subcube is 1/3 scale within the parent cube)
        float subcubeSize = 1.0f / 3.0f;
        glm::vec3 subcubeMin = glm::vec3(voxelHit.worldPos) + glm::vec3(subcubeLocalPos) * subcubeSize;
        glm::vec3 subcubeMax = subcubeMin + glm::vec3(subcubeSize);
        
        // Ray-AABB intersection test
        float intersectionDistance;
        if (rayAABBIntersect(rayOrigin, rayDirection, subcubeMin, subcubeMax, intersectionDistance)) {
            // Check if this is the closest intersection so far
            if (intersectionDistance >= 0.0f && intersectionDistance < closestDistance) {
                closestDistance = intersectionDistance;
                closestSubcube = subcube;
            }
        }
    }
    
    // Return the closest subcube hit, if any
    if (closestSubcube) {
        VoxelLocation subcubeLocation = voxelHit; // Copy base location
        subcubeLocation.subcubePos = closestSubcube->getLocalPosition();
        
        // Debug output to help troubleshoot
        if (debugFlags.hoverDetection) {
            LOG_TRACE_FMT("Application", "[SUBCUBE RESOLVE] Found closest subcube at world pos: (" << voxelHit.worldPos.x << "," << voxelHit.worldPos.y << "," << voxelHit.worldPos.z 
                      << ") subcube: (" << subcubeLocation.subcubePos.x << "," << subcubeLocation.subcubePos.y << "," << subcubeLocation.subcubePos.z 
                      << ") distance: " << closestDistance);
        }
        
        return subcubeLocation;
    }
    
    return VoxelLocation(); // No subcube intersected (ray passed through empty spaces)
}

// Adapter: Convert VoxelLocation to CubeLocation for backward compatibility
Application::CubeLocation Application::voxelLocationToCubeLocation(const VoxelLocation& voxelLoc) const {
    if (!voxelLoc.isValid()) {
        return CubeLocation(); // Invalid location
    }
    
    CubeLocation cubeLocation;
    cubeLocation.chunk = voxelLoc.chunk;
    cubeLocation.localPos = voxelLoc.localPos;
    cubeLocation.worldPos = voxelLoc.worldPos;
    cubeLocation.hitFace = voxelLoc.hitFace;
    cubeLocation.hitNormal = voxelLoc.hitNormal;
    
    if (voxelLoc.isSubcube()) {
        cubeLocation.isSubcube = true;
        cubeLocation.subcubePos = voxelLoc.subcubePos;
    } else {
        cubeLocation.isSubcube = false;
        cubeLocation.subcubePos = glm::ivec3(-1);
    }
    
    return cubeLocation;
}

bool Application::rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir, 
                                  const glm::vec3& aabbMin, const glm::vec3& aabbMax, 
                                  float& distance) const {
    // Standard ray-AABB intersection test
    glm::vec3 invDir = 1.0f / rayDir;
    glm::vec3 t1 = (aabbMin - rayOrigin) * invDir;
    glm::vec3 t2 = (aabbMax - rayOrigin) * invDir;
    
    glm::vec3 tMin = glm::min(t1, t2);
    glm::vec3 tMax = glm::max(t1, t2);
    
    float tNear = glm::max(glm::max(tMin.x, tMin.y), tMin.z);
    float tFar = glm::min(glm::min(tMax.x, tMax.y), tMax.z);
    
    if (tNear > tFar || tFar < 0.0f) {
        return false; // No intersection
    }
    
    distance = tNear > 0.0f ? tNear : tFar;
    return true;
}

void Application::togglePerformanceOverlay() {
    showPerformanceOverlay = !showPerformanceOverlay;
    LOG_INFO_FMT("Application", "Performance Overlay: " << (showPerformanceOverlay ? "ENABLED" : "DISABLED"));
}

void Application::renderPerformanceOverlay() {
    // This function is now replaced by ImGui overlay
    // Console output is only used when ImGui is not available
    return;
}

void Application::setRenderDistance(float distance) {
    if (distance != maxChunkRenderDistance) {
        maxChunkRenderDistance = distance;
        
        // Ensure chunk inclusion distance is always >= render distance
        if (chunkInclusionDistance < maxChunkRenderDistance) {
            chunkInclusionDistance = maxChunkRenderDistance * 1.5f; // 50% buffer
        }
        
        projectionMatrixNeedsUpdate = true; // Force projection matrix recalculation
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
        LOG_INFO_FMT("Application", "Chunk inclusion distance updated to: " << distance);
    }
}

// =============================================================================
// COLOR UTILITY FUNCTIONS
// =============================================================================

glm::vec3 Application::calculateLighterColor(const glm::vec3& originalColor) const {
    // Create a lighter version of the color by mixing with white
    // This approach maintains the color's hue while making it brighter
    const float lightenAmount = 0.4f; // How much lighter (0.0 = no change, 1.0 = pure white)
    
    // Mix the original color with white
    glm::vec3 lighterColor = glm::mix(originalColor, glm::vec3(1.0f), lightenAmount);
    
    // Ensure we don't exceed maximum color values
    lighterColor = glm::min(lighterColor, glm::vec3(1.0f));
    
    return lighterColor;
}


} // namespace VulkanCube
