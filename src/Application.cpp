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
    // Create core components
    windowManager = std::make_unique<UI::WindowManager>();
    vulkanDevice = std::make_unique<Vulkan::VulkanDevice>();
    renderPipeline = std::make_unique<Vulkan::RenderPipeline>(*vulkanDevice);
    dynamicRenderPipeline = std::make_unique<Vulkan::RenderPipeline>(*vulkanDevice);
    sceneManager = std::make_unique<Scene::SceneManager>();
    physicsWorld = std::make_unique<Physics::PhysicsWorld>();
    timer = std::make_unique<Timer>();
    chunkManager = std::make_unique<ChunkManager>();

    // Create WorldInitializer with all dependencies
    worldInitializer = std::make_unique<Core::WorldInitializer>(
        windowManager.get(),
        inputManager.get(),
        vulkanDevice.get(),
        renderPipeline.get(),
        dynamicRenderPipeline.get(),
        sceneManager.get(),
        physicsWorld.get(),
        timer.get(),
        chunkManager.get(),
        forceSystem.get(),
        mouseVelocityTracker.get(),
        performanceProfiler.get(),
        performanceMonitor.get(),
        imguiRenderer.get()
    );

    // Configure render distances
    worldInitializer->setMaxChunkRenderDistance(maxChunkRenderDistance);
    worldInitializer->setChunkInclusionDistance(chunkInclusionDistance);

    // Delegate all initialization to WorldInitializer
    if (!worldInitializer->initialize()) {
        LOG_ERROR("Application", "WorldInitializer failed!");
        return false;
    }

    // Initialize VoxelInteractionSystem after WorldInitializer (dependencies are ready)
    voxelInteractionSystem = std::make_unique<VoxelInteractionSystem>(
        chunkManager.get(),
        physicsWorld.get(),
        mouseVelocityTracker.get(),
        windowManager.get(),
        forceSystem.get()
    );
    LOG_INFO("Application", "VoxelInteractionSystem initialized successfully!");

    // Initialize RenderCoordinator after WorldInitializer (dependencies are ready)
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

    // Register all input actions (kept in Application)
    initializeInputActions();

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
