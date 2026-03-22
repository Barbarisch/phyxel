#include <gtest/gtest.h>
#include "core/EngineRuntime.h"
#include "core/EngineConfig.h"

using namespace Phyxel::Core;

// =============================================================================
// EngineRuntime unit tests (no GPU — cannot call initialize() in CI)
// =============================================================================

TEST(EngineRuntimeTest, DefaultConstruction) {
    EngineRuntime runtime;
    EXPECT_FALSE(runtime.isInitialized());
    EXPECT_EQ(runtime.getFrameCount(), 0);
}

TEST(EngineRuntimeTest, ConfigAccessBeforeInit) {
    EngineRuntime runtime;
    // Config should have default values before initialize()
    const auto& cfg = runtime.getConfig();
    EXPECT_EQ(cfg.windowWidth, 1600);
    EXPECT_EQ(cfg.windowHeight, 900);
}

TEST(EngineRuntimeTest, ShouldCloseBeforeInit) {
    EngineRuntime runtime;
    // Should return true (no window exists)
    EXPECT_TRUE(runtime.shouldClose());
}

TEST(EngineRuntimeTest, QuitSetsFlag) {
    EngineRuntime runtime;
    runtime.quit();
    EXPECT_TRUE(runtime.shouldClose());
}

TEST(EngineRuntimeTest, ShutdownBeforeInitIsSafe) {
    EngineRuntime runtime;
    // Should not crash or assert
    runtime.shutdown();
    EXPECT_FALSE(runtime.isInitialized());
}

TEST(EngineRuntimeTest, DoubleShutdownIsSafe) {
    EngineRuntime runtime;
    runtime.shutdown();
    runtime.shutdown(); // second call should be no-op
    EXPECT_FALSE(runtime.isInitialized());
}

TEST(EngineRuntimeTest, AccessorsReturnNullBeforeInit) {
    EngineRuntime runtime;
    EXPECT_EQ(runtime.getWindowManager(), nullptr);
    EXPECT_EQ(runtime.getVulkanDevice(), nullptr);
    EXPECT_EQ(runtime.getRenderPipeline(), nullptr);
    EXPECT_EQ(runtime.getDynamicRenderPipeline(), nullptr);
    EXPECT_EQ(runtime.getPhysicsWorld(), nullptr);
    EXPECT_EQ(runtime.getTimer(), nullptr);
    EXPECT_EQ(runtime.getChunkManager(), nullptr);
    EXPECT_EQ(runtime.getAudioSystem(), nullptr);
    EXPECT_EQ(runtime.getInputManager(), nullptr);
    EXPECT_EQ(runtime.getForceSystem(), nullptr);
    EXPECT_EQ(runtime.getImGuiRenderer(), nullptr);
    EXPECT_EQ(runtime.getMouseVelocityTracker(), nullptr);
    EXPECT_EQ(runtime.getPerformanceProfiler(), nullptr);
    EXPECT_EQ(runtime.getPerformanceMonitor(), nullptr);
    EXPECT_EQ(runtime.getCamera(), nullptr);
    EXPECT_EQ(runtime.getCameraManager(), nullptr);
}

TEST(EngineRuntimeTest, ConfigStoredOnInit_WouldNeedGPU) {
    // This test documents what initialize() should do.
    // Cannot actually call initialize() without a GPU + GLFW context.
    EngineConfig cfg;
    cfg.windowTitle = "TestWindow";
    cfg.windowWidth = 800;
    cfg.windowHeight = 600;

    EngineRuntime runtime;
    // runtime.initialize(cfg);  // would need GPU
    // EXPECT_TRUE(runtime.isInitialized());
    // EXPECT_EQ(runtime.getConfig().windowTitle, "TestWindow");
    EXPECT_FALSE(runtime.isInitialized());
}
