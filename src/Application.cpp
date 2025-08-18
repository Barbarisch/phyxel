#include "Application.h"
#include "utils/FileUtils.h"
#include "utils/Math.h"
#include "utils/PerformanceProfiler.h"
#include "examples/MultiChunkDemo.h"
#include "core/DynamicCube.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <cstring>
#include <limits>
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
    , imguiRenderer(std::make_unique<UI::ImGuiRenderer>()) {
    
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
    std::cout << "Initializing VulkanCube Application..." << std::endl;

    // Initialize window first
    if (!initializeWindow()) {
        std::cerr << "Failed to initialize window!" << std::endl;
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
        std::cerr << "Failed to initialize Vulkan!" << std::endl;
        return false;
    }

    // Load shaders for pipelines (after Vulkan is initialized)
    if (!renderPipeline->loadShaders("shaders/cube.vert.spv", "shaders/cube.frag.spv")) {
        std::cerr << "Failed to load static pipeline shaders!" << std::endl;
        return false;
    }
    
    if (!dynamicRenderPipeline->loadShaders("shaders/dynamic_subcube.vert.spv", "shaders/cube.frag.spv")) {
        std::cerr << "Failed to load dynamic pipeline shaders!" << std::endl;
        return false;
    }

    // Initialize chunk manager after Vulkan is ready
    chunkManager->initialize(vulkanDevice->getDevice(), vulkanDevice->getPhysicalDevice());
    
    // Create 10 chunks in a linear arrangement for testing
    //auto origins = MultiChunkDemo::createLinearChunks(10);
    //auto origins = MultiChunkDemo::createGridChunks(5, 5);
    auto origins = MultiChunkDemo::create3DGridChunks(2, 2, 2);
    
    // Debug: Print chunk origins
    std::cout << "=== CHUNK ORIGINS ===" << std::endl;
    for (size_t i = 0; i < origins.size(); ++i) {
        std::cout << "Chunk " << i << " origin: (" << origins[i].x << "," << origins[i].y << "," << origins[i].z << ")" << std::endl;
    }
    std::cout << "=====================" << std::endl;
    
    chunkManager->createChunks(origins);
    
    // Calculate face culling optimization after chunk creation (DISABLED for now)
    // chunkManager->calculateChunkFaceCulling();
    
    // Perform cross-chunk occlusion culling
    chunkManager->performOcclusionCulling();
    
    std::cout << "Created " << chunkManager->chunks.size() << " chunks for testing" << std::endl;

    if (!initializeScene()) {
        std::cerr << "Failed to initialize scene!" << std::endl;
        return false;
    }

    // Initialize camera after window is created
    initializeCamera();

    if (!initializePhysics()) {
        std::cerr << "Failed to initialize physics!" << std::endl;
        return false;
    }

    // Create physics bodies for all chunks AFTER physics world is initialized
    // Each chunk will be a compound shape made from individual cube collision boxes
    std::cout << "Creating chunk physics bodies..." << std::endl;
    for (auto& chunk : chunkManager->chunks) {
        chunk->setPhysicsWorld(physicsWorld.get());
        chunk->createChunkPhysicsBody();
    }

    if (!loadAssets()) {
        std::cerr << "Failed to load assets!" << std::endl;
        return false;
    }

    // Initialize ImGui after Vulkan is fully set up
    if (!imguiRenderer->initialize(window, vulkanDevice.get(), renderPipeline.get())) {
        std::cerr << "Failed to initialize ImGui!" << std::endl;
        return false;
    }

    timer->start();
    std::cout << "Application initialized successfully!" << std::endl;
    return true;
}

void Application::run() {
    std::cout << "Starting VulkanCube application..." << std::endl;
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
        
        // Render ImGui performance overlay instead of console output
        imguiRenderer->renderPerformanceOverlay(
            showPerformanceOverlay,
            timer.get(),
            performanceProfiler.get(),
            frameTiming,
            detailedTimings,
            sceneManager.get(),
            physicsWorld.get(),
            cameraPos,
            frameCount
        );
        
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
    
    std::cout << "Application shutting down..." << std::endl;
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
        std::cerr << "Failed to create Vulkan instance!" << std::endl;
        return false;
    }

    // Setup debug messenger
    if (!vulkanDevice->setupDebugMessenger()) {
        std::cerr << "Failed to setup debug messenger!" << std::endl;
        return false;
    }

    // Create Vulkan surface with the window (must be done before device selection)
    if (!vulkanDevice->createSurface(window)) {
        std::cerr << "Failed to create Vulkan surface!" << std::endl;
        return false;
    }

    // Now pick physical device and create logical device
    if (!vulkanDevice->pickPhysicalDevice()) {
        std::cerr << "Failed to pick physical device!" << std::endl;
        return false;
    }

    if (!vulkanDevice->createLogicalDevice()) {
        std::cerr << "Failed to create logical device!" << std::endl;
        return false;
    }

    // Create swapchain
    if (!vulkanDevice->createSwapChain(windowWidth, windowHeight)) {
        std::cerr << "Failed to create swapchain!" << std::endl;
        return false;
    }

    // Create render pass
    if (!renderPipeline->createRenderPass()) {
        std::cerr << "Failed to create render pass!" << std::endl;
        return false;
    }

    // Create depth resources
    if (!vulkanDevice->createDepthResources()) {
        std::cerr << "Failed to create depth resources!" << std::endl;
        return false;
    }

    // Create framebuffers
    if (!vulkanDevice->createFramebuffers(renderPipeline->getRenderPass())) {
        std::cerr << "Failed to create framebuffers!" << std::endl;
        return false;
    }

    // Load shaders for static pipeline
    if (!renderPipeline->loadShaders("shaders/cube.vert.spv", "shaders/cube.frag.spv")) {
        std::cerr << "Failed to load static graphics shaders!" << std::endl;
        return false;
    }

    // Load shaders for dynamic subcube pipeline
    if (!dynamicRenderPipeline->loadShaders("shaders/dynamic_subcube.vert.spv", "shaders/cube.frag.spv")) {
        std::cerr << "Failed to load dynamic subcube graphics shaders!" << std::endl;
        return false;
    }

    if (!renderPipeline->loadComputeShader("shaders/frustum_cull.comp.spv")) {
        std::cerr << "Failed to load compute shader!" << std::endl;
        return false;
    }

    // Create descriptor set layout before graphics pipeline
    if (!vulkanDevice->createDescriptorSetLayout()) {
        std::cerr << "Failed to create descriptor set layout!" << std::endl;
        return false;
    }

    if (!renderPipeline->createGraphicsPipeline()) {
        std::cerr << "Failed to create static graphics pipeline!" << std::endl;
        return false;
    }

    if (!dynamicRenderPipeline->createGraphicsPipelineForDynamicSubcubes()) {
        std::cerr << "Failed to create dynamic graphics pipeline!" << std::endl;
        return false;
    }

    if (!renderPipeline->createComputePipeline()) {
        return false;
    }

    // Note: Compute descriptor sets will be created after scene initialization
    // when we have actual AABB data to bind

    // Create command buffers
    if (!vulkanDevice->createCommandBuffers()) {
        std::cerr << "Failed to create command buffers!" << std::endl;
        return false;
    }

    // Create rendering buffers
    if (!vulkanDevice->createVertexBuffer()) {
        std::cerr << "Failed to create vertex buffer!" << std::endl;
        return false;
    }

    if (!vulkanDevice->createIndexBuffer()) {
        std::cerr << "Failed to create index buffer!" << std::endl;
        return false;
    }

    if (!vulkanDevice->createInstanceBuffer()) {
        std::cerr << "Failed to create instance buffer!" << std::endl;
        return false;
    }

    // Create dynamic subcube buffer (support up to 1000 dynamic subcubes)
    if (!vulkanDevice->createDynamicSubcubeBuffer(1000)) {
        std::cerr << "Failed to create dynamic subcube buffer!" << std::endl;
        return false;
    }

    // Create frustum culling buffers (support up to 35,000 instances)
    if (!vulkanDevice->createFrustumCullingBuffers(35000)) {
        std::cerr << "Failed to create frustum culling buffers!" << std::endl;
        return false;
    }

    // Create dynamic subcube buffer (support up to 1000 dynamic subcubes)
    if (!vulkanDevice->createDynamicSubcubeBuffer(1000)) {
        std::cerr << "Failed to create dynamic subcube buffer!" << std::endl;
        return false;
    }

    if (!vulkanDevice->createUniformBuffers()) {
        std::cerr << "Failed to create uniform buffers!" << std::endl;
        return false;
    }

    if (!vulkanDevice->createDescriptorPool()) {
        std::cerr << "Failed to create descriptor pool!" << std::endl;
        return false;
    }

    if (!vulkanDevice->createComputeDescriptorPool()) {
        std::cerr << "Failed to create compute descriptor pool!" << std::endl;
        return false;
    }

    if (!vulkanDevice->createDescriptorSets()) {
        std::cerr << "Failed to create descriptor sets!" << std::endl;
        return false;
    }

    // Create synchronization objects
    if (!vulkanDevice->createSyncObjects()) {
        std::cerr << "Failed to create sync objects!" << std::endl;
        return false;
    }

    std::cout << "Vulkan subsystem initialized successfully" << std::endl;
    return true;
}

