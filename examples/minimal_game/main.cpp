/**
 * @file main.cpp
 * @brief Entry point for the minimal example game.
 *
 * Demonstrates the simplest way to create a standalone game using the
 * Phyxel engine: create an EngineRuntime, implement GameCallbacks, run.
 */

#include "MinimalGame.h"
#include "core/EngineRuntime.h"
#include "core/EngineConfig.h"
#include "utils/Logger.h"

int main() {
    // Load engine configuration (or use defaults)
    Phyxel::Core::EngineConfig config;
    Phyxel::Core::EngineConfig::loadFromFile("engine.json", config);

    // Create the engine runtime
    Phyxel::Core::EngineRuntime engine;
    if (!engine.initialize(config)) {
        LOG_ERROR("main", "Failed to initialize engine");
        return 1;
    }

    // Create and run the game
    Examples::MinimalGame game;
    engine.run(game);

    return 0;
}
