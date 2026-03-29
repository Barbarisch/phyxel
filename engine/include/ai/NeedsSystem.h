#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace AI {

/// Types of needs an NPC can have.
enum class NeedType {
    Hunger,
    Rest,
    Social,
    Safety,
    Entertainment,
    Comfort
};

std::string needTypeToString(NeedType type);
NeedType needTypeFromString(const std::string& str);

/// A single need with a current value, decay rate, and urgency threshold.
/// Value ranges from 0 (depleted) to 100 (fully satisfied).
/// When value drops below urgencyThreshold, the NPC should prioritize fulfilling it.
struct Need {
    NeedType type = NeedType::Hunger;
    float value = 100.0f;          ///< Current satisfaction (0-100)
    float decayRate = 1.0f;        ///< Points lost per game hour
    float urgencyThreshold = 30.0f;///< Below this, need becomes urgent

    /// Returns 0.0 when fully satisfied, 1.0 when completely depleted.
    float getUrgency() const {
        return 1.0f - (value / 100.0f);
    }

    /// Whether the need is below the urgency threshold.
    bool isUrgent() const {
        return value < urgencyThreshold;
    }

    nlohmann::json toJson() const;
    static Need fromJson(const nlohmann::json& j);
};

/// Per-NPC needs system. Tracks multiple needs that decay over time and
/// can be fulfilled by activities at certain locations.
class NeedsSystem {
public:
    NeedsSystem();

    /// Initialize with default needs (Hunger, Rest, Social, Safety, Entertainment, Comfort).
    void initDefaults();

    /// Add or replace a need.
    void setNeed(const Need& need);

    /// Remove a need type.
    void removeNeed(NeedType type);

    /// Get a need, or nullptr if not tracked.
    const Need* getNeed(NeedType type) const;
    Need* getNeedMut(NeedType type);

    /// Get all needs.
    const std::unordered_map<NeedType, Need>& getAllNeeds() const { return m_needs; }

    /// Decay all needs based on elapsed game hours.
    /// Call this with delta hours from DayNightCycle, not real seconds.
    void update(float deltaHours);

    /// Fulfill a need by the given amount (clamped to 100).
    void fulfill(NeedType type, float amount);

    /// Get the most urgent need (lowest value).
    const Need* getMostUrgent() const;

    /// Get all urgent needs (below their threshold).
    std::vector<const Need*> getUrgentNeeds() const;

    /// Map an ActivityType to the needs it fulfills and by how much.
    /// Returns pairs of (NeedType, fulfillAmount).
    static std::vector<std::pair<NeedType, float>> getFulfillmentForActivity(const std::string& activity);

    /// Serialize.
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    std::unordered_map<NeedType, Need> m_needs;
};

} // namespace AI
} // namespace Phyxel
