#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace AI {

/// What the NPC is supposed to do during a schedule block.
enum class ActivityType {
    Sleep,
    Eat,
    Work,
    Patrol,
    Socialize,
    Worship,
    Train,
    Shop,
    Wander,
    Guard,
    Custom
};

/// A single time block in an NPC's daily schedule.
struct ScheduleEntry {
    float startHour = 0.0f;       ///< Start hour (0.0–24.0)
    float endHour = 24.0f;        ///< End hour (0.0–24.0). May wrap past midnight.
    ActivityType activity = ActivityType::Wander;
    std::string locationId;       ///< Location to go to during this block (empty = stay put)
    int priority = 0;             ///< Higher priority entries override lower ones

    nlohmann::json toJson() const;
    static ScheduleEntry fromJson(const nlohmann::json& j);
    static ActivityType activityFromString(const std::string& s);
    static std::string activityToString(ActivityType a);

    /// Does this entry contain the given hour? Handles midnight wrapping.
    bool containsHour(float hour) const;
};

/// An ordered list of schedule entries covering a 24-hour day.
/// Entries may overlap — higher priority wins.
class Schedule {
public:
    Schedule() = default;

    /// Add an entry to the schedule.
    void addEntry(const ScheduleEntry& entry);

    /// Get the active entry for a given hour. Returns the highest-priority
    /// entry that contains the hour, or nullptr if none match.
    const ScheduleEntry* getCurrentActivity(float hour) const;

    /// Get all entries.
    const std::vector<ScheduleEntry>& getEntries() const { return m_entries; }

    /// Clear all entries.
    void clear() { m_entries.clear(); }

    /// Number of entries.
    size_t size() const { return m_entries.size(); }

    nlohmann::json toJson() const;
    static Schedule fromJson(const nlohmann::json& arr);

    // ========================================================================
    // Default schedules for common roles
    // ========================================================================

    static Schedule guardSchedule();
    static Schedule merchantSchedule();
    static Schedule farmerSchedule();
    static Schedule innkeeperSchedule();
    static Schedule defaultSchedule();

    /// Get a default schedule for a role name (case-insensitive).
    /// Returns defaultSchedule() if role is unrecognized.
    static Schedule forRole(const std::string& role);

private:
    std::vector<ScheduleEntry> m_entries;
};

} // namespace AI
} // namespace Phyxel
