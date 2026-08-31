#pragma once

#include "core/InitiativeTracker.h"
#include "core/Party.h"
#include "core/AttackResolver.h"
#include "core/DiceSystem.h"
#include "core/TurnActor.h"

#include <string>
#include <functional>

namespace Phyxel {

namespace Scene { class Entity; }
namespace Core { class EntityRegistry; class CombatDirector; class CombatSystem;
                 class SpellcasterComponent; }

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

    /// The director gates the AI (turn-based mode only) and is the authority
    /// for whose turn it is + advancing turns. When set, its InitiativeTracker
    /// supersedes any tracker set via setInitiativeTracker().
    void setCombatDirector(CombatDirector* director)       { m_director = director; }

    /// Damage from enemy attacks routes through the unified CombatSystem entry
    /// (death/hit-react/events). Optional — falls back to a raw HP decrement.
    void setCombatSystem(CombatSystem* combat)             { m_combat = combat; }

    /// Host-supplied adapter factory: given the acting enemy Entity, return an
    /// ITurnActorBody that drives its live character (or nullptr to fall back to
    /// instant, non-animated execution). Keeps this core system scene-free.
    using BodyProvider = std::function<ITurnActorBody*(Scene::Entity*)>;
    void setBodyProvider(BodyProvider provider)            { m_bodyProvider = std::move(provider); }
    /// The HUMAN player's entity id. When set, the AI drives every OTHER
    /// combatant's turn — including player-side COMPANIONS (they auto-fight,
    /// targeting the opposing side). When unset (legacy hosts), all
    /// player-side turns wait for input, so single-player behavior is unchanged.
    void setPlayerEntityId(const std::string& id)          { m_playerEntityId = id; }

    /// Host-supplied caster lookup: given a combatant's entity id, return its
    /// SpellcasterComponent (or nullptr for a non-caster). When an acting NPC
    /// has one, the AI will CAST instead of closing to melee if it holds a
    /// castable damaging spell and the target is out of reach. Keeps the core
    /// data-source-agnostic: the host may back it with game.json companions,
    /// MonsterDefinition spell lists, or anything else.
    using CasterProvider = std::function<SpellcasterComponent*(const std::string&)>;
    void setCasterProvider(CasterProvider provider)        { m_casterProvider = std::move(provider); }

    /// Optional cast visual (animation + VFX), mirroring
    /// PlayerTurnController::CastExecutor: the host plays the cast and invokes
    /// onRelease at the release frame. Without it, casts resolve immediately.
    using CastExecutor = std::function<void(const std::string& casterId,
                                            const std::string& spellId,
                                            const glm::vec3& targetPos,
                                            std::function<void()> onRelease)>;
    void setCastExecutor(CastExecutor exec)                { m_castExecutor = std::move(exec); }

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

    /// Melee reach in feet at which the NPC attacks instead of moving (default 5).
    void setMeleeReachFeet(float reachFeet) { m_reachFeet = reachFeet; }

private:
    // The active InitiativeTracker (director's if set, else the legacy pointer).
    InitiativeTracker* tracker() const;

    // Per-turn phase machine (drives one enemy turn across multiple frames).
    // Casting resolves within the frame it is chosen (the visual, if any, plays
    // out asynchronously via the executor), so it needs no phase of its own.
    enum class Phase { Idle, Thinking, Moving, Attacking, Done };

    void beginEnemyTurn(const std::string& enemyId, Scene::Entity* enemyEntity);
    void decideNextAction();                 // choose Cast / Move / Attack / Done
    void resolveEnemyAttack(Scene::Entity* enemyEntity);
    std::string acquireTarget(const glm::vec3& fromPos, const std::string& selfId) const;
    void finishTurn();

    /// The best castable damaging spell for the acting NPC ("" if none): the
    /// highest-level castable damaging spell it has prepared, else a castable
    /// damaging cantrip. Slots are enforced through canCast.
    std::string chooseSpell(SpellcasterComponent& caster) const;

    /// Roll + apply one NPC spell against m_targetId (attack roll / saving
    /// throw / auto-hit), spending the slot. Routes damage through the same
    /// CombatSystem funnel as resolveEnemyAttack.
    void resolveEnemyCast(Scene::Entity* enemyEntity, SpellcasterComponent& caster,
                          const std::string& spellId);

    // -----------------------------------------------------------------------
    // Wiring
    // -----------------------------------------------------------------------
    InitiativeTracker* m_tracker  = nullptr;   // legacy direct tracker (optional)
    CombatDirector*    m_director = nullptr;
    CombatSystem*      m_combat   = nullptr;
    Party*             m_party    = nullptr;
    EntityRegistry*    m_registry = nullptr;
    BodyProvider       m_bodyProvider;
    CasterProvider     m_casterProvider;
    CastExecutor       m_castExecutor;

    // -----------------------------------------------------------------------
    // Tuning
    // -----------------------------------------------------------------------
    float m_thinkDelay = 0.6f;   ///< seconds before the enemy starts acting
    float m_moveSpeed  = 4.0f;   ///< (legacy no-body fallback) m/s advance
    float m_reachFeet  = 5.0f;   ///< melee reach in feet

    // -----------------------------------------------------------------------
    // Per-turn state
    // -----------------------------------------------------------------------
    Phase       m_phase   = Phase::Idle;
    std::string m_actingId;        ///< combatant whose turn we are currently running
    std::string m_playerEntityId;  ///< the human's entity — the only turn the AI waits on
    std::string m_targetId;        ///< chosen target this turn
    float       m_thinkAccum = 0.0f;
    bool        m_attacked   = false;  ///< guard: resolve damage once per attack
    TurnActor   m_turnActor;

    DiceSystem m_dice;
};

} // namespace Core
} // namespace Phyxel
