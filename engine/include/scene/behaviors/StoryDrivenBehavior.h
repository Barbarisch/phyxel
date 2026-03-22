#pragma once

#include "scene/NPCBehavior.h"
#include "story/CharacterAgent.h"
#include "story/CharacterProfile.h"
#include "story/CharacterMemory.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace Phyxel {

namespace Story { class StoryEngine; }

namespace Scene {

// ============================================================================
// StoryDrivenBehavior — NPCBehavior that delegates decisions to a CharacterAgent.
//
// Bridges the engine's NPC system with the story system. Builds
// CharacterDecisionContext from NPCContext + story data, and translates
// CharacterDecisions into NPC actions (movement, animation, speech).
// ============================================================================

/// Callback invoked when the behavior produces a decision.
/// Game code can react to decisions (execute movement, trigger animation, etc.).
using DecisionCallback = std::function<void(const std::string& npcId,
                                             const Story::CharacterDecision& decision)>;

class StoryDrivenBehavior : public NPCBehavior {
public:
    /// @param agent         The character AI agent (ownership NOT transferred).
    /// @param profile       Character profile pointer (must outlive this behavior).
    /// @param memory        Character memory pointer (must outlive this behavior).
    /// @param storyEngine   Story engine for knowledge/event access (optional).
    StoryDrivenBehavior(Story::CharacterAgent* agent,
                        Story::CharacterProfile* profile,
                        Story::CharacterMemory* memory,
                        Story::StoryEngine* storyEngine = nullptr);

    // --- NPCBehavior interface ---
    void update(float dt, NPCContext& ctx) override;
    void onInteract(Entity* interactor) override;
    void onEvent(const std::string& eventType, const nlohmann::json& data) override;
    std::string getBehaviorName() const override { return "StoryDriven"; }

    // --- Configuration ---

    /// Set how often the agent re-evaluates decisions (seconds). Default: 1.0
    void setDecisionInterval(float seconds) { m_decisionInterval = seconds; }
    float getDecisionInterval() const { return m_decisionInterval; }

    /// Set callback for decision events.
    void setDecisionCallback(DecisionCallback callback) { m_onDecision = std::move(callback); }

    /// Set the situation description builder (converts NPCContext to natural language).
    using SituationBuilder = std::function<std::string(const NPCContext&)>;
    void setSituationBuilder(SituationBuilder builder) { m_situationBuilder = std::move(builder); }

    /// Get the last decision made by the agent.
    const Story::CharacterDecision& getLastDecision() const { return m_lastDecision; }

    /// Get the interacting entity's character profile (set by onInteract via registry lookup).
    void setInteractorProfileLookup(
        std::function<Story::CharacterProfile*(const std::string& entityId)> lookup) {
        m_lookupProfile = std::move(lookup);
    }

    /// Access the agent.
    Story::CharacterAgent* getAgent() const { return m_agent; }

private:
    Story::CharacterAgent* m_agent;
    Story::CharacterProfile* m_profile;
    Story::CharacterMemory* m_memory;
    Story::StoryEngine* m_storyEngine;

    float m_decisionInterval = 1.0f;
    float m_decisionTimer = 0.0f;

    Story::CharacterDecision m_lastDecision;
    DecisionCallback m_onDecision;
    SituationBuilder m_situationBuilder;
    std::function<Story::CharacterProfile*(const std::string&)> m_lookupProfile;

    // Conversation state
    std::string m_conversationPartnerId;
    std::string m_conversationHistory;

    Story::CharacterDecisionContext buildContext(const NPCContext& ctx) const;
    std::string buildDefaultSituation(const NPCContext& ctx) const;
};

} // namespace Scene
} // namespace Phyxel