bool Application::initializeScene() {
    if (!sceneManager->initialize(32)) { // 32x32x32 grid
        return false;
    }

    // Create test scene based on original code
    createTestScene();

    // Now that we have scene data, create compute descriptor sets for frustum culling
    if (!vulkanDevice->createComputeDescriptorSets(renderPipeline.get())) {
        std::cerr << "Failed to create compute descriptor sets!" << std::endl;
        return false;
    }

    std::cout << "Scene subsystem initialized successfully" << std::endl;
    return true;
}

bool Application::initializePhysics() {
    if (!physicsWorld->initialize()) {
        return false;
    }

    // Create ground plane
    physicsWorld->createGround();

    // DON'T add static cubes to physics - they don't need simulation!
    // In the original code, physics was commented out for the 32K static cube grid
    // std::vector<glm::vec3> positions, sizes;
    // sceneManager->getPhysicsData(positions, sizes);
    // physicsWorld->addCubesFromScene(positions, sizes);

    std::cout << "Physics subsystem initialized successfully (static cubes excluded)" << std::endl;
    return true;
}

bool Application::loadAssets() {
    // Load textures, models, etc.
    // For now, just return true as we have basic cube rendering
    
    std::cout << "Assets loaded successfully" << std::endl;
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
        std::cout << "[PHYSICS STEP] Using FIXED timestep: " << (1.0f/60.0f) << "s (60Hz), frame deltaTime: " << deltaTime << std::endl;
    }
    frameCount++;

    // Get physics transforms and sync with scene
    std::vector<glm::mat4> transforms;
    physicsWorld->getTransforms(transforms);
    sceneManager->syncWithPhysics(transforms);

    // DON'T update scene instance data here - it will be done in drawFrame()
    // sceneManager->updateInstanceData();
    
    // NOTE: Frustum culling statistics are now collected in drawFrame() after GPU compute

    // Set camera in scene manager
    sceneManager->setCamera(cameraPos, cameraFront, cameraUp);
    
    // Update view and projection matrices
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 proj = glm::perspective(
        glm::radians(45.0f), 
        static_cast<float>(windowWidth) / static_cast<float>(windowHeight), 
        0.1f, 
        200.0f  // Use same far plane as cached version
    );
    proj[1][1] *= -1; // Flip Y for Vulkan
    
    sceneManager->updateView(view, proj);
}

void Application::render() {
    drawFrame();
}

