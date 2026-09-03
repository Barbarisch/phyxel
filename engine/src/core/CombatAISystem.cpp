#include "core/CombatAISystem.h"
#include "core/CombatDirector.h"
#include "core/CombatSystem.h"
#include "core/EntityRegistry.h"
#include "core/HealthComponent.h"
#include "core/MonsterDefinition.h"
#include "core/DamageTypes.h"
#include "core/SpellDefinition.h"
#include "core/SpellcasterComponent.h"
#include "core/CombatLog.h"
#include "scene/Entity.h"
#include "scene/NPCEntity.h"
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

    if (m_director) CombatLog::instance().setRound(m_director->currentRound());
    {
        const CombatTactics& tac = tacticsFor(enemyId);
        const int hpPct = static_cast<int>(hpFraction(enemyEntity) * 100.0f);
        std::string prof = "brute (nearest, no kiting, fights to the death)";
        if (m_tacticsProvider && m_tacticsProvider(enemyId)) {
            prof = "priority=";
            switch (tac.priority) {
                case CombatTactics::Priority::Weakest: prof += "weakest"; break;
                case CombatTactics::Priority::Casters: prof += "casters"; break;
                case CombatTactics::Priority::Focus:   prof += "focus";   break;
                default:                               prof += "nearest"; break;
            }
            if (tac.preferredRangeFeet > 0.0f)
                prof += ", kites at " + std::to_string((int)tac.preferredRangeFeet) + "ft";
            if (tac.fleeBelowHpFrac > 0.0f)
                prof += ", flees under " + std::to_string((int)(tac.fleeBelowHpFrac*100)) + "%";
            if (tac.healAllyBelowFrac > 0.0f)
                prof += ", heals allies under " + std::to_string((int)(tac.healAllyBelowFrac*100)) + "%";
        }
        CombatLog::instance().add(enemyId, "turn", "begin",
            "turn starts at " + std::to_string(hpPct) + "% hp — profile: " + prof,
            {{"hp_pct", hpPct}, {"profile", prof}});
    }

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

CombatAISystem::AiPlan CombatAISystem::planFor(const std::string& entityId) const {
    AiPlan plan;
    Scene::Entity* e = m_registry ? m_registry->getEntity(entityId) : nullptr;
    if (!e) return plan;

    const CombatTactics& tac = tacticsFor(entityId);
    switch (tac.priority) {
        case CombatTactics::Priority::Weakest: plan.priority = "weakest"; break;
        case CombatTactics::Priority::Casters: plan.priority = "casters"; break;
        case CombatTactics::Priority::Focus:   plan.priority = "focus";   break;
        default:                               plan.priority = "nearest"; break;
    }
    plan.preferredRangeFeet = tac.preferredRangeFeet;
    plan.fleeBelowHpFrac    = tac.fleeBelowHpFrac;
    plan.healAllyBelowFrac  = tac.healAllyBelowFrac;
    plan.targetByPriority   = acquireTarget(e->getPosition(), entityId);

    // The nearest-AI counterfactual, computed by temporarily running the same
    // selection with a default profile — so the two answers are apples to
    // apples (same candidate set, same liveness rules).
    {
        TacticsProvider saved = m_tacticsProvider;
        auto* self = const_cast<CombatAISystem*>(this);
        self->m_tacticsProvider = nullptr;          // default = Nearest
        plan.nearest = acquireTarget(e->getPosition(), entityId);
        self->m_tacticsProvider = std::move(saved);
    }

    if (tac.healAllyBelowFrac > 0.0f)
        plan.woundedAlly = findWoundedAlly(entityId, tac.healAllyBelowFrac);
    return plan;
}

const CombatTactics& CombatAISystem::tacticsFor(const std::string& id) const {
    static const CombatTactics kDefault{};   // brute: nearest foe, no kiting/morale
    if (m_tacticsProvider)
        if (const CombatTactics* t = m_tacticsProvider(id)) return *t;
    return kDefault;
}

float CombatAISystem::hpFraction(Scene::Entity* e) {
    if (!e) return 0.0f;
    auto* hc = e->getHealthComponent();
    if (!hc || hc->getMaxHealth() <= 0.0f) return 1.0f;
    return hc->getHealth() / hc->getMaxHealth();
}

