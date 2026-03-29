#include <gtest/gtest.h>
#include "ai/ContextManager.h"
#include "ai/ConversationMemory.h"
#include "story/StoryEngine.h"
#include "core/EntityRegistry.h"
#include "core/NPCManager.h"

#ifdef ENABLE_WORLD_STORAGE
#include <sqlite3.h>
#endif

using namespace Phyxel;

// ============================================================================
// Enhanced ContextManager Tests — WorldView, Needs, Social context
// ============================================================================

class ContextManagerEnhancedTest : public ::testing::Test {
protected:
    void SetUp() override {
        storyEngine = std::make_unique<Story::StoryEngine>();
        registry = std::make_unique<Core::EntityRegistry>();
        memory = std::make_unique<AI::ConversationMemory>();

#ifdef ENABLE_WORLD_STORAGE
        sqlite3_open(":memory:", &db);
        memory->initialize(db);
#endif

        // Add a test NPC character to StoryEngine
        Story::CharacterProfile merchant;
        merchant.id = "merchant_01";
        merchant.name = "Elara";
        merchant.description = "A clever merchant in the market square";
        merchant.traits.openness = 0.7f;
        merchant.traits.conscientiousness = 0.6f;
        merchant.traits.extraversion = 0.8f;
        merchant.traits.agreeableness = 0.5f;
        merchant.traits.neuroticism = 0.3f;

        Story::CharacterGoal goal;
        goal.id = "sell_goods";
        goal.description = "Sell wares and make a profit";
        goal.priority = 0.8f;
        merchant.goals.push_back(goal);

        storyEngine->addCharacter(std::move(merchant));

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

TEST_F(ContextManagerEnhancedTest, SetNPCManagerDoesNotCrash) {
    Core::NPCManager npcManager;
    EXPECT_NO_THROW(contextManager->setNPCManager(&npcManager));
}

TEST_F(ContextManagerEnhancedTest, SetNPCManagerNull) {
    EXPECT_NO_THROW(contextManager->setNPCManager(nullptr));
}

TEST_F(ContextManagerEnhancedTest, BuildContextWithoutNPCManager) {
    // Should still produce valid context even without NPCManager set
    auto ctx = contextManager->buildContext("merchant_01", "Hello!", glm::vec3(16, 20, 16));
    ASSERT_FALSE(ctx.messages.empty());
    EXPECT_EQ(ctx.messages.front().role, "system");
    EXPECT_EQ(ctx.messages.back().role, "user");
    EXPECT_EQ(ctx.messages.back().content, "Hello!");
}

TEST_F(ContextManagerEnhancedTest, BuildContextWithNPCManagerButNoEntity) {
    // NPCManager set, but no actual NPCEntity in the registry for this character
    Core::NPCManager npcManager;
    contextManager->setNPCManager(&npcManager);

    auto ctx = contextManager->buildContext("merchant_01", "Hi there!", glm::vec3(0));
    ASSERT_FALSE(ctx.messages.empty());
    // Context should still work — just no WorldView/Needs/Social sections
    EXPECT_EQ(ctx.messages.front().role, "system");
}

TEST_F(ContextManagerEnhancedTest, BuildContextSystemPromptContainsPersonality) {
    auto ctx = contextManager->buildContext("merchant_01", "What do you sell?", glm::vec3(0));
    const auto& systemPrompt = ctx.messages.front().content;
    // Should contain NPC name
    EXPECT_NE(systemPrompt.find("Elara"), std::string::npos);
}

TEST_F(ContextManagerEnhancedTest, BuildContextUnknownNPCDoesNotCrash) {
    Core::NPCManager npcManager;
    contextManager->setNPCManager(&npcManager);

    // Unknown NPC should not crash, even with NPCManager set
    EXPECT_NO_THROW(
        contextManager->buildContext("nonexistent_npc", "Hello", glm::vec3(0))
    );
}

TEST_F(ContextManagerEnhancedTest, TokenEstimateWithNPCManager) {
    Core::NPCManager npcManager;
    contextManager->setNPCManager(&npcManager);

    auto ctx = contextManager->buildContext("merchant_01", "Hello!", glm::vec3(0));
    EXPECT_GT(ctx.estimatedTokens, 0);
}

TEST_F(ContextManagerEnhancedTest, RepeatSetNPCManagerOverrides) {
    Core::NPCManager npcManager1;
    Core::NPCManager npcManager2;

    contextManager->setNPCManager(&npcManager1);
    contextManager->setNPCManager(&npcManager2);
    // Should use the latest one — no crash
    auto ctx = contextManager->buildContext("merchant_01", "Hey!", glm::vec3(0));
    EXPECT_FALSE(ctx.messages.empty());
}

TEST_F(ContextManagerEnhancedTest, BuildContextWithActionInstructions) {
    auto ctx = contextManager->buildContext("merchant_01", "Give me something", glm::vec3(0));
    const auto& systemPrompt = ctx.messages.front().content;
    // Action instructions should be in the system prompt (from buildActionInstructions)
    // These include tags like EMOTE, GIVE_ITEM, QUEST_UPDATE, MOOD
    EXPECT_NE(systemPrompt.find("EMOTE"), std::string::npos)
        << "System prompt should contain action instructions with EMOTE tag";
}
