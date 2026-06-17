#include "core/PlayerTurnController.h"
#include "core/CombatDirector.h"
#include "core/CombatSystem.h"
#include "core/EntityRegistry.h"
#include "core/HealthComponent.h"
#include "core/AttackResolver.h"
#include "scene/Entity.h"
#include "utils/Logger.h"

#include <cmath>

namespace Phyxel {
namespace Core {

static bool ptcAlive(Scene::Entity* e) {
    if (!e) return false;
    auto* hc = e->getHealthComponent();
    return !hc || hc->isAlive();
}

const ActionBudget* PlayerTurnController::budget() const {
    if (!m_bound || !m_director) return nullptr;
    if (auto* p = m_director->initiative().find(m_playerId)) return &p->budget;
    return nullptr;
}

Scene::Entity* PlayerTurnController::lookup(const std::string& id) const {
    return (m_registry && !id.empty()) ? m_registry->getEntity(id) : nullptr;
}

int PlayerTurnController::pseudoAC(Scene::Entity* e) {
    // Generic entities have no CharacterSheet — derive AC from HP% (mirrors the
    // enemy AI so both sides use the same yardstick until real sheets are wired).
    auto* hc = e ? e->getHealthComponent() : nullptr;
    if (!hc || hc->getMaxHealth() <= 0.0f) return 10;
    float frac = hc->getHealth() / hc->getMaxHealth();
    return 8 + static_cast<int>(frac * 6.0f);
}

int PlayerTurnController::targetAC(const std::string& targetId) const {
    Scene::Entity* t = lookup(targetId);
    return t ? pseudoAC(t) : 0;
}

float PlayerTurnController::hitChanceVs(const std::string& targetId) const {
    Scene::Entity* t = lookup(targetId);
    if (!ptcAlive(t)) return 0.0f;
    return AttackResolver::hitChance(m_attackBonus, pseudoAC(t));
}

float PlayerTurnController::distanceTo(const std::string& targetId) const {
    Scene::Entity* t = lookup(targetId);
    if (!t) return -1.0f;
    Scene::Entity* p = lookup(m_playerId);
    if (!p) return -1.0f;
    glm::vec3 a = p->getPosition(), b = t->getPosition();
    float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

bool PlayerTurnController::inReachOf(const std::string& targetId) const {
    Scene::Entity* t = lookup(targetId);
    if (!t) return false;
    return m_turnActor.inReach(t->getPosition(), m_reachFeet);
}

void PlayerTurnController::tick(float dt) {
    // Active only in turn-based combat.
    if (!m_director || !m_director->inCombat() || !m_director->isTurnBased()) {
        if (m_bound) unbind();
        return;
    }

    const std::string cur = m_director->currentEntityId();

    // Bind on the player's turn; unbind when it is someone else's turn.
    if (cur == m_playerId && !cur.empty()) {
        if (!m_bound) {
            Scene::Entity* e = m_registry ? m_registry->getEntity(m_playerId) : nullptr;
            beginPlayerTurn(e);
        }
    } else {
        if (m_bound) unbind();
        return;
    }

    if (!m_bound) return;

    m_turnActor.tick(dt);

    // Resolve a queued attack once the swing animation completes.
    if (m_resolvingAttack && !m_turnActor.isBusy()) {
        Scene::Entity* t = m_registry ? m_registry->getEntity(m_attackTargetId) : nullptr;
        resolvePlayerAttack(t, m_attackTargetId);
        m_resolvingAttack = false;
        m_attackTargetId.clear();
    }
}

void PlayerTurnController::beginPlayerTurn(Scene::Entity* playerEntity) {
    ITurnActorBody* body = (m_bodyProvider && playerEntity) ? m_bodyProvider(playerEntity) : nullptr;
    ActionBudget* bud = nullptr;
    if (auto* p = m_director->initiative().find(m_playerId)) bud = &p->budget;

    if (body && bud) {
        m_turnActor.begin(body, bud);
        m_bound = true;
        m_resolvingAttack = false;
        m_attackTargetId.clear();
        LOG_DEBUG("PlayerTurn", "Player '{}' turn begun.", m_playerId);
    } else {
        m_bound = false;  // can't drive without a live body + budget
    }
}

void PlayerTurnController::unbind() {
    m_turnActor.end();
    m_bound = false;
    m_resolvingAttack = false;
    m_attackTargetId.clear();
}

bool PlayerTurnController::requestMove(const glm::vec3& worldPoint) {
    if (!m_bound) return false;
    return m_turnActor.requestMove(worldPoint);
}

bool PlayerTurnController::requestAttack(const std::string& targetId) {
    if (!m_bound || targetId.empty()) return false;
    Scene::Entity* t = m_registry ? m_registry->getEntity(targetId) : nullptr;
    if (!ptcAlive(t)) return false;

    if (!m_turnActor.requestAttack(t->getPosition(), m_reachFeet)) return false;

    // Damage resolves when the swing completes (see tick()).
    m_resolvingAttack = true;
    m_attackTargetId  = targetId;
    return true;
}

void PlayerTurnController::endTurn() {
    if (!m_bound) return;
    // Resolve a still-pending attack before yielding the turn.
    if (m_resolvingAttack) {
        Scene::Entity* t = m_registry ? m_registry->getEntity(m_attackTargetId) : nullptr;
        resolvePlayerAttack(t, m_attackTargetId);
        m_resolvingAttack = false;
        m_attackTargetId.clear();
    }
    unbind();
    if (m_director) m_director->advanceTurn();
}

void PlayerTurnController::resolvePlayerAttack(Scene::Entity* target, const std::string& targetId) {
    if (!ptcAlive(target)) return;

    int ac = pseudoAC(target);

    auto dice   = DiceExpression::parse(m_damageDice);
    auto result = AttackResolver::resolveAttack(
        m_attackBonus, ac, dice, m_damageType,
        DamageResistance::Normal, false, false, m_dice);

    if (!result.hit) {
        LOG_INFO("PlayerTurn", "Player misses '{}' (roll {} vs AC {}).",
                 targetId, result.attackTotal, ac);
        return;
    }

    if (m_combat) {
        m_combat->applyDamage(target, targetId, static_cast<float>(result.finalDamage),
                              m_playerId, m_damageType);
    } else if (auto* hc = target->getHealthComponent()) {
        hc->takeDamage(static_cast<float>(result.finalDamage));
    }
    LOG_INFO("PlayerTurn", "Player hits '{}' for {} ({}) damage (roll {} vs AC {}).",
             targetId, result.finalDamage, m_damageDice, result.attackTotal, ac);
}

} // namespace Core
} // namespace Phyxel
