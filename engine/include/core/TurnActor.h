#pragma once

#include "core/ActionEconomy.h"

#include <glm/glm.hpp>

namespace Phyxel {
namespace Core {

/// Abstract handle to whatever a TurnActor drives during its turn — a live
/// AnimatedVoxelCharacter in the running engine, or a mock in tests. Keeping
/// the locomotion/animation behind this interface lets TurnActor stay headless
/// and unit-testable (the budget gating, feet<->world-unit conversion, and the
/// move/attack handshake have no scene dependency). See docs/TurnBasedCombat.md S3.
class ITurnActorBody {
public:
    virtual ~ITurnActorBody() = default;

    /// Current world position.
    virtual glm::vec3 position() const = 0;

    /// Drive locomotion one tick toward `target` (face it + feed the movement
    /// FSM). Returns the horizontal (XZ) distance, in WORLD UNITS, actually
    /// moved since the previous call — TurnActor debits the movement budget
    /// from this, so the body is the authority on how far it travelled.
    virtual float stepToward(const glm::vec3& target, float dt) = 0;

    /// Stop locomotion (zero movement input).
    virtual void stop() = 0;

    /// Begin an attack animation aimed at `targetPos` (face it + trigger the
    /// swing). Damage lands later on the animation's hit frame through the
    /// existing onHitFrame -> CombatSystem path; TurnActor only orchestrates
    /// timing + budget here.
    virtual void beginAttack(const glm::vec3& targetPos) = 0;

    /// True while an attack animation is actively playing.
    virtual bool isAttacking() const = 0;
};

/// Default world-units-per-foot. World units are ~metres in this engine
/// (characters are ~1.8 u tall), so a D&D foot is the literal 0.3048 m. That
/// puts a 30 ft move at ~9.1 u and a 5 ft melee reach at ~1.52 u (matching the
/// ~1.5 u attack ranges combat already uses). One constant, one place — every
/// movement-range / reach conversion goes through TurnActor.
constexpr float kDefaultWorldUnitsPerFoot = 0.3048f;

/// TurnActor — executes ONE creature's turn by translating combat intents
/// ("move to here", "attack there") into ITurnActorBody commands, gated by an
/// ActionBudget. It owns the feet<->world-unit conversion and the
/// turn-advance-vs-animation handshake: while an intent is in progress the
/// actor is "busy" and the turn must not advance.
///
/// Lifecycle per turn:
///   actor.begin(body, &participant.budget);
///   // controller (player input S5 / enemy AI S4) issues intents:
///   actor.requestMove(target);            // or requestAttack(pos, reachFeet)
///   ... each frame: actor.tick(dt);       // until !actor.isBusy()
///   actor.end();                          // then InitiativeTracker::endTurn()
class TurnActor {
public:
    enum class Activity { Idle, Moving, Attacking };

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Bind the body + this turn's action budget. Does not reset the budget
    /// (the InitiativeTracker already reset it at turn start).
    void begin(ITurnActorBody* body, ActionBudget* budget);

    /// Unbind at end of turn (stops any locomotion).
    void end();

    bool isBound() const { return m_body != nullptr && m_budget != nullptr; }

    // -----------------------------------------------------------------------
    // Intents (return false if the budget/state disallows the request)
    // -----------------------------------------------------------------------

    /// Walk toward `target`, spending movement as the body travels. Stops on
    /// arrival or when movement runs out. Rejected if already busy or no
    /// movement remains.
    bool requestMove(const glm::vec3& target);

    /// Attack at `targetPos`. Spends the action. Rejected if busy, the action
    /// is already spent, or the target is beyond `reachFeet`.
    bool requestAttack(const glm::vec3& targetPos, float reachFeet);

    // -----------------------------------------------------------------------
    // Per-frame
    // -----------------------------------------------------------------------

    /// Advance the in-progress intent and debit the budget. No-op when Idle.
    void tick(float dt);

    // -----------------------------------------------------------------------
    // Queries
    // -----------------------------------------------------------------------

    Activity activity() const { return m_activity; }
    bool isBusy() const { return m_activity != Activity::Idle; }

    bool canMove() const { return m_budget && m_budget->canMove(); }
    bool canAct()  const { return m_budget && m_budget->canAct(); }

    /// Remaining movement this turn, in feet (budget units) and world units.
    int   movementRemainingFeet()  const { return m_budget ? m_budget->movementRemaining : 0; }
    float movementRemainingUnits() const { return movementRemainingFeet() * m_unitsPerFoot; }

    /// True if `targetPos` is within `reachFeet` of the body right now (XZ).
    bool inReach(const glm::vec3& targetPos, float reachFeet) const;

    // -----------------------------------------------------------------------
    // Tuning
    // -----------------------------------------------------------------------

    float worldUnitsPerFoot() const { return m_unitsPerFoot; }
    void  setWorldUnitsPerFoot(float v) { if (v > 0.0f) m_unitsPerFoot = v; }

    float feetToUnits(float feet)  const { return feet * m_unitsPerFoot; }
    float unitsToFeet(float units) const { return units / m_unitsPerFoot; }

private:
    void stopAndIdle();
    float horizontalDist(const glm::vec3& a, const glm::vec3& b) const;

    ITurnActorBody* m_body   = nullptr;
    ActionBudget*   m_budget = nullptr;
    Activity        m_activity = Activity::Idle;

    glm::vec3 m_moveTarget{0.0f};

    float m_unitsPerFoot = kDefaultWorldUnitsPerFoot;
    float m_pendingFeet  = 0.0f;   // fractional feet carried between ticks

    bool  m_attackSawActive = false;  // have we observed the swing actually start?
    float m_attackTimer     = 0.0f;   // safety timeout for the attack handshake

    static constexpr float kArriveEpsUnits   = 0.30f;  // "reached" threshold (XZ)
    static constexpr float kAttackTimeoutSec = 3.0f;   // give up waiting on a swing
};

} // namespace Core
} // namespace Phyxel
