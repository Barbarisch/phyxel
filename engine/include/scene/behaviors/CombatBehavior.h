#pragma once

#include "scene/NPCBehavior.h"
#include "ai/PerceptionSystem.h"
#include <string>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Scene {

class AnimatedVoxelCharacter;

/// Real-time melee enemy behavior (souls-style). NOT the turn-based
/// Core::CombatAISystem — this drives the NPC's AnimatedVoxelCharacter every
/// frame via control inputs (like a player) so the full melee FSM (moveset
/// attacks) runs. Loop: acquire the nearest live "animated" target within
/// aggro range, approach, circle-strafe at reach while an attack cooldown
/// ticks, commit a telegraphed attack, then back off briefly before
/// re-engaging. NPC swings deal damage through Core::CombatSystem (wired via
/// the NPC's onHitFrame, mirroring the player's wiring), so the player's dodge
/// i-frames cleanly avoid them.
class CombatBehavior : public NPCBehavior {
public:
    void update(float dt, NPCContext& ctx) override;
    void onInteract(Entity* /*interactor*/) override {}
    void onEvent(const std::string& /*eventType*/, const nlohmann::json& /*data*/) override {}
    std::string getBehaviorName() const override { return "Combat"; }

    // --- Tuning (sensible defaults; data-light for v1) ---
    void setAggroRange(float r)    { m_aggroRange = r; }
    void setAttackRange(float r)   { m_attackRange = r; }
    void setMoveSpeed(float s)     { m_moveSpeed = s; }
    void setAttackCooldown(float s){ m_attackCooldown = s; }
    void setAttackDamage(float d)  { m_attackDamage = d; }
    void setAttackReach(float r)   { m_attackReach = r; }

    AI::PerceptionComponent& getPerception() { return m_perception; }
    const std::string& getTargetId() const { return m_targetId; }
    const char* getStateName() const;

private:
    enum class State { Seek, Approach, Strafe, Attack, Recover };

    void ensureWired(NPCContext& ctx, AnimatedVoxelCharacter* character);
    std::string acquireTarget(NPCContext& ctx, const glm::vec3& selfPos) const;

    AI::PerceptionComponent m_perception;   ///< FOV cone (debug viz / future LOS); targeting is distance-based for v1
    State       m_state          = State::Seek;
    std::string m_targetId;

    float m_aggroRange     = 16.0f;  ///< acquire targets within this range
    float m_attackRange    = 1.5f;   ///< stop & attack within this range (close, so hand-origin hits land despite dodge-drift)
    float m_moveSpeed      = 1.0f;   ///< control-input magnitude scale (1 = run toward target)
    float m_attackCooldown = 1.8f;   ///< seconds between committed attacks
    float m_recoverTime    = 0.0f;   ///< post-swing back-off (0 = none; backing off let the target escape and caused map-drift)
    float m_attackDamage   = 8.0f;
    float m_attackReach    = 2.2f;   ///< hit sphere radius from the swinging hand (getAttackOrigin); forgiving enough to land amid drift

    // Evasion: roll away when the current target is mid-swing within reach.
    float m_evadeRange     = 2.8f;   ///< react to an attacker swinging within this range
    float m_dodgeCooldown  = 1.1f;   ///< min seconds between evasive rolls
    float m_evadeChance    = 0.45f;  ///< chance to react to a given incoming swing (so fights resolve, not stalemate)

    float m_cooldownTimer  = 0.0f;
    float m_recoverTimer   = 0.0f;
    float m_dodgeCdTimer   = 0.0f;
    bool  m_evadedThisSwing = false; ///< one evade per detected swing
    float m_strafeSign     = 1.0f;
    float m_strafeRetarget = 0.0f;
    bool  m_wired          = false;  ///< moveset + damage callback installed once
    bool  m_hadTarget      = false;  ///< true once we've engaged — so losing the last enemy = victory
    float m_comboTimer     = 0.0f;   ///< while >0 mid-swing, keep buffering light inputs to chain the combo

public:
    /// Faction tag — combat NPCs only target entities whose faction differs
    /// (empty = hostile to everyone, the default). Lets two NPCs duel.
    void setFaction(const std::string& f) { m_faction = f; }
    /// Equip a weapon by item id; the moveset is resolved from it like the
    /// player's held weapon (empty = unarmed). Set before the first update.
    void setWeapon(const std::string& id) { m_weaponId = id; }
    /// The equipped weapon item id (empty = unarmed). Used by the host to draw
    /// the held weapon visual.
    const std::string& getWeaponId() const { return m_weaponId; }
private:
    std::string m_faction;
    std::string m_weaponId;
};

} // namespace Scene
} // namespace Phyxel