void Application::renderStaticGeometry() {
    // Render static cubes and static subcubes using the standard pipeline
    // Bind graphics pipeline
    renderPipeline->bindGraphicsPipeline(vulkanDevice->getCommandBuffer(currentFrame));
    
    // Draw indexed cubes using chunk manager
    if (chunkManager && !chunkManager->chunks.empty()) {
        // Render each chunk separately
        for (size_t i = 0; i < chunkManager->chunks.size(); ++i) {
            const Chunk* chunk = chunkManager->chunks[i].get();
            
            // Skip chunks with no static faces
            if (chunk->getNumInstances() == 0) continue;
            
            // Bind this chunk's instance buffer
            VkBuffer instanceBuffers[] = {chunk->getInstanceBuffer()};
            VkDeviceSize instanceOffsets[] = {0};
            vkCmdBindVertexBuffers(vulkanDevice->getCommandBuffer(currentFrame), 1, 1, instanceBuffers, instanceOffsets);
            
            // Set chunk origin as push constants for world positioning
            glm::ivec3 worldOrigin = chunk->getWorldOrigin();
            glm::vec3 chunkBaseOffset(worldOrigin.x, worldOrigin.y, worldOrigin.z);
            vulkanDevice->pushConstants(currentFrame, renderPipeline->getGraphicsLayout(), chunkBaseOffset);
            
            // Draw this chunk's static geometry
            vulkanDevice->drawIndexed(currentFrame, 36, chunk->getNumInstances());
        }
    }
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
        std::cout << "[DEBUG] Dynamic objects: " << subcubeCount << " subcubes, " << cubeCount 
                  << " cubes, " << allDynamicSubcubeFaces.size() << " total faces" << std::endl;
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
    // Wait for previous frame
    vulkanDevice->waitForFence(currentFrame);
    vulkanDevice->resetFence(currentFrame);

    // Acquire next image
    uint32_t imageIndex;
    VkResult result = vulkanDevice->acquireNextImage(currentFrame, &imageIndex);
    
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to acquire swapchain image!" << std::endl;
        return;
    }

    // Update scene data for rendering (optimized for static cubes)
    auto instanceUpdateStart = std::chrono::high_resolution_clock::now();
    bool instanceDataChanged = sceneManager->updateInstanceData();
    
    // Ensure instance buffer is uploaded at least once
    static bool instanceBufferUploaded = false;
    if (!instanceBufferUploaded) {
        instanceDataChanged = true;  // Force upload on first frame
        instanceBufferUploaded = true;
    }
    
    // Only update GPU buffer when data actually changes (not every frame)
    if (instanceDataChanged) {
        const auto& sceneInstanceData = sceneManager->getInstanceData();
        if (!sceneInstanceData.empty()) {
            // Optimize: Use static_cast to avoid copying - both structs are binary compatible
            static_assert(sizeof(VulkanCube::InstanceData) == sizeof(Vulkan::InstanceData), 
                         "Scene and Vulkan InstanceData must be the same size");
            
            std::vector<Vulkan::InstanceData> vulkanInstances;
            vulkanInstances.resize(sceneInstanceData.size());
            
            // Use memcpy for bulk copy instead of element-by-element copy
            std::memcpy(vulkanInstances.data(), sceneInstanceData.data(), 
                       sceneInstanceData.size() * sizeof(Vulkan::InstanceData));
            
            // Track memory bandwidth for instance buffer update
            size_t instanceDataSize = vulkanInstances.size() * sizeof(Vulkan::InstanceData);
            performanceProfiler->recordMemoryTransfer(instanceDataSize);
            
            vulkanDevice->updateInstanceBuffer(vulkanInstances);
            
            // Update AABB buffer for frustum culling with all chunks
            // Update AABB buffer for frustum culling
            auto cubePositions = sceneManager->getCubePositions();
            vulkanDevice->updateAABBBuffer(cubePositions);
        }
    }
    auto instanceUpdateEnd = std::chrono::high_resolution_clock::now();

    // Prepare uniform buffer data (optimized)
    auto uboStart = std::chrono::high_resolution_clock::now();
    // Use the user-controlled camera matrices
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    
    // Cache projection matrix - only recalculate on window resize
    if (projectionMatrixNeedsUpdate) {
        cachedProjectionMatrix = glm::perspective(
            glm::radians(45.0f), 
            (float)windowWidth / (float)windowHeight, 
            0.1f, 
            200.0f  // Increased far plane for large scene
        );
        cachedProjectionMatrix[1][1] *= -1; // Flip Y for Vulkan
        projectionMatrixNeedsUpdate = false;
    }
    
    glm::mat4 proj = cachedProjectionMatrix;

    // Get actual cube count for rendering from chunk manager (multi-chunk system)
    // Get actual cube count for rendering
    size_t cubeCount = sceneManager->getCubeCount();
    auto uboEnd = std::chrono::high_resolution_clock::now();
    
    // Update uniform buffer with camera matrices
    auto uniformUploadStart = std::chrono::high_resolution_clock::now();
    
    // Track memory bandwidth for uniform buffer update
    size_t uniformBufferSize = sizeof(glm::mat4) * 2 + sizeof(uint32_t); // view + proj + cubeCount
    performanceProfiler->recordMemoryTransfer(uniformBufferSize);
    
    vulkanDevice->updateUniformBuffer(currentFrame, view, proj, static_cast<uint32_t>(cubeCount));
    auto uniformUploadEnd = std::chrono::high_resolution_clock::now();

    // Record command buffer
    auto recordStart = std::chrono::high_resolution_clock::now();
    vulkanDevice->resetCommandBuffer(currentFrame);
    vulkanDevice->beginCommandBuffer(currentFrame);
    
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
    
    // Record occlusion culling statistics from chunk manager (if using chunks)
    if (chunkManager && !chunkManager->chunks.empty()) {
        // Get occlusion stats from chunk manager
        auto chunkStats = chunkManager->getPerformanceStats();
        frameTiming.fullyOccludedCubes = static_cast<int>(chunkStats.fullyOccludedCubes);
        frameTiming.partiallyOccludedCubes = static_cast<int>(chunkStats.partiallyOccludedCubes);
        frameTiming.totalHiddenFaces = static_cast<int>(chunkStats.totalHiddenFaces);
        frameTiming.occlusionCulledInstances = static_cast<int>(chunkStats.fullyOccludedCubes);
        frameTiming.faceCulledFaces = static_cast<int>(chunkStats.totalHiddenFaces);
    } else {
        // Fallback to old SceneManager occlusion culling
        int fullyOccluded, partiallyOccluded, totalHiddenFaces;
        sceneManager->getOcclusionCullStats(fullyOccluded, partiallyOccluded, totalHiddenFaces);
        frameTiming.fullyOccludedCubes = fullyOccluded;
        frameTiming.partiallyOccludedCubes = partiallyOccluded;
        frameTiming.totalHiddenFaces = totalHiddenFaces;
        frameTiming.occlusionCulledInstances = fullyOccluded;
        frameTiming.faceCulledFaces = totalHiddenFaces;
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
        // Render static geometry first
        renderStaticGeometry();
        
        // Render dynamic subcubes with separate pipeline
        renderDynamicSubcubes();
        
        // Get accurate performance statistics from chunk manager
        auto chunkStats = chunkManager->getPerformanceStats();
        
        // Update frame timing with chunk-based statistics
        frameTiming.drawCalls = static_cast<int>(chunkManager->chunks.size());
        frameTiming.vertexCount = static_cast<int>(chunkStats.totalVertices);
        frameTiming.visibleInstances = static_cast<int>(chunkStats.totalCubes);
        frameTiming.fullyOccludedCubes = static_cast<int>(chunkStats.fullyOccludedCubes);
        frameTiming.partiallyOccludedCubes = static_cast<int>(chunkStats.partiallyOccludedCubes);
        frameTiming.totalHiddenFaces = static_cast<int>(chunkStats.totalHiddenFaces);
        frameTiming.faceCulledFaces = static_cast<int>(chunkStats.totalHiddenFaces);
        frameTiming.occlusionCulledInstances = static_cast<int>(chunkStats.fullyOccludedCubes);
    } else if (cubeCount > 0) {
        // Fallback to old single-chunk rendering if chunk manager isn't available
        glm::vec3 chunkBaseOffset(0.0f, 0.0f, 0.0f);
        vulkanDevice->pushConstants(currentFrame, renderPipeline->getGraphicsLayout(), chunkBaseOffset);
        vulkanDevice->drawIndexed(currentFrame, 36, static_cast<uint32_t>(cubeCount));
        frameTiming.drawCalls = 1;
        frameTiming.vertexCount = static_cast<int>(cubeCount * 36);
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
        std::cerr << "Failed to submit command buffer!" << std::endl;
        return;
    }
    auto submitEnd = std::chrono::high_resolution_clock::now();

    // Present frame
    auto presentStart = std::chrono::high_resolution_clock::now();
    if (!vulkanDevice->presentFrame(imageIndex, currentFrame)) {
        std::cerr << "Failed to present frame!" << std::endl;
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
    detailedTiming.instanceUpdateTime = std::chrono::duration<double, std::milli>(instanceUpdateEnd - instanceUpdateStart).count();
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
    
    std::cout << "Performance Stats:" << std::endl;
    std::cout << "  FPS: " << fps << std::endl;
    std::cout << "  CPU Frame Time: " << frameTiming.cpuFrameTime << "ms" << std::endl;
    std::cout << "  GPU Frame Time: " << frameTiming.gpuFrameTime << "ms" << std::endl;
    std::cout << "  Vertices: " << frameTiming.vertexCount << std::endl;
    std::cout << "  Draw Calls: " << frameTiming.drawCalls << std::endl;
    std::cout << "  Visible Instances: " << frameTiming.visibleInstances << std::endl;
    std::cout << "  Culled Instances: " << frameTiming.culledInstances << std::endl;
    std::cout << "  Cubes in Scene: " << sceneManager->getCubeCount() << std::endl;
    std::cout << "  Physics Bodies: " << physicsWorld->getRigidBodyCount() << std::endl;
    std::cout << "---" << std::endl;
}

void Application::createTestScene() {
    // Generate the full 32x32x32 chunk like the original code
    std::vector<glm::vec3> palette = {
        {0.0f, 1.0f, 0.0f}, // green
        {0.0f, 0.0f, 1.0f}, // blue
        {1.0f, 1.0f, 0.0f}, // yellow
        {1.0f, 0.0f, 1.0f}, // magenta
        {0.0f, 1.0f, 1.0f}  // cyan
    };
    
    sceneManager->clearCubes();
    
    // Create full 32x32x32 dense grid like the original
    int gridSize = 32;
    for (int y = 0; y < gridSize; ++y) {
        for (int z = 0; z < gridSize; ++z) {
            for (int x = 0; x < gridSize; ++x) {
                // Add cube at each position
                sceneManager->addCube(x, y, z);
                
                // Set random color from palette (this will need to be done in SceneManager)
                // For now just add the cube and let SceneManager assign colors
            }
        }
    }
    
    // Calculate face masks and perform occlusion culling ONCE after all cubes are added
    sceneManager->recalculateFaceMasks();
    sceneManager->performOcclusionCulling();
    
    std::cout << "Created test scene with " << sceneManager->getCubeCount() << " cubes (full 32x32x32 grid)" << std::endl;
}

bool Application::initializeWindow() {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW!" << std::endl;
        return false;
    }

    // Configure GLFW for Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    // Create window
    window = glfwCreateWindow(windowWidth, windowHeight, windowTitle.c_str(), nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window!" << std::endl;
        glfwTerminate();
        return false;
    }

    std::cout << "Window initialized successfully" << std::endl;
    return true;
}

void Application::initializeCamera() {
    // Set up mouse callbacks
    glfwSetWindowUserPointer(window, this);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    
    // Keep cursor visible and free - no mouse capture
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    
    std::cout << "Camera controls initialized - hold right mouse button and drag to look around" << std::endl;
}

