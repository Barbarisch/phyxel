#include "core/InitiativeTracker.h"

#include <algorithm>
#include <stdexcept>

namespace Phyxel {
namespace Core {

// ---------------------------------------------------------------------------
// CombatParticipant
// ---------------------------------------------------------------------------

bool CombatParticipant::operator>(const CombatParticipant& other) const {
    if (initiativeRoll != other.initiativeRoll) return initiativeRoll > other.initiativeRoll;
    if (initiativeModifier != other.initiativeModifier) return initiativeModifier > other.initiativeModifier;
    return entityId < other.entityId;  // stable alphabetical tiebreak
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void InitiativeTracker::startCombat(const std::vector<std::string>& entityIds, int defaultSpeed) {
    m_order.clear();
    m_currentIndex = 0;
    m_round        = 1;
    m_active       = true;
    m_defaultSpeed = defaultSpeed;

    for (const auto& id : entityIds) {
        CombatParticipant p;
        p.entityId = id;
        p.budget.reset(defaultSpeed);
        m_order.push_back(p);
    }
}

void InitiativeTracker::rollInitiative(const std::string& entityId, int dexMod, DiceSystem& dice) {
    auto* p = find(entityId);
    if (!p) return;
    auto roll = dice.roll(DieType::D20, dexMod);
    p->initiativeRoll     = roll.total;
    p->initiativeModifier = dexMod;
}

void InitiativeTracker::setInitiative(const std::string& entityId, int roll, int modifier) {
    auto* p = find(entityId);
    if (!p) return;
    p->initiativeRoll     = roll;
    p->initiativeModifier = modifier;
}

void InitiativeTracker::sortOrder() {
    // Stable sort: preserve insertion order for equal-priority participants
    std::stable_sort(m_order.begin(), m_order.end(),
        [](const CombatParticipant& a, const CombatParticipant& b) { return a > b; });
    m_currentIndex = 0;
}

void InitiativeTracker::setSurprised(const std::string& entityId, bool surprised) {
    auto* p = find(entityId);
    if (p) p->isSurprised = surprised;
}

// ---------------------------------------------------------------------------
// Turn management
// ---------------------------------------------------------------------------

void InitiativeTracker::advanceIndex() {
    if (m_order.empty()) return;
    m_currentIndex = (m_currentIndex + 1) % m_order.size();
    if (m_currentIndex == 0) nextRound();
}

const std::string& InitiativeTracker::endTurn() {
    static const std::string empty;
    if (!m_active || m_order.empty()) return empty;

    // Mark current participant as having acted
    m_order[m_currentIndex].hasActedThisRound = true;

    advanceIndex();

    // Reset the new current participant's budget
    // Skip surprised entities on round 1 (they lose their first turn)
    while (m_active && !m_order.empty()) {
        auto& next = m_order[m_currentIndex];
        if (m_round == 1 && next.isSurprised && !next.hasActedThisRound) {
            // Surprised: skip turn but mark as having "acted" (their skipped turn)
            next.hasActedThisRound = true;
            advanceIndex();
        } else {
            break;
        }
    }

    if (!m_order.empty()) {
        auto& cur = m_order[m_currentIndex];
        cur.budget.reset(m_defaultSpeed);
        cur.hasActedThisRound = false;
    }

    return currentEntityId();
}

void InitiativeTracker::nextRound() {
    ++m_round;
    // Clear hasActedThisRound for all participants
    for (auto& p : m_order) p.hasActedThisRound = false;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

const std::string& InitiativeTracker::currentEntityId() const {
    static const std::string empty;
    if (!m_active || m_order.empty()) return empty;
    return m_order[m_currentIndex].entityId;
}

const CombatParticipant& InitiativeTracker::currentParticipant() const {
    if (m_order.empty()) throw std::runtime_error("No participants in combat");
    return m_order[m_currentIndex];
}

CombatParticipant& InitiativeTracker::currentParticipant() {
    if (m_order.empty()) throw std::runtime_error("No participants in combat");
    return m_order[m_currentIndex];
}

bool InitiativeTracker::isEntityTurn(const std::string& entityId) const {
    if (!m_active || m_order.empty()) return false;
    return m_order[m_currentIndex].entityId == entityId;
}

const CombatParticipant* InitiativeTracker::find(const std::string& entityId) const {
    for (const auto& p : m_order) if (p.entityId == entityId) return &p;
    return nullptr;
}

CombatParticipant* InitiativeTracker::find(const std::string& entityId) {
    for (auto& p : m_order) if (p.entityId == entityId) return &p;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Reactions
// ---------------------------------------------------------------------------

bool InitiativeTracker::canReact(const std::string& entityId) const {
    const auto* p = find(entityId);
    return p && p->budget.canReact();
}

bool InitiativeTracker::useReaction(const std::string& entityId) {
    auto* p = find(entityId);
    return p && p->budget.spendReaction();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void InitiativeTracker::removeParticipant(const std::string& entityId) {
    auto it = std::find_if(m_order.begin(), m_order.end(),
        [&](const CombatParticipant& p) { return p.entityId == entityId; });
    if (it == m_order.end()) return;

    size_t removedIdx = static_cast<size_t>(it - m_order.begin());
    m_order.erase(it);

    if (m_order.empty()) { endCombat(); return; }

    // Keep currentIndex valid
    if (removedIdx < m_currentIndex) {
        m_currentIndex--;
    } else if (m_currentIndex >= m_order.size()) {
        m_currentIndex = 0;
        nextRound();
    }
}

void InitiativeTracker::endCombat() {
    m_order.clear();
    m_currentIndex = 0;
    m_round  = 0;
    m_active = false;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

nlohmann::json InitiativeTracker::toJson() const {
    nlohmann::json j;
    j["active"]       = m_active;
    j["round"]        = m_round;
    j["currentIndex"] = m_currentIndex;
    j["defaultSpeed"] = m_defaultSpeed;

    nlohmann::json order = nlohmann::json::array();
    for (const auto& p : m_order) {
        order.push_back({
            {"entityId",           p.entityId},
            {"initiativeRoll",     p.initiativeRoll},
            {"initiativeModifier", p.initiativeModifier},
            {"isSurprised",        p.isSurprised},
            {"hasActedThisRound",  p.hasActedThisRound},
            {"isPlayer",           p.isPlayer},
            {"budget",             p.budget.toJson()}
        });
    }
    j["order"] = order;
    return j;
}

void InitiativeTracker::fromJson(const nlohmann::json& j) {
    m_active       = j.value("active", false);
    m_round        = j.value("round", 0);
    m_currentIndex = j.value("currentIndex", 0u);
    m_defaultSpeed = j.value("defaultSpeed", 30);

    m_order.clear();
    if (j.contains("order")) {
        for (const auto& pj : j["order"]) {
            CombatParticipant p;
            p.entityId           = pj.value("entityId", "");
            p.initiativeRoll     = pj.value("initiativeRoll", 0);
            p.initiativeModifier = pj.value("initiativeModifier", 0);
            p.isSurprised        = pj.value("isSurprised", false);
            p.hasActedThisRound  = pj.value("hasActedThisRound", false);
            p.isPlayer           = pj.value("isPlayer", false);
            if (pj.contains("budget")) p.budget.fromJson(pj["budget"]);
            m_order.push_back(p);
        }
    }

    if (m_currentIndex >= m_order.size()) m_currentIndex = 0;
}

} // namespace Core
} // namespace Phyxel
