#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// PartyMember
// ---------------------------------------------------------------------------

struct PartyMember {
    std::string entityId;
    std::string name;
    int         characterLevel = 1;   ///< Total character level (sum of multiclass levels)
    bool        isAlive        = true;

    nlohmann::json toJson() const;
    static PartyMember fromJson(const nlohmann::json& j);
};

// ---------------------------------------------------------------------------
// Party
// ---------------------------------------------------------------------------

/// Tracks a group of adventurers for encounter balancing, shared rests,
/// and campaign tracking.
class Party {
public:
    // -----------------------------------------------------------------------
    // Membership
    // -----------------------------------------------------------------------

    /// Add a member. No-op if entityId is already in the party.
    void addMember(const std::string& entityId, const std::string& name, int characterLevel);

    /// Remove a member by entityId. No-op if not present.
    void removeMember(const std::string& entityId);

    bool hasMember(const std::string& entityId) const;

    /// Get a member by entityId. Returns nullptr if not found.
    const PartyMember* getMember(const std::string& entityId) const;
    PartyMember*       getMember(const std::string& entityId);

    const std::vector<PartyMember>& getMembers() const { return m_members; }

    int size() const { return static_cast<int>(m_members.size()); }

    // -----------------------------------------------------------------------
    // Level queries (used for encounter budget)
    // -----------------------------------------------------------------------

    int totalLevel()   const;  ///< Sum of all member levels
    int averageLevel() const;  ///< Rounded-down average (0 if empty)

    // -----------------------------------------------------------------------
    // Leader
    // -----------------------------------------------------------------------

    void        setLeader(const std::string& entityId);
    const std::string& getLeaderId() const { return m_leaderId; }

    // -----------------------------------------------------------------------
    // Status
    // -----------------------------------------------------------------------

    void setAlive(const std::string& entityId, bool alive);
    int  aliveCount() const;
    bool isWiped()    const { return aliveCount() == 0 && !m_members.empty(); }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    void clear();

    // -----------------------------------------------------------------------
    // Serialization
    // -----------------------------------------------------------------------

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    std::vector<PartyMember> m_members;
    std::string              m_leaderId;
};

} // namespace Phyxel::Core
