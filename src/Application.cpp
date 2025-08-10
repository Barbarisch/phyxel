#include "Application.h"
#include "utils/FileUtils.h"
#include "utils/Math.h"
#include "utils/PerformanceProfiler.h"
#include "examples/MultiChunkDemo.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <cstring>
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
    sceneManager = std::make_unique<Scene::SceneManager>();
    physicsWorld = std::make_unique<Physics::PhysicsWorld>();
    timer = std::make_unique<Timer>();
    chunkManager = std::make_unique<ChunkManager>();

    // Initialize subsystems
    if (!initializeVulkan()) {
        std::cerr << "Failed to initialize Vulkan!" << std::endl;
        return false;
    }

    // Initialize chunk manager after Vulkan is ready
    chunkManager->initialize(vulkanDevice->getDevice(), vulkanDevice->getPhysicalDevice());
    
    // Create 10 chunks in a linear arrangement for testing
    //auto origins = MultiChunkDemo::createLinearChunks(10);
    //auto origins = MultiChunkDemo::createGridChunks(5, 5);
    auto origins = MultiChunkDemo::create3DGridChunks(2, 2, 2);
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

    // Load shaders
    if (!renderPipeline->loadShaders("shaders/cube.vert.spv", "shaders/cube.frag.spv")) {
        std::cerr << "Failed to load graphics shaders!" << std::endl;
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

    // Create frustum culling buffers (support up to 35,000 instances)
    if (!vulkanDevice->createFrustumCullingBuffers(35000)) {
        std::cerr << "Failed to create frustum culling buffers!" << std::endl;
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
    }
    
    // Update physics
    physicsWorld->stepSimulation(deltaTime);

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

    // Update physics simulation (disabled for static cube grid like original code)
    auto physicsStart = std::chrono::high_resolution_clock::now();
    // physicsWorld->stepSimulation(deltaTime); // DISABLED - 32K static cubes don't need physics!
    auto physicsEnd = std::chrono::high_resolution_clock::now();
    
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
    
    // Draw indexed cubes using chunk manager
    if (chunkManager && !chunkManager->chunks.empty()) {
        // Render each chunk separately
        for (size_t i = 0; i < chunkManager->chunks.size(); ++i) {
            const Chunk* chunk = chunkManager->chunks[i].get();
            
            // Bind this chunk's instance buffer
            VkBuffer instanceBuffers[] = {chunk->getInstanceBuffer()};
            VkDeviceSize instanceOffsets[] = {0};
            vkCmdBindVertexBuffers(vulkanDevice->getCommandBuffer(currentFrame), 1, 1, instanceBuffers, instanceOffsets);
            
            // Set chunk origin as push constants for world positioning
            glm::ivec3 worldOrigin = chunk->getWorldOrigin();
            glm::vec3 chunkBaseOffset(worldOrigin.x, worldOrigin.y, worldOrigin.z);
            vulkanDevice->pushConstants(currentFrame, renderPipeline->getGraphicsLayout(), chunkBaseOffset);
            
            // Draw this chunk (32,768 instances)
            vulkanDevice->drawIndexed(currentFrame, 36, chunk->getNumInstances());
        }
        
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
    detailedTiming.physicsTime = std::chrono::duration<double, std::milli>(physicsEnd - physicsStart).count();
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
        // Simple hash for tracking: combine coordinates to create unique ID
        hoveredCube = hoveredLocation.worldPos.x + hoveredLocation.worldPos.y * 1000 + hoveredLocation.worldPos.z * 1000000;
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
                if (chunk->getCubeAt(localPos)) {
                    // Found a valid cube! Return all coordinate info to avoid re-conversion
                    return CubeLocation(chunk, localPos, voxel);
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

void Application::setHoveredCubeInChunksOptimized(const CubeLocation& location) {
    if (!location.isValid()) return;
    
    // Clear previous hover
    clearHoveredCubeInChunksOptimized();
    
    // Use the Chunk class's public interface
    Cube* cube = location.chunk->getCubeAt(location.localPos);
    if (!cube) return;
    
    // Store original color and location for later restoration
    originalHoveredColor = cube->color;
    currentHoveredLocation = location;
    hasHoveredCube = true;
    
    std::cout << "[HOVER] Setting hover at world pos: (" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
              << ") original color: (" << originalHoveredColor.x << "," << originalHoveredColor.y << "," << originalHoveredColor.z << ")" << std::endl;
    
    // Debug output: chunk coordinate information
    glm::ivec3 chunkCoord = ChunkManager::worldToChunkCoord(location.worldPos);
    glm::ivec3 chunkWorldPos = chunkCoord * 32; // Each chunk is 32x32x32
    std::cout << "[HOVER] World pos: (" << location.worldPos.x << "," << location.worldPos.y << "," << location.worldPos.z 
              << ") | Local pos in chunk: (" << location.localPos.x << "," << location.localPos.y << "," << location.localPos.z 
              << ") | Chunk world pos: (" << chunkWorldPos.x << "," << chunkWorldPos.y << "," << chunkWorldPos.z << ")" << std::endl;
    
    // Set hover color (darken the cube by setting it to black)
    glm::vec3 hoverColor = glm::vec3(0.0f, 0.0f, 0.0f); // Black for hover effect
    cube->color = hoverColor;
    
    // Mark chunk for update using optimized dirty tracking
    if (chunkManager) {
        chunkManager->markChunkDirty(location.chunk);
    }
}

void Application::clearHoveredCubeInChunksOptimized() {
    if (hasHoveredCube && currentHoveredLocation.isValid()) {
        // Use the Chunk class's public interface
        Cube* cube = currentHoveredLocation.chunk->getCubeAt(currentHoveredLocation.localPos);
        if (cube) {
            // Restore original color
            cube->color = originalHoveredColor;
            
            // Use the proper dirty tracking system for immediate updates
            if (chunkManager) {
                chunkManager->markChunkDirty(currentHoveredLocation.chunk);
            }
        }
        
        hasHoveredCube = false;
        currentHoveredLocation = CubeLocation(); // Reset to invalid state
    }
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
    originalHoveredColor = cube->color;
    currentHoveredWorldPos = worldPos;
    hasHoveredCube = true;
    
    std::cout << "[HOVER] Setting hover at world pos: (" << worldPos.x << "," << worldPos.y << "," << worldPos.z 
              << ") original color: (" << originalHoveredColor.x << "," << originalHoveredColor.y << "," << originalHoveredColor.z << ")" << std::endl;
    
    // Get chunk coordinate information for debugging
    glm::ivec3 chunkCoord = chunkManager->worldToChunkCoord(worldPos);
    glm::ivec3 localPos = chunkManager->worldToLocalCoord(worldPos);
    glm::ivec3 chunkWorldPos = chunkCoord * 32; // Each chunk is 32x32x32
    std::cout << "[HOVER] World pos: (" << worldPos.x << "," << worldPos.y << "," << worldPos.z 
              << ") | Local pos in chunk: (" << localPos.x << "," << localPos.y << "," << localPos.z 
              << ") | Chunk world pos: (" << chunkWorldPos.x << "," << chunkWorldPos.y << "," << chunkWorldPos.z << ")" << std::endl;
    
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
