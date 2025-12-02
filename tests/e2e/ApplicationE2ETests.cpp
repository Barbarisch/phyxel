#include "E2ETestFixture.h"
#include <gtest/gtest.h>

using namespace VulkanCube;
using namespace VulkanCube::Testing;

/**
 * @brief End-to-End tests for Application lifecycle and workflows
 * 
 * These tests verify complete application workflows from initialization
 * through execution to cleanup.
 */

// ============================================================================
// APPLICATION LIFECYCLE TESTS
// ============================================================================

TEST_F(E2ETestFixture, ApplicationInitializationAndShutdown) {
    logProgress("Testing application initialization and shutdown workflow");
    
    Application app;
    
    // Test initialization
    logProgress("Initializing application...");
    double initTime = measureTime([&]() {
        ASSERT_TRUE(app.initialize());
    });
    
    logProgress("Application initialized in " + std::to_string(initTime) + " ms");
    EXPECT_LT(initTime, 5000.0); // Should initialize within 5 seconds
    
    // Test cleanup
    logProgress("Cleaning up application...");
    double cleanupTime = measureTime([&]() {
        app.cleanup();
    });
    
    logProgress("Application cleaned up in " + std::to_string(cleanupTime) + " ms");
    EXPECT_LT(cleanupTime, 1000.0); // Should cleanup within 1 second
    
    logProgress("✓ Application lifecycle test completed");
}

TEST_F(E2ETestFixture, MultipleInitializationCycles) {
    logProgress("Testing multiple init/cleanup cycles");
    
    for (int cycle = 0; cycle < 3; ++cycle) {
        logProgress("Cycle " + std::to_string(cycle + 1) + "/3");
        
        Application app;
        ASSERT_TRUE(app.initialize());
        
        // Brief pause to simulate running
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        app.cleanup();
    }
    
    logProgress("✓ Multiple initialization cycles completed");
}

TEST_F(E2ETestFixture, WindowConfiguration) {
    logProgress("Testing window configuration");
    
    Application app;
    
    // Configure window before initialization
    app.setTitle("E2E Test Window");
    app.setWindowSize(800, 600);
    
    ASSERT_TRUE(app.initialize());
    
    // Verify window was created with correct settings
    // (Would need getters on Application to verify)
    
    app.cleanup();
    
    logProgress("✓ Window configuration test completed");
}

// ============================================================================
// RENDERING PIPELINE TESTS
// ============================================================================

TEST_F(E2ETestFixture, RenderingSystemInitialization) {
    logProgress("Testing rendering system initialization");
    
    Application app;
    ASSERT_TRUE(app.initialize());
    
    // Verify rendering systems are initialized
    // - Vulkan device created
    // - Render pipeline ready
    // - Swap chain configured
    
    // Note: Would need Application::getRenderingState() or similar
    
    app.cleanup();
    
    logProgress("✓ Rendering system initialization test completed");
}

// ============================================================================
// PHYSICS AND WORLD TESTS
// ============================================================================

TEST_F(E2ETestFixture, PhysicsWorldInitialization) {
    logProgress("Testing physics world initialization");
    
    Application app;
    ASSERT_TRUE(app.initialize());
    
    // Verify physics world is initialized
    // - Bullet physics world created
    // - Gravity configured
    // - Ready for simulation
    
    // Note: Would need Application::getPhysicsWorld() or similar
    
    app.cleanup();
    
    logProgress("✓ Physics world initialization test completed");
}

TEST_F(E2ETestFixture, ChunkSystemInitialization) {
    logProgress("Testing chunk system initialization");
    
    Application app;
    ASSERT_TRUE(app.initialize());
    
    // Verify chunk system is initialized
    // - ChunkManager created
    // - Initial chunks loaded
    // - Ready for voxel operations
    
    // Note: Would need Application::getChunkManager() or similar
    
    app.cleanup();
    
    logProgress("✓ Chunk system initialization test completed");
}

// ============================================================================
// INTEGRATION WORKFLOW TESTS
// ============================================================================

TEST_F(E2ETestFixture, CompleteApplicationWorkflow) {
    logProgress("Testing complete application workflow");
    
    Application app;
    
    // Step 1: Initialize
    logProgress("Step 1: Initializing application");
    ASSERT_TRUE(app.initialize());
    
    // Step 2: Simulate some application runtime
    logProgress("Step 2: Simulating application runtime");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Step 3: Verify all systems are still valid
    logProgress("Step 3: Verifying system health");
    EXPECT_TRUE(verifyApplicationState(app));
    
    // Step 4: Cleanup
    logProgress("Step 4: Cleaning up");
    app.cleanup();
    
    logProgress("✓ Complete workflow test passed");
}

TEST_F(E2ETestFixture, InitializationPerformance) {
    logProgress("Testing initialization performance");
    
    const int warmupRuns = 1;
    const int testRuns = 3;
    
    std::vector<double> initTimes;
    std::vector<double> cleanupTimes;
    
    // Warmup
    for (int i = 0; i < warmupRuns; ++i) {
        Application app;
        app.initialize();
        app.cleanup();
    }
    
    // Measurement runs
    for (int i = 0; i < testRuns; ++i) {
        Application app;
        
        double initTime = measureTime([&]() {
            app.initialize();
        });
        initTimes.push_back(initTime);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        double cleanupTime = measureTime([&]() {
            app.cleanup();
        });
        cleanupTimes.push_back(cleanupTime);
    }
    
    // Calculate averages
    double avgInit = 0.0;
    double avgCleanup = 0.0;
    for (int i = 0; i < testRuns; ++i) {
        avgInit += initTimes[i];
        avgCleanup += cleanupTimes[i];
    }
    avgInit /= testRuns;
    avgCleanup /= testRuns;
    
    logProgress("Average initialization time: " + std::to_string(avgInit) + " ms");
    logProgress("Average cleanup time: " + std::to_string(avgCleanup) + " ms");
    
    // Performance expectations
    EXPECT_LT(avgInit, 5000.0);      // < 5 seconds
    EXPECT_LT(avgCleanup, 1000.0);   // < 1 second
    
    logProgress("✓ Initialization performance test completed");
}

// ============================================================================
// ERROR HANDLING TESTS
// ============================================================================

TEST_F(E2ETestFixture, GracefulShutdownWithoutInit) {
    logProgress("Testing cleanup without initialization");
    
    Application app;
    
    // Should handle cleanup gracefully even if not initialized
    EXPECT_NO_THROW(app.cleanup());
    
    logProgress("✓ Graceful shutdown test completed");
}

TEST_F(E2ETestFixture, MultipleCleanupCalls) {
    logProgress("Testing multiple cleanup calls");
    
    Application app;
    ASSERT_TRUE(app.initialize());
    
    // Multiple cleanup calls should be safe
    EXPECT_NO_THROW(app.cleanup());
    EXPECT_NO_THROW(app.cleanup());
    EXPECT_NO_THROW(app.cleanup());
    
    logProgress("✓ Multiple cleanup test completed");
}
