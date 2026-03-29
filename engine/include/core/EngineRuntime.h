#pragma once

#include "core/EngineConfig.h"
#include <memory>
#include <functional>

// Forward declarations — no heavy includes in this header
namespace Phyxel {
    namespace UI {
        class WindowManager;
        class ImGuiRenderer;
    }
    namespace Input { class InputManager; }
    namespace Vulkan {
        class VulkanDevice;
        class RenderPipeline;
    }
    namespace Physics { class PhysicsWorld; }
    namespace Graphics {
        class Camera;
        class CameraManager;
    }
    namespace Utils { class PerformanceMonitor; }
    class Timer;
    class ChunkManager;
    class ForceSystem;
    class MouseVelocityTracker;
    class PerformanceProfiler;
    namespace Core { class LocationRegistry; }
}

namespace Phyxel {
namespace Core {

class WorldInitializer;
class GameCallbacks;
class AudioSystem;

/**
 * @brief Core engine runtime — owns and initializes all engine subsystems.
 *
 * EngineRuntime handles the heavy lifting of creating a window, setting up
 * Vulkan rendering, initializing physics, loading the voxel world, and
 * providing a frame loop.  Games create an EngineRuntime, call initialize(),
 * then either use run(GameCallbacks&) for a standard loop or drive the loop
 * manually with beginFrame() / endFrame().
 *
 * Usage (callback style):
 *   EngineRuntime engine;
 *   MyGame game;
 *   if (!engine.initialize(config)) return 1;
 *   engine.run(game);            // blocks until window closed
 *
 * Usage (manual loop):
 *   EngineRuntime engine;
 *   engine.initialize(config);
 *   while (!engine.shouldClose()) {
 *       float dt = engine.beginFrame();
 *       // ... game logic using engine.getXxx() ...
 *       engine.endFrame();
 *   }
 *   engine.shutdown();
 */
class EngineRuntime {
public:
    EngineRuntime();
    ~EngineRuntime();

    // Non-copyable, non-movable (owns Vulkan resources)
    EngineRuntime(const EngineRuntime&) = delete;
    EngineRuntime& operator=(const EngineRuntime&) = delete;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// Create all core subsystems from the given configuration.
    /// Returns false on critical failure (window, Vulkan, etc.).
    bool initialize(const EngineConfig& config);

    /// Standard game loop — calls GameCallbacks hooks each frame.
    /// Blocks until the window is closed or quit() is called.
    void run(GameCallbacks& game);

    /// Tear down all subsystems in the correct order.
    /// Safe to call multiple times.
    void shutdown();

    /// Signal the runtime to exit at the end of the current frame.
    void quit();

    // ========================================================================
    // Manual frame control (alternative to run())
    // ========================================================================

    /// Poll events, update timer, return delta time.
    float beginFrame();

    /// Finish the frame (increment counter, etc.).
    void endFrame();

    /// True when the window close button has been clicked or quit() called.
    bool shouldClose() const;

    // ========================================================================
    // Subsystem accessors (non-owning pointers — lifetime = EngineRuntime)
    // ========================================================================

    UI::WindowManager*          getWindowManager()          const;
    Vulkan::VulkanDevice*       getVulkanDevice()           const;
    Vulkan::RenderPipeline*     getRenderPipeline()         const;
    Vulkan::RenderPipeline*     getDynamicRenderPipeline()  const;
    Physics::PhysicsWorld*      getPhysicsWorld()           const;
    Timer*                      getTimer()                  const;
    ChunkManager*               getChunkManager()           const;
    AudioSystem*                getAudioSystem()            const;
    Input::InputManager*        getInputManager()           const;
    ForceSystem*                getForceSystem()            const;
    UI::ImGuiRenderer*          getImGuiRenderer()          const;
    MouseVelocityTracker*       getMouseVelocityTracker()   const;
    PerformanceProfiler*        getPerformanceProfiler()    const;
    Utils::PerformanceMonitor*  getPerformanceMonitor()     const;
    Graphics::Camera*           getCamera()                 const;
    Graphics::CameraManager*    getCameraManager()          const;
    Core::LocationRegistry*     getLocationRegistry()       const;

    /// Access the active configuration (read-only).
    const EngineConfig& getConfig() const { return config_; }

    /// Access the active configuration (mutable — for project switching).
    EngineConfig& getConfigMutable() { return config_; }

    /// Frame counter (incremented each endFrame).
    int getFrameCount() const { return frameCount_; }

    /// Delta time from the most recent beginFrame() call.
    float getLastDeltaTime() const { return lastDeltaTime_; }

    /// True after initialize() succeeds and before shutdown().
    bool isInitialized() const { return initialized_; }

private:
    // Configuration
    EngineConfig config_;
    bool initialized_ = false;
    bool quitRequested_ = false;
    int  frameCount_ = 0;
    float lastDeltaTime_ = 0.0f;
    double lastFrameTime_ = 0.0;

    // ========================================================================
    // Owned subsystems (created in initialize, destroyed in shutdown)
    // ========================================================================

    // Window & display
    std::unique_ptr<UI::WindowManager> windowManager_;

    // Vulkan rendering
    std::unique_ptr<Vulkan::VulkanDevice>    vulkanDevice_;
    std::unique_ptr<Vulkan::RenderPipeline>  renderPipeline_;
    std::unique_ptr<Vulkan::RenderPipeline>  dynamicRenderPipeline_;
    std::unique_ptr<UI::ImGuiRenderer>       imguiRenderer_;

    // World & physics
    std::unique_ptr<ChunkManager>            chunkManager_;
    std::unique_ptr<Physics::PhysicsWorld>   physicsWorld_;
    std::unique_ptr<ForceSystem>             forceSystem_;

    // Input
    std::unique_ptr<Input::InputManager>     inputManager_;
    std::unique_ptr<MouseVelocityTracker>    mouseVelocityTracker_;

    // Timing & profiling
    std::unique_ptr<Timer>                   timer_;
    std::unique_ptr<PerformanceProfiler>     performanceProfiler_;
    std::unique_ptr<Utils::PerformanceMonitor> performanceMonitor_;

    // Audio
    std::unique_ptr<AudioSystem>             audioSystem_;

    // Camera
    std::unique_ptr<Graphics::Camera>        camera_;
    std::unique_ptr<Graphics::CameraManager> cameraManager_;

    // Locations
    std::unique_ptr<Core::LocationRegistry>  locationRegistry_;

    // Initialization orchestrator (kept alive for potential re-init)
    std::unique_ptr<WorldInitializer>        worldInitializer_;
};

} // namespace Core
} // namespace Phyxel
