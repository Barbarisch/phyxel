#include "scene/behaviors/CombatBehavior.h"
#include "scene/NPCEntity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "core/EntityRegistry.h"
#include "core/CombatSystem.h"
#include "core/HealthComponent.h"
#include "utils/Logger.h"

#include <cmath>
#include <cstdlib>

namespace Phyxel {
namespace Scene {

// Resolve the AnimatedVoxelCharacter behind any fighter entity — an NPCEntity
// wraps one; the player is registered as the character directly.
static AnimatedVoxelCharacter* charOf(Entity* e) {
    if (auto* n = dynamic_cast<NPCEntity*>(e)) return n->getAnimatedCharacter();
    return dynamic_cast<AnimatedVoxelCharacter*>(e);
}

const char* CombatBehavior::getStateName() const {
    switch (m_state) {
        case State::Seek:     return "Seek";
        case State::Approach: return "Approach";
        case State::Strafe:   return "Strafe";
        case State::Attack:   return "Attack";
        case State::Recover:  return "Recover";
    }
    return "?";
}

void CombatBehavior::ensureWired(NPCContext& ctx, AnimatedVoxelCharacter* character) {
    if (m_wired || !character) return;
    m_wired = true;

    // Give the enemy a basic melee moveset so lightAttack() animates. These are
    // real swing mocaps already in humanoid.anim (the player uses them too).
    AnimatedVoxelCharacter::MeleeMoveset ms;
    ms.lightChain     = {"melee_attack_horizontal", "melee_attack_down"};
    ms.heavy          = "melee_chop_2h";
    ms.attackRate     = 1.4f;   // snappier than the raw ~2.4s mocap swing
    ms.chainWindowFrac = 0.35f;
    character->setMoveset(std::move(ms));

    // NPC swings land damage at the clip's hit frame, exactly like the player
    // (Application::createAnimatedCharacter). attackerEntity is the registered
    // NPCEntity so CombatSystem excludes the attacker from its own cone; the
    // target's dodge i-frames (isDodgeInvulnerable) make the hit whiff.
    Core::CombatSystem*  combat = ctx.combatSystem;
    Core::EntityRegistry* reg   = ctx.entityRegistry;
    Scene::Entity*        self  = ctx.self;
    const std::string     selfId = ctx.selfId;
    const float dmg = m_attackDamage, reach = m_attackReach;
    if (combat && reg && self) {
        character->setOnHitFrame([combat, reg, character, self, selfId, dmg, reach]() {
            Core::CombatSystem::AttackParams p;
            p.attackerId     = selfId;
            p.attackerEntity = self;
            // Originate the hit at the swinging hand so it connects on visual contact.
            p.attackerPos    = character->getAttackOrigin();
            const float yaw  = character->getYaw();   // visual front is +Z at yaw 0
            p.attackerForward = glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
            p.damage = dmg;
            p.reach  = reach;
            p.coneAngleDeg = 150.0f;  // origin already at the hand
            auto events = combat->performAttack(p, *reg);
            for (const auto& ev : events) {
                LOG_INFO("CombatAI", "{} hit {} for {:.1f} dmg{}",
                         selfId, ev.targetId, ev.actualDamage, ev.killed ? " (killed)" : "");
            }
        });
    }
}

std::string CombatBehavior::acquireTarget(NPCContext& ctx, const glm::vec3& selfPos) const {
    if (!ctx.entityRegistry) return "";
    // Nearest LIVE opponent that isn't us — the player ("animated") OR another
    // combat NPC ("npc"), so two AI fighters will duel each other.
    std::string best;
    float bestD2 = m_aggroRange * m_aggroRange;
    auto consider = [&](const char* type) {
        for (const auto& [id, e] : ctx.entityRegistry->getEntitiesByType(type)) {
            if (!e || e == ctx.self) continue;          // skip self
            const auto* hp = e->getHealthComponent();
            if (!hp || !hp->isAlive()) continue;         // skip dead / healthless
            const glm::vec3 d = e->getPosition() - selfPos;
            const float d2 = d.x * d.x + d.z * d.z;
            if (d2 < bestD2) { bestD2 = d2; best = id; }
        }
    };
    consider("animated");
    consider("npc");
    return best;
}

void CombatBehavior::update(float dt, NPCContext& ctx) {
    if (!ctx.self) return;
    auto* npc = dynamic_cast<NPCEntity*>(ctx.self);
    AnimatedVoxelCharacter* character = npc ? npc->getAnimatedCharacter() : nullptr;
    ensureWired(ctx, character);

    if (m_cooldownTimer > 0.0f) m_cooldownTimer -= dt;

    const glm::vec3 selfPos = ctx.self->getPosition();

    // Acquire / validate the target (the player).
    if (m_targetId.empty() || !ctx.entityRegistry || !ctx.entityRegistry->getEntity(m_targetId))
        m_targetId = acquireTarget(ctx, selfPos);

    Scene::Entity* target = (ctx.entityRegistry && !m_targetId.empty())
                                ? ctx.entityRegistry->getEntity(m_targetId) : nullptr;

    // Helper: feed the character control inputs (drives the full FSM, unlike
    // setMoveVelocity which bypasses updateStateMachine). forward<0 = move
    // toward facing (W), forward>0 = backward (S), strafe = sidestep.
    auto drive = [&](float forward, float strafe) {
        if (character) character->setControlInput(forward, 0.0f, strafe);
        else           ctx.self->setMoveVelocity(glm::vec3(0.0f));
    };

    if (!target) {                       // --- Seek: nobody to fight ---
        m_state = State::Seek;
        drive(0.0f, 0.0f);
        return;
    }

    glm::vec3 toTarget = target->getPosition() - selfPos;
    toTarget.y = 0.0f;
    const float dist = glm::length(toTarget);
    const glm::vec3 dir = dist > 1e-4f ? toTarget / dist : glm::vec3(0.0f, 0.0f, 1.0f);

    // Lose the target once it leaves aggro range (+hysteresis).
    if (dist > m_aggroRange * 1.3f) {
        m_targetId.clear();
        m_state = State::Seek;
        drive(0.0f, 0.0f);
        return;
    }

    // Always face the target (model fronts +Z, so yaw = atan2(x, z)). This also
    // aims the hit-frame cone. During movement the input branch keeps this yaw
    // (only the external-velocity path would override it, which we don't use).
    if (character) character->setFacingYaw(std::atan2(dir.x, dir.z));

    // Already mid-roll? Let it finish — the dodge owns movement + i-frames.
    if (character && character->isDodging()) return;

    // --- Evade: roll clear when the target is swinging at us within reach ---
    // Gated by a dodge cooldown + reaction chance + one-per-swing so fights
    // actually resolve instead of becoming a perfect-dodge stalemate.
    if (m_dodgeCdTimer > 0.0f) m_dodgeCdTimer -= dt;
    AnimatedVoxelCharacter* targetChar = charOf(target);
    const bool targetSwinging = targetChar &&
        targetChar->getAnimationState() == AnimatedCharacterState::Attack;
    if (!targetSwinging) {
        m_evadedThisSwing = false;            // reset for the next swing
    } else if (character && dist < m_evadeRange && m_dodgeCdTimer <= 0.0f &&
               !m_evadedThisSwing &&
               (std::rand() / static_cast<float>(RAND_MAX)) < m_evadeChance) {
        glm::vec2 perp(-dir.z, dir.x);        // sidestep-roll around the attacker
        if (std::rand() % 2) perp = -perp;
        character->dodge(perp);
        m_dodgeCdTimer  = m_dodgeCooldown;
        m_evadedThisSwing = true;
        return;
    }

    const bool attacking = character &&
        character->getAnimationState() == AnimatedCharacterState::Attack;

    // Brief back-off after a swing (spacing — the souls "hit and reposition").
    if (m_recoverTimer > 0.0f && !attacking) {
        m_recoverTimer -= dt;
        m_state = State::Recover;
        drive(0.6f, 0.0f);   // forward>0 = step backward, away from the target
        return;
    }

    if (dist > m_attackRange) {           // --- Approach ---
        m_state = State::Approach;
        drive(-m_moveSpeed, 0.0f);        // run toward the target to close fast
        return;
    }

    // In range.
    if (!attacking && m_cooldownTimer <= 0.0f && character) {   // --- Attack ---
        m_state = State::Attack;
        character->lightAttack();
        m_cooldownTimer = m_attackCooldown;
        m_recoverTimer  = m_recoverTime;
        drive(0.0f, 0.0f);
    } else if (attacking) {               // mid-swing: hold ground (don't cancel)
        drive(0.0f, 0.0f);
    } else {                              // --- in range, waiting on cooldown ---
        // Hold position and face the target. (Continuous circle-strafing made
        // two AIs orbit-drift across the map and shifted the target out of reach
        // mid-swing — repositioning is a later polish item; a stable square-up
        // gives reliable trade-blows-and-dodge exchanges.)
        m_state = State::Strafe;
        drive(0.0f, 0.0f);
    }
}

} // namespace Scene
} // namespace Phyxel
