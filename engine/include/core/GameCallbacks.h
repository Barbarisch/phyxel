#pragma once

#include <string>

namespace Phyxel {
namespace Core {

class EngineRuntime;

/**
 * @brief Interface for game-specific hooks into the engine runtime loop.
 *
 * Implement this interface and pass it to EngineRuntime::run() to receive
 * callbacks at key points in the engine lifecycle: initialization, per-frame
 * update, rendering, and shutdown.
 *
 * Example:
 *   class MyGame : public GameCallbacks {
 *       bool onInitialize(EngineRuntime& engine) override { ... }
 *       void onUpdate(EngineRuntime& engine, float dt) override { ... }
 *       void onRender(EngineRuntime& engine) override { ... }
 *       void onShutdown() override { ... }
 *   };
 */
class GameCallbacks {
public:
    virtual ~GameCallbacks() = default;

    /// Called once after EngineRuntime::initialize() succeeds.
    /// Set up game-specific subsystems, entities, etc.
    /// Return false to abort startup.
    virtual bool onInitialize(EngineRuntime& engine) { return true; }

    /// Called once per frame with the delta time in seconds.
    virtual void onUpdate(EngineRuntime& engine, float deltaTime) {}

    /// Called once per frame after onUpdate, before the frame is presented.
    /// Use this for ImGui overlays, HUD rendering, etc.
    virtual void onRender(EngineRuntime& engine) {}

    /// Called once per frame before update, after input events are polled.
    virtual void onHandleInput(EngineRuntime& engine) {}

    /// Called once when the engine is shutting down.
    /// Clean up game-specific resources here. Core engine subsystems
    /// are still alive at this point.
    virtual void onShutdown() {}

    // ========================================================================
    // Scene lifecycle (optional — only called for multi-scene games)
    // ========================================================================

    /// Called after the old scene has been unloaded but before the new one loads.
    virtual void onSceneUnload(EngineRuntime& /*engine*/, const std::string& /*sceneId*/) {}

    /// Called after a new scene has been loaded and is ready.
    virtual void onSceneLoad(EngineRuntime& /*engine*/, const std::string& /*sceneId*/) {}

    /// Called once the new scene is fully ready (after all loading + nav rebuild).
    virtual void onSceneReady(EngineRuntime& /*engine*/, const std::string& /*sceneId*/) {}
};

} // namespace Core
} // namespace Phyxel
