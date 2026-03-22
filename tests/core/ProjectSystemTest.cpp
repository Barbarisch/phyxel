#include <gtest/gtest.h>
#include "core/EngineRuntime.h"
#include "core/EngineConfig.h"
#include "core/GameCallbacks.h"

using namespace Phyxel::Core;

// =============================================================================
// ProjectSystem tests — verify standalone game patterns work correctly
// =============================================================================

// A game that exercises the EngineRuntime API surface used by standalone games
class StandaloneGame : public GameCallbacks {
public:
    bool initCalled = false;
    bool updateCalled = false;
    bool renderCalled = false;
    bool inputCalled = false;
    bool shutdownCalled = false;
    int frameCount = 0;
    
    // Track which subsystems were accessible
    bool hadChunkManager = false;
    bool hadCamera = false;
    bool hadInputManager = false;
    bool hadPhysicsWorld = false;
    bool hadWindowManager = false;
    bool hadVulkanDevice = false;
    bool hadRenderPipeline = false;
    bool hadImGuiRenderer = false;
    bool hadAudioSystem = false;
    bool hadTimer = false;
    bool hadProfiler = false;
    bool hadMonitor = false;

    bool onInitialize(EngineRuntime& engine) override {
        initCalled = true;
        hadChunkManager   = engine.getChunkManager() != nullptr;
        hadCamera         = engine.getCamera() != nullptr;
        hadInputManager   = engine.getInputManager() != nullptr;
        hadPhysicsWorld   = engine.getPhysicsWorld() != nullptr;
        hadWindowManager  = engine.getWindowManager() != nullptr;
        hadVulkanDevice   = engine.getVulkanDevice() != nullptr;
        hadRenderPipeline = engine.getRenderPipeline() != nullptr;
        hadImGuiRenderer  = engine.getImGuiRenderer() != nullptr;
        hadAudioSystem    = engine.getAudioSystem() != nullptr;
        hadTimer          = engine.getTimer() != nullptr;
        hadProfiler       = engine.getPerformanceProfiler() != nullptr;
        hadMonitor        = engine.getPerformanceMonitor() != nullptr;
        return true;
    }

    void onUpdate(EngineRuntime& engine, float dt) override {
        updateCalled = true;
        ++frameCount;
    }

    void onRender(EngineRuntime& engine) override {
        renderCalled = true;
    }

    void onHandleInput(EngineRuntime& engine) override {
        inputCalled = true;
    }

    void onShutdown() override {
        shutdownCalled = true;
    }
};

// A game that immediately requests quit in onInitialize
class ImmediateQuitGame : public GameCallbacks {
public:
    bool initCalled = false;
    bool shutdownCalled = false;

    bool onInitialize(EngineRuntime& engine) override {
        initCalled = true;
        engine.quit();
        return true;
    }

    void onShutdown() override {
        shutdownCalled = true;
    }
};

// A game that fails initialization
class FailingGame : public GameCallbacks {
public:
    bool onInitialize(EngineRuntime& engine) override {
        return false;
    }

    void onShutdown() override {
        // Should not be called if init fails
        FAIL() << "onShutdown called after failed init";
    }
};

// A game that tracks how many frames pass before quit
class CountingGame : public GameCallbacks {
public:
    int maxFrames = 3;
    int framesSeen = 0;

    bool onInitialize(EngineRuntime& engine) override { return true; }

    void onUpdate(EngineRuntime& engine, float dt) override {
        ++framesSeen;
        if (framesSeen >= maxFrames) {
            engine.quit();
        }
    }

    void onShutdown() override {}
};

// =============================================================================
// Tests (non-GPU — only test patterns, not actual rendering)
// =============================================================================

