#pragma once

#include "core/GameCallbacks.h"
#include "core/EngineRuntime.h"

namespace Examples {

/**
 * @brief Minimal example game using EngineRuntime + GameCallbacks.
 *
 * Demonstrates the simplest possible standalone game:
 * - Spawns a flat platform of voxels
 * - Free camera to fly around
 * - No game-specific subsystems needed
 *
 * Usage:
 *   Phyxel::Core::EngineRuntime engine;
 *   Phyxel::Core::EngineConfig config;
 *   engine.initialize(config);
 *   Examples::MinimalGame game;
 *   engine.run(game);
 */
class MinimalGame : public Phyxel::Core::GameCallbacks {
public:
    bool onInitialize(Phyxel::Core::EngineRuntime& engine) override;
    void onUpdate(Phyxel::Core::EngineRuntime& engine, float dt) override;
    void onRender(Phyxel::Core::EngineRuntime& engine) override;
    void onHandleInput(Phyxel::Core::EngineRuntime& engine) override;
    void onShutdown() override;

private:
    bool initialized_ = false;
    float elapsed_ = 0.0f;
};

} // namespace Examples
