#pragma once

#include "core/GameCallbacks.h"
#include "core/EngineRuntime.h"
#include "core/Inventory.h"
#include "core/HealthComponent.h"
#include "ui/GameScreen.h"

namespace Examples {

/**
 * @brief Minimal example game using EngineRuntime + GameCallbacks.
 *
 * Demonstrates:
 * - Flat voxel platform with pillars
 * - Main menu / pause menu / HUD / inventory screen via GameScreen + GameMenus
 * - Free camera to fly around
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

    Phyxel::UI::GameScreen screen_;
    Phyxel::Core::Inventory inventory_;
    Phyxel::Core::HealthComponent health_{100.0f};
};

} // namespace Examples
