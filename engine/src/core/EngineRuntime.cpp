#include "core/EngineRuntime.h"
#include "core/GameCallbacks.h"
#include "core/AssetManager.h"
#include "core/WorldInitializer.h"
#include "core/AudioSystem.h"
#include "core/LocationRegistry.h"
#include "ui/WindowManager.h"
#include "ui/ImGuiRenderer.h"
#include "input/InputManager.h"
#include "vulkan/VulkanDevice.h"
#include "vulkan/RenderPipeline.h"
#include "physics/PhysicsWorld.h"
#include "graphics/Camera.h"
#include "graphics/CameraManager.h"
#include "core/ChunkManager.h"
#include "core/ForceSystem.h"
#include "utils/Timer.h"
#include "utils/PerformanceProfiler.h"
#include "utils/PerformanceMonitor.h"
#include "utils/Logger.h"
#include "scene/VoxelInteractionSystem.h"  // MouseVelocityTracker
#include <GLFW/glfw3.h>
#include <thread>
#include <chrono>

namespace Phyxel {
namespace Core {

// ============================================================================
// Construction / Destruction
// ============================================================================

EngineRuntime::EngineRuntime() = default;

EngineRuntime::~EngineRuntime() {
    shutdown();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool EngineRuntime::initialize(const EngineConfig& config) {
    if (initialized_) {
        LOG_WARN("EngineRuntime", "Already initialized — ignoring duplicate call");
        return true;
    }

    config_ = config;

    // Initialize global AssetManager from config
    AssetManager::instance().initialize(config_);

    // -- Create subsystems that need no dependencies first --
    performanceProfiler_ = std::make_unique<PerformanceProfiler>();
    performanceMonitor_  = std::make_unique<Utils::PerformanceMonitor>();
    imguiRenderer_       = std::make_unique<UI::ImGuiRenderer>();
    inputManager_        = std::make_unique<Input::InputManager>();
    forceSystem_         = std::make_unique<ForceSystem>();
    mouseVelocityTracker_= std::make_unique<MouseVelocityTracker>();

    // -- Create core components --
    windowManager_          = std::make_unique<UI::WindowManager>();
    vulkanDevice_           = std::make_unique<Vulkan::VulkanDevice>();
    renderPipeline_         = std::make_unique<Vulkan::RenderPipeline>(*vulkanDevice_);
    dynamicRenderPipeline_  = std::make_unique<Vulkan::RenderPipeline>(*vulkanDevice_);
    physicsWorld_           = std::make_unique<Physics::PhysicsWorld>();
    timer_                  = std::make_unique<Timer>();
    chunkManager_           = std::make_unique<ChunkManager>();
    audioSystem_            = std::make_unique<AudioSystem>();

    if (!audioSystem_->initialize()) {
        LOG_ERROR("EngineRuntime", "Failed to initialize AudioSystem");
        // Non-critical — continue
    }

    // -- WorldInitializer orchestrates the complex init sequence --
    worldInitializer_ = std::make_unique<WorldInitializer>(
        windowManager_.get(),
        inputManager_.get(),
        vulkanDevice_.get(),
        renderPipeline_.get(),
        dynamicRenderPipeline_.get(),
        physicsWorld_.get(),
        timer_.get(),
        chunkManager_.get(),
        forceSystem_.get(),
        mouseVelocityTracker_.get(),
        performanceProfiler_.get(),
        performanceMonitor_.get(),
        imguiRenderer_.get(),
        &config_
    );

    if (!worldInitializer_->initialize()) {
        LOG_ERROR("EngineRuntime", "WorldInitializer failed!");
        return false;
    }

    // -- Camera (uses InputManager state set by WorldInitializer) --
    auto camPos = inputManager_->getCameraPosition();
    float yaw   = inputManager_->getYaw();
    float pitch  = inputManager_->getPitch();
    camera_ = std::make_unique<Graphics::Camera>(
        camPos, glm::vec3(0.0f, 1.0f, 0.0f), yaw, pitch);
    camera_->setMode(Graphics::CameraMode::Free);
    cameraManager_ = std::make_unique<Graphics::CameraManager>(camera_.get());

    // Location registry for named world locations
    locationRegistry_ = std::make_unique<Core::LocationRegistry>();

    // Sync InputManager with Camera state
    inputManager_->setCameraPosition(camera_->getPosition());
    inputManager_->setYawPitch(camera_->getYaw(), camera_->getPitch());

    initialized_ = true;
    lastFrameTime_ = glfwGetTime();
    LOG_INFO("EngineRuntime", "Engine runtime initialized successfully");
    return true;
}

void EngineRuntime::run(GameCallbacks& game) {
    if (!initialized_) {
        LOG_ERROR("EngineRuntime", "Cannot run — not initialized");
        return;
    }

    if (!game.onInitialize(*this)) {
        LOG_ERROR("EngineRuntime", "GameCallbacks::onInitialize() failed");
        return;
    }

    quitRequested_ = false;

    while (!shouldClose()) {
        float dt = beginFrame();
        game.onHandleInput(*this);
        game.onUpdate(*this, dt);
        game.onRender(*this);
        endFrame();
    }

    game.onShutdown();
    shutdown();
}

void EngineRuntime::shutdown() {
    if (!initialized_) return;
    initialized_ = false;

    LOG_INFO("EngineRuntime", "Shutting down core engine...");

    // ImGui must be cleaned up before Vulkan device
    if (imguiRenderer_) imguiRenderer_->cleanup();

    // Render pipelines hold Vulkan resources
    if (renderPipeline_)        renderPipeline_->cleanup();
    if (dynamicRenderPipeline_) dynamicRenderPipeline_->cleanup();

    // Save and clean up chunks before Vulkan device goes away
    if (chunkManager_) {
        LOG_INFO("EngineRuntime", "Saving modified chunks to database...");
        auto saveStart = std::chrono::steady_clock::now();
        chunkManager_->saveDirtyChunks();
        auto saveEnd = std::chrono::steady_clock::now();
        auto saveMs = std::chrono::duration_cast<std::chrono::milliseconds>(saveEnd - saveStart).count();
        LOG_INFO("EngineRuntime", "Chunk save complete ({} ms)", saveMs);

        LOG_INFO("EngineRuntime", "Cleaning up chunk resources...");
        auto cleanStart = std::chrono::steady_clock::now();
        chunkManager_->cleanup();
        auto cleanEnd = std::chrono::steady_clock::now();
        auto cleanMs = std::chrono::duration_cast<std::chrono::milliseconds>(cleanEnd - cleanStart).count();
        LOG_INFO("EngineRuntime", "Chunk cleanup complete ({} ms)", cleanMs);
    }

    // Vulkan device
    if (vulkanDevice_) vulkanDevice_->cleanup();

    // Physics
    if (physicsWorld_) physicsWorld_->cleanup();

    // Audio
    audioSystem_.reset();

    // Window (GLFW)
    windowManager_.reset();

    // Release remaining subsystems
    cameraManager_.reset();
    camera_.reset();
    worldInitializer_.reset();
    renderPipeline_.reset();
    dynamicRenderPipeline_.reset();
    vulkanDevice_.reset();
    physicsWorld_.reset();
    timer_.reset();
    chunkManager_.reset();
    imguiRenderer_.reset();
    forceSystem_.reset();
    inputManager_.reset();
    mouseVelocityTracker_.reset();
    performanceProfiler_.reset();
    performanceMonitor_.reset();

    LOG_INFO("EngineRuntime", "Core engine shut down");
}

void EngineRuntime::quit() {
    quitRequested_ = true;
}

// ============================================================================
// Manual frame control
// ============================================================================

float EngineRuntime::beginFrame() {
    performanceProfiler_->startFrame();

    // Reset mouse delta before polling new events
    if (inputManager_) inputManager_->resetMouseDelta();

    // Poll GLFW events
    windowManager_->pollEvents();

    // Compute delta time from wall clock
    double currentTime = glfwGetTime();
    float dt = static_cast<float>(currentTime - lastFrameTime_);
    lastFrameTime_ = currentTime;
    lastDeltaTime_ = dt;

    timer_->update();
    return dt;
}

void EngineRuntime::endFrame() {
    performanceProfiler_->endFrame();
    ++frameCount_;
}

bool EngineRuntime::shouldClose() const {
    if (quitRequested_) return true;
    if (windowManager_) return windowManager_->shouldClose();
    return true;
}

// ============================================================================
// Accessors
// ============================================================================

UI::WindowManager*          EngineRuntime::getWindowManager()          const { return windowManager_.get(); }
Vulkan::VulkanDevice*       EngineRuntime::getVulkanDevice()           const { return vulkanDevice_.get(); }
Vulkan::RenderPipeline*     EngineRuntime::getRenderPipeline()         const { return renderPipeline_.get(); }
Vulkan::RenderPipeline*     EngineRuntime::getDynamicRenderPipeline()  const { return dynamicRenderPipeline_.get(); }
Physics::PhysicsWorld*      EngineRuntime::getPhysicsWorld()           const { return physicsWorld_.get(); }
Timer*                      EngineRuntime::getTimer()                  const { return timer_.get(); }
ChunkManager*               EngineRuntime::getChunkManager()           const { return chunkManager_.get(); }
AudioSystem*                EngineRuntime::getAudioSystem()            const { return audioSystem_.get(); }
Input::InputManager*        EngineRuntime::getInputManager()           const { return inputManager_.get(); }
ForceSystem*                EngineRuntime::getForceSystem()            const { return forceSystem_.get(); }
UI::ImGuiRenderer*          EngineRuntime::getImGuiRenderer()          const { return imguiRenderer_.get(); }
MouseVelocityTracker*       EngineRuntime::getMouseVelocityTracker()   const { return mouseVelocityTracker_.get(); }
PerformanceProfiler*        EngineRuntime::getPerformanceProfiler()    const { return performanceProfiler_.get(); }
Utils::PerformanceMonitor*  EngineRuntime::getPerformanceMonitor()     const { return performanceMonitor_.get(); }
Graphics::Camera*           EngineRuntime::getCamera()                 const { return camera_.get(); }
Graphics::CameraManager*    EngineRuntime::getCameraManager()          const { return cameraManager_.get(); }
Core::LocationRegistry*     EngineRuntime::getLocationRegistry()       const { return locationRegistry_.get(); }

} // namespace Core
} // namespace Phyxel
