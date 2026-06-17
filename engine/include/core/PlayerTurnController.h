#pragma once

#include "core/TurnActor.h"
#include "core/DiceSystem.h"
#include "core/DamageTypes.h"

#include <string>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {

namespace Scene { class Entity; }
namespace Core { class CombatDirector; class EntityRegistry; class CombatSystem; }

namespace Core {

/// Drives the PLAYER's turn in turn-based combat. The mirror of CombatAISystem:
/// it binds a TurnActor to the player's live character + this turn's ActionBudget
/// and executes intents — but the intents come from the player (input / UI / HTTP),
/// not an AI. Move debits the movement budget; attack spends the action and
/// resolves d20-vs-AC, routing damage through the unified CombatSystem funnel.
///
/// Headless: operates on entity-ID strings + an injected body provider, so the
/// move/turn-flow logic is unit-testable. See docs/TurnBasedCombat.md S5.
class PlayerTurnController {
public:
    // -----------------------------------------------------------------------
    // Wiring
    // -----------------------------------------------------------------------
    void setCombatDirector(CombatDirector* d)        { m_director = d; }
    void setEntityRegistry(EntityRegistry* r)        { m_registry = r; }
    void setCombatSystem(CombatSystem* c)            { m_combat = c; }

    using BodyProvider = std::function<ITurnActorBody*(Scene::Entity*)>;
    void setBodyProvider(BodyProvider p)             { m_bodyProvider = std::move(p); }

    /// The controlled player's entity id (must match its id in the encounter).
    void setPlayerEntityId(const std::string& id)    { m_playerId = id; }
    const std::string& playerEntityId() const        { return m_playerId; }

    // -----------------------------------------------------------------------
    // Per-frame
    // -----------------------------------------------------------------------
    void tick(float dt);

    // -----------------------------------------------------------------------
    // Intents (from input / UI / HTTP). Return false if not allowed right now.
    // -----------------------------------------------------------------------

    /// Walk the player toward a world point, spending movement.
    bool requestMove(const glm::vec3& worldPoint);

    /// Attack a target entity by id (must be in reach + action available).
    bool requestAttack(const std::string& targetId);

    /// End the player's turn now (advances the director).
    void endTurn();

    // -----------------------------------------------------------------------
    // Queries (for UI / control suppression)
    // -----------------------------------------------------------------------

    /// True when it is the player's turn in active turn-based combat and we are
    /// bound — i.e. the tactical controls should be active and the real-time
    /// movement/attack input must be suppressed.
    bool isPlayerTurnActive() const { return m_bound; }

    bool isBusy() const { return m_turnActor.isBusy(); }
    const ActionBudget* budget() const;       ///< nullptr if not bound
    float movementRemainingUnits() const { return m_turnActor.movementRemainingUnits(); }
    float reachFeet() const { return m_reachFeet; }
    int   attackBonus() const { return m_attackBonus; }

    // -----------------------------------------------------------------------
    // Targeting preview (S6) — no RNG; for the UI + headless queries
    // -----------------------------------------------------------------------

    /// Probability [0,1] the player's attack hits the target (AttackResolver
    /// math, pseudo-AC). Returns 0 if the target is missing/dead.
    float hitChanceVs(const std::string& targetId) const;

    /// The target's pseudo-AC (8 + floor(HP%·6)); 0 if missing.
    int   targetAC(const std::string& targetId) const;

    /// Horizontal distance from the player to the target in world units (-1 if missing).
    float distanceTo(const std::string& targetId) const;

    /// True if the target is within melee reach right now.
    bool  inReachOf(const std::string& targetId) const;

    /// Soft target selection for the HUD hit-chance readout (set by hover/click).
    void setSelectedTarget(const std::string& id) { m_selectedTarget = id; }
    const std::string& selectedTarget() const { return m_selectedTarget; }

    // -----------------------------------------------------------------------
    // Tuning (player attack profile; refine from held weapon / sheet later)
    // -----------------------------------------------------------------------
    void setReachFeet(float f)         { if (f > 0.0f) m_reachFeet = f; }
    void setAttackBonus(int b)         { m_attackBonus = b; }
    void setDamageDice(const std::string& d) { m_damageDice = d; }
    void setDamageType(DamageType t)   { m_damageType = t; }

private:
    void beginPlayerTurn(Scene::Entity* playerEntity);
    void unbind();
    void resolvePlayerAttack(Scene::Entity* target, const std::string& targetId);
    Scene::Entity* lookup(const std::string& id) const;
    static int pseudoAC(Scene::Entity* e);

    CombatDirector* m_director = nullptr;
    EntityRegistry* m_registry = nullptr;
    CombatSystem*   m_combat   = nullptr;
    BodyProvider    m_bodyProvider;

    std::string m_playerId;
    std::string m_selectedTarget;
    bool        m_bound = false;

    TurnActor m_turnActor;
    DiceSystem m_dice;

    // Pending-attack tracking: resolve damage once when the swing completes.
    bool        m_resolvingAttack = false;
    std::string m_attackTargetId;

    float       m_reachFeet   = 5.0f;
    int         m_attackBonus = 5;
    std::string m_damageDice  = "1d6+3";
    DamageType  m_damageType  = DamageType::Physical;
};

} // namespace Core
} // namespace Phyxel
