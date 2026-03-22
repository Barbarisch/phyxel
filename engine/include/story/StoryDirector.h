#pragma once

#include "story/StoryDirectorTypes.h"
#include "story/StoryTypes.h"
#include "story/CharacterProfile.h"
#include "story/EventBus.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace Phyxel {
namespace Story {

// ============================================================================
// DirectorCallback — notifies the game when the director wants to act
//
// Rather than directly manipulating the world, the director emits actions
// that the game layer executes. This keeps the story system decoupled.
// ============================================================================

using DirectorActionCallback = std::function<void(const DirectorAction&)>;

// ============================================================================
// ConditionEvaluator — evaluates trigger/completion conditions
//
// The default evaluator checks if a world variable with the given key
// exists and is truthy (non-zero, non-empty, non-false).
// Games can provide a custom evaluator for more complex expressions.
// ============================================================================

using ConditionEvaluator = std::function<bool(const std::string& condition, const WorldState& world)>;

// ============================================================================
// StoryDirector — the GM agent
//
// Manages narrative pacing and arc progression. Evaluates story beats
// against world state and decides when/how to advance the narrative.
//
// Each arc has a constraint mode controlling how aggressively the director
// intervenes:
//   - Scripted:  forces beats in order
//   - Guided:    creates opportunities, nudges toward beats
//   - Emergent:  observes, injects catalysts for drama
//   - Freeform:  manages pacing only (no beats)
// ============================================================================

class StoryDirector {
public:
    StoryDirector() = default;
    ~StoryDirector() = default;

    // Non-copyable
    StoryDirector(const StoryDirector&) = delete;
    StoryDirector& operator=(const StoryDirector&) = delete;

    // === Arc Management ===

    void addArc(StoryArc arc);
    bool removeArc(const std::string& arcId);
    void activateArc(const std::string& arcId);
    void deactivateArc(const std::string& arcId);

    const StoryArc* getArc(const std::string& arcId) const;
    StoryArc* getArcMut(const std::string& arcId);
    const std::vector<StoryArc>& getArcs() const { return m_arcs; }
    std::vector<std::string> getActiveArcIds() const;

    // === Update ===

    /// Main update loop. Evaluates active arcs, checks beat conditions,
    /// manages pacing, and emits director actions.
    void update(float dt, WorldState& worldState);

    // === Configuration ===

    /// Set the callback that receives director actions.
    void setActionCallback(DirectorActionCallback callback) { m_actionCallback = std::move(callback); }

    /// Set a custom condition evaluator. If not set, uses default variable-based evaluation.
    void setConditionEvaluator(ConditionEvaluator eval) { m_conditionEvaluator = std::move(eval); }

    /// Subscribe the director to an EventBus so it can react to world events.
    void listenTo(EventBus& bus);

    // === Director Actions (convenience) ===

    /// Inject an event into the world. Emits a "inject_event" DirectorAction.
    void injectEvent(const WorldEvent& event);

    /// Promote a character's agency level.
    void promoteCharacterAgency(const std::string& characterId, AgencyLevel level);

    /// Suggest a new goal to a character.
    void suggestGoal(const std::string& characterId, const std::string& goalId,
                     const std::string& description, float priority = 0.5f);

    /// Modify a faction relation.
    void modifyFactionRelation(const std::string& factionA, const std::string& factionB, float delta);

    /// Set a world variable.
    void setWorldVariable(const std::string& key, const std::string& value);

    /// Skip a beat (mark as Skipped).
    void skipBeat(const std::string& arcId, const std::string& beatId);

    /// Manually complete a beat.
    void completeBeat(const std::string& arcId, const std::string& beatId, float worldTime);

    // === Queries ===

    /// Get the target tension for an arc at its current progress.
    float getTargetTension(const std::string& arcId) const;

    /// Get the director's recommended global tension based on all active arcs.
    float getRecommendedTension() const;

    /// How long since the last beat completed in any active arc (game-time seconds).
    float getTimeSinceLastBeat() const { return m_timeSinceLastBeat; }

    /// Get all pending director actions (emitted but not yet confirmed executed).
    const std::vector<DirectorAction>& getPendingActions() const { return m_pendingActions; }

    /// Clear the pending action queue (call after game processes them).
    void clearPendingActions() { m_pendingActions.clear(); }

    // === Serialization ===

    nlohmann::json saveState() const;
    void loadState(const nlohmann::json& state);

private:
    // Beat evaluation per constraint mode
    void evaluateScriptedArc(StoryArc& arc, WorldState& worldState);
    void evaluateGuidedArc(StoryArc& arc, WorldState& worldState);
    void evaluateEmergentArc(StoryArc& arc, WorldState& worldState);
    void evaluateFreeformArc(StoryArc& arc, WorldState& worldState);

    // Condition checking
    bool evaluateCondition(const std::string& condition, const WorldState& worldState) const;
    bool arePrerequisitesMet(const StoryBeat& beat, const StoryArc& arc) const;

    // Pacing
    void updatePacing(float dt, WorldState& worldState);
    float interpolateTensionCurve(const std::vector<float>& curve, float progress) const;

    // Action emission
    void emitAction(DirectorAction action);

    // Event listener (from EventBus)
    void onWorldEvent(const WorldEvent& event);

    std::vector<StoryArc> m_arcs;
    DirectorActionCallback m_actionCallback;
    ConditionEvaluator m_conditionEvaluator;
    std::vector<DirectorAction> m_pendingActions;
    float m_timeSinceLastBeat = 0.0f;
    int m_eventBusSubscriptionId = -1;
};

} // namespace Story
} // namespace Phyxel