std::string CombatAISystem::acquireTarget(const glm::vec3& fromPos, const std::string& selfId) const {
    if (!m_registry) return "";

    const CombatTactics& tac = tacticsFor(selfId);

    // Candidate set: the OPPOSING side of whoever is acting (an enemy hunts
    // player-side combatants; an allied companion hunts enemies).
    std::vector<std::string> foes;
    if (m_director) {
        const bool selfPlayerSide = m_director->isPlayerSide(selfId);
        for (const auto& p : tracker()->turnOrder())
            if (m_director->isPlayerSide(p.entityId) != selfPlayerSide &&
                p.entityId != selfId && isAlive(m_registry->getEntity(p.entityId)))
                foes.push_back(p.entityId);
    } else if (m_party) {
        for (const auto& m : m_party->getMembers())
            if (m.isAlive && m.entityId != selfId &&
                isAlive(m_registry->getEntity(m.entityId)))
                foes.push_back(m.entityId);
    }
    if (foes.empty()) return "";

    auto distSqTo = [&](const std::string& id) {
        glm::vec3 d = m_registry->getEntity(id)->getPosition() - fromPos;
        return glm::dot(d, d);
    };
    auto nearest = [&]() {
        const std::string* best = nullptr; float bestD = std::numeric_limits<float>::max();
        for (const auto& id : foes) { float d = distSqTo(id); if (d < bestD) { bestD = d; best = &id; } }
        return best ? *best : std::string();
    };

    // The candidate table every priority reasons over — logged once so the
    // decision below can be checked against what the AI actually saw.
    auto candidates = [&]() {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& id : foes) {
            Scene::Entity* e = m_registry->getEntity(id);
            auto* hc = e ? e->getHealthComponent() : nullptr;
            arr.push_back({{"id", id},
                           {"dist", std::sqrt(distSqTo(id))},
                           {"hp", hc ? hc->getHealth() : -1.0f},
                           {"hp_frac", hpFraction(e)}});
        }
        return arr;
    };

    switch (tac.priority) {
        case CombatTactics::Priority::Weakest: {
            // Finish the wounded: lowest CURRENT hp, distance breaking ties.
            const std::string* best = nullptr;
            float bestHp = std::numeric_limits<float>::max(), bestD = 0.0f;
            for (const auto& id : foes) {
                Scene::Entity* e = m_registry->getEntity(id);
                auto* hc = e->getHealthComponent();
                const float hp = hc ? hc->getHealth() : 1e9f;
                const float d  = distSqTo(id);
                if (!best || hp < bestHp || (hp == bestHp && d < bestD)) {
                    best = &id; bestHp = hp; bestD = d;
                }
            }
            const std::string near = nearest();
            const std::string pick = best ? *best : near;
            CombatLog::instance().add(selfId, "target", "weakest",
                pick == near
                    ? "weakest is also the nearest (" + pick + ", hp " + std::to_string((int)bestHp) + ")"
                    : "chose " + pick + " (hp " + std::to_string((int)bestHp) +
                      ") OVER nearer " + near + " — finish the wounded",
                {{"picked", pick}, {"nearest", near}, {"candidates", candidates()}});
            return pick;
        }
        case CombatTactics::Priority::Casters: {
            // Kill the artillery first: nearest foe that can still cast.
            const std::string* best = nullptr; float bestD = std::numeric_limits<float>::max();
            for (const auto& id : foes) {
                if (!m_casterProvider) break;
                SpellcasterComponent* sc = m_casterProvider(id);
                if (!sc) continue;
                const float d = distSqTo(id);
                if (d < bestD) { bestD = d; best = &id; }
            }
            const std::string near = nearest();
            const std::string pick = best ? *best : near;
            CombatLog::instance().add(selfId, "target", "casters",
                best ? "chose caster " + pick + " — kill the artillery first"
                     : "no enemy caster in range; fell back to nearest " + near,
                {{"picked", pick}, {"nearest", near}, {"candidates", candidates()}});
            return pick;
        }
        case CombatTactics::Priority::Focus: {
            // Pile onto the side's current focus while it lives.
            const bool selfPlayerSide = m_director && m_director->isPlayerSide(selfId);
            const std::string& focus = selfPlayerSide ? m_focusPlayerSide : m_focusEnemySide;
            if (!focus.empty() && isAlive(m_registry->getEntity(focus)) &&
                std::find(foes.begin(), foes.end(), focus) != foes.end()) {
                CombatLog::instance().add(selfId, "target", "focus",
                    "joining the pack on " + focus + " (side focus-fire)",
                    {{"picked", focus}, {"nearest", nearest()}, {"candidates", candidates()}});
                return focus;
            }
            const std::string near = nearest();
            CombatLog::instance().add(selfId, "target", "focus",
                "no living side focus yet — taking nearest " + near + " and setting the focus",
                {{"picked", near}, {"candidates", candidates()}});
            return near;
        }
        case CombatTactics::Priority::Nearest:
        default: {
            const std::string near = nearest();
            CombatLog::instance().add(selfId, "target", "nearest",
                "closest living foe: " + near,
                {{"picked", near}, {"candidates", candidates()}});
            return near;
        }
    }
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

    const CombatTactics& tac = tacticsFor(m_actingId);
    const glm::vec3 selfPos = enemy->getPosition();

    // Remember this side's focus so Priority::Focus combatants can pile on.
    if (m_director) {
        (m_director->isPlayerSide(m_actingId) ? m_focusPlayerSide : m_focusEnemySide) = m_targetId;
    }

    // ── MORALE ── Broken: disengage and run from the nearest foe. A fleeing
    // NPC neither attacks nor casts — it is out of the fight until it rallies
    // (v1: it keeps running while below the threshold).
    if (tac.fleeBelowHpFrac > 0.0f && hpFraction(enemy) < tac.fleeBelowHpFrac) {
        const int pct = static_cast<int>(hpFraction(enemy) * 100.0f);
        const int thr = static_cast<int>(tac.fleeBelowHpFrac * 100.0f);
        if (m_turnActor.canMove()) {
            glm::vec3 away = selfPos - target->getPosition(); away.y = 0.0f;
            const float len = std::sqrt(away.x * away.x + away.z * away.z);
            if (len > 1e-4f) {
                away /= len;
                const glm::vec3 dest = selfPos + away * m_turnActor.feetToUnits(30.0f);
                if (m_turnActor.requestMove(dest)) {
                    LOG_INFO("CombatAI", "NPC '{}' FLEES from '{}' (hp {}%)",
                             m_actingId, m_targetId, pct);
                    CombatLog::instance().add(m_actingId, "action", "flee",
                        "MORALE BROKE: hp " + std::to_string(pct) + "% is under its " +
                        std::to_string(thr) + "% threshold — disengaging from " + m_targetId +
                        " instead of fighting",
                        {{"hp_pct", pct}, {"threshold_pct", thr}, {"from", m_targetId}});
                    m_phase = Phase::Moving;
                    return;
                }
            }
        }
        CombatLog::instance().add(m_actingId, "action", "cower",
            "morale broken (hp " + std::to_string(pct) + "% < " + std::to_string(thr) +
            "%) but it cannot move — turn wasted",
            {{"hp_pct", pct}, {"threshold_pct", thr}});
        m_phase = Phase::Done;
        return;
    }

    // ── HEALER ── A wounded ally outranks hurting the enemy.
    if (tac.healAllyBelowFrac > 0.0f && m_turnActor.canAct() && m_casterProvider) {
        if (SpellcasterComponent* sc = m_casterProvider(m_actingId)) {
            const std::string allyId = findWoundedAlly(m_actingId, tac.healAllyBelowFrac);
            const int thr = static_cast<int>(tac.healAllyBelowFrac * 100.0f);
            if (!allyId.empty()) {
                const std::string healId = chooseHealSpell(*sc);
                const int allyPct = static_cast<int>(
                    hpFraction(m_registry->getEntity(allyId)) * 100.0f);
                if (!healId.empty()) {
                    CombatLog::instance().add(m_actingId, "action", "heal",
                        "SUPPORT: ally " + allyId + " at " + std::to_string(allyPct) +
                        "% is under the " + std::to_string(thr) + "% line — casting " +
                        healId + " instead of attacking " + m_targetId,
                        {{"ally", allyId}, {"ally_hp_pct", allyPct},
                         {"threshold_pct", thr}, {"spell", healId},
                         {"forgone_target", m_targetId}});
                    resolveEnemyHeal(enemy, *sc, healId, allyId);
                    m_phase = Phase::Done;
                    return;
                }
                CombatLog::instance().add(m_actingId, "reject", "heal",
                    "wanted to heal " + allyId + " (" + std::to_string(allyPct) +
                    "%) but has no castable heal left — falling through to attack",
                    {{"ally", allyId}, {"ally_hp_pct", allyPct}});
            }
        }
    }

    // ── KITING ── A ranged fighter backs off instead of trading in melee:
    // if the target is inside its preferred range, WITHDRAW this turn. It
    // still shoots on later turns from the new distance.
    if (tac.preferredRangeFeet > 0.0f && m_turnActor.canMove()) {
        glm::vec3 away = selfPos - target->getPosition(); away.y = 0.0f;
        const float distUnits = std::sqrt(away.x * away.x + away.z * away.z);
        const float wantUnits = m_turnActor.feetToUnits(tac.preferredRangeFeet);
        if (distUnits < wantUnits && distUnits > 1e-4f) {
            away /= distUnits;
            const glm::vec3 dest = selfPos + away * (wantUnits - distUnits + 1.0f);
            if (m_turnActor.requestMove(dest)) {
                LOG_INFO("CombatAI", "NPC '{}' KITES away from '{}' ({} -> {} units)",
                         m_actingId, m_targetId, static_cast<int>(distUnits),
                         static_cast<int>(wantUnits));
                CombatLog::instance().add(m_actingId, "action", "kite",
                    "RANGED: " + m_targetId + " closed to " +
                    std::to_string((int)distUnits) + "u, inside its preferred " +
                    std::to_string((int)tac.preferredRangeFeet) + "ft (" +
                    std::to_string((int)wantUnits) + "u) — withdrawing rather than trading blows",
                    {{"target", m_targetId}, {"dist_units", distUnits},
                     {"preferred_units", wantUnits},
                     {"preferred_feet", tac.preferredRangeFeet}});
                m_phase = Phase::Moving;
                return;
            }
            CombatLog::instance().add(m_actingId, "reject", "kite",
                "wanted to withdraw from " + m_targetId + " but has no movement left",
                {{"dist_units", distUnits}, {"preferred_units", wantUnits}});
        }
    }

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
                const SpellDefinition* sd = SpellRegistry::instance().getSpell(spellId);
                const int lvl = sd ? sd->level : 0;
                const int slotsLeft = (lvl >= 1 && lvl <= SpellSlots::MAX_SPELL_LEVEL)
                                          ? sc->slots().remaining[lvl - 1] : -1;
                CombatLog::instance().add(m_actingId, "action", "cast",
                    "CASTER: " + spellId + (lvl > 0
                        ? " (level " + std::to_string(lvl) + ", " +
                          std::to_string(slotsLeft) + " slot(s) before this cast)"
                        : " (cantrip, unlimited)") +
                    " at " + m_targetId + " — spells beat melee while it has any",
                    {{"spell", spellId}, {"level", lvl},
                     {"slots_before", slotsLeft}, {"target", m_targetId}});
                resolveEnemyCast(enemy, *sc, spellId);
                m_phase = Phase::Done;   // one action per turn (v1, like attacks)
                return;
            }
            CombatLog::instance().add(m_actingId, "reject", "cast",
                "is a caster but has nothing castable left (no slots / no damaging "
                "spell) — falling back to melee",
                {{"slots_remaining", sc->slots().totalRemaining()}});
        }
    }

    // Try to attack (TurnActor rejects if out of reach / no action).
    const float reachUnits = m_turnActor.feetToUnits(m_reachFeet);
    const float toTarget = glm::length(glm::vec2(targetPos.x - selfPos.x,
                                                 targetPos.z - selfPos.z));
    if (m_turnActor.canAct() && m_turnActor.requestAttack(targetPos, m_reachFeet)) {
        CombatLog::instance().add(m_actingId, "action", "attack",
            "MELEE: " + m_targetId + " is " + std::to_string((int)toTarget) +
            "u away, inside its " + std::to_string((int)m_reachFeet) + "ft reach",
            {{"target", m_targetId}, {"dist_units", toTarget},
             {"reach_units", reachUnits}});
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
        if (m_turnActor.requestMove(dest)) {
            CombatLog::instance().add(m_actingId, "action", "advance",
                "OUT OF REACH: " + m_targetId + " is " + std::to_string((int)toTarget) +
                "u away vs " + std::to_string((int)reachUnits) +
                "u reach, and nothing castable — closing the distance",
                {{"target", m_targetId}, {"dist_units", toTarget},
                 {"reach_units", reachUnits}});
            m_phase = Phase::Moving;
            return;
        }
    }

    CombatLog::instance().add(m_actingId, "action", "pass",
        "no action left: cannot reach " + m_targetId + " (" +
        std::to_string((int)toTarget) + "u), nothing to cast, no movement remaining",
        {{"target", m_targetId}, {"dist_units", toTarget}});
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