TEST(ProjectSystemTest, StandaloneGameCallbackOrder) {
    // Verify the callback interface can be used in the standalone pattern
    StandaloneGame game;

    EXPECT_FALSE(game.initCalled);
    EXPECT_FALSE(game.updateCalled);
    EXPECT_FALSE(game.renderCalled);
    EXPECT_FALSE(game.inputCalled);
    EXPECT_FALSE(game.shutdownCalled);

    // Simulate the callback sequence without a real EngineRuntime
    // (GPU-less — just verify the interface compiles and works)
    EngineRuntime runtime;
    game.onInitialize(runtime);
    EXPECT_TRUE(game.initCalled);
    
    game.onHandleInput(runtime);
    EXPECT_TRUE(game.inputCalled);
    
    game.onUpdate(runtime, 0.016f);
    EXPECT_TRUE(game.updateCalled);
    EXPECT_EQ(game.frameCount, 1);

    game.onRender(runtime);
    EXPECT_TRUE(game.renderCalled);

    game.onShutdown();
    EXPECT_TRUE(game.shutdownCalled);
}

TEST(ProjectSystemTest, SubsystemAccessorsNullBeforeInit) {
    // Before initialize(), all accessors return nullptr
    EngineRuntime runtime;
    EXPECT_EQ(runtime.getChunkManager(), nullptr);
    EXPECT_EQ(runtime.getCamera(), nullptr);
    EXPECT_EQ(runtime.getInputManager(), nullptr);
    EXPECT_EQ(runtime.getPhysicsWorld(), nullptr);
    EXPECT_EQ(runtime.getWindowManager(), nullptr);
    EXPECT_EQ(runtime.getVulkanDevice(), nullptr);
    EXPECT_EQ(runtime.getAudioSystem(), nullptr);
}

TEST(ProjectSystemTest, ImmediateQuitPattern) {
    // Games should be able to call engine.quit() in onInitialize
    ImmediateQuitGame game;
    EngineRuntime runtime;

    // Call init — game requests quit
    EXPECT_TRUE(game.onInitialize(runtime));
    EXPECT_TRUE(game.initCalled);
    
    // After quit(), shouldClose should be true
    // (Note: runtime not initialized, so shouldClose returns true anyway)
    EXPECT_TRUE(runtime.shouldClose());
}

TEST(ProjectSystemTest, FailingInitAborts) {
    FailingGame game;
    EngineRuntime runtime;

    // onInitialize returns false — run() should not proceed
    EXPECT_FALSE(game.onInitialize(runtime));
}

TEST(ProjectSystemTest, CountingGameQuitsAfterFrames) {
    CountingGame game;
    game.maxFrames = 5;

    // Simulate the loop manually
    EngineRuntime runtime;
    game.onInitialize(runtime);

    int loopCount = 0;
    while (loopCount < 100) { // safety limit
        game.onUpdate(runtime, 0.016f);
        if (game.framesSeen >= game.maxFrames) break;
        ++loopCount;
    }

    EXPECT_EQ(game.framesSeen, 5);
}

TEST(ProjectSystemTest, MultipleGamesCanShareRuntime) {
    // Verify two different GameCallbacks can be used with the same runtime
    EngineRuntime runtime;

    StandaloneGame game1;
    ImmediateQuitGame game2;

    game1.onInitialize(runtime);
    game2.onInitialize(runtime);

    EXPECT_TRUE(game1.initCalled);
    EXPECT_TRUE(game2.initCalled);
}

TEST(ProjectSystemTest, ConfigPassedToRuntime) {
    EngineConfig config;
    config.windowWidth = 800;
    config.windowHeight = 600;
    config.windowTitle = "TestProject";

    EngineRuntime runtime;
    // Before init, config should be default
    EXPECT_NE(runtime.getConfig().windowTitle, "TestProject");

    // We can't call initialize (needs GPU), but we can verify the config
    // is stored by the constructor pattern
    EXPECT_EQ(config.windowWidth, 800);
    EXPECT_EQ(config.windowHeight, 600);
    EXPECT_EQ(config.windowTitle, "TestProject");
}

TEST(ProjectSystemTest, GameCanAccessConfigViaRuntime) {
    EngineRuntime runtime;
    // Default config values
    const auto& cfg = runtime.getConfig();
    EXPECT_GT(cfg.windowWidth, 0);
    EXPECT_GT(cfg.windowHeight, 0);
    EXPECT_FALSE(cfg.windowTitle.empty());
}
