#include <gtest/gtest.h>
#include "ai/LLMClient.h"

using namespace Phyxel::AI;

// ============================================================================
// LLMConfig Tests
// ============================================================================

TEST(LLMConfigTest, Defaults) {
    LLMConfig config;
    EXPECT_EQ(config.provider, "anthropic");
    EXPECT_EQ(config.model, "");
    EXPECT_EQ(config.apiKey, "");
    EXPECT_EQ(config.ollamaHost, "http://localhost:11434");
    EXPECT_EQ(config.maxTokens, 1024);
    EXPECT_FLOAT_EQ(config.temperature, 0.8f);
    EXPECT_EQ(config.timeoutMs, 30000);
}

TEST(LLMConfigTest, DefaultModelPerProvider) {
    EXPECT_FALSE(LLMConfig::getDefaultModel("anthropic").empty());
    EXPECT_FALSE(LLMConfig::getDefaultModel("openai").empty());
    EXPECT_FALSE(LLMConfig::getDefaultModel("ollama").empty());
    // Unknown provider returns empty (no fallback)
    EXPECT_TRUE(LLMConfig::getDefaultModel("unknown").empty());
}

// ============================================================================
// LLMClient Tests (no network — just config/state validation)
// ============================================================================

TEST(LLMClientTest, DefaultNotConfigured) {
    LLMClient client;
    EXPECT_FALSE(client.isConfigured());
}

TEST(LLMClientTest, ConfiguredWithApiKey) {
    LLMConfig config;
    config.apiKey = "sk-test-key";
    LLMClient client(config);
    EXPECT_TRUE(client.isConfigured());
}

TEST(LLMClientTest, OllamaConfiguredWithoutKey) {
    LLMConfig config;
    config.provider = "ollama";
    LLMClient client(config);
    // Ollama doesn't need an API key
    EXPECT_TRUE(client.isConfigured());
}

TEST(LLMClientTest, TokenUsageInitiallyZero) {
    LLMClient client;
    auto usage = client.getTokenUsage();
    EXPECT_EQ(usage.totalInput, 0);
    EXPECT_EQ(usage.totalOutput, 0);
    EXPECT_EQ(usage.totalCalls, 0);
}

TEST(LLMClientTest, SetConfig) {
    LLMClient client;
    EXPECT_FALSE(client.isConfigured());

    LLMConfig config;
    config.apiKey = "sk-test-key";
    client.setConfig(config);
    EXPECT_TRUE(client.isConfigured());
    EXPECT_EQ(client.getConfig().apiKey, "sk-test-key");
}

TEST(LLMClientTest, CompleteWithoutKeyReturnsError) {
    LLMClient client;
    std::vector<LLMMessage> messages = {
        {"user", "Hello"}
    };
    auto response = client.complete(messages);
    EXPECT_FALSE(response.ok());
    EXPECT_FALSE(response.error.empty());
}

// ============================================================================
// LLMMessage and LLMResponse Tests
// ============================================================================

TEST(LLMMessageTest, Construction) {
    LLMMessage msg{"user", "Hello NPC"};
    EXPECT_EQ(msg.role, "user");
    EXPECT_EQ(msg.content, "Hello NPC");
}

TEST(LLMResponseTest, OkWhenNoError) {
    LLMResponse resp;
    resp.content = "Hello!";
    EXPECT_TRUE(resp.ok());
}

TEST(LLMResponseTest, NotOkWhenError) {
    LLMResponse resp;
    resp.error = "Network timeout";
    EXPECT_FALSE(resp.ok());
}
