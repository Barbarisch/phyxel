#pragma once

#include "scene/NPCBehavior.h"
#include "story/CharacterAgent.h"
#include "story/CharacterProfile.h"
#include "story/CharacterMemory.h"
#include "ai/Schedule.h"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

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

    /// Set how often the agent re-evaluates decisions (seconds). Default: 1.5
    void setDecisionInterval(float seconds) { m_decisionInterval = seconds; }
    float getDecisionInterval() const { return m_decisionInterval; }

    /// Wander/roam speed for move_to decisions (world units/sec). Default: 1.5
    void setWalkSpeed(float speed) { m_walkSpeed = speed; }

    /// Give this character a daily routine. When set (and a DayNightCycle +
    /// LocationRegistry are available), the character heads to its scheduled
    /// location for the current hour instead of wandering randomly.
    void setSchedule(AI::Schedule schedule) {
        m_schedule = std::move(schedule);
        m_hasSchedule = m_schedule.size() > 0;
    }
    /// Name of the activity the character is currently doing (from its schedule).
    const std::string& getCurrentActivity() const { return m_currentActivity; }

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

    float m_decisionInterval = 1.5f;
    float m_decisionTimer = 0.0f;

    Story::CharacterDecision m_lastDecision;
    DecisionCallback m_onDecision;
    SituationBuilder m_situationBuilder;
    std::function<Story::CharacterProfile*(const std::string&)> m_lookupProfile;

    // Conversation state
    std::string m_conversationPartnerId;
    std::string m_conversationHistory;

    // Embodied action state (translating decisions into movement/speech).
    float     m_walkSpeed = 1.5f;
    bool      m_moving = false;
    glm::vec3 m_roamTarget{0.0f};
    glm::vec3 m_anchor{0.0f};     // home point wander stays near (first-seen position)
    bool      m_anchorSet = false;

    // Daily routine (optional).
    AI::Schedule m_schedule;
    bool         m_hasSchedule = false;
    std::string  m_currentActivity;   // e.g. "Work", "Sleep" (from the active schedule entry)

    // Path-following state (used when a NavGraph is available via NPCContext;
    // otherwise movement falls back to direct-line steering).
    std::vector<glm::vec3> m_path;
    size_t                 m_pathIndex = 0;
    glm::vec3              m_pathTarget{0.0f};   // destination the current path was planned for
    bool                  m_hasPathTarget = false;

    // Async path-query state (used when ctx.pathService is present): a query is in
    // flight; we hold and poll for it instead of blocking the main thread on A*.
    uint64_t m_pathHandle = 0;   // PathService::Handle (0 = none/invalid)
    bool     m_pathPending = false;

    Story::CharacterDecisionContext buildContext(const NPCContext& ctx) const;
    std::string buildDefaultSituation(const NPCContext& ctx) const;

    // Decision execution
    bool applySchedule(NPCContext& ctx);                 // route toward the scheduled location; true if it set intent
    void applyDecision(NPCContext& ctx);                 // set up movement/speech from m_lastDecision
    void updateMovement(float dt, NPCContext& ctx);      // per-frame steering toward roam target
    void replanPath(NPCContext& ctx, const glm::vec3& from);  // (re)compute a NavGraph path to m_roamTarget
    void maybeAmbientChatter(NPCContext& ctx);           // personality-driven idle speech
    void pickRoamTarget(NPCContext& ctx);                // choose a new wander point near the anchor
    void sayBubble(NPCContext& ctx, const std::string& text);
};

} // namespace Scene
} // namespace Phyxel
