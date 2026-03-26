#include <gtest/gtest.h>
#include "ai/AIConversationService.h"
#include "story/StoryEngine.h"
#include "core/EntityRegistry.h"
#include "ui/DialogueSystem.h"

#ifdef ENABLE_WORLD_STORAGE
#include <sqlite3.h>
#endif

using namespace Phyxel;

// ============================================================================
// AIConversationService Tests
// ============================================================================

class AIConversationServiceTest : public ::testing::Test {
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

TEST_F(AIConversationServiceTest, NotInitializedByDefault) {
    EXPECT_FALSE(service->isConfigured());
}

TEST_F(AIConversationServiceTest, InitializeWithNullDb) {
    // Service can initialize without a db (ConversationMemory will fail but service still works)
    AI::LLMConfig config;
    config.apiKey = "sk-test";
    // initialize may succeed or fail depending on impl — just verify no crash
    service->initialize(nullptr, config);
    // Service is "configured" based on API key presence, not db
    EXPECT_TRUE(service->isConfigured());
}

TEST_F(AIConversationServiceTest, InitializeWithDbSucceeds) {
#ifdef ENABLE_WORLD_STORAGE
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);

    AI::LLMConfig config;
    config.apiKey = "sk-test-key";
    EXPECT_TRUE(service->initialize(db, config));
    EXPECT_TRUE(service->isConfigured());

    service.reset();
    sqlite3_close(db);
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}

TEST_F(AIConversationServiceTest, NotConfiguredWithoutApiKey) {
#ifdef ENABLE_WORLD_STORAGE
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);

    AI::LLMConfig config;
    // No API key
    EXPECT_TRUE(service->initialize(db, config));
    EXPECT_FALSE(service->isConfigured());

    service.reset();
    sqlite3_close(db);
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}

TEST_F(AIConversationServiceTest, UpdateConfig) {
#ifdef ENABLE_WORLD_STORAGE
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);

    AI::LLMConfig config;
    EXPECT_TRUE(service->initialize(db, config));
    EXPECT_FALSE(service->isConfigured());

    AI::LLMConfig newConfig;
    newConfig.apiKey = "sk-new-key";
    service->setLLMConfig(newConfig);
    EXPECT_TRUE(service->isConfigured());

    service.reset();
    sqlite3_close(db);
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}

TEST_F(AIConversationServiceTest, AccessInternals) {
#ifdef ENABLE_WORLD_STORAGE
    sqlite3* db = nullptr;
    ASSERT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);

    AI::LLMConfig config;
    config.apiKey = "sk-test";
    service->initialize(db, config);

    EXPECT_NE(service->getLLMClient(), nullptr);
    EXPECT_NE(service->getContextManager(), nullptr);
    EXPECT_NE(service->getConversationMemory(), nullptr);

    service.reset();
    sqlite3_close(db);
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}
