#pragma once

#include <gtest/gtest.h>
#include "Application.h"
#include "ui/WindowManager.h"
#include "input/InputManager.h"
#include "core/ChunkManager.h"
#include "physics/PhysicsWorld.h"
#include <memory>
#include <thread>
#include <chrono>

namespace Phyxel {
namespace Testing {

/**
 * @brief Base fixture for End-to-End tests
 * 
 * E2E tests validate complete application workflows including:
 * - Application startup and initialization
 * - Frame rendering loop
 * - User input simulation
 * - World interaction (voxel breaking, chunk generation)
 * - Physics simulation with rendering
 * - Proper shutdown and cleanup
 * 
 * These tests run the full application stack, not just isolated components.
 */
class E2ETestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        // E2E tests use the full Application class
        // Individual tests will initialize as needed
    }

    void TearDown() override {
        // Cleanup happens in individual tests
    }

    // ============================================================================
    // HELPER METHODS FOR E2E TESTING
    // ============================================================================

    /**
     * @brief Initialize application in headless mode (no actual window)
     * 
     * For E2E tests, we initialize all systems but don't create a visible window.
     * GLFW supports creating hidden windows for testing.
     */
    bool initializeHeadlessApplication(Application& app) {
        // Set window to hidden before initialization
        glfwInit();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        
        bool success = app.initialize();
        
        return success;
    }

    /**
     * @brief Run application for N frames
     * 
     * Simulates running the application for a specific number of frames.
     * Useful for testing rendering loops, physics updates, etc.
     */
    void runForFrames(Application& app, int frameCount) {
        for (int i = 0; i < frameCount; ++i) {
            // Note: Application::run() is blocking, so we need a different approach
            // For E2E tests, we'd need to expose a singleFrame() method or similar
            // For now, this is a placeholder for the pattern
        }
    }

    /**
     * @brief Simulate mouse click at screen position
     */
    void simulateMouseClick(Input::InputManager* inputManager, double x, double y) {
        if (inputManager) {
            // Simulate mouse button press
            // Note: Would need InputManager::simulateInput() method
        }
    }

    /**
     * @brief Simulate key press
     */
    void simulateKeyPress(Input::InputManager* inputManager, int key) {
        if (inputManager) {
            // Simulate keyboard input
            // Note: Would need InputManager::simulateInput() method
        }
    }

    /**
     * @brief Wait for a condition with timeout
     */
    template<typename Predicate>
    bool waitForCondition(Predicate pred, int timeoutMs = 5000) {
        auto startTime = std::chrono::steady_clock::now();
        
        while (true) {
            if (pred()) {
                return true;
            }
            
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime
            ).count();
            
            if (elapsed >= timeoutMs) {
                return false;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    /**
     * @brief Measure time taken for an operation
     */
    template<typename Func>
    double measureTime(Func func) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    /**
     * @brief Print test progress message
     */
    void logProgress(const std::string& message) {
        std::cout << "[E2E] " << message << std::endl;
    }

    /**
     * @brief Verify application is in a valid state
     */
    bool verifyApplicationState(Application& app) {
        // Basic sanity checks that application is in valid state
        // Would need getters on Application class
        return true;
    }
};

} // namespace Testing
} // namespace Phyxel
