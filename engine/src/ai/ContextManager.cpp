#include "ai/ContextManager.h"
#include "ai/ConversationMemory.h"
#include "story/StoryEngine.h"
#include "story/CharacterProfile.h"
#include "story/CharacterMemory.h"
#include "story/StoryDirector.h"
#include "story/StoryDirectorTypes.h"
#include "story/StoryTypes.h"
#include "core/EntityRegistry.h"
#include "scene/Entity.h"
#include "utils/Logger.h"

#include <sstream>
#include <algorithm>

namespace Phyxel {
namespace AI {

// ============================================================================
// Construction
// ============================================================================

ContextManager::ContextManager(Story::StoryEngine* story,
                               ConversationMemory* memory,
                               Core::EntityRegistry* registry)
    : m_story(story)
    , m_memory(memory)
    , m_registry(registry)
{
}

// ============================================================================
// Build Complete Context
// ============================================================================

ConversationContext ContextManager::buildContext(
    const std::string& npcId,
    const std::string& playerMessage,
    const glm::vec3& playerPos)
{
    ConversationContext ctx;

    const auto* profile = m_story ? m_story->getCharacter(npcId) : nullptr;
    if (!profile) {
        LOG_WARN("AI", "ContextManager: no CharacterProfile for '{}'", npcId);
        // Minimal fallback — still usable
        ctx.messages.push_back({"system", "You are an NPC in a voxel world. Respond in character. Keep responses under 3 sentences."});
        ctx.messages.push_back({"user", playerMessage});
        ctx.estimatedTokens = estimateTokens(playerMessage) + 30;
        return ctx;
    }

    // === Build system prompt (character identity + personality + mood) ===
    std::string systemPrompt = buildSystemPrompt(*profile);

    // === Append optional context sections ===
    std::string worldCtx = buildWorldContext(*profile);
    std::string relCtx = buildRelationshipContext(npcId);
    std::string nearbyCtx = buildNearbyContext(playerPos, npcId);
    std::string memCtx = buildMemoryContext(npcId);
    std::string summaryCtx = buildConversationSummary(npcId);
    std::string questCtx = buildQuestContext(npcId);
    std::string actionCtx = buildActionInstructions(profile->name);

    // Assemble system message with all context sections
    std::ostringstream sys;
    sys << systemPrompt;
    if (!worldCtx.empty())   sys << "\n\n" << worldCtx;
    if (!relCtx.empty())     sys << "\n\n" << relCtx;
    if (!nearbyCtx.empty())  sys << "\n\n" << nearbyCtx;
    if (!memCtx.empty())     sys << "\n\n" << memCtx;
    if (!summaryCtx.empty()) sys << "\n\n" << summaryCtx;
    if (!questCtx.empty())   sys << "\n\n" << questCtx;
    if (!actionCtx.empty())  sys << "\n\n" << actionCtx;

    ctx.messages.push_back({"system", sys.str()});

    // === Add recent conversation history as user/assistant turns ===
    if (m_memory) {
        auto recentTurns = m_memory->getRecentTurns(npcId, 10);
        for (const auto& turn : recentTurns) {
            std::string role = (turn.speaker == "player") ? "user" : "assistant";
            ctx.messages.push_back({role, turn.text});
        }
    }

    // === Add current player message ===
    ctx.messages.push_back({"user", playerMessage});

    // Estimate total tokens
    int total = 0;
    for (const auto& msg : ctx.messages) {
        total += estimateTokens(msg.content) + 4; // ~4 tokens overhead per message
    }
    ctx.estimatedTokens = total;

    // Trim if over budget
    trimToTokenBudget(ctx);

    return ctx;
}

// ============================================================================
// System Prompt — Character Identity
// ============================================================================

std::string ContextManager::buildSystemPrompt(const Story::CharacterProfile& npc) {
    std::ostringstream ss;
    ss << "You are " << npc.name;
    if (!npc.description.empty()) {
        ss << ", " << npc.description;
    }
    ss << ".";

    // Personality traits (Big Five)
    const auto& t = npc.traits;
    ss << "\n\nPersonality: ";
    std::vector<std::string> traitDescs;
    if (t.openness > 0.7f) traitDescs.push_back("curious and imaginative");
    else if (t.openness < 0.3f) traitDescs.push_back("practical and conventional");

    if (t.conscientiousness > 0.7f) traitDescs.push_back("organized and reliable");
    else if (t.conscientiousness < 0.3f) traitDescs.push_back("spontaneous and flexible");

    if (t.extraversion > 0.7f) traitDescs.push_back("outgoing and talkative");
    else if (t.extraversion < 0.3f) traitDescs.push_back("reserved and quiet");

    if (t.agreeableness > 0.7f) traitDescs.push_back("kind and cooperative");
    else if (t.agreeableness < 0.3f) traitDescs.push_back("blunt and competitive");

    if (t.neuroticism > 0.7f) traitDescs.push_back("anxious and emotional");
    else if (t.neuroticism < 0.3f) traitDescs.push_back("calm and steady");

    // Custom traits
    for (const auto& [name, val] : t.customTraits) {
        if (val > 0.5f) {
            traitDescs.push_back(name);
        }
    }

    if (traitDescs.empty()) {
        ss << "balanced and moderate.";
    } else {
        for (size_t i = 0; i < traitDescs.size(); i++) {
            if (i > 0) ss << ", ";
            ss << traitDescs[i];
        }
        ss << ".";
    }

    // Current mood
    const auto* emotion = m_story->getCharacterEmotion(npc.id);
    if (emotion) {
        std::string mood = emotion->dominantEmotion();
        if (!mood.empty() && mood != "neutral") {
            ss << "\nCurrent mood: " << mood << ".";
        }
    }

    // Active goals
    bool hasGoals = false;
    for (const auto& goal : npc.goals) {
        if (goal.isActive) {
            if (!hasGoals) {
                ss << "\n\nYour current goals:";
                hasGoals = true;
            }
            ss << "\n- " << goal.description;
            if (goal.priority > 0.8f) ss << " (high priority)";
        }
    }

    return ss.str();
}

// ============================================================================
// World Context — Time, weather, recent events
// ============================================================================

std::string ContextManager::buildWorldContext(const Story::CharacterProfile& npc) {
    if (!m_story) return "";

    const auto& worldState = m_story->getWorldState();
    std::ostringstream ss;
    ss << "World context:";

    // World variables (time, weather, etc.)
    for (const auto& [key, var] : worldState.variables) {
        if (key == "time" || key == "weather" || key == "season" || key == "location") {
            ss << "\n- " << key << ": ";
            std::visit([&ss](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>) ss << v;
                else if constexpr (std::is_same_v<T, bool>) ss << (v ? "true" : "false");
                else ss << v;
            }, var.value);
        }
    }

