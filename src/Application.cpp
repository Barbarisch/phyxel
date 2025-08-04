#include "Application.h"
#include "utils/FileUtils.h"
#include "utils/Math.h"
#include "utils/PerformanceProfiler.h"
#include <iostream>
#include <iomanip>
#include <chrono>
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
    , cameraPos(10.0f, 10.0f, 10.0f)
    , cameraFront(glm::normalize(glm::vec3(0.0f) - glm::vec3(10.0f, 10.0f, 10.0f)))
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

    // Initialize subsystems
    if (!initializeVulkan()) {
        std::cerr << "Failed to initialize Vulkan!" << std::endl;
        return false;
    }

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
                printProfilingInfo(fpsFrameCount);
                printDetailedTimings();
                
                // Print new performance profiler reports
                performanceProfiler->printFrameReport();
                performanceProfiler->printMemoryReport();
                performanceProfiler->printCullingReport();
            }
            
            fpsFrameCount = 0;
            fpsTimer = currentTime;
        }
        
        // Print basic stats every 60 frames (only when overlay is disabled)
        if (frameCount % 60 == 0 && !showPerformanceOverlay) {
            printPerformanceStats();
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

    if (!vulkanDevice->createUniformBuffers()) {
        std::cerr << "Failed to create uniform buffers!" << std::endl;
        return false;
    }

    if (!vulkanDevice->createDescriptorPool()) {
        std::cerr << "Failed to create descriptor pool!" << std::endl;
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
    
    // Update physics
    physicsWorld->stepSimulation(deltaTime);

    // Get physics transforms and sync with scene
    std::vector<glm::mat4> transforms;
    physicsWorld->getTransforms(transforms);
    sceneManager->syncWithPhysics(transforms);

    // Update scene
    sceneManager->updateInstanceData();
    sceneManager->performFrustumCulling();
    
    // Track culling statistics for performance profiler
    const auto& visibilityBuffer = sceneManager->getVisibilityBuffer();
    uint32_t totalObjects = static_cast<uint32_t>(visibilityBuffer.size());
    uint32_t visibleObjects = 0;
    for (uint32_t visible : visibilityBuffer) {
        if (visible) visibleObjects++;
    }
    uint32_t culledObjects = totalObjects - visibleObjects;
    
    // Record frustum culling statistics (placeholder timing for now)
    performanceProfiler->recordFrustumCulling(totalObjects, culledObjects, 0.0);

    // Set camera in scene manager
    sceneManager->setCamera(cameraPos, cameraFront, cameraUp);
    
    // Update view and projection matrices
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 proj = glm::perspective(
        glm::radians(45.0f), 
        static_cast<float>(windowWidth) / static_cast<float>(windowHeight), 
        0.1f, 
        100.0f
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
    
    // Update scene data for rendering
    auto instanceUpdateStart = std::chrono::high_resolution_clock::now();
    sceneManager->updateInstanceData();
    
    // Update the GPU instance buffer with current scene data
    const auto& sceneInstanceData = sceneManager->getInstanceData();
    if (!sceneInstanceData.empty()) {
        // Convert from Scene::InstanceData to Vulkan::InstanceData (they're identical structs)
        std::vector<Vulkan::InstanceData> vulkanInstances;
        vulkanInstances.reserve(sceneInstanceData.size());
        
        for (const auto& sceneInstance : sceneInstanceData) {
            Vulkan::InstanceData vulkanInstance;
            vulkanInstance.packedData = sceneInstance.packedData;  // Direct copy of packed data
            vulkanInstance.color = sceneInstance.color;
            vulkanInstances.push_back(vulkanInstance);
        }
        
        // Track memory bandwidth for instance buffer update
        size_t instanceDataSize = vulkanInstances.size() * sizeof(Vulkan::InstanceData);
        performanceProfiler->recordMemoryTransfer(instanceDataSize);
        
        vulkanDevice->updateInstanceBuffer(vulkanInstances);
    }
    auto instanceUpdateEnd = std::chrono::high_resolution_clock::now();

    // Prepare uniform buffer data
    auto uboStart = std::chrono::high_resolution_clock::now();
    // Use the user-controlled camera matrices
    glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);
    glm::mat4 proj = glm::perspective(
        glm::radians(45.0f), 
        (float)windowWidth / (float)windowHeight, 
        0.1f, 
        200.0f  // Increased far plane for large scene
    );
    proj[1][1] *= -1; // Flip Y for Vulkan

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
    
    // Draw indexed cubes
    if (cubeCount > 0) {
        // For now, render as a single chunk at origin (0,0,0) with push constants
        glm::vec3 chunkBaseOffset(0.0f, 0.0f, 0.0f);
        vulkanDevice->pushConstants(currentFrame, renderPipeline->getGraphicsLayout(), chunkBaseOffset);
        vulkanDevice->drawIndexed(currentFrame, 36, static_cast<uint32_t>(cubeCount));
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
    
    // Update frame timing stats
    frameTiming.drawCalls = 1;
    frameTiming.vertexCount = static_cast<int>(cubeCount * 36); // 36 vertices per cube
    frameTiming.visibleInstances = static_cast<int>(cubeCount);
    frameTiming.culledInstances = 0;
    
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
    
    // Calculate face masks ONCE after all cubes are added - this was the key optimization in original!
    sceneManager->recalculateFaceMasks();
    
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
    
    // Perform picking
    int hoveredCube = sceneManager->pickCube(cameraPos, rayDirection);
    
    // Update hover state if changed
    if (hoveredCube != lastHoveredCube) {
        if (lastHoveredCube >= 0) {
            sceneManager->clearHoveredCube();
        }
        
        if (hoveredCube >= 0) {
            sceneManager->setHoveredCube(hoveredCube);
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
