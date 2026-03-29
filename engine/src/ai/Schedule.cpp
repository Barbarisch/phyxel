#include "ai/Schedule.h"
#include <algorithm>

namespace Phyxel {
namespace AI {

// ============================================================================
// ScheduleEntry
// ============================================================================

ActivityType ScheduleEntry::activityFromString(const std::string& s) {
    if (s == "Sleep")     return ActivityType::Sleep;
    if (s == "Eat")       return ActivityType::Eat;
    if (s == "Work")      return ActivityType::Work;
    if (s == "Patrol")    return ActivityType::Patrol;
    if (s == "Socialize") return ActivityType::Socialize;
    if (s == "Worship")   return ActivityType::Worship;
    if (s == "Train")     return ActivityType::Train;
    if (s == "Shop")      return ActivityType::Shop;
    if (s == "Wander")    return ActivityType::Wander;
    if (s == "Guard")     return ActivityType::Guard;
    return ActivityType::Custom;
}

std::string ScheduleEntry::activityToString(ActivityType a) {
    switch (a) {
        case ActivityType::Sleep:     return "Sleep";
        case ActivityType::Eat:       return "Eat";
        case ActivityType::Work:      return "Work";
        case ActivityType::Patrol:    return "Patrol";
        case ActivityType::Socialize: return "Socialize";
        case ActivityType::Worship:   return "Worship";
        case ActivityType::Train:     return "Train";
        case ActivityType::Shop:      return "Shop";
        case ActivityType::Wander:    return "Wander";
        case ActivityType::Guard:     return "Guard";
        case ActivityType::Custom:    return "Custom";
    }
    return "Custom";
}

bool ScheduleEntry::containsHour(float hour) const {
    if (startHour <= endHour) {
        // Normal range e.g. 8.0–17.0
        return hour >= startHour && hour < endHour;
    } else {
        // Wraps past midnight e.g. 22.0–6.0
        return hour >= startHour || hour < endHour;
    }
}

nlohmann::json ScheduleEntry::toJson() const {
    return {
        {"startHour", startHour},
        {"endHour", endHour},
        {"activity", activityToString(activity)},
        {"locationId", locationId},
        {"priority", priority}
    };
}

ScheduleEntry ScheduleEntry::fromJson(const nlohmann::json& j) {
    ScheduleEntry e;
    e.startHour = j.value("startHour", 0.0f);
    e.endHour = j.value("endHour", 24.0f);
    e.activity = activityFromString(j.value("activity", "Wander"));
    e.locationId = j.value("locationId", "");
    e.priority = j.value("priority", 0);
    return e;
}

// ============================================================================
// Schedule
// ============================================================================

void Schedule::addEntry(const ScheduleEntry& entry) {
    m_entries.push_back(entry);
}

const ScheduleEntry* Schedule::getCurrentActivity(float hour) const {
    const ScheduleEntry* best = nullptr;
    for (const auto& entry : m_entries) {
        if (entry.containsHour(hour)) {
            if (!best || entry.priority > best->priority) {
                best = &entry;
            }
        }
    }
    return best;
}

nlohmann::json Schedule::toJson() const {
    auto arr = nlohmann::json::array();
    for (const auto& entry : m_entries) {
        arr.push_back(entry.toJson());
    }
    return arr;
}

Schedule Schedule::fromJson(const nlohmann::json& arr) {
    Schedule s;
    for (const auto& j : arr) {
        s.addEntry(ScheduleEntry::fromJson(j));
    }
    return s;
}

// ============================================================================
// Default Schedules
// ============================================================================

Schedule Schedule::guardSchedule() {
    Schedule s;
    s.addEntry({22.0f, 6.0f,  ActivityType::Sleep,  "home",       0});
    s.addEntry({6.0f,  7.0f,  ActivityType::Eat,    "tavern",     0});
    s.addEntry({7.0f,  12.0f, ActivityType::Guard,   "guard_post", 0});
    s.addEntry({12.0f, 13.0f, ActivityType::Eat,    "tavern",     0});
    s.addEntry({13.0f, 18.0f, ActivityType::Patrol,  "",           0});
    s.addEntry({18.0f, 19.0f, ActivityType::Eat,    "tavern",     0});
    s.addEntry({19.0f, 22.0f, ActivityType::Socialize,"tavern",   0});
    return s;
}

Schedule Schedule::merchantSchedule() {
    Schedule s;
    s.addEntry({22.0f, 7.0f,  ActivityType::Sleep,    "home",   0});
    s.addEntry({7.0f,  8.0f,  ActivityType::Eat,      "home",   0});
    s.addEntry({8.0f,  12.0f, ActivityType::Work,     "market",  0});
    s.addEntry({12.0f, 13.0f, ActivityType::Eat,      "tavern",  0});
    s.addEntry({13.0f, 18.0f, ActivityType::Work,     "market",  0});
    s.addEntry({18.0f, 19.0f, ActivityType::Eat,      "tavern",  0});
    s.addEntry({19.0f, 22.0f, ActivityType::Socialize, "tavern", 0});
    return s;
}

Schedule Schedule::farmerSchedule() {
    Schedule s;
    s.addEntry({21.0f, 5.0f,  ActivityType::Sleep,     "home",  0});
    s.addEntry({5.0f,  6.0f,  ActivityType::Eat,       "home",  0});
    s.addEntry({6.0f,  12.0f, ActivityType::Work,      "farm",  0});
    s.addEntry({12.0f, 13.0f, ActivityType::Eat,       "home",  0});
    s.addEntry({13.0f, 18.0f, ActivityType::Work,      "farm",  0});
    s.addEntry({18.0f, 19.0f, ActivityType::Eat,       "tavern",0});
    s.addEntry({19.0f, 21.0f, ActivityType::Socialize, "tavern",0});
    return s;
}

Schedule Schedule::innkeeperSchedule() {
    Schedule s;
    s.addEntry({1.0f,  7.0f,  ActivityType::Sleep,     "home",   0});
    s.addEntry({7.0f,  8.0f,  ActivityType::Eat,       "tavern", 0});
    s.addEntry({8.0f,  12.0f, ActivityType::Work,      "tavern", 0});
    s.addEntry({12.0f, 13.0f, ActivityType::Eat,       "tavern", 0});
    s.addEntry({13.0f, 22.0f, ActivityType::Work,      "tavern", 0});
    s.addEntry({22.0f, 1.0f,  ActivityType::Socialize, "tavern", 0});
    return s;
}

Schedule Schedule::defaultSchedule() {
    Schedule s;
    s.addEntry({22.0f, 7.0f,  ActivityType::Sleep,     "home",  0});
    s.addEntry({7.0f,  8.0f,  ActivityType::Eat,       "home",  0});
    s.addEntry({8.0f,  12.0f, ActivityType::Wander,    "",      0});
    s.addEntry({12.0f, 13.0f, ActivityType::Eat,       "tavern",0});
    s.addEntry({13.0f, 18.0f, ActivityType::Wander,    "",      0});
    s.addEntry({18.0f, 19.0f, ActivityType::Eat,       "tavern",0});
    s.addEntry({19.0f, 22.0f, ActivityType::Socialize, "tavern",0});
    return s;
}

Schedule Schedule::forRole(const std::string& role) {
    // Case-insensitive comparison
    std::string lower = role;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "guard")      return guardSchedule();
    if (lower == "merchant")   return merchantSchedule();
    if (lower == "farmer")     return farmerSchedule();
    if (lower == "innkeeper")  return innkeeperSchedule();
    return defaultSchedule();
}

} // namespace AI
} // namespace Phyxel
