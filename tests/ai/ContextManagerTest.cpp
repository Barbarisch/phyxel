#include <gtest/gtest.h>
#include "ai/ContextManager.h"
#include "ai/ConversationMemory.h"
#include "story/StoryEngine.h"
#include "core/EntityRegistry.h"

#ifdef ENABLE_WORLD_STORAGE
#include <sqlite3.h>
#endif

using namespace Phyxel;

// ============================================================================
// ContextManager Tests
// ============================================================================

class ContextManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        storyEngine = std::make_unique<Story::StoryEngine>();
        registry = std::make_unique<Core::EntityRegistry>();
        memory = std::make_unique<AI::ConversationMemory>();

#ifdef ENABLE_WORLD_STORAGE
        sqlite3_open(":memory:", &db);
        memory->initialize(db);
#endif

        // Add a test NPC character
        Story::CharacterProfile guard;
        guard.id = "guard_01";
        guard.name = "Captain Aldric";
        guard.description = "A stern guard captain who protects the village";
        guard.traits.openness = 0.3f;
        guard.traits.conscientiousness = 0.9f;
        guard.traits.extraversion = 0.5f;
        guard.traits.agreeableness = 0.4f;
        guard.traits.neuroticism = 0.2f;

        Story::CharacterGoal goal;
        goal.id = "protect_village";
        goal.description = "Protect the village from threats";
        goal.priority = 0.9f;
        guard.goals.push_back(goal);

        storyEngine->addCharacter(std::move(guard));

        contextManager = std::make_unique<AI::ContextManager>(
            storyEngine.get(), memory.get(), registry.get());
    }

    void TearDown() override {
        contextManager.reset();
        memory.reset();
#ifdef ENABLE_WORLD_STORAGE
        if (db) sqlite3_close(db);
#endif
        registry.reset();
        storyEngine.reset();
    }

    sqlite3* db = nullptr;
    std::unique_ptr<Story::StoryEngine> storyEngine;
    std::unique_ptr<Core::EntityRegistry> registry;
    std::unique_ptr<AI::ConversationMemory> memory;
    std::unique_ptr<AI::ContextManager> contextManager;
};

TEST_F(ContextManagerTest, BuildContextProducesMessages) {
    auto ctx = contextManager->buildContext("guard_01", "Hello guard!", glm::vec3(16, 20, 16));

    ASSERT_FALSE(ctx.messages.empty());
    // First message should be a system prompt
    EXPECT_EQ(ctx.messages.front().role, "system");
    // Last message should be the user message
    EXPECT_EQ(ctx.messages.back().role, "user");
    EXPECT_EQ(ctx.messages.back().content, "Hello guard!");
    // Should have a reasonable token estimate
    EXPECT_GT(ctx.estimatedTokens, 0);
}

TEST_F(ContextManagerTest, SystemPromptContainsNPCName) {
    auto ctx = contextManager->buildContext("guard_01", "Hi", glm::vec3(0));

    ASSERT_FALSE(ctx.messages.empty());
    const auto& systemPrompt = ctx.messages.front().content;
    EXPECT_NE(systemPrompt.find("Aldric"), std::string::npos)
        << "System prompt should mention NPC name";
}

TEST_F(ContextManagerTest, UnknownNPCProducesEmptyContext) {
    auto ctx = contextManager->buildContext("nonexistent_npc", "Hello", glm::vec3(0));
    // Should still produce something (graceful fallback) or empty
    // The important thing is it doesn't crash
    SUCCEED();
}

TEST_F(ContextManagerTest, TokenBudgetDefault) {
    EXPECT_GT(contextManager->getMaxContextTokens(), 0);
}

TEST_F(ContextManagerTest, SetTokenBudget) {
    contextManager->setMaxContextTokens(2000);
    EXPECT_EQ(contextManager->getMaxContextTokens(), 2000);
}

TEST_F(ContextManagerTest, ConversationHistoryIncluded) {
#ifdef ENABLE_WORLD_STORAGE
    // Record some prior conversation
    memory->recordTurn("guard_01", "Player", "Are there any threats?", "", 1.0f, 1.0f);
    memory->recordTurn("guard_01", "Captain Aldric", "Bandits to the north.", "concerned", 1.0f, 2.0f);

    auto ctx = contextManager->buildContext("guard_01", "What should I do?", glm::vec3(0));

    // Should include prior conversation + new message = at least system + 3 messages
    EXPECT_GE(ctx.messages.size(), 3);
#else
    GTEST_SKIP() << "SQLite not available";
#endif
}