    // Faction info if relevant
    if (!npc.factionId.empty()) {
        ss << "\nYou belong to the " << npc.factionId << " faction.";
    }

    std::string result = ss.str();
    return (result == "World context:") ? "" : result; // Return empty if no data
}

// ============================================================================
// Relationship Context
// ============================================================================

std::string ContextManager::buildRelationshipContext(const std::string& npcId) {
    const auto* profile = m_story ? m_story->getCharacter(npcId) : nullptr;
    if (!profile || profile->relationships.empty()) return "";

    std::ostringstream ss;
    ss << "Your relationships:";

    for (const auto& rel : profile->relationships) {
        ss << "\n- " << rel.targetCharacterId;
        if (!rel.label.empty()) ss << " (" << rel.label << ")";
        ss << ": trust=" << static_cast<int>(rel.trust * 100) << "%";
        if (rel.affection > 0.3f) ss << ", friendly";
        else if (rel.affection < -0.3f) ss << ", hostile";
        if (rel.fear > 0.5f) ss << ", feared";
    }

    return ss.str();
}

// ============================================================================
// Nearby Context — What entities are near the conversation
// ============================================================================

std::string ContextManager::buildNearbyContext(const glm::vec3& playerPos,
                                                const std::string& npcId) {
    if (!m_registry) return "";

    auto nearby = m_registry->getEntitiesNear(playerPos, 30.0f);
    if (nearby.size() <= 1) return ""; // Only the NPC itself

    std::ostringstream ss;
    ss << "Nearby:";

    int count = 0;
    for (const auto& [id, entity] : nearby) {
        if (id == npcId) continue; // Skip self
        if (count >= 5) break;      // Limit for token budget

        glm::vec3 pos = entity->getPosition();
        float dist = glm::length(pos - playerPos);

        ss << "\n- " << id << " (" << static_cast<int>(dist) << " blocks away)";
        count++;
    }

    return (count == 0) ? "" : ss.str();
}

// ============================================================================
// Memory Context — What this NPC knows/remembers
// ============================================================================

std::string ContextManager::buildMemoryContext(const std::string& npcId, int maxFacts) {
    if (!m_story) return "";

    const auto* charMemory = m_story->getCharacterMemory(npcId);
    if (!charMemory) return "";

    // Use the built-in summary builder
    std::string summary = charMemory->buildContextSummary(maxFacts);
    if (summary.empty()) return "";

    return "What you know:\n" + summary;
}

// ============================================================================
// Conversation Summary — Compressed history of past interactions
// ============================================================================

std::string ContextManager::buildConversationSummary(const std::string& npcId) {
    if (!m_memory) return "";

    std::string summary = m_memory->getSummary(npcId);
    if (summary.empty()) return "";

    return "Previous interactions summary:\n" + summary;
}

// ============================================================================
// Quest Context — Active story arcs involving this NPC
// ============================================================================

std::string ContextManager::buildQuestContext(const std::string& npcId) {
    if (!m_story) return "";

    const auto* profile = m_story->getCharacter(npcId);
    if (!profile) return "";

    auto& director = m_story->getDirector();
    auto activeArcs = director.getActiveArcIds();
    if (activeArcs.empty()) return "";

    std::ostringstream ss;
    bool hasRelevant = false;

    for (const auto& arcId : activeArcs) {
        const auto* arc = director.getArc(arcId);
        if (!arc) continue;

        // Check if this NPC is relevant to the arc (by role or explicit reference)
        // Simple heuristic: check if NPC name appears in arc description or beats
        bool relevant = false;
        if (arc->description.find(profile->name) != std::string::npos ||
            arc->description.find(npcId) != std::string::npos) {
            relevant = true;
        }

        // Also check if any beat mentions this character
        if (!relevant) {
            for (const auto& beat : arc->beats) {
                if (beat.description.find(profile->name) != std::string::npos ||
                    beat.description.find(npcId) != std::string::npos) {
                    relevant = true;
                    break;
                }
            }
        }

        // If NPC has quest-relevant roles, always include
        for (const auto& role : profile->roles) {
            if (role == "questgiver" || role == "vendor" || role == "guide") {
                relevant = true;
                break;
            }
        }

        if (relevant) {
            if (!hasRelevant) {
                ss << "Active story context:";
                hasRelevant = true;
            }
            ss << "\n- " << arc->name << ": " << arc->description;

            float tension = director.getTargetTension(arcId);
            if (tension > 0.7f) ss << " (tension is high)";
            else if (tension < 0.3f) ss << " (things are calm)";
        }
    }

    return hasRelevant ? ss.str() : "";
}

// ============================================================================
// Action Instructions — Tell the LLM how to format responses
// ============================================================================

std::string ContextManager::buildActionInstructions(const std::string& npcName) {
    std::ostringstream ss;
    ss << "Instructions:\n";
    ss << "- Respond in character as " << npcName << ". Stay consistent with your personality.\n";
    ss << "- Keep responses under 3 sentences unless the topic requires more.\n";
    ss << "- You may include action tags on separate lines:\n";
    ss << "  [EMOTE:wave] [EMOTE:nod] [EMOTE:shrug] — perform a gesture\n";
    ss << "  [GIVE_ITEM:item_name] — give an item to the player\n";
    ss << "  [QUEST_UPDATE:quest_id:started|completed|failed] — update quest state\n";
    ss << "  [MOOD:happy|sad|angry|scared|neutral] — change your emotional state\n";
    ss << "- Only use action tags when narratively appropriate.\n";
    ss << "- Do not break character or reference being an AI.";
    return ss.str();
}

// ============================================================================
// Token Estimation & Trimming
// ============================================================================

int ContextManager::estimateTokens(const std::string& text) {
    // Rough heuristic: ~4 characters per token for English text
    return static_cast<int>(text.size()) / 4 + 1;
}

void ContextManager::trimToTokenBudget(ConversationContext& ctx) {
    if (ctx.estimatedTokens <= m_maxContextTokens) return;

    // Strategy: keep system message (first) and current user message (last),
    // trim conversation history from the oldest turns
    while (ctx.estimatedTokens > m_maxContextTokens && ctx.messages.size() > 2) {
        // Remove the second message (oldest history turn)
        int removed = estimateTokens(ctx.messages[1].content) + 4;
        ctx.messages.erase(ctx.messages.begin() + 1);
        ctx.estimatedTokens -= removed;
    }

    // If still over budget after trimming history, truncate system prompt
    if (ctx.estimatedTokens > m_maxContextTokens && !ctx.messages.empty()) {
        auto& sysMsg = ctx.messages[0];
        int targetChars = (m_maxContextTokens - estimateTokens(ctx.messages.back().content) - 50) * 4;
        if (targetChars > 0 && static_cast<int>(sysMsg.content.size()) > targetChars) {
            sysMsg.content = sysMsg.content.substr(0, targetChars) + "\n[Context truncated]";
            // Recalculate
            int total = 0;
            for (const auto& msg : ctx.messages) {
                total += estimateTokens(msg.content) + 4;
            }
            ctx.estimatedTokens = total;
        }
    }
}

} // namespace AI
} // namespace Phyxel
