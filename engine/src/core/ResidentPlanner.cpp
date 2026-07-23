#include "core/ResidentPlanner.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

std::string ResidentPlanner::roleForLocation(const Location& loc) {
    // deriveLocations sets Location.name to the building typology.
    if (loc.type == LocationType::Tavern) return "innkeeper";
    if (loc.type == LocationType::Work) {
        if (loc.name == "blacksmith")    return "blacksmith";
        if (loc.name == "bakery")        return "baker";
        if (loc.name == "butcher")       return "butcher";
        if (loc.name == "apothecary")    return "apothecary";
        return "shopkeeper";             // general_store + future trades
    }
    return "villager";
}

std::vector<ResidentPlan> ResidentPlanner::planResidents(const std::vector<Location>& locations) {
    // Deterministic order regardless of caller's container iteration order.
    std::vector<Location> sorted = locations;
    std::sort(sorted.begin(), sorted.end(),
              [](const Location& a, const Location& b) { return a.id < b.id; });

    // Everyone's evening haunt: the first tavern (deterministic by id order).
    std::string tavernId;
    for (const auto& l : sorted)
        if (l.type == LocationType::Tavern) { tavernId = l.id; break; }

    std::vector<ResidentPlan> plans;
    for (const auto& loc : sorted) {
        if (loc.type != LocationType::Home && loc.type != LocationType::Work &&
            loc.type != LocationType::Tavern)
            continue;

        ResidentPlan p;
        p.name = "res_" + loc.id;
        p.role = roleForLocation(loc);
        p.homeLocationId = loc.id;
        p.spawnPos = loc.position;

        const std::string social = tavernId.empty() ? loc.id : tavernId;
        // Staggered transitions: everyone changing activity at the same instant
        // funnels the whole village through one pinch point at once (measured: an
        // 11-body jam leaving the tavern at 20:00 sharp). Jitter each boundary
        // +/-18 min deterministically per resident (hash of the home id); entries
        // share the jittered boundaries so the day still tiles without gaps.
        unsigned h = 2166136261u;
        for (char ch : loc.id) { h ^= static_cast<unsigned char>(ch); h *= 16777619u; }
        auto jit = [&h]() {
            h ^= h >> 13; h *= 2246822519u; h ^= h >> 16;
            return (static_cast<float>(h % 600) / 1000.0f) - 0.3f;   // [-0.3, +0.3) h
        };
        const float bMorning = 6.0f + jit(), bEvening = 17.0f + jit(), bNight = 20.0f + jit();
        // Night block wraps midnight — ScheduleEntry::containsHour handles it.
        p.schedule.addEntry({bNight, bMorning, AI::ActivityType::Sleep, loc.id, 1});
        if (loc.type == LocationType::Tavern) {
            // The innkeeper keeps the house open all day and evening.
            p.schedule.addEntry({bMorning, bNight, AI::ActivityType::Work, loc.id, 1});
        } else if (loc.type == LocationType::Work) {
            p.schedule.addEntry({bMorning, bEvening, AI::ActivityType::Work, loc.id, 1});
            p.schedule.addEntry({bEvening, bNight, AI::ActivityType::Socialize, social, 1});
        } else {  // Home
            p.schedule.addEntry({bMorning, bEvening, AI::ActivityType::Wander, loc.id, 1});
            p.schedule.addEntry({bEvening, bNight, AI::ActivityType::Socialize, social, 1});
        }
        plans.push_back(std::move(p));
    }
    return plans;
}

} // namespace Core
} // namespace Phyxel
