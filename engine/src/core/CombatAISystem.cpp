#include "core/CombatAISystem.h"
#include "core/CombatDirector.h"
#include "core/CombatSystem.h"
#include "core/EntityRegistry.h"
#include "core/HealthComponent.h"
#include "core/MonsterDefinition.h"
#include "core/DamageTypes.h"
#include "core/SpellDefinition.h"
#include "core/SpellcasterComponent.h"
#include "scene/Entity.h"
#include "utils/Logger.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <limits>
#include <cmath>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool isAlive(Scene::Entity* e) {
    if (!e) return false;
    auto* hc = e->getHealthComponent();
    return !hc || hc->isAlive();
}

static float horizDist(const glm::vec3& a, const glm::vec3& b) {
    float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

InitiativeTracker* CombatAISystem::tracker() const {
    if (m_director) return &m_director->initiative();
    return m_tracker;
}

// ---------------------------------------------------------------------------
// Per-frame update
// ---------------------------------------------------------------------------

void CombatAISystem::tick(float dt) {
    // Only runs in turn-based mode while an encounter is active.
    if (!m_director || !m_director->inCombat() || !m_director->isTurnBased()) {
        if (!m_actingId.empty()) { m_turnActor.end(); m_actingId.clear(); m_phase = Phase::Idle; }
        return;
    }

    InitiativeTracker* tr = tracker();
    if (!tr || !tr->isCombatActive()) {
        if (!m_actingId.empty()) { m_turnActor.end(); m_actingId.clear(); m_phase = Phase::Idle; }
        return;
    }

    const std::string curId = tr->currentEntityId();
    if (curId.empty()) return;

    // The HUMAN's turn: the AI waits (player input drives it — S5). When the
    // host told us which entity the human controls, only THAT turn waits and
    // player-side COMPANIONS auto-fight; legacy hosts (no player id set) keep
    // the old behavior of waiting on every player-side turn.
    const bool humansTurn = m_playerEntityId.empty()
                              ? m_director->isPlayerSide(curId)
                              : (curId == m_playerEntityId);
    if (humansTurn) {
        if (!m_actingId.empty()) { m_turnActor.end(); m_actingId.clear(); m_phase = Phase::Idle; }
        return;
    }

    // New enemy turn → set it up.
    if (curId != m_actingId) {
        Scene::Entity* e = m_registry ? m_registry->getEntity(curId) : nullptr;
        beginEnemyTurn(curId, e);
    }

    Scene::Entity* enemy = m_registry ? m_registry->getEntity(m_actingId) : nullptr;
    if (!isAlive(enemy)) { finishTurn(); return; }

    switch (m_phase) {
        case Phase::Thinking:
            m_thinkAccum += dt;
            if (m_thinkAccum >= m_thinkDelay) decideNextAction();
            break;

        case Phase::Moving:
            m_turnActor.tick(dt);
            if (!m_turnActor.isBusy()) decideNextAction();
            break;

        case Phase::Attacking:
            m_turnActor.tick(dt);
            if (!m_turnActor.isBusy()) {
                if (!m_attacked) { resolveEnemyAttack(enemy); m_attacked = true; }
                m_phase = Phase::Done;   // one attack per turn (v1)
            }
            break;

        case Phase::Done:
            finishTurn();
            break;

        case Phase::Idle:
            break;
    }
}

// ---------------------------------------------------------------------------
// Turn setup / teardown
// ---------------------------------------------------------------------------

void CombatAISystem::beginEnemyTurn(const std::string& enemyId, Scene::Entity* enemyEntity) {
    m_actingId   = enemyId;
    m_thinkAccum = 0.0f;
    m_attacked   = false;
    m_targetId.clear();

    glm::vec3 pos = enemyEntity ? enemyEntity->getPosition() : glm::vec3(0.0f);
    m_targetId = acquireTarget(pos, enemyId);

    // Bind the TurnActor to the live body + this turn's budget. Without a body
    // adapter (headless / no provider) we fall back to an instant resolution.
    ITurnActorBody* body = (m_bodyProvider && enemyEntity) ? m_bodyProvider(enemyEntity) : nullptr;
    ActionBudget*   budget = nullptr;
    if (auto* p = tracker()->find(enemyId)) budget = &p->budget;

    if (body && budget) {
        m_turnActor.begin(body, budget);
        m_phase = Phase::Thinking;
    } else {
        // Instant fallback: resolve a single attack if a target is in reach,
        // then end the turn next frame.
        if (enemyEntity && !m_targetId.empty()) {
            Scene::Entity* t = m_registry ? m_registry->getEntity(m_targetId) : nullptr;
            if (t && horizDist(pos, t->getPosition()) <= kDefaultWorldUnitsPerFoot * m_reachFeet + 1.0f) {
                resolveEnemyAttack(enemyEntity);
            }
        }
        m_phase = Phase::Done;
    }
}

void CombatAISystem::finishTurn() {
    m_turnActor.end();
    LOG_DEBUG("CombatAI", "NPC '{}' ends turn.", m_actingId);
    m_actingId.clear();
    m_phase = Phase::Idle;
    if (m_director) m_director->advanceTurn();
    else if (m_tracker) m_tracker->endTurn();
}

// ---------------------------------------------------------------------------
// Decisions
// ---------------------------------------------------------------------------

std::string CombatAISystem::acquireTarget(const glm::vec3& fromPos, const std::string& selfId) const {
    if (!m_registry) return "";

    std::string best;
    float bestDistSq = std::numeric_limits<float>::max();

    auto consider = [&](const std::string& id) {
        if (id == selfId) return;
        Scene::Entity* e = m_registry->getEntity(id);
        if (!isAlive(e)) return;
        glm::vec3 d = e->getPosition() - fromPos;
        float dsq = glm::dot(d, d);
        if (dsq < bestDistSq) { bestDistSq = dsq; best = id; }
    };

    // Target the OPPOSING side of whoever is acting: an enemy hunts
    // player-side combatants; an allied companion hunts enemies.
    if (m_director) {
        const bool selfPlayerSide = m_director->isPlayerSide(selfId);
        for (const auto& p : tracker()->turnOrder())
            if (m_director->isPlayerSide(p.entityId) != selfPlayerSide) consider(p.entityId);
    } else if (m_party) {
        for (const auto& m : m_party->getMembers())
            if (m.isAlive) consider(m.entityId);
    }

    return best;
}

void CombatAISystem::decideNextAction() {
    Scene::Entity* enemy = m_registry ? m_registry->getEntity(m_actingId) : nullptr;
    if (!enemy) { m_phase = Phase::Done; return; }

    // Validate / re-acquire the target (it may have died or moved).
    Scene::Entity* target = (m_registry && !m_targetId.empty())
                                ? m_registry->getEntity(m_targetId) : nullptr;
    if (!isAlive(target)) {
        m_targetId = acquireTarget(enemy->getPosition(), m_actingId);
        target = (m_registry && !m_targetId.empty()) ? m_registry->getEntity(m_targetId) : nullptr;
    }
    if (!target) { m_phase = Phase::Done; return; }

    const glm::vec3 targetPos = target->getPosition();

    // A CASTER casts — that is what makes it read as a caster. This sits BEFORE
    // the melee attempt on purpose: when an ally closed on the wizard, an
    // attack-first order had it drop fire_bolt (1d10 at range) to club with a
    // 1d4 fist, which looked like the cast branch was broken. Melee is the
    // FALLBACK, for when it has nothing castable left.
    if (m_turnActor.canAct() && m_casterProvider) {
        if (SpellcasterComponent* sc = m_casterProvider(m_actingId)) {
            const std::string spellId = chooseSpell(*sc);
            if (!spellId.empty()) {
                resolveEnemyCast(enemy, *sc, spellId);
                m_phase = Phase::Done;   // one action per turn (v1, like attacks)
                return;
            }
        }
    }

    // Try to attack (TurnActor rejects if out of reach / no action).
    if (m_turnActor.canAct() && m_turnActor.requestAttack(targetPos, m_reachFeet)) {
        m_phase = Phase::Attacking;
        m_attacked = false;
        return;
    }

    // Otherwise close the distance, stopping just inside reach.
    if (m_turnActor.canMove()) {
        glm::vec3 enemyPos = enemy->getPosition();
        glm::vec3 dir = enemyPos - targetPos; dir.y = 0.0f;
        float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        glm::vec3 dest = targetPos;
        if (len > 1e-4f) {
            dir /= len;
            dest = targetPos + dir * (m_turnActor.feetToUnits(m_reachFeet) * 0.8f);
        }
        if (m_turnActor.requestMove(dest)) { m_phase = Phase::Moving; return; }
    }

    m_phase = Phase::Done;
}

// ---------------------------------------------------------------------------
// Attack resolution (D&D d20 vs AC; damage through the unified funnel)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Spell selection + resolution (NPC casters — enemies and companions alike)
// ---------------------------------------------------------------------------

std::string CombatAISystem::chooseSpell(SpellcasterComponent& caster) const {
    // Highest-level castable DAMAGING prepared spell wins (spend the big one
    // while it lasts); fall back to a damaging cantrip, which never runs dry.
    // Heals/utility are deliberately out of scope for v1 — a healer AI needs
    // ally-HP scoring, which is its own increment.
    const std::string* best = nullptr;
    int bestLevel = 0;
    for (const auto& ks : caster.knownSpells()) {
        const SpellDefinition* def = SpellRegistry::instance().getSpell(ks.spellId);
        if (!def || !def->hasDamage()) continue;
        if (!caster.canCast(ks.spellId, def->level)) continue;   // prepared + slot
        if (!best || def->level > bestLevel) { best = &ks.spellId; bestLevel = def->level; }
    }
    if (best) return *best;

    for (const auto& id : caster.cantrips()) {
        const SpellDefinition* def = SpellRegistry::instance().getSpell(id);
        if (def && def->hasDamage()) return id;
    }
    return {};
}

void CombatAISystem::resolveEnemyCast(Scene::Entity* enemyEntity,
                                      SpellcasterComponent& caster,
                                      const std::string& spellId) {
    Scene::Entity* target = (m_registry && !m_targetId.empty())
                                ? m_registry->getEntity(m_targetId) : nullptr;
    const SpellDefinition* def = SpellRegistry::instance().getSpell(spellId);
    if (!target || !isAlive(target) || !def) return;

    // NPCs have no CharacterSheet here, so use the monster-tier defaults the
    // rest of this system already assumes (prof +2, +3 casting mod ~ a CR 1-4
    // caster). A sheet-bearing NPC can be upgraded to derived stats later,
    // exactly as the player was.
    const int profBonus = 2, castMod = 3;
    const int saveDC    = 8 + profBonus + castMod;
    const int spellAtk  = profBonus + castMod;

    const int slotLevel = def->isCantrip() ? 0 : def->level;
    if (!caster.canCast(spellId, slotLevel)) return;

    DiceExpression dmgExpr = def->isCantrip()
                                 ? def->cantripDiceAt(caster.characterLevel())
                                 : def->damageAt(def->level);
    const int full = std::max(0, m_dice.rollExpression(dmgExpr).total);

    int targetAC = 10;
    auto* targetHC = target->getHealthComponent();
    if (targetHC && targetHC->getMaxHealth() > 0.0f) {
        float frac = targetHC->getHealth() / targetHC->getMaxHealth();
        targetAC = 8 + static_cast<int>(frac * 6.0f);
    }

    int dmg = full;
    std::string how = "auto-hits";
    switch (def->resolutionType) {
        case SpellResolutionType::SavingThrow: {
            const int save = m_dice.roll(DieType::D20, 0).total;
            const bool saved = save >= saveDC;
            dmg = !saved ? full : (def->halfDamageOnSave ? full / 2 : 0);
            how = "save " + std::to_string(save) + " vs DC " + std::to_string(saveDC) +
                  (saved ? " (saved)" : " (failed)");
            break;
        }
        case SpellResolutionType::AttackRoll: {
            auto r = AttackResolver::resolveAttack(spellAtk, targetAC, dmgExpr,
                                                   def->damageType, DamageResistance::Normal,
                                                   false, false, m_dice);
            dmg = r.hit ? r.finalDamage : 0;
            how = "roll " + std::to_string(r.attackTotal) + " vs AC " + std::to_string(targetAC) +
                  (r.hit ? " (hit)" : " (miss)");
            break;
        }
        default: break;
    }

    // Spend the slot at commit time — mirrors the player's cast path, so an
    // NPC caster genuinely runs dry instead of nuking every round.
    if (slotLevel > 0) caster.spendSlot(slotLevel);

    const std::string casterId = m_actingId, targetId = m_targetId;
    const DamageType dtype = def->damageType;
    auto onRelease = [this, casterId, targetId, dmg, dtype]() {
        Scene::Entity* t = (m_registry && !targetId.empty())
                               ? m_registry->getEntity(targetId) : nullptr;
        if (!t || dmg <= 0) return;
        if (m_combat) m_combat->applyDamage(t, targetId, static_cast<float>(dmg), casterId, dtype);
        else if (auto* hc = t->getHealthComponent()) hc->takeDamage(static_cast<float>(dmg));
    };

    LOG_INFO("CombatAI", "NPC '{}' casts '{}' at '{}' for {} ({})",
             m_actingId, spellId, m_targetId, dmg, how);

    if (m_castExecutor) m_castExecutor(casterId, spellId, target->getPosition(), onRelease);
    else                onRelease();
}

void CombatAISystem::resolveEnemyAttack(Scene::Entity* enemyEntity) {
    Scene::Entity* target = (m_registry && !m_targetId.empty())
                                ? m_registry->getEntity(m_targetId) : nullptr;
    if (!target || !isAlive(target)) return;

    int         attackBonus   = 3;        // fallbacks
    std::string damageDiceStr = "1d4";
    DamageType  damageType    = DamageType::Physical;

    if (const MonsterDefinition* def = MonsterRegistry::instance().getMonster(m_actingId)) {
        if (!def->attacks.empty()) {
            const auto& atk = def->attacks[0];
            attackBonus   = atk.toHitBonus;
            damageDiceStr = atk.damageDice;
            const std::string& dt = atk.damageType;
            if      (dt == "fire")      damageType = DamageType::Fire;
            else if (dt == "cold")      damageType = DamageType::Ice;
            else if (dt == "lightning") damageType = DamageType::Lightning;
            else if (dt == "poison")    damageType = DamageType::Poison;
            else if (dt == "necrotic")  damageType = DamageType::Necrotic;
            else if (dt == "radiant")   damageType = DamageType::Radiant;
            else                        damageType = DamageType::Physical;
        }
    }

    // Generic entities have no CharacterSheet — derive a pseudo-AC from HP%.
    int targetAC = 10;
    auto* targetHC = target->getHealthComponent();
    if (targetHC && targetHC->getMaxHealth() > 0.0f) {
        float frac = targetHC->getHealth() / targetHC->getMaxHealth();
        targetAC = 8 + static_cast<int>(frac * 6.0f);
    }

    auto damageDice = DiceExpression::parse(damageDiceStr);
    auto result = AttackResolver::resolveAttack(
        attackBonus, targetAC, damageDice, damageType,
        DamageResistance::Normal, false, false, m_dice);

    if (!result.hit) {
        LOG_INFO("CombatAI", "NPC '{}' misses '{}' (roll {} vs AC {}).",
                 m_actingId, m_targetId, result.attackTotal, targetAC);
        return;
    }

    // Route damage through the unified entry point (death/hit-react/events).
    if (m_combat) {
        m_combat->applyDamage(target, m_targetId, static_cast<float>(result.finalDamage),
                              m_actingId, damageType);
    } else if (targetHC) {
        targetHC->takeDamage(static_cast<float>(result.finalDamage));
    }
    LOG_INFO("CombatAI", "NPC '{}' hits '{}' for {} ({}) damage (roll {} vs AC {}).",
             m_actingId, m_targetId, result.finalDamage, damageDiceStr,
             result.attackTotal, targetAC);
}

} // namespace Phyxel::Core
