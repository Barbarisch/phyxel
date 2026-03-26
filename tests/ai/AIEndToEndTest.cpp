#include <gtest/gtest.h>
#include "ai/LLMClient.h"
#include "ai/ContextManager.h"
#include "ai/ConversationMemory.h"
#include "ai/AIConversationService.h"
#include "story/StoryEngine.h"
#include "core/EntityRegistry.h"
#include "ui/DialogueSystem.h"
#include <cstdlib>

using namespace Phyxel;

// ============================================================================
// AI End-to-End Tests
// These tests exercise the full LLM pipeline. Skipped if no API key is set.
// Set PHYXEL_AI_API_KEY environment variable to enable.
// ============================================================================

static std::string getApiKey() {
    const char* key = std::getenv("PHYXEL_AI_API_KEY");
    return (key && key[0] != '\0') ? std::string(key) : "";
}

class AIEndToEndTest : public ::testing::Test {
protected:
    void SetUp() override {
        apiKey = getApiKey();
        if (apiKey.empty()) {
            GTEST_SKIP() << "PHYXEL_AI_API_KEY not set — skipping live LLM test";
        }
    }

    std::string apiKey;
};

// Test: Direct LLMClient call to Claude API
TEST_F(AIEndToEndTest, LLMClientDirectCall) {
    AI::LLMConfig config;
    config.provider = "anthropic";
    config.apiKey = apiKey;
    config.maxTokens = 50;  // Keep it short for tests
    config.timeoutMs = 15000;

    AI::LLMClient client(config);
    ASSERT_TRUE(client.isConfigured());

    std::vector<AI::LLMMessage> messages = {
        {"user", "Say hello in exactly 3 words."}
    };

    auto response = client.complete(messages);

    EXPECT_TRUE(response.ok()) << "LLM error: " << response.error;
    EXPECT_FALSE(response.content.empty());
    EXPECT_GT(response.inputTokens, 0);
    EXPECT_GT(response.outputTokens, 0);

    // Verify token usage tracking
    auto usage = client.getTokenUsage();
    EXPECT_EQ(usage.totalCalls, 1);
    EXPECT_GT(usage.totalInput, 0);
    EXPECT_GT(usage.totalOutput, 0);
}

// Test: ContextManager builds proper prompt for NPC conversation
TEST_F(AIEndToEndTest, ContextManagerBuildAndCall) {
    auto storyEngine = std::make_unique<Story::StoryEngine>();
    auto registry = std::make_unique<Core::EntityRegistry>();

    AI::ConversationMemory memory;  // Not initialized (no db) — that's fine

    AI::ContextManager contextMgr(storyEngine.get(), &memory, registry.get());

    // Build context for a conversation
    auto ctx = contextMgr.buildContext("herbalist", "Hello, what herbs do you sell?", glm::vec3(16, 20, 16));

    EXPECT_FALSE(ctx.messages.empty());
    EXPECT_GT(ctx.estimatedTokens, 0);

    // The context should have a system message and a user message
    bool hasSystem = false;
    bool hasUser = false;
    for (const auto& msg : ctx.messages) {
        if (msg.role == "system") hasSystem = true;
        if (msg.role == "user") hasUser = true;
    }
    EXPECT_TRUE(hasSystem) << "Context should include a system prompt";
    EXPECT_TRUE(hasUser) << "Context should include the user message";

    // Now call the LLM with this context
    AI::LLMConfig config;
    config.provider = "anthropic";
    config.apiKey = apiKey;
    config.maxTokens = 100;
    config.timeoutMs = 15000;

    AI::LLMClient client(config);
    auto response = client.complete(ctx.messages);

    EXPECT_TRUE(response.ok()) << "LLM error: " << response.error;
    EXPECT_FALSE(response.content.empty());
}

// Test: Full AIConversationService pipeline (initialize + configure)
TEST_F(AIEndToEndTest, FullServicePipeline) {
    auto storyEngine = std::make_unique<Story::StoryEngine>();
    auto registry = std::make_unique<Core::EntityRegistry>();
    auto dialogueSystem = std::make_unique<UI::DialogueSystem>();

    AI::AIConversationService service(storyEngine.get(), registry.get(), dialogueSystem.get());

    AI::LLMConfig config;
    config.provider = "anthropic";
    config.apiKey = apiKey;
    config.maxTokens = 50;

    // Initialize without a database (memory won't persist but service works)
    ASSERT_TRUE(service.initialize(nullptr, config));
    EXPECT_TRUE(service.isConfigured());

    // Verify internals are accessible
    ASSERT_NE(service.getLLMClient(), nullptr);
    ASSERT_NE(service.getContextManager(), nullptr);
    ASSERT_NE(service.getConversationMemory(), nullptr);

    // Update config at runtime (simulates settings change)
    AI::LLMConfig newConfig;
    newConfig.provider = "anthropic";
    newConfig.apiKey = apiKey;
    newConfig.maxTokens = 80;
    service.setLLMConfig(newConfig);

    EXPECT_EQ(service.getLLMClient()->getConfig().maxTokens, 80);
    EXPECT_TRUE(service.isConfigured());
}
