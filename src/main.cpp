#include "Application.h"
#include <iostream>
#include <stdexcept>

int main() {
    VulkanCube::Application app;

    try {
        // Initialize the application
        if (!app.initialize()) {
            std::cerr << "Failed to initialize application!" << std::endl;
            return -1;
        }

        // Run the main loop
        app.run();

    } catch (const std::exception& e) {
        std::cerr << "Application error: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}
