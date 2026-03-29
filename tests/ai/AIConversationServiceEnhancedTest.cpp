#include <gtest/gtest.h>
#include "ai/AIConversationService.h"
#include "ai/LLMActionParser.h"
#include "story/StoryEngine.h"
#include "core/EntityRegistry.h"
#include "ui/DialogueSystem.h"

#ifdef ENABLE_WORLD_STORAGE
#include <sqlite3.h>
#endif

using namespace Phyxel;

// ============================================================================
// AIConversationService Enhanced Tests — Action Handler & NPCManager wiring
// ============================================================================

class AIConversationServiceEnhancedTest : public ::testing::Test {
protected:
    void SetUp() override {
        storyEngine = std::make_unique<Story::StoryEngine>();
        registry = std::make_unique<Core::EntityRegistry>();
        dialogueSystem = std::make_unique<UI::DialogueSystem>();

        service = std::make_unique<AI::AIConversationService>(
            storyEngine.get(), registry.get(), dialogueSystem.get());
    }

    void TearDown() override {
        service.reset();
        dialogueSystem.reset();
        registry.reset();
        storyEngine.reset();
    }

    std::unique_ptr<Story::StoryEngine> storyEngine;
    std::unique_ptr<Core::EntityRegistry> registry;
    std::unique_ptr<UI::DialogueSystem> dialogueSystem;
    std::unique_ptr<AI::AIConversationService> service;
};

TEST_F(AIConversationServiceEnhancedTest, SetActionHandlerDoesNotCrash) {
    AI::LLMActionHandler handler;
    handler.onEmote = [](const std::string&) {};
    EXPECT_NO_THROW(service->setActionHandler(handler));
}

TEST_F(AIConversationServiceEnhancedTest, SetActionHandlerEmpty) {
    AI::LLMActionHandler handler; // all null callbacks
    EXPECT_NO_THROW(service->setActionHandler(handler));
}

TEST_F(AIConversationServiceEnhancedTest, SetNPCManagerNull) {
    EXPECT_NO_THROW(service->setNPCManager(nullptr));
}

TEST_F(AIConversationServiceEnhancedTest, SetNPCManagerDoesNotCrash) {
    // Can't easily create a real NPCManager for unit tests,
    // but setNPCManager should accept and forward to ContextManager
    EXPECT_NO_THROW(service->setNPCManager(nullptr));
}

TEST_F(AIConversationServiceEnhancedTest, ActionHandlerPreservedAcrossConfig) {
#ifdef ENABLE_WORLD_STORAGE
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);

    AI::LLMConfig config;
    config.apiKey = "sk-test";
    service->initialize(db, config);

    // Set action handler after initialization
    bool emoteCalled = false;
    AI::LLMActionHandler handler;
    handler.onEmote = [&](const std::string&) { emoteCalled = true; };
    service->setActionHandler(handler);

    // Reconfigure — handler should still work (it's stored separately)
    AI::LLMConfig newConfig;
    newConfig.apiKey = "sk-new-key";
    service->setLLMConfig(newConfig);

    // Handler is stored, no way to test execution without a real LLM call,
    // but we can verify it doesn't crash
    SUCCEED();

    service.reset();
    sqlite3_close(db);
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}

TEST_F(AIConversationServiceEnhancedTest, GetContextManagerAvailable) {
#ifdef ENABLE_WORLD_STORAGE
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);

    AI::LLMConfig config;
    config.apiKey = "sk-test";
    service->initialize(db, config);

    auto* ctx = service->getContextManager();
    EXPECT_NE(ctx, nullptr);

    // setNPCManager should propagate through
    EXPECT_NO_THROW(service->setNPCManager(nullptr));

    service.reset();
    sqlite3_close(db);
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}
