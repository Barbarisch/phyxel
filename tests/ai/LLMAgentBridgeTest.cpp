#include <gtest/gtest.h>
#include "ai/LLMAgentBridge.h"

using namespace Phyxel::AI;

// ============================================================================
// LLMAgentBridge Tests (no network — callback creation and null safety)
// ============================================================================

TEST(LLMAgentBridgeTest, NullClientReturnsNullCallback) {
    auto callback = LLMAgentBridge::createCallback(nullptr);
    EXPECT_EQ(callback, nullptr);
}

TEST(LLMAgentBridgeTest, UnconfiguredClientReturnsNullCallback) {
    LLMClient client; // default: not configured (no API key)
    auto callback = LLMAgentBridge::createCallback(&client);
    EXPECT_EQ(callback, nullptr);
}

TEST(LLMAgentBridgeTest, ConfiguredClientReturnsValidCallback) {
    LLMConfig config;
    config.apiKey = "sk-test-key";
    LLMClient client(config);
    auto callback = LLMAgentBridge::createCallback(&client);
    EXPECT_NE(callback, nullptr);
}

TEST(LLMAgentBridgeTest, NullClientWithSystemPromptReturnsNull) {
    auto callback = LLMAgentBridge::createCallback(nullptr, "You are a guard.");
    EXPECT_EQ(callback, nullptr);
}

TEST(LLMAgentBridgeTest, UnconfiguredClientWithSystemPromptReturnsNull) {
    LLMClient client;
    auto callback = LLMAgentBridge::createCallback(&client, "You are a guard.");
    EXPECT_EQ(callback, nullptr);
}

TEST(LLMAgentBridgeTest, ConfiguredClientWithSystemPromptReturnsValidCallback) {
    LLMConfig config;
    config.apiKey = "sk-test-key";
    LLMClient client(config);
    auto callback = LLMAgentBridge::createCallback(&client, "You are a guard.");
    EXPECT_NE(callback, nullptr);
}

TEST(LLMAgentBridgeTest, OllamaClientReturnsValidCallback) {
    LLMConfig config;
    config.provider = "ollama";
    // Ollama doesn't need an API key
    LLMClient client(config);
    auto callback = LLMAgentBridge::createCallback(&client);
    EXPECT_NE(callback, nullptr);
}