void Application::mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    Application* app = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
    
    // Always track mouse position for hover detection
    app->currentMouseX = xpos;
    app->currentMouseY = ypos;
    
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
        std::cout << "Camera look: yaw=" << std::fixed << std::setprecision(1) 
                  << app->yaw << "° pitch=" << app->pitch << "°" << std::endl;
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
            std::cout << "*** RIGHT MOUSE PRESSED - CAMERA LOOK MODE ENABLED ***" << std::endl;
        } else if (action == GLFW_RELEASE) {
            // Stop camera rotation mode
            app->mouseCaptured = false;
            std::cout << "*** RIGHT MOUSE RELEASED - CAMERA LOOK MODE DISABLED ***" << std::endl;
        }
    }
    
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        // Check for Ctrl modifier for subdivision
        if (mods & GLFW_MOD_CONTROL) {
            // Ctrl+Left Click: Subdivide cube into 27 subcubes
            app->subdivideHoveredCube();
        } else {
            // Left Click: Break objects with physics (both cubes and subcubes)
            if (app->hasHoveredCube && app->currentHoveredLocation.isSubcube) {
                // Break subcube with physics (existing behavior)
                app->subdivideHoveredCube();
            } else {
                // Break regular cube into dynamic cube with physics
                app->breakHoveredCube();
            }
        }
    }
    
    if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
        // Middle Click: Subdivide cube into 27 subcubes
        app->subdivideHoveredCube();
    }
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
    
    // Test frustum culling positions with T key
    static bool tPressed = false;
    if (glfwGetKey(window, GLFW_KEY_T) == GLFW_PRESS && !tPressed) {
        static int testPosition = 0;
        testPosition = (testPosition + 1) % 4;
        
        switch (testPosition) {
            case 0: // Outside view (default)
                cameraPos = glm::vec3(50.0f, 50.0f, 50.0f);
                cameraFront = glm::normalize(glm::vec3(16.0f, 16.0f, 16.0f) - cameraPos);
                std::cout << "[DEBUG] Test position 1: Outside view (should see most cubes)" << std::endl;
                break;
            case 1: // Inside grid, looking at corner
                cameraPos = glm::vec3(16.0f, 16.0f, 16.0f);
                cameraFront = glm::normalize(glm::vec3(31.0f, 31.0f, 31.0f) - cameraPos);
                std::cout << "[DEBUG] Test position 2: Inside grid, corner view (should cull ~50%)" << std::endl;
                break;
            case 2: // Ground level, looking up
                cameraPos = glm::vec3(16.0f, -5.0f, 16.0f);
                cameraFront = glm::normalize(glm::vec3(16.0f, 16.0f, 16.0f) - cameraPos);
                std::cout << "[DEBUG] Test position 3: Ground level (should cull most cubes)" << std::endl;
                break;
            case 3: // Edge view
                cameraPos = glm::vec3(-10.0f, 16.0f, 16.0f);
                cameraFront = glm::normalize(glm::vec3(31.0f, 16.0f, 16.0f) - cameraPos);
                std::cout << "[DEBUG] Test position 4: Edge view (should cull ~50%)" << std::endl;
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
    std::cout << "=== SPAWNING TEST DYNAMIC SUBCUBE ===" << std::endl;
    
    // Spawn above the center of the 2x2x2 chunk grid
    // Chunk grid spans from (0,0,0) to (64,64,64) with Y being UP
    // Center is (32,32,32), top surface is at Y=64
    // Spawn clearly above at center: (32,80,32) - well above all chunks in Y
    glm::vec3 spawnPosition = glm::vec3(32.0f, 80.0f, 32.0f);
    
    std::cout << "Spawn position: (" << spawnPosition.x << ", " << spawnPosition.y << ", " << spawnPosition.z << ")" << std::endl;
    std::cout << "Camera position: (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")" << std::endl;
    
    // Create a new dynamic subcube with raw pointer (matching existing pattern)
    Subcube* dynamicSubcube = new Subcube(spawnPosition, glm::vec3(1.0f, 0.0f, 0.0f)); // Red color
    
    // Create physics body for the dynamic subcube (match visual size)
    glm::vec3 subcubeSize = glm::vec3(1.0f / 3.0f); // Match visual subcube size
    
    std::cout << "[PHYSICS DEBUG] G-spawning subcube at world pos (" 
              << spawnPosition.x << "," << spawnPosition.y << "," << spawnPosition.z 
              << ") with physics size " << subcubeSize.x << std::endl;
    
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
        
        std::cout << "Created physics body for dynamic subcube with angular velocity: (" 
                  << randomAngularVelocity.x() << ", " << randomAngularVelocity.y() << ", " << randomAngularVelocity.z() << ")" << std::endl;
    } else {
        std::cout << "ERROR: Failed to create physics body for dynamic subcube" << std::endl;
    }
    
    // Add to global dynamic subcube management (not tied to any specific chunk)
    if (chunkManager) {
        chunkManager->addGlobalDynamicSubcube(std::unique_ptr<Subcube>(dynamicSubcube));
        std::cout << "Added dynamic subcube to global management" << std::endl;
        std::cout << "Press G again to spawn another subcube!" << std::endl;
    } else {
        std::cout << "ERROR: No chunk manager available for global dynamic subcube management" << std::endl;
    }
}

FrameTiming Application::profileFrame() {
    auto frameEnd = std::chrono::high_resolution_clock::now();
    
    FrameTiming timing;
    timing.cpuFrameTime = std::chrono::duration<double, std::milli>(frameEnd - frameStartTime).count();
    timing.gpuFrameTime = timing.cpuFrameTime; // Placeholder - actual GPU timing would need GPU queries
    
    // Use chunk manager statistics if available (multi-chunk system)
    timing.vertexCount = static_cast<int>(sceneManager->getCubeCount() * 36); // 36 vertices per cube
    timing.drawCalls = 1;
    timing.visibleInstances = static_cast<int>(sceneManager->getCubeCount());
    timing.culledInstances = 0; // Would need actual frustum culling data
    
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
    
    std::cout << "\n=== FRAME PROFILING ===\n";
    std::cout << "FPS: " << fps << "\n";
    std::cout << "Avg CPU Frame Time: " << std::fixed << std::setprecision(2) << avgCpuTime << "ms\n";
    std::cout << "Avg Vertices/Frame: " << avgVertices << "\n";
    std::cout << "Avg Visible Cubes: " << avgVisible << "\n";
    std::cout << "Avg Culled Cubes: " << avgCulled << "\n";
    
    if (avgVisible + avgCulled > 0) {
        std::cout << "Culling Efficiency: " << std::fixed << std::setprecision(1) 
                  << (100.0 * avgCulled / (avgVisible + avgCulled)) << "%\n";
    }
    
    std::cout << "Camera Position: (" << std::fixed << std::setprecision(1) 
              << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")\n";
    std::cout << "======================\n";
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
    
    std::cout << "\n=== DETAILED FRAME TIMING ===\n";
    std::cout << "Total Frame Time:    " << std::fixed << std::setprecision(2) << avg.totalFrameTime << "ms\n";
    std::cout << "  Physics:           " << std::fixed << std::setprecision(2) << avg.physicsTime << "ms\n";
    std::cout << "  UBO Fill:          " << std::fixed << std::setprecision(2) << avg.uboFillTime << "ms\n";
    std::cout << "  Instance Update:   " << std::fixed << std::setprecision(2) << avg.instanceUpdateTime << "ms\n";
    std::cout << "  Uniform Upload:    " << std::fixed << std::setprecision(2) << avg.uniformUploadTime << "ms\n";
    std::cout << "  Command Record:    " << std::fixed << std::setprecision(2) << avg.commandRecordTime << "ms\n";
    std::cout << "  GPU Submit:        " << std::fixed << std::setprecision(2) << avg.gpuSubmitTime << "ms\n";
    std::cout << "  Present:           " << std::fixed << std::setprecision(2) << avg.presentTime << "ms\n";
    std::cout << "  Mouse Pick:        " << std::fixed << std::setprecision(2) << avg.mousePickTime << "ms\n";
    
    // Calculate percentage breakdown
    double total = avg.totalFrameTime;
    if (total > 0) {
        std::cout << "\nTiming Breakdown:\n";
        std::cout << "  Physics:         " << std::fixed << std::setprecision(1) << (avg.physicsTime / total * 100.0) << "%\n";
        std::cout << "  UBO Fill:        " << std::fixed << std::setprecision(1) << (avg.uboFillTime / total * 100.0) << "%\n";
        std::cout << "  Instance Update: " << std::fixed << std::setprecision(1) << (avg.instanceUpdateTime / total * 100.0) << "%\n";
        std::cout << "  Uniform Upload:  " << std::fixed << std::setprecision(1) << (avg.uniformUploadTime / total * 100.0) << "%\n";
        std::cout << "  Command Record:  " << std::fixed << std::setprecision(1) << (avg.commandRecordTime / total * 100.0) << "%\n";
        std::cout << "  GPU Submit:      " << std::fixed << std::setprecision(1) << (avg.gpuSubmitTime / total * 100.0) << "%\n";
        std::cout << "  Present:         " << std::fixed << std::setprecision(1) << (avg.presentTime / total * 100.0) << "%\n";
    }
    std::cout << "============================\n";
}

