#include <gtest/gtest.h>
#include "core/GameCallbacks.h"
#include "core/EngineRuntime.h"

using namespace Phyxel::Core;

// =============================================================================
// GameCallbacks tests — verify the interface contract
// =============================================================================

// A concrete test implementation that tracks which callbacks fired
class TestGame : public GameCallbacks {
public:
    bool initCalled = false;
    bool initReturnValue = true;
    int updateCount = 0;
    int renderCount = 0;
    int inputCount = 0;
    bool shutdownCalled = false;
    float lastDeltaTime = 0.0f;

    bool onInitialize(EngineRuntime& engine) override {
        initCalled = true;
        return initReturnValue;
    }

    void onUpdate(EngineRuntime& engine, float deltaTime) override {
        ++updateCount;
        lastDeltaTime = deltaTime;
    }

    void onRender(EngineRuntime& engine) override {
        ++renderCount;
    }

    void onHandleInput(EngineRuntime& engine) override {
        ++inputCount;
    }

    void onShutdown() override {
        shutdownCalled = true;
    }
};

TEST(GameCallbacksTest, DefaultImplementationsDoNothing) {
    // Base class methods are all no-ops (except onInitialize returns true)
    class EmptyGame : public GameCallbacks {};
    EmptyGame game;
    EngineRuntime engine; // uninitialized — just for type checking
    EXPECT_TRUE(game.onInitialize(engine));
    // These should compile and not crash
    game.onUpdate(engine, 0.016f);
    game.onRender(engine);
    game.onHandleInput(engine);
    game.onShutdown();
}

TEST(GameCallbacksTest, TestGameTracksInit) {
    TestGame game;
    EngineRuntime engine;
    EXPECT_FALSE(game.initCalled);
    EXPECT_TRUE(game.onInitialize(engine));
    EXPECT_TRUE(game.initCalled);
}

TEST(GameCallbacksTest, TestGameTracksUpdate) {
    TestGame game;
    EngineRuntime engine;
    EXPECT_EQ(game.updateCount, 0);
    game.onUpdate(engine, 0.033f);
    EXPECT_EQ(game.updateCount, 1);
    EXPECT_FLOAT_EQ(game.lastDeltaTime, 0.033f);
}

TEST(GameCallbacksTest, TestGameTracksRender) {
    TestGame game;
    EngineRuntime engine;
    game.onRender(engine);
    game.onRender(engine);
    EXPECT_EQ(game.renderCount, 2);
}

TEST(GameCallbacksTest, TestGameTracksInput) {
    TestGame game;
    EngineRuntime engine;
    game.onHandleInput(engine);
    EXPECT_EQ(game.inputCount, 1);
}

TEST(GameCallbacksTest, TestGameTracksShutdown) {
    TestGame game;
    EXPECT_FALSE(game.shutdownCalled);
    game.onShutdown();
    EXPECT_TRUE(game.shutdownCalled);
}

TEST(GameCallbacksTest, InitCanReturnFalse) {
    TestGame game;
    game.initReturnValue = false;
    EngineRuntime engine;
    EXPECT_FALSE(game.onInitialize(engine));
}

TEST(GameCallbacksTest, PolymorphicBehavior) {
    TestGame testGame;
    GameCallbacks* base = &testGame;
    EngineRuntime engine;
    base->onUpdate(engine, 0.01f);
    EXPECT_EQ(testGame.updateCount, 1);
    base->onShutdown();
    EXPECT_TRUE(testGame.shutdownCalled);
}
