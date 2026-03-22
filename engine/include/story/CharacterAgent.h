#pragma once

#include "story/CharacterProfile.h"
#include "story/CharacterMemory.h"
#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Story {

// ============================================================================
// CharacterDecisionContext — the subjective view a character agent receives.
// Never includes WorldState — only what the character knows.
// ============================================================================

struct CharacterDecisionContext {
    // Who am I?
    const CharacterProfile* profile = nullptr;

    // What do I know? (built from CharacterMemory::buildContextSummary)
    std::string knowledgeSummary;

    // What's happening right now? (nearby entities, current location, recent events)
    std::string currentSituation;

    // What are my options?
    std::vector<std::string> availableActions;

    // Who am I talking to (if in dialogue)? Only public-facing info visible.
    const CharacterProfile* conversationPartner = nullptr;
    std::string conversationHistory;
};

// ============================================================================
// CharacterDecision — the result of an agent's decision-making
// ============================================================================

struct CharacterDecision {
    std::string action;        // "speak", "move_to", "attack", "trade", "flee", "wait", "idle"
    nlohmann::json parameters; // Action-specific params
    std::string reasoning;     // For debugging/logging

    // If action is "speak":
    std::string dialogueText;
    std::string emotion;       // Emotion expressed in speech
};

void to_json(nlohmann::json& j, const CharacterDecision& d);
void from_json(const nlohmann::json& j, CharacterDecision& d);

// ============================================================================
// CharacterAgent — abstract interface for character AI
//
// Each character gets its own agent. The agent sees ONLY what the character
// knows — never the full world state. This enforces knowledge asymmetry.
// ============================================================================

class CharacterAgent {
public:
    virtual ~CharacterAgent() = default;

    /// Given the character's subjective view, decide what to do.
    virtual CharacterDecision decide(const CharacterDecisionContext& context) = 0;

    /// Generate dialogue for a conversation.
    virtual std::string generateDialogue(const CharacterDecisionContext& context) = 0;

    /// Returns a human-readable name for this agent type.
    virtual std::string getAgentName() const = 0;
};

} // namespace Story
} // namespace Phyxel
