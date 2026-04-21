#pragma once

#include "core/ActionEconomy.h"
#include "core/CharacterAttributes.h"
#include "core/DiceSystem.h"

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

/// One creature's slot in combat turn order.
struct CombatParticipant {
    std::string entityId;
    int  initiativeRoll     = 0;   // d20 + DEX mod
    int  initiativeModifier = 0;   // DEX mod, used as tiebreaker
    bool isSurprised        = false; // skips their FIRST turn
    bool hasActedThisRound  = false; // tracks first-turn surprise skip
    bool isPlayer           = false;
    ActionBudget budget;

    /// For sorting: higher roll wins; equal rolls use modifier, then entityId for stability.
    bool operator>(const CombatParticipant& other) const;
};

/// Tracks D&D 5e combat: initiative order, round counting, per-turn action budgets,
/// and surprise. Does NOT own entities — it operates on string IDs.
///
/// Typical flow:
///   tracker.startCombat(ids);
///   for each entity: tracker.rollInitiative(id, dexMod, speed);
///   tracker.sortOrder();
///
///   while combat active:
///     auto& p = tracker.currentParticipant();
///     // game logic acts on p.entityId using p.budget
///     tracker.endTurn();         // advance + reset next entity's budget
///
class InitiativeTracker {
public:
    // -----------------------------------------------------------------------
    // Setup
    // -----------------------------------------------------------------------

    /// Begin combat. Registers all entity IDs. Does not roll initiative yet.
    void startCombat(const std::vector<std::string>& entityIds, int defaultSpeed = 30);

    /// Roll initiative for one entity: 1d20 + dexMod. Stores result.
    void rollInitiative(const std::string& entityId, int dexMod, DiceSystem& dice);

    /// Manually set initiative for an entity (useful for NPCs with fixed initative or tests).
    void setInitiative(const std::string& entityId, int roll, int modifier = 0);

    /// Sort the turn order (highest initiative first). Call after all rolls are done.
    void sortOrder();

    /// Mark an entity as surprised (skips their first turn action and movement).
    void setSurprised(const std::string& entityId, bool surprised);

    // -----------------------------------------------------------------------
    // Turn management
    // -----------------------------------------------------------------------

    /// Advance to the next participant. Resets their ActionBudget.
    /// If we wrap past the last participant, automatically calls nextRound().
    /// Returns the entity ID whose turn it now is.
    const std::string& endTurn();

    /// Manually advance to the next round (called by endTurn when wrapping).
    void nextRound();

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------

    bool isCombatActive() const { return m_active; }
    int  currentRound()   const { return m_round; }

    /// Entity whose turn it currently is.
    const std::string&        currentEntityId() const;
    const CombatParticipant&  currentParticipant() const;
    CombatParticipant&        currentParticipant();

    /// Full ordered list (index 0 = highest initiative).
    const std::vector<CombatParticipant>& turnOrder() const { return m_order; }

    /// True if this is the given entity's turn right now.
    bool isEntityTurn(const std::string& entityId) const;

    /// Find a participant by entity ID (nullptr if not in combat).
    const CombatParticipant* find(const std::string& entityId) const;
    CombatParticipant*       find(const std::string& entityId);

    // -----------------------------------------------------------------------
    // Reactions (out-of-turn)
    // -----------------------------------------------------------------------

    bool canReact(const std::string& entityId) const;
    bool useReaction(const std::string& entityId);

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Remove an entity from combat (dead, fled, etc.).
    void removeParticipant(const std::string& entityId);

    /// End combat completely, clearing all state.
    void endCombat();

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    std::vector<CombatParticipant> m_order;
    size_t m_currentIndex = 0;
    int    m_round        = 0;
    bool   m_active       = false;
    int    m_defaultSpeed = 30;

    void advanceIndex();
};

} // namespace Core
} // namespace Phyxel
