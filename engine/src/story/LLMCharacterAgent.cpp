#include "story/LLMCharacterAgent.h"
#include "story/RuleBasedCharacterAgent.h"
#include <sstream>

namespace Phyxel {
namespace Story {

LLMCharacterAgent::LLMCharacterAgent(LLMRequestCallback requestCallback,
                                       CharacterAgent* fallback)
    : m_requestCallback(std::move(requestCallback))
    , m_fallback(fallback)
{
    m_systemPrompt = "You are a character in a voxel game world. "
                     "Stay in character at all times. Respond ONLY with valid JSON.";
}

void LLMCharacterAgent::setSystemPrompt(const std::string& systemPrompt) {
    m_systemPrompt = systemPrompt;
}

CharacterDecision LLMCharacterAgent::decide(const CharacterDecisionContext& context) {
    if (!m_requestCallback) {
        if (m_fallback) return m_fallback->decide(context);
        return {"idle", {}, "LLM unavailable, no fallback", "", ""};
    }

    std::string prompt = buildDecisionPrompt(context);
    std::string response = m_requestCallback(prompt);

    if (response.empty()) {
        if (m_fallback) return m_fallback->decide(context);
        return {"idle", {}, "LLM returned empty response", "", ""};
    }

    CharacterDecision decision;
    if (parseDecisionResponse(response, decision)) {
        return decision;
    }

    // Parse failed — fall back
    if (m_fallback) return m_fallback->decide(context);
    return {"idle", {}, "LLM response parse failed", "", ""};
}

std::string LLMCharacterAgent::generateDialogue(const CharacterDecisionContext& context) {
    if (!m_requestCallback) {
        if (m_fallback) return m_fallback->generateDialogue(context);
        return "";
    }

    std::string prompt = buildDialoguePrompt(context);
    std::string response = m_requestCallback(prompt);

    if (response.empty()) {
        if (m_fallback) return m_fallback->generateDialogue(context);
        return "";
    }

    // Try to extract dialogue from JSON response
    try {
        auto j = nlohmann::json::parse(response);
        if (j.contains("dialogue")) return j["dialogue"].get<std::string>();
        if (j.contains("text")) return j["text"].get<std::string>();
    } catch (...) {
        // Not JSON — treat the raw response as dialogue text
    }

    // Raw text response — use directly (trimmed)
    auto trimmed = response;
    while (!trimmed.empty() && (trimmed.front() == '"' || trimmed.front() == ' '))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == '"' || trimmed.back() == ' '))
        trimmed.pop_back();
    return trimmed;
}

std::string LLMCharacterAgent::buildDecisionPrompt(const CharacterDecisionContext& context) const {
    std::ostringstream ss;

    ss << m_systemPrompt << "\n\n";

    // Character identity
    if (context.profile) {
        ss << "## Your Character\n";
        ss << "Name: " << context.profile->name << "\n";
        ss << "Description: " << context.profile->description << "\n";
        ss << "Personality: openness=" << context.profile->traits.openness
           << " conscientiousness=" << context.profile->traits.conscientiousness
           << " extraversion=" << context.profile->traits.extraversion
           << " agreeableness=" << context.profile->traits.agreeableness
           << " neuroticism=" << context.profile->traits.neuroticism << "\n";
        ss << "Current emotion: " << context.profile->emotion.dominantEmotion() << "\n";

        if (!context.profile->goals.empty()) {
            ss << "Goals:\n";
            for (const auto& goal : context.profile->goals) {
                if (goal.isActive) {
                    ss << "- " << goal.description << " (priority: " << goal.priority << ")\n";
                }
            }
        }

        if (!context.profile->roles.empty()) {
            ss << "Roles: ";
            for (size_t i = 0; i < context.profile->roles.size(); ++i) {
                if (i > 0) ss << ", ";
                ss << context.profile->roles[i];
            }
            ss << "\n";
        }
    }

    // Knowledge
    if (!context.knowledgeSummary.empty()) {
        ss << "\n## What You Know\n" << context.knowledgeSummary << "\n";
    }

    // Current situation
    if (!context.currentSituation.empty()) {
        ss << "\n## Current Situation\n" << context.currentSituation << "\n";
    }

    // Conversation partner
    if (context.conversationPartner) {
        ss << "\n## Conversation Partner\n";
        ss << "Name: " << context.conversationPartner->name << "\n";
        ss << "Description: " << context.conversationPartner->description << "\n";
        if (!context.conversationHistory.empty()) {
            ss << "Conversation so far:\n" << context.conversationHistory << "\n";
        }
    }

    // Available actions
    ss << "\n## Available Actions\n";
    if (context.availableActions.empty()) {
        ss << "speak, move_to, attack, trade, flee, wait, idle\n";
    } else {
        for (size_t i = 0; i < context.availableActions.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << context.availableActions[i];
        }
        ss << "\n";
    }

    // Response format
    ss << "\n## Response Format\n";
    ss << "Respond with a JSON object:\n";
    ss << "{\n";
    ss << "  \"action\": \"<action_name>\",\n";
    ss << "  \"parameters\": {},\n";
    ss << "  \"reasoning\": \"<brief explanation>\",\n";
    ss << "  \"dialogueText\": \"<if speaking>\",\n";
    ss << "  \"emotion\": \"<current emotion>\"\n";
    ss << "}\n";

    return ss.str();
}

std::string LLMCharacterAgent::buildDialoguePrompt(const CharacterDecisionContext& context) const {
    std::ostringstream ss;

    ss << m_systemPrompt << "\n\n";

    if (context.profile) {
        ss << "You are " << context.profile->name << ". " << context.profile->description << "\n";
        ss << "Personality: openness=" << context.profile->traits.openness
           << " extraversion=" << context.profile->traits.extraversion
           << " agreeableness=" << context.profile->traits.agreeableness << "\n";
        ss << "Current emotion: " << context.profile->emotion.dominantEmotion() << "\n";
    }

    if (!context.knowledgeSummary.empty()) {
        ss << "\nWhat you know:\n" << context.knowledgeSummary << "\n";
    }

    if (context.conversationPartner) {
        ss << "\nYou are speaking with " << context.conversationPartner->name << ".\n";
        if (!context.conversationHistory.empty()) {
            ss << "Conversation so far:\n" << context.conversationHistory << "\n";
        }
    }

    ss << "\nRespond in-character with a single line of natural dialogue. "
       << "Stay consistent with your personality and emotions. "
       << "Keep it brief (1-2 sentences).\n";

    return ss.str();
}

bool LLMCharacterAgent::parseDecisionResponse(const std::string& response, CharacterDecision& out) {
    try {
        auto j = nlohmann::json::parse(response);
        if (!j.contains("action")) return false;

        out.action = j["action"].get<std::string>();
        if (j.contains("parameters")) out.parameters = j["parameters"];
        if (j.contains("reasoning")) out.reasoning = j["reasoning"].get<std::string>();
        if (j.contains("dialogueText")) out.dialogueText = j["dialogueText"].get<std::string>();
        if (j.contains("emotion")) out.emotion = j["emotion"].get<std::string>();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace Story
} // namespace Phyxel
