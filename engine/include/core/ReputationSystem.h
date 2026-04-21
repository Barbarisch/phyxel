#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel::Core {

// ---------------------------------------------------------------------------
// ReputationTier — D&D-style faction standing
// ---------------------------------------------------------------------------

enum class ReputationTier {
    Hostile      = 0,  // score < -500
    Unfriendly   = 1,  // -500 to -1
    Neutral      = 2,  //    0 to 249
    Friendly     = 3,  //  250 to 499
    Honored      = 4,  //  500 to 749
    Exalted      = 5   // >= 750
};

const char*    reputationTierName(ReputationTier tier);
ReputationTier reputationTierFromString(const char* name);

// Thresholds (inclusive lower bounds)
constexpr int REP_HOSTILE_MAX     = -501;  // < -500
constexpr int REP_UNFRIENDLY_MIN  = -500;
constexpr int REP_NEUTRAL_MIN     =    0;
constexpr int REP_FRIENDLY_MIN    =  250;
constexpr int REP_HONORED_MIN     =  500;
constexpr int REP_EXALTED_MIN     =  750;

constexpr int REP_MIN = -1000;
constexpr int REP_MAX =  1000;

// ---------------------------------------------------------------------------
// FactionDefinition
// ---------------------------------------------------------------------------

struct FactionDefinition {
    std::string id;
    std::string name;
    std::string description;
    int startingReputation = 0;                 ///< Default score for new entity/faction pairs
    std::vector<std::string> enemyFactionIds;   ///< Factions that are enemies
    std::vector<std::string> allyFactionIds;    ///< Factions that are allies

    nlohmann::json toJson() const;
    static FactionDefinition fromJson(const nlohmann::json& j);
};

// ---------------------------------------------------------------------------
// FactionRegistry — singleton
// ---------------------------------------------------------------------------

class FactionRegistry {
public:
    static FactionRegistry& instance();

    void registerFaction(FactionDefinition def);
    const FactionDefinition* getFaction(const std::string& factionId) const;
    std::vector<std::string> getAllFactionIds() const;
    size_t count() const { return m_factions.size(); }
    void clear();

    bool loadFromFile(const std::string& filePath);
    int  loadFromDirectory(const std::string& dirPath);

private:
    FactionRegistry() = default;
    std::unordered_map<std::string, FactionDefinition> m_factions;
};

// ---------------------------------------------------------------------------
// ReputationSystem — per-entity, per-faction reputation scores
// ---------------------------------------------------------------------------

class ReputationSystem {
public:
    /// Get reputation score. Returns faction's startingReputation (or 0) if unset.
    int getReputation(const std::string& entityId, const std::string& factionId) const;

    /// Set reputation score, clamped to [REP_MIN, REP_MAX].
    void setReputation(const std::string& entityId, const std::string& factionId, int value);

    /// Adjust reputation by delta (clamped).
    void adjustReputation(const std::string& entityId, const std::string& factionId, int delta);

    /// Get tier for an entity with a faction.
    ReputationTier getTier(const std::string& entityId, const std::string& factionId) const;

    /// Convert a raw score to a tier.
    static ReputationTier tierForScore(int score);

    /// True if tier is Hostile.
    bool isHostile(const std::string& entityId, const std::string& factionId) const;

    /// True if tier is Friendly or better.
    bool isFriendly(const std::string& entityId, const std::string& factionId) const;

    /// True if tier is Honored or better.
    bool isHonored(const std::string& entityId, const std::string& factionId) const;

    /// Remove all reputation records for an entity.
    void removeEntity(const std::string& entityId);

    /// Clear all records.
    void clear();

    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    // entityId → factionId → score
    std::unordered_map<std::string,
        std::unordered_map<std::string, int>> m_reputations;
};

} // namespace Phyxel::Core
