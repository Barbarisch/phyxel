#pragma once

#include "scene/NPCBehavior.h"
#include "ai/PerceptionSystem.h"
#include "ai/CommandStructure.h"
#include "ai/TacticalSpace.h"
#include "core/PathService.h"
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

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
    /// (empty = hostile to everyone, the default). Lets two NPCs duel, and
    /// lets N-vs-N battles hold their lines. Mirrored onto the OWNING ENTITY
    /// on the first update, because target selection has to read the
    /// allegiance of OTHER combatants, not just its own.
    void setFaction(const std::string& f) { m_faction = f; }
    const std::string& faction() const { return m_faction; }
    /// Equip a weapon by item id; the moveset is resolved from it like the
    /// player's held weapon (empty = unarmed). Set before the first update.
    void setWeapon(const std::string& id) { m_weaponId = id; }

    // ── Tactical layer ──────────────────────────────────────────
    // INTELLIGENCE (D&D-ish 3..18) is the single dial that separates a mob
    // from soldiers. It is not a damage stat — it changes HOW the NPC thinks:
    //   * reaction delay   — dull troops dither before re-deciding
    //   * cover discipline — the chance it breaks line of sight when hurt
    //   * obedience        — whether it follows its squad's order or just
    //                        charges the nearest enemy like everyone used to
    //   * target choice    — bright fighters finish the wounded
    // Every effect is observable in play, which is the point: you should be
    // able to SEE which side is better led.
    void setIntelligence(int score) { m_intelligence = score; }
    int  intelligence() const { return m_intelligence; }

    /// Squad orders. Optional — unsquadded NPCs behave exactly as before.
    void setCommandStructure(AI::CommandStructure* cs) { m_command = cs; }

    /// Voxel world used for line-of-sight and cover search (null = no cover).
    void setChunkManager(ChunkManager* cm) { m_chunks = cm; }

    /// Give this fighter the shared async pathfinder. Without it, combat
    /// movement is a straight line at the target and ANY wall defeats it — the
    /// horde in the Redoubt siege walked into the outside of a fort and was
    /// shot to pieces without ever finding the open gate. Non-owning; the
    /// service is owned by NPCManager and outlives the behavior.
    void setPathService(Core::PathService* ps) { m_pathService = ps; }

    /// Current tactical intent, for debug overlays and the decision log.
    const char* intentName() const { return m_intent; }
    /// The equipped weapon item id (empty = unarmed). Used by the host to draw
    /// the held weapon visual.
    const std::string& getWeaponId() const { return m_weaponId; }
private:
    std::string m_faction;
    std::string m_weaponId;

    // ── Tactical state ──────────────────────────────────────────
    int   m_intelligence = 10;          ///< 3..18; 10 = unremarkable
    AI::CommandStructure* m_command = nullptr;   // not owned
    ChunkManager*         m_chunks  = nullptr;   // not owned
    Core::PathService*    m_pathService = nullptr;  // not owned
    const char* m_intent = "engage";    ///< debug label for the current intent

    // ── Pathing around obstacles ────────────────────────────────
    // Combat movement is a straight line whenever the straight line WORKS —
    // that is the common case in an open field and costs nothing. A* is only
    // requested when directRouteWalkable() says the way is blocked, so 200
    // fighters in the open generate zero path queries.
    std::vector<glm::vec3> m_pathWaypoints;
    size_t m_pathIdx        = 0;
    uint64_t m_pathHandle   = 0;        ///< outstanding async request (0 = none)
    float m_repathTimer     = 0.0f;     ///< cooldown between path requests
    glm::vec3 m_pathGoal{0.0f};         ///< goal the current path was built for
    bool  m_routeBlocked    = false;    ///< last walkability verdict (telemetry)

    bool      m_takingCover = false;
    glm::vec3 m_coverPos{0.0f};
    float     m_coverCooldown = 0.0f;   ///< don't re-search every frame
    float     m_thinkTimer    = 0.0f;   ///< intelligence-scaled reaction delay
    glm::vec3 m_holdAnchor{0.0f};       ///< ground given up on a Hold order
    bool      m_holding = false;        ///< anchor is valid

    // Cumulative tallies. An instantaneous census of intents badly undercounts
    // cover, because moving to cover is a TRANSIT state that ends the moment
    // the fighter arrives — a poll every 10 s catches almost none of them.
    // These count the DECISIONS, which is what "does the tactical layer fire?"
    // actually asks.
    int m_coverTaken   = 0;   ///< times this NPC broke off to take cover
    int m_coverDenied  = 0;   ///< times it wanted cover and the ground offered none
    int m_ordersObeyed = 0;   ///< times it acted on a squad order
    int m_ordersIgnored = 0;  ///< times it free-lanced instead

public:
    int coverTaken()    const { return m_coverTaken; }
    int coverDenied()   const { return m_coverDenied; }
    int ordersObeyed()  const { return m_ordersObeyed; }
    int ordersIgnored() const { return m_ordersIgnored; }

private:

    /// Derived from intelligence: seconds between tactical re-evaluations.
    float reactionDelay() const {
        const float t = glm::clamp((m_intelligence - 3) / 15.0f, 0.0f, 1.0f);
        return 1.4f - t * 1.25f;        // INT 3 -> 1.4s, INT 18 -> 0.15s
    }
    /// Probability this NPC actually uses cover when it should.
    float coverDiscipline() const {
        return glm::clamp((m_intelligence - 6) / 12.0f, 0.0f, 0.95f);
    }
    /// Probability it obeys its squad's order rather than free-lancing.
    float obedience() const {
        return glm::clamp((m_intelligence - 4) / 14.0f, 0.0f, 1.0f);
    }
};

} // namespace Scene
} // namespace Phyxel
