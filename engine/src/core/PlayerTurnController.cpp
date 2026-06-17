#include "core/PlayerTurnController.h"
#include "core/CombatDirector.h"
#include "core/CombatSystem.h"
#include "core/EntityRegistry.h"
#include "core/HealthComponent.h"
#include "core/AttackResolver.h"
#include "core/SpellDefinition.h"
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

std::vector<std::string> PlayerTurnController::gatherAreaTargets(const glm::vec3& center,
                                                                float radiusFeet) const {
    std::vector<std::string> out;
    if (!m_director || !m_registry) return out;
    float r = m_turnActor.feetToUnits(radiusFeet);
    for (const auto& p : m_director->initiative().turnOrder()) {
        if (m_director->isPlayerSide(p.entityId)) continue;     // enemies only
        Scene::Entity* e = m_registry->getEntity(p.entityId);
        if (!ptcAlive(e)) continue;
        glm::vec3 d = e->getPosition() - center; d.y = 0.0f;
        if (std::sqrt(d.x * d.x + d.z * d.z) <= r) out.push_back(p.entityId);
    }
    return out;
}

std::vector<std::string> PlayerTurnController::aoeTargetsAt(const std::string& spellId,
                                                           const glm::vec3& center) const {
    const SpellDefinition* def = SpellRegistry::instance().getSpell(spellId);
    if (!def || !def->isAreaSpell()) return {};
    return gatherAreaTargets(center, def->areaSizeFeet);
}

bool PlayerTurnController::castSpell(const std::string& spellId, const std::string& targetId) {
    if (!m_bound || spellId.empty()) return false;

    const SpellDefinition* def = SpellRegistry::instance().getSpell(spellId);
    if (!def) { LOG_WARN("PlayerTurn", "Unknown spell '{}'", spellId); return false; }

    ActionBudget* b = nullptr;
    if (auto* p = m_director->initiative().find(m_playerId)) b = &p->budget;
    if (!b) return false;

    // Cost: bonus-action spells take the bonus action, everything else the action.
    bool useBonus = (def->castingTime == CastingTime::BonusAction);
    if (useBonus ? !b->canBonusAct() : !b->canAct()) return false;

    Scene::Entity* target = lookup(targetId);
    glm::vec3 targetPos = target ? target->getPosition() : glm::vec3(0.0f);
    m_selectedTarget = targetId;
    const DamageType dtype = def->damageType;

    DiceExpression dmgExpr = def->isCantrip() ? def->cantripDiceAt(m_casterLevel)
                                              : def->damageAt(def->level);

    // Outcome lists applied at the release frame: damage hits + a single heal.
    std::vector<std::pair<std::string, int>> dmgHits;
    int applyHeal = 0;

    auto resolveDamageVs = [&](Scene::Entity* t, int full) -> int {
        switch (def->resolutionType) {
            case SpellResolutionType::SavingThrow: {
                int save = m_dice.roll(DieType::D20, 0).total;   // no sheet -> flat d20
                bool saved = save >= m_spellSaveDC;
                return !saved ? full : (def->halfDamageOnSave ? full / 2 : 0);
            }
            case SpellResolutionType::AttackRoll: {
                auto r = AttackResolver::resolveAttack(m_spellAttackBonus, pseudoAC(t), dmgExpr,
                                                       dtype, DamageResistance::Normal, false, false, m_dice);
                return r.hit ? r.finalDamage : 0;
            }
            default: return full;   // AutoHit and anything else
        }
    };

    if (def->hasHeal() && target) {
        DiceExpression healExpr = def->healDiceAt(def->level);
        applyHeal = def->healBase + (healExpr.count > 0 ? m_dice.rollExpression(healExpr).total : 0);
        LOG_INFO("PlayerTurn", "Player casts '{}' on '{}' (heal {}).", spellId, targetId, applyHeal);
    } else if (def->isAreaSpell() && def->hasDamage()) {
        // Area spell: roll the base damage once, apply (full/half/0) per target
        // in the radius around the cast centre.
        int full = std::max(0, m_dice.rollExpression(dmgExpr).total);
        auto ids = gatherAreaTargets(targetPos, def->areaSizeFeet);
        for (const auto& id : ids) {
            Scene::Entity* t = lookup(id);
            int dmg = resolveDamageVs(t, full);
            if (dmg > 0) dmgHits.emplace_back(id, dmg);
        }
        LOG_INFO("PlayerTurn", "Player casts AoE '{}' ({}ft) at '{}': {} of {} in area hit (base {}).",
                 spellId, def->areaSizeFeet, targetId, dmgHits.size(), ids.size(), full);
    } else if (def->hasDamage() && target) {
        int full = std::max(0, m_dice.rollExpression(dmgExpr).total);
        int dmg = resolveDamageVs(target, full);
        if (dmg > 0) dmgHits.emplace_back(targetId, dmg);
        LOG_INFO("PlayerTurn", "Player casts '{}' at '{}' -> {} dmg.", spellId, targetId, dmg);
    } else {
        LOG_INFO("PlayerTurn", "Player casts '{}' (utility).", spellId);
    }

    // Spend the budget now (the cast is committed).
    if (useBonus) b->spendBonusAction(); else b->spendAction();

    // Apply the pre-rolled outcome at the release frame.
    auto onRelease = [this, dtype, dmgHits, applyHeal, healTarget = targetId]() {
        for (const auto& [id, dmg] : dmgHits) {
            Scene::Entity* t = lookup(id);
            if (!t) continue;
            if (m_combat) m_combat->applyDamage(t, id, static_cast<float>(dmg), m_playerId, dtype);
            else if (auto* hc = t->getHealthComponent()) hc->takeDamage(static_cast<float>(dmg));
        }
        if (applyHeal > 0) {
            if (Scene::Entity* t = lookup(healTarget))
                if (auto* hc = t->getHealthComponent()) hc->heal(static_cast<float>(applyHeal));
        }
    };

    if (m_castExecutor) m_castExecutor(spellId, targetId, targetPos, onRelease);
    else                onRelease();
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
