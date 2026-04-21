#pragma once

#include "core/InitiativeTracker.h"
#include "core/Party.h"
#include "core/AttackResolver.h"
#include "core/DiceSystem.h"

#include <string>

namespace Phyxel {

namespace Core { class EntityRegistry; }

namespace Core {

/// Drives enemy turns in D&D combat.
///
/// Each frame, tick() checks whether it is a non-player NPC's turn.
/// If so it:
///   1. Waits a short "think" delay so the HUD is visible before acting.
///   2. Picks the nearest hostile target (player/party member).
///   3. Moves the NPC a step toward the target (uses setMoveVelocity if in range).
///   4. If within melee reach, executes an attack roll against the target.
///   5. Calls InitiativeTracker::endTurn() to advance to the next participant.
///
/// MonsterDefinition is looked up from the MonsterRegistry by the NPC's
/// entity ID. If no definition is found, fallback stats (AC 10, 1d4 bludgeoning)
/// are used so the system works without data files.
class CombatAISystem {
public:
    CombatAISystem() = default;

    // -----------------------------------------------------------------------
    // Wiring — call before tick()
    // -----------------------------------------------------------------------

    void setInitiativeTracker(InitiativeTracker* tracker) { m_tracker = tracker; }
    void setParty(Party* party)                            { m_party = party; }
    void setEntityRegistry(EntityRegistry* registry)       { m_registry = registry; }

    // -----------------------------------------------------------------------
    // Per-frame update
    // -----------------------------------------------------------------------

    /// Call once per frame from the main game loop.
    /// @param dt  Delta time in seconds.
    void tick(float dt);

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    /// Seconds to wait before the AI acts on its turn (default 0.6 s).
    void setThinkDelay(float seconds) { m_thinkDelay = seconds; }

    /// Movement speed applied to NPC while advancing on a target (default 4.0 m/s).
    void setMoveSpeed(float speed) { m_moveSpeed = speed; }

    /// Melee reach in world units at which the NPC attacks instead of moving (default 2.0).
    void setMeleeReach(float reach) { m_meleeReach = reach; }

private:
    // Checks if the current turn belongs to a non-party NPC.
    bool isEnemyTurn() const;

    // Execute the enemy's combat action and advance the turn.
    void executeEnemyAction();

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    InitiativeTracker* m_tracker  = nullptr;
    Party*             m_party    = nullptr;
    EntityRegistry*    m_registry = nullptr;

    float m_thinkDelay = 0.6f;   ///< seconds before acting
    float m_moveSpeed  = 4.0f;
    float m_meleeReach = 2.0f;

    // Accumulated think time for the current enemy turn.
    float m_thinkAccum = 0.0f;
    // ID of the entity whose turn we started timing.
    std::string m_timedEntityId;

    DiceSystem m_dice;
};

} // namespace Core
} // namespace Phyxel
