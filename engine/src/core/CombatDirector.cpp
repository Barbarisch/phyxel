#include "core/CombatDirector.h"

namespace Phyxel {
namespace Core {

const std::string CombatDirector::s_empty;

CombatMode combatModeFromString(const std::string& s) {
    return (s == "turn_based" || s == "turnbased" || s == "turn-based")
        ? CombatMode::TurnBased
        : CombatMode::RealTime;
}

const char* combatModeToString(CombatMode m) {
    return (m == CombatMode::TurnBased) ? "turn_based" : "real_time";
}

void CombatDirector::beginEncounter(const std::vector<Combatant>& combatants, DiceSystem& dice) {
    if (m_inCombat) return;
    if (combatants.empty()) return;

    m_side.clear();
    for (const auto& c : combatants) {
        m_side[c.entityId] = c.isPlayerSide;
    }
    m_inCombat = true;

    if (m_mode == CombatMode::TurnBased) {
        std::vector<std::string> ids;
        ids.reserve(combatants.size());
        for (const auto& c : combatants) ids.push_back(c.entityId);
        m_tracker.startCombat(ids);
        for (const auto& c : combatants) {
            m_tracker.rollInitiative(c.entityId, c.initiativeBonus, dice);
            if (auto* p = m_tracker.find(c.entityId)) {
                p->isPlayer = c.isPlayerSide;
                p->budget.reset(c.speed);
            }
        }
        m_tracker.sortOrder();
    }
}

void CombatDirector::beginEncounter(const std::vector<Combatant>& combatants) {
    DiceSystem dice;
    beginEncounter(combatants, dice);
}

void CombatDirector::endEncounter() {
    m_tracker.endCombat();
    m_side.clear();
    m_inCombat = false;
}

const std::string& CombatDirector::advanceTurn() {
    if (!m_inCombat || m_mode != CombatMode::TurnBased || !m_tracker.isCombatActive())
        return s_empty;
    return m_tracker.endTurn();
}

const std::string& CombatDirector::currentEntityId() const {
    if (!m_inCombat || m_mode != CombatMode::TurnBased || !m_tracker.isCombatActive())
        return s_empty;
    return m_tracker.currentEntityId();
}

int CombatDirector::currentRound() const {
    if (m_mode != CombatMode::TurnBased) return 0;
    return m_tracker.currentRound();
}

bool CombatDirector::isEntityTurn(const std::string& entityId) const {
    if (!m_inCombat || m_mode != CombatMode::TurnBased || !m_tracker.isCombatActive())
        return false;
    return m_tracker.isEntityTurn(entityId);
}

bool CombatDirector::isPlayerTurn() const {
    const std::string& cur = currentEntityId();
    if (cur.empty()) return false;
    return isPlayerSide(cur);
}

bool CombatDirector::isPlayerSide(const std::string& entityId) const {
    auto it = m_side.find(entityId);
    return it != m_side.end() && it->second;
}

void CombatDirector::removeCombatant(const std::string& entityId) {
    if (!m_inCombat) return;
    m_side.erase(entityId);
    m_tracker.removeParticipant(entityId);

    // Auto-end when one side is gone.
    if (playerSideWiped() || enemySideWiped()) {
        endEncounter();
    }
}

int CombatDirector::playerSideCount() const {
    int n = 0;
    for (const auto& kv : m_side) if (kv.second) ++n;
    return n;
}

int CombatDirector::enemySideCount() const {
    int n = 0;
    for (const auto& kv : m_side) if (!kv.second) ++n;
    return n;
}

bool CombatDirector::playerSideWiped() const {
    return !m_side.empty() && playerSideCount() == 0;
}

bool CombatDirector::enemySideWiped() const {
    return !m_side.empty() && enemySideCount() == 0;
}

nlohmann::json CombatDirector::toJson() const {
    nlohmann::json sides = nlohmann::json::object();
    for (const auto& kv : m_side) sides[kv.first] = kv.second;
    return {
        {"mode",      combatModeToString(m_mode)},
        {"inCombat",  m_inCombat},
        {"sides",     sides},
        {"initiative", m_tracker.toJson()}
    };
}

void CombatDirector::fromJson(const nlohmann::json& j) {
    if (j.contains("mode"))     m_mode     = combatModeFromString(j.value("mode", "real_time"));
    if (j.contains("inCombat")) m_inCombat = j.value("inCombat", false);
    m_side.clear();
    if (j.contains("sides") && j["sides"].is_object()) {
        for (auto it = j["sides"].begin(); it != j["sides"].end(); ++it) {
            m_side[it.key()] = it.value().get<bool>();
        }
    }
    if (j.contains("initiative")) m_tracker.fromJson(j["initiative"]);
}

} // namespace Core
} // namespace Phyxel
