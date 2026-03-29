#pragma once

#include "story/LLMCharacterAgent.h"
#include "ai/LLMClient.h"

namespace Phyxel {
namespace AI {

// ============================================================================
// LLMAgentBridge — connects Story's LLMCharacterAgent to AI's LLMClient
//
// Creates a LLMRequestCallback that wraps an LLMClient, allowing the
// Story system's character agents to use the engine's LLM infrastructure
// without direct dependency.
//
// Usage:
//   auto callback = LLMAgentBridge::createCallback(llmClient);
//   auto agent = std::make_unique<Story::LLMCharacterAgent>(callback, &fallback);
// ============================================================================

class LLMAgentBridge {
public:
    /// Create a callback that sends prompts through the given LLMClient.
    /// The LLMClient must outlive the returned callback.
    static Story::LLMRequestCallback createCallback(LLMClient* client) {
        if (!client || !client->isConfigured()) return nullptr;

        return [client](const std::string& prompt) -> std::string {
            std::vector<LLMMessage> messages;
            messages.push_back({"user", prompt});

            auto response = client->complete(messages);
            if (response.ok()) {
                return response.content;
            }
            return "";  // Empty string signals failure → fallback agent
        };
    }

    /// Create a callback with a system prompt prepended.
    static Story::LLMRequestCallback createCallback(LLMClient* client,
                                                      const std::string& systemPrompt) {
        if (!client || !client->isConfigured()) return nullptr;

        return [client, systemPrompt](const std::string& prompt) -> std::string {
            std::vector<LLMMessage> messages;
            if (!systemPrompt.empty()) {
                messages.push_back({"system", systemPrompt});
            }
            messages.push_back({"user", prompt});

            auto response = client->complete(messages);
            if (response.ok()) {
                return response.content;
            }
            return "";
        };
    }
};

} // namespace AI
} // namespace Phyxel