void Application::updateMouseHover() {
    // Skip hover detection if right mouse button is pressed (camera mode)
    if (mouseCaptured) {
        return;
    }
    
    // Create ray from mouse position
    glm::vec3 rayDirection = screenToWorldRay(currentMouseX, currentMouseY);
    
    // Use optimized ChunkManager for O(1) cube picking with DDA algorithm (avoids repeated coordinate conversions)
    CubeLocation hoveredLocation = pickCubeInChunksOptimized(cameraPos, rayDirection);
    
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
// OPTIMIZED CHUNK-BASED HOVER DETECTION WITH DDA ALGORITHM
// =============================================================================

Application::CubeLocation Application::pickCubeInChunksOptimized(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const {
    if (!chunkManager) {
        return CubeLocation(); // Invalid location
    }
    
    // DDA algorithm for efficient voxel traversal
    // Based on "A Fast Voxel Traversal Algorithm for Ray Tracing" by John Amanatides and Andrew Woo
    
    float maxDistance = 200.0f; // Maximum ray distance
    
    // Current voxel position (integer coordinates)
    glm::ivec3 voxel = glm::ivec3(glm::floor(rayOrigin));
    
    // Direction to step in each dimension (-1, 0, or 1)
    glm::ivec3 step = glm::ivec3(
        rayDirection.x > 0 ? 1 : (rayDirection.x < 0 ? -1 : 0),
        rayDirection.y > 0 ? 1 : (rayDirection.y < 0 ? -1 : 0),
        rayDirection.z > 0 ? 1 : (rayDirection.z < 0 ? -1 : 0)
    );
    
    // Calculate delta distances (how far along the ray we must travel for each component to cross one grid line)
    glm::vec3 deltaDist = glm::vec3(
        rayDirection.x != 0 ? glm::abs(1.0f / rayDirection.x) : std::numeric_limits<float>::max(),
        rayDirection.y != 0 ? glm::abs(1.0f / rayDirection.y) : std::numeric_limits<float>::max(),
        rayDirection.z != 0 ? glm::abs(1.0f / rayDirection.z) : std::numeric_limits<float>::max()
    );
    
    // Calculate step and initial sideDist
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
    
    // Perform DDA traversal
    int maxSteps = 500; // Prevent infinite loops
    for (int step_count = 0; step_count < maxSteps; ++step_count) {
        // Calculate chunk and local coordinates ONCE per voxel
        glm::ivec3 chunkCoord = ChunkManager::worldToChunkCoord(voxel);
        glm::ivec3 localPos = ChunkManager::worldToLocalCoord(voxel);
        
        // Bounds check local position
        if (localPos.x >= 0 && localPos.x < 32 &&
            localPos.y >= 0 && localPos.y < 32 &&
            localPos.z >= 0 && localPos.z < 32) {
            
            // Get chunk using O(1) hash map lookup
            Chunk* chunk = chunkManager->getChunkAtCoord(chunkCoord);
            if (chunk) {
                // Use the Chunk class's public interface
                Cube* cube = chunk->getCubeAt(localPos);
                if (cube) {
                    // Check if cube is subdivided - if so, find specific subcube
                    if (cube->isSubdivided()) {
                        // Instead of geometric calculation, test actual existing subcubes
                        CubeLocation subcubeHit = findExistingSubcubeHit(chunk, localPos, voxel, rayOrigin, rayDirection);
                        if (subcubeHit.isValid()) {
                            return subcubeHit;
                        }
                    } else if (cube->isVisible()) {
                        // Regular visible cube - return normal cube location
                        return CubeLocation(chunk, localPos, voxel);
                    }
                    // If cube exists but is not visible and not subdivided, skip it
                }
            }
        }
        
        // Move to next voxel
        if (sideDist.x < sideDist.y && sideDist.x < sideDist.z) {
            // X-face hit
            sideDist.x += deltaDist.x;
            voxel.x += step.x;
        } else if (sideDist.y < sideDist.z) {
            // Y-face hit
            sideDist.y += deltaDist.y;
            voxel.y += step.y;
        } else {
            // Z-face hit
            sideDist.z += deltaDist.z;
            voxel.z += step.z;
        }
        
        // Check if we've gone too far
        glm::vec3 currentPos = glm::vec3(voxel);
        if (glm::length(currentPos - rayOrigin) > maxDistance) {
            break;
        }
    }
    
    return CubeLocation(); // No cube found
}

glm::ivec3 Application::findSubcubeHit(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::ivec3& cubeWorldPos) const {
    // Convert cube world position to cube-local ray
    glm::vec3 cubeLocalOrigin = rayOrigin - glm::vec3(cubeWorldPos);
    
    // Subcubes are 1/3 scale, so we have a 3x3x3 grid within each cube
    float subcubeSize = 1.0f / 3.0f;
    
    // Use DDA algorithm within the cube for precise subcube detection
    glm::vec3 currentPos = cubeLocalOrigin;
    
    // If ray origin is outside the cube, find entry point
    if (currentPos.x < 0.0f || currentPos.x >= 1.0f ||
        currentPos.y < 0.0f || currentPos.y >= 1.0f ||
        currentPos.z < 0.0f || currentPos.z >= 1.0f) {
        
        // Ray-AABB intersection to find entry point into the cube
        glm::vec3 cubeMin = glm::vec3(0.0f);
        glm::vec3 cubeMax = glm::vec3(1.0f);
        
        glm::vec3 invDir = 1.0f / rayDirection;
        glm::vec3 t1 = (cubeMin - cubeLocalOrigin) * invDir;
        glm::vec3 t2 = (cubeMax - cubeLocalOrigin) * invDir;
        
        glm::vec3 tMin = glm::min(t1, t2);
        glm::vec3 tMax = glm::max(t1, t2);
        
        float tNear = glm::max(glm::max(tMin.x, tMin.y), tMin.z);
        float tFar = glm::min(glm::min(tMax.x, tMax.y), tMax.z);
        
        if (tNear <= tFar && tFar >= 0.0f) {
            // Ray enters the cube at tNear
            float entryT = tNear >= 0.0f ? tNear : 0.0f;
            currentPos = cubeLocalOrigin + rayDirection * (entryT + 0.001f); // Small offset to avoid edge issues
        } else {
            return glm::ivec3(-1); // No intersection with cube
        }
    }
    
    // Now we're inside the cube, determine which subcube we hit first
    // Use small step size for precision
    float stepSize = 0.01f;
    int maxSteps = 200; // Maximum steps to traverse the cube
    
    for (int i = 0; i < maxSteps; ++i) {
        // Check if we're still within cube bounds [0, 1]
        if (currentPos.x < 0.0f || currentPos.x >= 1.0f ||
            currentPos.y < 0.0f || currentPos.y >= 1.0f ||
            currentPos.z < 0.0f || currentPos.z >= 1.0f) {
            break; // Ray exited the cube
        }
        
        // Calculate which subcube we're in (0-2 in each dimension)
        glm::ivec3 subcubePos = glm::ivec3(
            glm::clamp(int(currentPos.x / subcubeSize), 0, 2),
            glm::clamp(int(currentPos.y / subcubeSize), 0, 2),
            glm::clamp(int(currentPos.z / subcubeSize), 0, 2)
        );
        
        return subcubePos; // Return first subcube hit
        
        // Move along ray
        currentPos += rayDirection * stepSize;
    }
    
    return glm::ivec3(-1); // No subcube hit
}

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
    
    return CubeLocation(); // No intersection found
}

void Application::setHoveredCubeInChunksOptimized(const CubeLocation& location) {
    if (!location.isValid()) return;
    
    // Debug: Show what type of location we're hovering
    std::cout << "[HOVER DEBUG] Hovering - isSubcube: " << location.isSubcube 
              << ", world pos: (" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z << ")";
    if (location.isSubcube) {
        std::cout << ", subcube pos: (" << location.subcubePos.x << "," << location.subcubePos.y << "," << location.subcubePos.z << ")";
    }
    std::cout << std::endl;
    
    // Clear previous hover
    clearHoveredCubeInChunksOptimized();
    
    // Handle subcube vs regular cube hover differently
    if (location.isSubcube) {
        // Hovering over a specific subcube
        Cube* cube = location.chunk->getCubeAt(location.localPos);
        if (!cube || !cube->isSubdivided()) return;
        
        Subcube* subcube = cube->getSubcubeAt(location.subcubePos);
        if (!subcube) return;
        
        // Store original subcube color and location for later restoration
        originalHoveredColor = subcube->getOriginalColor(); // Use original color, not current color
        currentHoveredLocation = location;
        hasHoveredCube = true;
        
        std::cout << "[SUBCUBE HOVER] Setting hover at world pos: (" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                  << ") subcube: (" << location.subcubePos.x << "," << location.subcubePos.y << "," << location.subcubePos.z 
                  << ") original color: (" << originalHoveredColor.x << "," << originalHoveredColor.y << "," << originalHoveredColor.z << ")" << std::endl;
        
        // Set hover color (make it bright white for clear visibility)
        glm::vec3 hoverColor = glm::vec3(1.0f, 1.0f, 1.0f); // Bright white for subcube hover
        
        // Use efficient subcube color update
        if (chunkManager) {
            chunkManager->setSubcubeColorEfficient(location.worldPos, location.subcubePos, hoverColor);
        }
    } else {
        // Hovering over a regular cube (not subdivided)
        Cube* cube = location.chunk->getCubeAt(location.localPos);
        if (!cube) return;
        
        // Store original color and location for later restoration
        originalHoveredColor = cube->getColor();
        currentHoveredLocation = location;
        hasHoveredCube = true;
        
        std::cout << "[CUBE HOVER] Setting hover at world pos: (" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
                  << ") original color: (" << originalHoveredColor.x << "," << originalHoveredColor.y << "," << originalHoveredColor.z << ")" << std::endl;
        
        // Set hover color (darken the cube by setting it to black)
        glm::vec3 hoverColor = glm::vec3(0.0f, 0.0f, 0.0f); // Black for regular cube hover
        
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
                std::cout << "[SUBCUBE HOVER] Cleared hover for subcube at world pos: (" << currentHoveredLocation.worldPos.x << "," << currentHoveredLocation.worldPos.y << "," << currentHoveredLocation.worldPos.z 
                          << ") subcube: (" << currentHoveredLocation.subcubePos.x << "," << currentHoveredLocation.subcubePos.y << "," << currentHoveredLocation.subcubePos.z << ")" << std::endl;
            } else {
                // Restore regular cube color
                chunkManager->setCubeColorEfficient(currentHoveredLocation.worldPos, originalHoveredColor);
                std::cout << "[CUBE HOVER] Cleared hover for cube at world pos: (" << currentHoveredLocation.worldPos.x << "," << currentHoveredLocation.worldPos.y << "," << currentHoveredLocation.worldPos.z << ")" << std::endl;
            }
        }
        
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation(); // Reset to invalid state
    }
}

