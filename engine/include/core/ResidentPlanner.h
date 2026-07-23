#pragma once

// ============================================================================
// ResidentPlanner — playable-town increment 4: derive the NPC residents of a
// settlement from its registered building locations (the markers every v2
// build now registers — see StructureRealizer::deriveLocations).
//
// One resident per Home/Work/Tavern location. Medieval pattern: tradesfolk
// LIVE at their workplace (shop-house), so every resident's home is their own
// building. Daily loop (hours are a DESIGN DECISION for a legible day cycle,
// not sourced): everyone sleeps at home 20-6; trades work their own shop by
// day; everyone but the innkeeper socializes at the tavern 17-20.
//
// Pure (no engine deps) — unit-testable. The settlement build spawns from
// these plans; nothing here touches the world.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ai/Schedule.h"
#include "core/LocationRegistry.h"

namespace Phyxel {
namespace Core {

struct ResidentPlan {
    std::string name;            ///< deterministic, unique: "res_" + home location id
    std::string role;            ///< appearance seed + flavor (villager/blacksmith/innkeeper/...)
    std::string homeLocationId;
    glm::vec3   spawnPos{0.0f};  ///< the home location anchor (outdoor, ground level)
    AI::Schedule schedule;
};

class ResidentPlanner {
public:
    /// Plan one resident per Home/Work/Tavern location (Custom/other types get none).
    /// The first Tavern location (by id order, deterministic) is everyone's evening
    /// social target; with no tavern, evenings are spent at home.
    static std::vector<ResidentPlan> planResidents(const std::vector<Location>& locations);

    /// The occupation for a building location (by its name, which carries the
    /// typology): tavern->innkeeper, blacksmith->blacksmith, ..., dwelling->villager.
    static std::string roleForLocation(const Location& loc);
};

} // namespace Core
} // namespace Phyxel
