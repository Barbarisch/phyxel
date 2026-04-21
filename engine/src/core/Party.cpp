#include "core/Party.h"
#include <algorithm>
#include <numeric>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// PartyMember JSON
// ---------------------------------------------------------------------------

nlohmann::json PartyMember::toJson() const {
    return {
        {"entityId",        entityId},
        {"name",            name},
        {"characterLevel",  characterLevel},
        {"isAlive",         isAlive}
    };
}

PartyMember PartyMember::fromJson(const nlohmann::json& j) {
    return {
        j.value("entityId",       ""),
        j.value("name",           ""),
        j.value("characterLevel",  1),
        j.value("isAlive",         true)
    };
}

// ---------------------------------------------------------------------------
// Membership
// ---------------------------------------------------------------------------

void Party::addMember(const std::string& entityId,
                      const std::string& name,
                      int characterLevel) {
    if (hasMember(entityId)) return;
    m_members.push_back({ entityId, name, characterLevel, true });
    if (m_leaderId.empty()) m_leaderId = entityId;
}

void Party::removeMember(const std::string& entityId) {
    auto it = std::find_if(m_members.begin(), m_members.end(),
        [&](const PartyMember& m) { return m.entityId == entityId; });
    if (it == m_members.end()) return;
    m_members.erase(it);
    if (m_leaderId == entityId) {
        m_leaderId = m_members.empty() ? "" : m_members.front().entityId;
    }
}

bool Party::hasMember(const std::string& entityId) const {
    return getMember(entityId) != nullptr;
}

const PartyMember* Party::getMember(const std::string& entityId) const {
    for (const auto& m : m_members)
        if (m.entityId == entityId) return &m;
    return nullptr;
}

PartyMember* Party::getMember(const std::string& entityId) {
    for (auto& m : m_members)
        if (m.entityId == entityId) return &m;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Level queries
// ---------------------------------------------------------------------------

int Party::totalLevel() const {
    int total = 0;
    for (const auto& m : m_members) total += m.characterLevel;
    return total;
}

int Party::averageLevel() const {
    if (m_members.empty()) return 0;
    return totalLevel() / static_cast<int>(m_members.size());
}

// ---------------------------------------------------------------------------
// Leader / status
// ---------------------------------------------------------------------------

void Party::setLeader(const std::string& entityId) {
    if (hasMember(entityId)) m_leaderId = entityId;
}

void Party::setAlive(const std::string& entityId, bool alive) {
    auto* m = getMember(entityId);
    if (m) m->isAlive = alive;
}

int Party::aliveCount() const {
    int count = 0;
    for (const auto& m : m_members) if (m.isAlive) ++count;
    return count;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Party::clear() {
    m_members.clear();
    m_leaderId.clear();
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

nlohmann::json Party::toJson() const {
    nlohmann::json j;
    j["leaderId"] = m_leaderId;
    auto arr = nlohmann::json::array();
    for (const auto& m : m_members) arr.push_back(m.toJson());
    j["members"] = arr;
    return j;
}

void Party::fromJson(const nlohmann::json& j) {
    m_members.clear();
    m_leaderId = j.value("leaderId", "");
    if (j.contains("members") && j["members"].is_array()) {
        for (const auto& mj : j["members"])
            m_members.push_back(PartyMember::fromJson(mj));
    }
}

} // namespace Phyxel::Core