void Application::removeHoveredCube() {
    // Check if we have a valid hovered cube
    if (!hasHoveredCube || !currentHoveredLocation.isValid()) {
        std::cout << "[CUBE REMOVAL] No cube is currently being hovered - cannot remove" << std::endl;
        return;
    }
    
    // Get the chunk and remove the cube using the stored location
    Chunk* chunk = currentHoveredLocation.chunk;
    if (!chunk) {
        std::cout << "[CUBE REMOVAL] ERROR: Invalid chunk pointer" << std::endl;
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation();
        return;
    }
    
    bool removed = false;
    
    if (currentHoveredLocation.isSubcube) {
        // Remove a specific subcube
        removed = chunk->removeSubcube(currentHoveredLocation.localPos, currentHoveredLocation.subcubePos);
        if (removed) {
            std::cout << "[SUBCUBE REMOVAL] Successfully removed subcube at world pos: (" 
                      << currentHoveredLocation.worldPos.x << "," 
                      << currentHoveredLocation.worldPos.y << "," 
                      << currentHoveredLocation.worldPos.z << ") subcube: ("
                      << currentHoveredLocation.subcubePos.x << ","
                      << currentHoveredLocation.subcubePos.y << ","
                      << currentHoveredLocation.subcubePos.z << ")" << std::endl;
                      
            // Check if this was the last subcube - if so, restore the parent cube
            auto remainingSubcubes = chunk->getSubcubesAt(currentHoveredLocation.localPos);
            if (remainingSubcubes.empty()) {
                // No more subcubes left, restore the parent cube
                Cube* parentCube = chunk->getCubeAt(currentHoveredLocation.localPos);
                if (parentCube) {
                    parentCube->show(); // Make parent cube visible again
                    std::cout << "[CUBE RESTORATION] Restored parent cube as no subcubes remain" << std::endl;
                }
            }
        }
    } else {
        // Remove a regular cube (this will also remove all its subcubes if subdivided)
        Cube* cube = chunk->getCubeAt(currentHoveredLocation.localPos);
        if (cube && cube->isSubdivided()) {
            // First clear all subcubes
            chunk->clearSubdivisionAt(currentHoveredLocation.localPos);
            std::cout << "[SUBDIVISION REMOVAL] Cleared all subcubes for cube removal" << std::endl;
        }
        
        // Remove the cube itself
        removed = chunk->removeCube(currentHoveredLocation.localPos);
        if (removed) {
            std::cout << "[CUBE REMOVAL] Successfully removed cube at world pos: (" 
                      << currentHoveredLocation.worldPos.x << "," 
                      << currentHoveredLocation.worldPos.y << "," 
                      << currentHoveredLocation.worldPos.z << ")" << std::endl;
        }
    }
    
    if (removed) {
        // No need to mark chunk dirty - removal methods now immediately update GPU buffer
        
        // Clear hover state since the object no longer exists
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation();
        lastHoveredCube = -1; // Reset the hover tracking
        
    } else {
        std::cout << "[REMOVAL] WARNING: Failed to remove object - it may not exist" << std::endl;
    }
}