std::string CombatAISystem::findWoundedAlly(const std::string& selfId, float frac) const {
    if (!m_registry || !m_director) return {};
    const bool selfPlayerSide = m_director->isPlayerSide(selfId);
    std::string worst;
    float worstFrac = frac;   // must be strictly under the threshold to qualify
    for (const auto& p : tracker()->turnOrder()) {
        if (m_director->isPlayerSide(p.entityId) != selfPlayerSide) continue;
        Scene::Entity* e = m_registry->getEntity(p.entityId);
        if (!isAlive(e)) continue;
        const float f = hpFraction(e);
        if (f < worstFrac) { worstFrac = f; worst = p.entityId; }
    }
    return worst;   // includes SELF: a bloodied healer patches itself up
}

std::string CombatAISystem::chooseHealSpell(SpellcasterComponent& caster) const {
    // Prefer the biggest castable heal (slots are finite); cantrip heals are
    // rare in 5e but supported for free.
    const std::string* best = nullptr;
    int bestLevel = -1;
    for (const auto& ks : caster.knownSpells()) {
        const SpellDefinition* def = SpellRegistry::instance().getSpell(ks.spellId);
        if (!def || !def->hasHeal()) continue;
        if (!caster.canCast(ks.spellId, def->level)) continue;
        if (def->level > bestLevel) { best = &ks.spellId; bestLevel = def->level; }
    }
    if (best) return *best;
    for (const auto& id : caster.cantrips()) {
        const SpellDefinition* def = SpellRegistry::instance().getSpell(id);
        if (def && def->hasHeal()) return id;
    }
    return {};
}

