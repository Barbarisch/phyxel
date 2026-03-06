#include "Application.h"
#include "utils/Logger.h"
#include <iostream>
#include <stdexcept>

int main() {
    Phyxel::Application app;

    try {
        // Initialize the application
        if (!app.initialize()) {
            LOG_ERROR("Main", "Failed to initialize application!");
            return -1;
        }

        // Run the main loop
        app.run();

    } catch (const std::exception& e) {
        LOG_ERROR_FMT("Main", "Application error: " << e.what());
        return -1;
    }

    return 0;
}
