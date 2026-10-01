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

#include <cstdlib>
#include <string>

int main(int argc, char* argv[]) {
    // Load engine configuration (or use defaults)
    Phyxel::Core::EngineConfig config;
    Phyxel::Core::EngineConfig::loadFromFile("engine.json", config);

    // DEV/TEST ONLY: expose the real standalone game's localhost API for the
    // production validator. It remains disabled for normal player launches.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--test" || arg == "--api") {
            config.testApiEnabled = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                config.apiPort = std::atoi(argv[++i]);
            }
        }
    }

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
