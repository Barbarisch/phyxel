#include "core/CombatAISystem.h"
#include "core/EntityRegistry.h"
#include "core/HealthComponent.h"
#include "core/MonsterDefinition.h"
#include "core/DamageTypes.h"
#include "scene/Entity.h"
#include "utils/Logger.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <limits>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool isAlive(Scene::Entity* e) {
    if (!e) return false;
    auto* hc = e->getHealthComponent();
    return !hc || hc->isAlive();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CombatAISystem::tick(float dt) {
    if (!m_tracker || !m_tracker->isCombatActive()) {
        m_thinkAccum   = 0.0f;
        m_timedEntityId.clear();
        return;
    }

    if (!isEnemyTurn()) {
        // Reset timer if the current turn is no longer an enemy's.
        m_thinkAccum   = 0.0f;
        m_timedEntityId.clear();
        return;
    }

    const std::string& currentId = m_tracker->currentEntityId();

    // If we switched to a new enemy entity, restart the think timer.
    if (currentId != m_timedEntityId) {
        m_timedEntityId = currentId;
        m_thinkAccum    = 0.0f;
    }

    m_thinkAccum += dt;
    if (m_thinkAccum < m_thinkDelay) return;

    executeEnemyAction();
    m_thinkAccum   = 0.0f;
    m_timedEntityId.clear();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool CombatAISystem::isEnemyTurn() const {
    if (!m_tracker->isCombatActive()) return false;
    const std::string& id = m_tracker->currentEntityId();
    if (id.empty()) return false;
    // It is a player turn if the party owns the entity.
    if (m_party && m_party->hasMember(id)) return false;
    return true;
}

void CombatAISystem::executeEnemyAction() {
    if (!m_registry) {
        m_tracker->endTurn();
        return;
    }

    const std::string& enemyId = m_tracker->currentEntityId();
    Scene::Entity* enemyEntity = m_registry->getEntity(enemyId);

    // Check if the enemy is still alive (dead entities stay in tracker until removed).
    if (!isAlive(enemyEntity)) {
        m_tracker->endTurn();
        return;
    }

    glm::vec3 enemyPos = enemyEntity ? enemyEntity->getPosition() : glm::vec3(0.0f);

    // -----------------------------------------------------------------------
    // 1. Find nearest alive hostile target
    // -----------------------------------------------------------------------
    Scene::Entity* target      = nullptr;
    std::string    targetId;
    float          bestDistSq  = std::numeric_limits<float>::max();

    // Prefer party members; fall back to any registered entity that is not
    // an NPC (type tag "player" or "physics").
    auto tryTarget = [&](const std::string& id, Scene::Entity* e) {
        if (id == enemyId) return;
        if (!isAlive(e)) return;
        glm::vec3 d = e->getPosition() - enemyPos;
        float dsq = glm::dot(d, d);
        if (dsq < bestDistSq) {
            bestDistSq = dsq;
            target     = e;
            targetId   = id;
        }
    };

    if (m_party) {
        for (const auto& member : m_party->getMembers()) {
            if (!member.isAlive) continue;
            auto* e = m_registry->getEntity(member.entityId);
            tryTarget(member.entityId, e);
        }
    }

    // If party gave no target, search all non-NPC entities within 50 m.
    if (!target) {
        auto nearby = m_registry->getEntitiesNear(enemyPos, 50.0f);
        for (auto& [id, e] : nearby) {
            if (id == enemyId) continue;
            // Exclude other NPC combatants on the same side (simple heuristic:
            // if the entity is also in the turn order, skip it).
            if (m_tracker->find(id)) continue;
            tryTarget(id, e);
        }
    }

    if (!target) {
        // No target found — pass turn.
        LOG_DEBUG("CombatAI", "NPC '{}' found no target; ending turn.", enemyId);
        m_tracker->endTurn();
        return;
    }

    // -----------------------------------------------------------------------
    // 2. Move toward target or attack
    // -----------------------------------------------------------------------
    float dist = std::sqrt(bestDistSq);

    if (dist > m_meleeReach && enemyEntity) {
        // Move toward the target at m_moveSpeed for one "action tick".
        glm::vec3 dir = (target->getPosition() - enemyPos);
        if (glm::length(dir) > 0.001f) dir = glm::normalize(dir);
        enemyEntity->setMoveVelocity(dir * m_moveSpeed);
        // Update enemy position estimate for attack range check.
        dist -= m_moveSpeed * m_thinkDelay;
    }

    // -----------------------------------------------------------------------
    // 3. Attack if now in range
    // -----------------------------------------------------------------------
    if (dist <= m_meleeReach + 1.0f) {  // +1 tolerance for movement overshoot
        // Look up monster definition for proper attack stats.
        const MonsterDefinition* def = MonsterRegistry::instance().getMonster(enemyId);

        int         attackBonus = 3;        // fallback
        std::string damageDiceStr = "1d4";  // fallback
        DamageType  damageType  = DamageType::Physical;

        if (def && !def->attacks.empty()) {
            const auto& atk = def->attacks[0];
            attackBonus   = atk.toHitBonus;
            damageDiceStr = atk.damageDice;
            // Map string damage type to enum (best-effort).
            const std::string& dtStr = atk.damageType;
            if      (dtStr == "piercing")     damageType = DamageType::Physical;
            else if (dtStr == "slashing")     damageType = DamageType::Physical;
            else if (dtStr == "bludgeoning")  damageType = DamageType::Physical;
            else if (dtStr == "fire")         damageType = DamageType::Fire;
            else if (dtStr == "cold")         damageType = DamageType::Ice;
            else if (dtStr == "lightning")    damageType = DamageType::Lightning;
            else if (dtStr == "poison")       damageType = DamageType::Poison;
            else if (dtStr == "necrotic")     damageType = DamageType::Necrotic;
            else if (dtStr == "radiant")      damageType = DamageType::Radiant;
        }

        int targetAC = 10;
        auto* targetHC = target->getHealthComponent();
        if (targetHC) {
            // No CharacterSheet on generic entities; use HealthComponent-based
            // pseudo-AC: 8 + clamp(HP/maxHP * 4, 0, 6).
            if (targetHC->getMaxHealth() > 0.0f) {
                float frac = targetHC->getHealth() / targetHC->getMaxHealth();
                targetAC = 8 + static_cast<int>(frac * 6.0f);
            }
        }

        auto damageDice = DiceExpression::parse(damageDiceStr);
        auto result     = AttackResolver::resolveAttack(
            attackBonus, targetAC, damageDice, damageType,
            DamageResistance::Normal, false, false, m_dice);

        if (result.hit && targetHC) {
            targetHC->takeDamage(static_cast<float>(result.finalDamage));
            LOG_INFO("CombatAI",
                "NPC '{}' hits '{}' for {} {} damage (roll {}, AC {}).",
                enemyId, targetId, result.finalDamage,
                damageDiceStr, result.attackTotal, targetAC);
        } else {
            LOG_DEBUG("CombatAI",
                "NPC '{}' misses '{}' (roll {}, AC {}).",
                enemyId, targetId, result.attackTotal, targetAC);
        }

        // Stop movement after attacking.
        if (enemyEntity) enemyEntity->setMoveVelocity(glm::vec3(0.0f));
    }

    // -----------------------------------------------------------------------
    // 4. End the NPC's turn
    // -----------------------------------------------------------------------
    m_tracker->endTurn();
    LOG_DEBUG("CombatAI", "NPC '{}' ends turn.", enemyId);
}

} // namespace Phyxel::Core