void Application::subdivideHoveredCube() {
    // Check if we have a valid hovered cube
    if (!hasHoveredCube || !currentHoveredLocation.isValid()) {
        std::cout << "[CUBE SUBDIVISION] No cube is currently being hovered - cannot subdivide" << std::endl;
        return;
    }
    
    // Get the chunk using the stored location
    Chunk* chunk = currentHoveredLocation.chunk;
    if (!chunk) {
        std::cout << "[CUBE SUBDIVISION] ERROR: Invalid chunk pointer" << std::endl;
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation();
        return;
    }
    
    // Handle different cases based on what's being hovered
    if (currentHoveredLocation.isSubcube) {
        // Break a specific subcube (move from static to dynamic) with physics
        // Calculate impulse force away from the camera direction
        glm::vec3 impulseForce(0.0f, 2.0f, 0.0f); // Default upward force
        
        // Try to get a more interesting force direction based on camera position
        glm::vec3 cubeWorldPos = glm::vec3(currentHoveredLocation.worldPos);
        glm::vec3 forceDirection = normalize(cubeWorldPos - cameraPos);
        
        // Mix upward force with outward force for interesting breakage
        impulseForce = forceDirection * 3.0f + glm::vec3(0.0f, 5.0f, 0.0f);
        
        std::cout << "[PHYSICS] Applying breakage force: (" 
                  << impulseForce.x << "," << impulseForce.y << "," << impulseForce.z << ")" << std::endl;
        
        bool broken = chunk->breakSubcube(currentHoveredLocation.localPos, currentHoveredLocation.subcubePos, 
                                         physicsWorld.get(), impulseForce);
        if (broken) {
            // Transfer the newly broken subcube from chunk-based to global system
            auto& chunkDynamicSubcubes = const_cast<std::vector<Subcube*>&>(chunk->getDynamicSubcubes());
            if (!chunkDynamicSubcubes.empty()) {
                // The newly broken subcube should be the last one added
                Subcube* brokenSubcube = chunkDynamicSubcubes.back();
                if (brokenSubcube) {
                    // Create a unique_ptr copy for the global system
                    auto globalSubcube = std::make_unique<Subcube>(*brokenSubcube);
                    
                    // Restore the original color (in case it was affected by hover)
                    globalSubcube->setColor(globalSubcube->getOriginalColor());
                    
                    // Keep the subcube's original color (don't override with parent cube color)
                    std::cout << "[COLOR] Preserving subcube's original color: (" 
                              << globalSubcube->getOriginalColor().x << "," << globalSubcube->getOriginalColor().y << "," << globalSubcube->getOriginalColor().z << ")" << std::endl;
                    
                    // Transfer the physics body ownership
                    globalSubcube->setRigidBody(brokenSubcube->getRigidBody());
                    brokenSubcube->setRigidBody(nullptr);  // Prevent double deletion
                    
                    // Add to global system
                    chunkManager->addGlobalDynamicSubcube(std::move(globalSubcube));
                    
                    // Remove from chunk's dynamic list
                    delete brokenSubcube;
                    chunkDynamicSubcubes.pop_back();
                    
                    // Update chunk faces since we removed a dynamic subcube
                    chunk->rebuildDynamicSubcubeFaces();
                    
                    std::cout << "[TRANSFER] Moved broken subcube from chunk to global dynamic system" << std::endl;
                }
            }
            
            std::cout << "[SUBCUBE BREAKING] Successfully broke subcube at world pos: (" 
                      << currentHoveredLocation.worldPos.x << "," 
                      << currentHoveredLocation.worldPos.y << "," 
                      << currentHoveredLocation.worldPos.z << ") subcube: ("
                      << currentHoveredLocation.subcubePos.x << ","
                      << currentHoveredLocation.subcubePos.y << ","
                      << currentHoveredLocation.subcubePos.z << ")" << std::endl;
        } else {
            std::cout << "[SUBCUBE BREAKING] WARNING: Failed to break subcube" << std::endl;
        }
    } else {
        // Check if cube is already subdivided
        if (chunk->getSubcubesAt(currentHoveredLocation.localPos).size() > 0) {
            std::cout << "[CUBE SUBDIVISION] Cube at world pos (" 
                      << currentHoveredLocation.worldPos.x << "," 
                      << currentHoveredLocation.worldPos.y << "," 
                      << currentHoveredLocation.worldPos.z << ") is already subdivided" << std::endl;
            return;
        }
        
        // Subdivide the cube into 27 static subcubes
        bool subdivided = chunk->subdivideAt(currentHoveredLocation.localPos);
        if (subdivided) {
            std::cout << "[CUBE SUBDIVISION] Successfully subdivided cube at world pos: (" 
                      << currentHoveredLocation.worldPos.x << "," 
                      << currentHoveredLocation.worldPos.y << "," 
                      << currentHoveredLocation.worldPos.z << ") into 27 static subcubes" << std::endl;
        } else {
            std::cout << "[CUBE SUBDIVISION] WARNING: Failed to subdivide cube - cube may not exist" << std::endl;
        }
    }
    
    // Mark the chunk as dirty for GPU buffer update
    if (chunkManager) {
        chunkManager->markChunkDirty(chunk);
    }
    
    // Clear hover state
    hasHoveredCube = false;
    currentHoveredLocation = CubeLocation();
    lastHoveredCube = -1;
}

