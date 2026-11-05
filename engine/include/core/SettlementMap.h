#pragma once

// ============================================================================
// WorldForge v2 M5 (docs/WorldForgeV2Plan.md §M5, M5a built 2026-11-05): the settlement
// MAP as data — an OSM-like vector record per site: nodes, ways (streets, lanes, paths),
// lots (parcel + building footprint + typology + orientation + ground) and areas. A headless
// planner (SettlementMapPlanner) bakes it from the meso heightfield and the arriving roads;
// the realizer consumes it (every tier, D41); the ledger stores it OUT of the plan hash; the
// API exports it; worldforge_minimap draws it.
//
// Units: every coordinate is a SETTLEMENT-LOCAL cube (x east, z north) from the site's
// world min corner (originX, originZ); y is the surface height in cubes (world). Widths
// in cubes. Grades as rise over run. "Unchanged": the same site, seed and samplers plan to
// the byte-identical record (SettlementMapTest.MapIsIdenticalFromPlanTwice).
// ============================================================================

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/BuildingProgram.h"   // Rect

namespace Phyxel {
namespace Core {

struct MapNode {
    int id = 0;
    int x = 0, z = 0;   ///< settlement-local cubes
    int y = 0;          ///< the surface at the node (cubes, world y), from the planner's sampler
};

struct MapWay {
    int id = 0;
    std::string highway;      ///< "primary" | "residential" | "lane" | "path"
    std::vector<int> nodes;   ///< node ids in order; a way has at least two
    float width = 0.0f;       ///< paved width (cubes); a path's is the agent corridor
    float grade = 0.0f;       ///< the steepest rise over run between consecutive nodes
    int cls = 0;              ///< the road class it carries: 0 path | 1 track | 2 road | 3 highway
};

struct MapLot {
    int id = 0;
    Rect rect;                       ///< the parcel, settlement-local cubes
    Rect footprint;                  ///< the building footprint inside it (the typology's natural size)
    std::string typology;            ///< room_program.json key
    int stories = 1;
    int orientation = 0;             ///< the door's facing: 0 = +Z ('N') | 1 = +X ('E') | 2 = -Z ('S') | 3 = -X ('W')
    std::string ground = "graded";   ///< the ground treatment (M5c rings): "graded" | "terrace" | "preserve"
    int way = -1;                    ///< the way the lot fronts (-1 = none - an invariant violation)
    int setback = 1;                 ///< the drawn front-yard depth (cubes)
    float interest = 0.0f;           ///< the planner's interest when the lot was seated (diagnostic)
};

struct MapArea {
    int id = 0;
    std::string landuse;   ///< "market" | "square" | "farmland" | "graveyard"
    Rect rect;
};

struct SettlementMap {
    std::string era, tier, morphology;
    unsigned seed = 0;
    int originX = 0, originY = 0, originZ = 0;   ///< the site's world min corner (y nominal)
    int W = 0, D = 0;                             ///< the site rect (cubes)
    std::vector<MapNode> nodes;
    std::vector<MapWay> ways;
    std::vector<MapLot> lots;
    std::vector<MapArea> areas;
    int entryNode = -1;        ///< the node where the first arriving road meets the site (-1 = none)
    float bakeMs = 0.0f;       ///< the planner's wall time (diagnostic, never hashed)
    std::string refusal;       ///< non-empty = the planner refused the site, with the reason
    int droppedLots = 0;       ///< lots dropped for unbuildable ground (surfaced, like droppedPlots)
    int targetLots = 0;        ///< the count the growth aimed at (the program's band by seed); seated < target is silent above the minimum

    bool ok() const { return refusal.empty(); }
    const MapNode* node(int id) const {
        for (const auto& n : nodes) if (n.id == id) return &n;
        return nullptr;
    }
    static char sideChar(int orientation) {
        static const char kSide[4] = {'N', 'E', 'S', 'W'};
        return kSide[((orientation % 4) + 4) % 4];
    }
    static int orientationOf(char side) {
        return side == 'N' ? 0 : side == 'E' ? 1 : side == 'S' ? 2 : 3;
    }

    nlohmann::json toJson() const;
    static SettlementMap fromJson(const nlohmann::json& j);
};

}  // namespace Core
}  // namespace Phyxel