void CombatAISystem::resolveEnemyHeal(Scene::Entity* caster, SpellcasterComponent& sc,
                                      const std::string& spellId, const std::string& allyId) {
    Scene::Entity* ally = m_registry ? m_registry->getEntity(allyId) : nullptr;
    const SpellDefinition* def = SpellRegistry::instance().getSpell(spellId);
    if (!ally || !def) return;

    const int slotLevel = def->isCantrip() ? 0 : def->level;
    if (!sc.canCast(spellId, slotLevel)) return;

    DiceExpression healExpr = def->healDiceAt(def->level);
    const int amount = def->healBase +
                       (healExpr.count > 0 ? m_dice.rollExpression(healExpr).total : 0);
    if (slotLevel > 0) sc.spendSlot(slotLevel);

    const std::string casterId = m_actingId;
    auto onRelease = [this, allyId, amount]() {
        Scene::Entity* a = m_registry ? m_registry->getEntity(allyId) : nullptr;
        if (!a || amount <= 0) return;
        if (auto* hc = a->getHealthComponent()) hc->heal(static_cast<float>(amount));
    };

    LOG_INFO("CombatAI", "NPC '{}' HEALS '{}' with '{}' for {} (hp was {}%)",
             casterId, allyId, spellId, amount,
             static_cast<int>(hpFraction(ally) * 100.0f));

    if (m_castExecutor) m_castExecutor(casterId, spellId, ally->getPosition(), onRelease);
    else                onRelease();
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
    CombatLog::instance().add(m_actingId, "outcome", "cast_result",
        spellId + " vs " + m_targetId + ": " + how + " -> " +
        std::to_string(dmg) + " damage" +
        (slotLevel > 0 ? " (slot level " + std::to_string(slotLevel) + " spent, " +
                         std::to_string(caster.slots().remaining[slotLevel - 1]) + " left)"
                       : " (cantrip, no slot)"),
        {{"spell", spellId}, {"target", m_targetId}, {"damage", dmg},
         {"resolution", how}, {"slot_level", slotLevel}});

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

    // Stat-block resolution: an NPC spawned with a monsterId (spawn_npc /
    // spawn_encounter) names its stat block directly; the raw entity id
    // ("npc_<name>") stays as the legacy fallback for hand-keyed combats.
    std::string statBlockId = m_actingId;
    if (auto* npc = dynamic_cast<Scene::NPCEntity*>(enemyEntity)) {
        if (!npc->getMonsterId().empty()) statBlockId = npc->getMonsterId();
    }

    if (const MonsterDefinition* def = MonsterRegistry::instance().getMonster(statBlockId)) {
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
        CombatLog::instance().add(m_actingId, "outcome", "attack_result",
            "MISS on " + m_targetId + ": rolled " + std::to_string(result.attackTotal) +
            " (+" + std::to_string(attackBonus) + " to hit) vs AC " + std::to_string(targetAC),
            {{"target", m_targetId}, {"roll", result.attackTotal},
             {"attack_bonus", attackBonus}, {"target_ac", targetAC}, {"hit", false}});
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
    CombatLog::instance().add(m_actingId, "outcome", "attack_result",
        "HIT on " + m_targetId + ": rolled " + std::to_string(result.attackTotal) +
        " (+" + std::to_string(attackBonus) + ") vs AC " + std::to_string(targetAC) +
        " -> " + std::to_string(result.finalDamage) + " damage (" + damageDiceStr + ")",
        {{"target", m_targetId}, {"roll", result.attackTotal},
         {"attack_bonus", attackBonus}, {"target_ac", targetAC},
         {"damage", result.finalDamage}, {"dice", damageDiceStr}, {"hit", true}});
}

} // namespace Phyxel::Core