void Application::breakHoveredCube() {
    // Check if we have a valid hovered cube
    if (!hasHoveredCube || !currentHoveredLocation.isValid()) {
        std::cout << "[CUBE BREAKING] No cube is currently being hovered - cannot break" << std::endl;
        return;
    }
    
    // Only break regular cubes (not subcubes)
    if (currentHoveredLocation.isSubcube) {
        std::cout << "[CUBE BREAKING] Cannot break individual subcubes - use left click to break subcubes" << std::endl;
        return;
    }
    
    // Get the chunk using the stored location
    Chunk* chunk = currentHoveredLocation.chunk;
    if (!chunk) {
        std::cout << "[CUBE BREAKING] ERROR: Invalid chunk pointer" << std::endl;
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation();
        return;
    }
    
    // Check if cube exists and is not already subdivided
    if (!chunk->getCubeAt(currentHoveredLocation.localPos)) {
        std::cout << "[CUBE BREAKING] No cube exists at this location" << std::endl;
        return;
    }
    
    if (chunk->getSubcubesAt(currentHoveredLocation.localPos).size() > 0) {
        std::cout << "[CUBE BREAKING] Cube is already subdivided - cannot break subdivided cubes" << std::endl;
        return;
    }
    
    // Calculate impulse force away from the camera direction
    glm::vec3 impulseForce(0.0f, 2.0f, 0.0f); // Default upward force
    
    // Try to get a more interesting force direction based on camera position
    glm::vec3 cubeWorldPos = glm::vec3(currentHoveredLocation.worldPos);
    glm::vec3 forceDirection = normalize(cubeWorldPos - cameraPos);
    
    // Mix upward force with outward force for interesting breakage
    impulseForce = forceDirection * 4.0f + glm::vec3(0.0f, 6.0f, 0.0f);
    
    std::cout << "[PHYSICS] Applying breakage force to cube: (" 
              << impulseForce.x << "," << impulseForce.y << "," << impulseForce.z << ")" << std::endl;
    
    // Get the cube's original color before removing it
    const Cube* originalCube = chunk->getCubeAt(currentHoveredLocation.localPos);
    glm::vec3 originalColor = glm::vec3(0.8f, 0.6f, 0.4f); // Default color fallback
    if (originalCube) {
        originalColor = originalCube->getOriginalColor(); // Use original color, not hover-affected color
        std::cout << "[COLOR] Preserving original cube color: (" 
                  << originalColor.x << "," << originalColor.y << "," << originalColor.z << ")" << std::endl;
    }
    
    // Remove the cube from the chunk
    bool removed = chunk->removeCube(currentHoveredLocation.localPos);
    if (!removed) {
        std::cout << "[CUBE BREAKING] WARNING: Failed to remove cube from chunk" << std::endl;
        return;
    }
    
    // Create a dynamic cube at the same position with the original color
    auto dynamicCube = std::make_unique<DynamicCube>(cubeWorldPos, originalColor);
    
    // Create physics body for the dynamic cube
    glm::vec3 cubeSize(1.0f); // Full cube size
    btRigidBody* rigidBody = physicsWorld->createCube(cubeWorldPos, cubeSize, 2.0f); // 2.0kg mass (heavier than subcubes)
    dynamicCube->setRigidBody(rigidBody);
    
    // Set initial physics position
    dynamicCube->setPhysicsPosition(cubeWorldPos);
    
    // Apply initial impulse force to make it "break" away
    if (rigidBody && glm::length(impulseForce) > 0.0f) {
        btVector3 btImpulse(impulseForce.x, impulseForce.y, impulseForce.z);
        rigidBody->applyCentralImpulse(btImpulse);
        
        // Add random angular velocity for tumbling effect
        btVector3 angularVelocity(
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f,
            (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f
        );
        rigidBody->setAngularVelocity(angularVelocity);
        
        std::cout << "[PHYSICS] Applied impulse (" << impulseForce.x << "," << impulseForce.y << "," << impulseForce.z 
                  << ") and angular velocity (" << angularVelocity.x() << "," << angularVelocity.y() << "," << angularVelocity.z() << ")" << std::endl;
    }
    
    // Mark as broken
    dynamicCube->breakApart();
    
    // Add to global dynamic cubes system
    chunkManager->addGlobalDynamicCube(std::move(dynamicCube));
    
    // Mark the chunk as dirty for GPU buffer update
    chunkManager->markChunkDirty(chunk);
    
    std::cout << "[CUBE BREAKING] Successfully broke cube at world pos: (" 
              << currentHoveredLocation.worldPos.x << "," 
              << currentHoveredLocation.worldPos.y << "," 
              << currentHoveredLocation.worldPos.z << ") into dynamic cube" << std::endl;
    
    // Clear hover state
    hasHoveredCube = false;
    currentHoveredLocation = CubeLocation();
    lastHoveredCube = -1;
}

// =============================================================================
// LEGACY CHUNK-BASED HOVER DETECTION WITH DDA ALGORITHM (for compatibility)
// =============================================================================

glm::ivec3 Application::pickCubeInChunks(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const {
    if (!chunkManager) {
        return glm::ivec3(-1); // Invalid position to indicate no hit
    }
    
    // DDA algorithm for efficient voxel traversal
    // Based on "A Fast Voxel Traversal Algorithm for Ray Tracing" by John Amanatides and Andrew Woo
    
    float maxDistance = 200.0f; // Maximum ray distance
    glm::vec3 rayEnd = rayOrigin + rayDirection * maxDistance;
    
    // Current voxel position (integer coordinates)
    glm::ivec3 voxel = glm::ivec3(glm::floor(rayOrigin));
    
    // Direction to step in each dimension (-1, 0, or 1)
    glm::ivec3 step = glm::ivec3(
        rayDirection.x > 0 ? 1 : (rayDirection.x < 0 ? -1 : 0),
        rayDirection.y > 0 ? 1 : (rayDirection.y < 0 ? -1 : 0),
        rayDirection.z > 0 ? 1 : (rayDirection.z < 0 ? -1 : 0)
    );
    
    // Calculate delta distances (how far along the ray we must travel for each component to cross one grid line)
    glm::vec3 deltaDist = glm::vec3(
        rayDirection.x != 0 ? glm::abs(1.0f / rayDirection.x) : std::numeric_limits<float>::max(),
        rayDirection.y != 0 ? glm::abs(1.0f / rayDirection.y) : std::numeric_limits<float>::max(),
        rayDirection.z != 0 ? glm::abs(1.0f / rayDirection.z) : std::numeric_limits<float>::max()
    );
    
    // Calculate step and initial sideDist
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
    
    // Debug output (only print occasionally to avoid spam)
    static int debugCounter = 0;
    bool shouldDebug = (++debugCounter % 30 == 0); // Every 30th call
    
    // if (shouldDebug) {
    //     std::cout << "[HOVER] Ray: origin(" << rayOrigin.x << "," << rayOrigin.y << "," << rayOrigin.z 
    //               << ") dir(" << rayDirection.x << "," << rayDirection.y << "," << rayDirection.z << ")" << std::endl;
    //     std::cout << "[HOVER] Starting voxel: (" << voxel.x << "," << voxel.y << "," << voxel.z << ")" << std::endl;
    // }
    
    // Perform DDA traversal
    int maxSteps = 500; // Prevent infinite loops
    for (int step_count = 0; step_count < maxSteps; ++step_count) {
        // Check if current voxel contains a cube using O(1) chunk manager lookup
        if (chunkManager->getCubeAtFast(voxel)) {
            // if (shouldDebug) {
            //     std::cout << "[HOVER] Found cube at voxel: (" << voxel.x << "," << voxel.y << "," << voxel.z 
            //               << ") after " << step_count << " steps" << std::endl;
            // }
            return voxel; // Found a cube!
        }
        
        // Move to next voxel
        if (sideDist.x < sideDist.y && sideDist.x < sideDist.z) {
            // X-face hit
            sideDist.x += deltaDist.x;
            voxel.x += step.x;
        } else if (sideDist.y < sideDist.z) {
            // Y-face hit
            sideDist.y += deltaDist.y;
            voxel.y += step.y;
        } else {
            // Z-face hit
            sideDist.z += deltaDist.z;
            voxel.z += step.z;
        }
        
        // Check if we've gone too far
        glm::vec3 currentPos = glm::vec3(voxel);
        if (glm::length(currentPos - rayOrigin) > maxDistance) {
            break;
        }
    }
    
    // if (shouldDebug) {
    //     std::cout << "[HOVER] No cube found after " << maxSteps << " steps" << std::endl;
    // }
    
    return glm::ivec3(-1); // No cube found
}

void Application::setHoveredCubeInChunks(const glm::ivec3& worldPos) {
    if (!chunkManager) return;
    
    // Clear previous hover
    clearHoveredCubeInChunks();
    
    // Get the cube at this world position using O(1) lookup
    Cube* cube = chunkManager->getCubeAtFast(worldPos);
    if (!cube) return;
    
    // Store original color and world position for later restoration
    originalHoveredColor = cube->getColor();
    currentHoveredWorldPos = worldPos;
    hasHoveredCube = true;
    
    // std::cout << "[HOVER] Setting hover at world pos: (" << worldPos.x << "," << worldPos.y << "," << worldPos.z 
    //           << ") original color: (" << originalHoveredColor.x << "," << originalHoveredColor.y << "," << originalHoveredColor.z << ")" << std::endl;
    
    // // Get chunk coordinate information for debugging
    // glm::ivec3 chunkCoord = chunkManager->worldToChunkCoord(worldPos);
    // glm::ivec3 localPos = chunkManager->worldToLocalCoord(worldPos);
    // glm::ivec3 chunkWorldPos = chunkCoord * 32; // Each chunk is 32x32x32
    // std::cout << "[HOVER] World pos: (" << worldPos.x << "," << worldPos.y << "," << worldPos.z 
    //           << ") | Local pos in chunk: (" << localPos.x << "," << localPos.y << "," << localPos.z 
    //           << ") | Chunk world pos: (" << chunkWorldPos.x << "," << chunkWorldPos.y << "," << chunkWorldPos.z << ")" << std::endl;
    
    // Set hover color (darken the cube by setting it to black)
    glm::vec3 hoverColor = glm::vec3(0.0f, 0.0f, 0.0f); // Black for hover effect
    chunkManager->setCubeColorFast(worldPos, hoverColor);
}

void Application::clearHoveredCubeInChunks() {
    if (hasHoveredCube && chunkManager) {
        //std::cout << "[HOVER] Clearing hover at world pos: (" << currentHoveredWorldPos.x << "," << currentHoveredWorldPos.y << "," << currentHoveredWorldPos.z << ")" << std::endl;
        
        // Restore original color
        chunkManager->setCubeColorFast(currentHoveredWorldPos, originalHoveredColor);
        hasHoveredCube = false;
        currentHoveredWorldPos = glm::ivec3(-1);
    }
}

glm::ivec3 Application::raycastVoxelGrid(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const glm::ivec3& chunkOrigin) const {
    // This is a helper method for more advanced chunk-specific raycasting
    // For now, it delegates to the main pickCubeInChunks method
    // In a more advanced implementation, this could optimize by only checking within a specific chunk
    return pickCubeInChunks(rayOrigin, rayDirection);
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
    std::cout << "Performance Overlay: " << (showPerformanceOverlay ? "ENABLED" : "DISABLED") << std::endl;
}

void Application::renderPerformanceOverlay() {
    // This function is now replaced by ImGui overlay
    // Console output is only used when ImGui is not available
    return;
}

} // namespace VulkanCube
