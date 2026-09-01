#include "scene/behaviors/RangedCasterBehavior.h"
#include "scene/NPCEntity.h"
#include "scene/AnimatedVoxelCharacter.h"
#include "core/EntityRegistry.h"
#include "core/HealthComponent.h"
#include "utils/Logger.h"

#include <cmath>

namespace Phyxel {
namespace Scene {

const char* RangedCasterBehavior::stateName() const {
    switch (m_state) {
        case State::Seek:    return "Seek";
        case State::Close:   return "Close";
        case State::Hold:    return "Hold";
        case State::Retreat: return "Retreat";
    }
    return "?";
}

std::string RangedCasterBehavior::acquireTarget(NPCContext& ctx,
                                                const glm::vec3& selfPos) const {
    if (!ctx.entityRegistry || !ctx.self) return "";
    std::string best;
    float bestD2 = m_aggroRange * m_aggroRange;
    auto consider = [&](const char* type) {
        for (const auto& [id, e] : ctx.entityRegistry->getEntitiesByType(type)) {
            if (!e || e == ctx.self) continue;
            const auto* hp = e->getHealthComponent();
            if (!hp || !hp->isAlive()) continue;
            if (!ctx.self->hostileTo(*e)) continue;      // never nuke an ally
            const glm::vec3 d = e->getPosition() - selfPos;
            const float d2 = d.x * d.x + d.z * d.z;
            if (d2 < bestD2) { bestD2 = d2; best = id; }
        }
    };
    consider("animated");
    consider("npc");
    return best;
}

void RangedCasterBehavior::update(float dt, NPCContext& ctx) {
    if (!ctx.self) return;
    auto* npc = dynamic_cast<NPCEntity*>(ctx.self);
    AnimatedVoxelCharacter* character = npc ? npc->getAnimatedCharacter() : nullptr;

    // Publish allegiance once so other combatants can read it (see CombatBehavior).
    if (!m_publishedFaction) {
        m_publishedFaction = true;
        if (!m_faction.empty()) ctx.self->setFaction(m_faction);
    }

    if (character && character->isIncapacitated()) return;
    if (m_cooldownTimer > 0.0f) m_cooldownTimer -= dt;

    const glm::vec3 selfPos = ctx.self->getPosition();

    // Drop a dead/missing target, then re-acquire.
    Scene::Entity* cur = (ctx.entityRegistry && !m_targetId.empty())
                             ? ctx.entityRegistry->getEntity(m_targetId) : nullptr;
    const bool curDead = cur && (!cur->getHealthComponent() ||
                                 !cur->getHealthComponent()->isAlive());
    if (!cur || curDead) m_targetId = acquireTarget(ctx, selfPos);

    Scene::Entity* target = (ctx.entityRegistry && !m_targetId.empty())
                                ? ctx.entityRegistry->getEntity(m_targetId) : nullptr;

    auto drive = [&](float forward, float strafe) {
        if (character) character->setControlInput(forward, 0.0f, strafe);
        else           ctx.self->setMoveVelocity(glm::vec3(0.0f));
    };

    if (!target) {
        m_state = State::Seek;
        drive(0.0f, 0.0f);
        return;
    }

    glm::vec3 toTarget = target->getPosition() - selfPos;
    toTarget.y = 0.0f;
    const float dist = glm::length(toTarget);
    const glm::vec3 dir = dist > 1e-4f ? toTarget / dist : glm::vec3(0.0f, 0.0f, 1.0f);

    if (dist > m_aggroRange * 1.3f) {          // lost it
        m_targetId.clear();
        m_state = State::Seek;
        drive(0.0f, 0.0f);
        return;
    }

    // Always face the target — a caster that fires sideways looks broken, and
    // the cast animation reads off the body's facing.
    if (character && dist > 1e-4f) character->setFacingYaw(std::atan2(dir.x, dir.z));

    // Range band. Inside 60% of preferred = too close, back away (a caster in
    // melee is a dead caster); beyond 120% = close in; otherwise hold and cast.
    const float tooClose = m_preferredRange * 0.6f;
    const float tooFar   = m_preferredRange * 1.2f;

    if (dist < tooClose) {
        m_state = State::Retreat;
        drive(m_moveSpeed, 0.0f);              // forward>0 = backpedal
    } else if (dist > tooFar) {
        m_state = State::Close;
        drive(-m_moveSpeed, 0.0f);             // forward<0 = advance
    } else {
        m_state = State::Hold;
        drive(0.0f, 0.0f);
    }

    // Cast whenever the cooldown is up and we are anywhere in the band — a
    // real-time caster keeps up sustained fire rather than nova-ing once.
    // WALLS STOP SPELLS. This behavior had no notion of the world — only a
    // distance check — so a mage sealed inside a fort shot attackers straight
    // through the stonework. A blocked caster holds fire and CLOSES instead of
    // standing there dry-firing at a wall, which is what a real one would do.
    if (m_chunks && !AI::TacticalSpace::canSee(*m_chunks, selfPos, target->getPosition())) {
        m_lastShotBlocked = true;
        m_state = State::Close;
        drive(-m_moveSpeed, 0.0f);     // move to clear the corner / regain sight
        return;
    }
    m_lastShotBlocked = false;

    if (m_cooldownTimer <= 0.0f && !m_spells.empty() && dist <= m_aggroRange) {
        const std::string& spellId = m_spells[m_nextSpell % m_spells.size()];
        ++m_nextSpell;
        m_cooldownTimer = m_castCooldown;

        if (m_castHook) {
            m_castHook(ctx.selfId, spellId, m_targetId, target->getPosition(), m_damage);
        } else if (auto* hc = target->getHealthComponent()) {
            // Hookless fallback: raw damage. This BYPASSES CombatSystem, so
            // there is no death event, no hit reaction, no VFX and no log —
            // a 20v20 run where the hook was unwired looked like combatants
            // dying of nothing. Hosts should always set the hook; the warning
            // fires once so the omission is loud rather than mysterious.
            static bool warned = false;
            if (!warned) {
                warned = true;
                LOG_WARN("CasterAI", "no cast hook set — spell damage bypasses "
                                     "CombatSystem (no death events / VFX / logs)");
            }
            hc->takeDamage(m_damage);
        }
        // LOG_INFO, not DEBUG: debug lines are invisible in Release, and this
        // is the observable that proves the real-time cast path ran at all.
        // Plain {} only — this logger prints printf specs literally.
        LOG_INFO("CasterAI", "{} casts {} at {} ({}u)",
                 ctx.selfId, spellId, m_targetId, static_cast<int>(dist));
    }
}

} // namespace Scene
} // namespace Phyxel
