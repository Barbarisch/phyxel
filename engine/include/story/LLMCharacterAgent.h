#pragma once

#include "story/CharacterAgent.h"
#include <functional>
#include <string>

namespace Phyxel {
namespace Story {

// ============================================================================
// LLMCharacterAgent — AI-backed character agent.
//
// Builds structured prompts from CharacterDecisionContext and delegates
// to a configurable LLM backend via callback. Falls back to a
// RuleBasedCharacterAgent if the LLM is unavailable.
// ============================================================================

/// Callback that sends a prompt string and returns an LLM response string.
/// The caller is responsible for wiring this to their LLM backend (Goose, OpenAI, etc.).
/// Returns empty string on failure.
using LLMRequestCallback = std::function<std::string(const std::string& prompt)>;

class RuleBasedCharacterAgent; // forward declaration for fallback

class LLMCharacterAgent : public CharacterAgent {
public:
    /// @param requestCallback  Function that sends a prompt to an LLM and returns the response.
    /// @param fallback  Rule-based agent to use when LLM is unavailable (optional, can be null).
    explicit LLMCharacterAgent(LLMRequestCallback requestCallback,
                                CharacterAgent* fallback = nullptr);

    /// Set a system prompt prefix applied to all LLM calls.
    void setSystemPrompt(const std::string& systemPrompt);
    const std::string& getSystemPrompt() const { return m_systemPrompt; }

    /// Set the maximum response token budget hint (included in prompt).
    void setMaxResponseTokens(int tokens) { m_maxResponseTokens = tokens; }
    int getMaxResponseTokens() const { return m_maxResponseTokens; }

    /// Check if the LLM backend is configured.
    bool isLLMAvailable() const { return m_requestCallback != nullptr; }

    /// Set/replace the fallback agent.
    void setFallback(CharacterAgent* fallback) { m_fallback = fallback; }

    // --- CharacterAgent interface ---
    CharacterDecision decide(const CharacterDecisionContext& context) override;
    std::string generateDialogue(const CharacterDecisionContext& context) override;
    std::string getAgentName() const override { return "LLM"; }

    // --- Prompt builders (public for testing) ---

    /// Build the decision prompt from context.
    std::string buildDecisionPrompt(const CharacterDecisionContext& context) const;

    /// Build the dialogue prompt from context.
    std::string buildDialoguePrompt(const CharacterDecisionContext& context) const;

    /// Parse a decision from LLM JSON response. Returns nullopt on parse failure.
    static bool parseDecisionResponse(const std::string& response, CharacterDecision& out);

private:
    LLMRequestCallback m_requestCallback;
    CharacterAgent* m_fallback = nullptr;
    std::string m_systemPrompt;
    int m_maxResponseTokens = 200;
};

} // namespace Story
} // namespace Phyxel
