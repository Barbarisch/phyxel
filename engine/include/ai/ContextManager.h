#pragma once

#include <string>
#include <vector>
#include <functional>
#include <glm/vec3.hpp>

#include "ai/LLMClient.h"

namespace Phyxel {

// Forward declarations (avoid heavy includes)
namespace Story {
    class StoryEngine;
    struct CharacterProfile;
}
namespace Scene {
    class Entity;
}
namespace Core {
    class EntityRegistry;
    class NPCManager;
}

namespace AI {

class ConversationMemory;

// ============================================================================
// ConversationContext — Ready-to-send prompt for LLMClient
// ============================================================================

struct ConversationContext {
    std::vector<LLMMessage> messages;   // system + conversation history + user
    int estimatedTokens = 0;            // Rough estimate (~4 chars per token)
};

// ============================================================================
// ContextManager — Assembles NPC conversation prompts from game state
//
// This is where the "intelligence" lives. Pulls personality, knowledge,
// relationships, quest state, nearby entities, and conversation history
// to construct the optimal LLM prompt.
// ============================================================================

class ContextManager {
public:
    ContextManager(Story::StoryEngine* story,
                   ConversationMemory* memory,
                   Core::EntityRegistry* registry);

    /// Build complete prompt for an NPC conversation turn
    ConversationContext buildContext(
        const std::string& npcId,
        const std::string& playerMessage,
        const glm::vec3& playerPos
    );

    /// Token budget — context will be trimmed to fit
    void setMaxContextTokens(int tokens) { m_maxContextTokens = tokens; }
    int getMaxContextTokens() const { return m_maxContextTokens; }

    /// Optional: set NPCManager for social context (needs, relationships, worldview)
    void setNPCManager(Core::NPCManager* npcManager) { m_npcManager = npcManager; }

private:
    // Context assembly (in priority order — highest priority survives trimming)
    std::string buildSystemPrompt(const Story::CharacterProfile& npc);
    std::string buildWorldContext(const Story::CharacterProfile& npc);
    std::string buildRelationshipContext(const std::string& npcId);
    std::string buildNearbyContext(const glm::vec3& playerPos, const std::string& npcId);
    std::string buildMemoryContext(const std::string& npcId, int maxFacts = 10);
    std::string buildConversationSummary(const std::string& npcId);
    std::string buildQuestContext(const std::string& npcId);
    std::string buildActionInstructions(const std::string& npcName);
    std::string buildWorldViewContext(const std::string& npcId);
    std::string buildNeedsContext(const std::string& npcId);
    std::string buildSocialRelationshipContext(const std::string& npcId);

    // Rough token estimator (~4 chars per token)
    static int estimateTokens(const std::string& text);

    // Trim context to fit within token budget
    void trimToTokenBudget(ConversationContext& ctx);

    Story::StoryEngine* m_story;
    ConversationMemory* m_memory;
    Core::EntityRegistry* m_registry;
    Core::NPCManager* m_npcManager = nullptr;
    int m_maxContextTokens = 4000;
};

} // namespace AI
} // namespace Phyxel
