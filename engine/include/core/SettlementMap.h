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

#include <functional>
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
    // M5b-1a (2026-11-13, sequencing amendment 1): the lot's APPROACH as map data - what the planner, the forge and
    // the realizer each inferred on their own until M5b-1.
    bool hasDoor = false;            ///< the planner predicted the front door (false: no room program / no exterior door)
    int doorX = 0, doorZ = 0;        ///< the WALL cell the door is cut through (settlement-local cubes, inside the footprint)
    int doorNX = 0, doorNZ = 0;      ///< its outward normal (a unit axis); the approach runs door + normal * k
    std::string style;               ///< the building style the lot is built in (structure_styles.json key; "" = the realizer draws it)
    // increment C: the SEAT - a lot whose door meets a paved way is built at the way's level (padCubeForStreet)
    bool hasSeat = false;            ///< false: the door meets no paving within reach - seated on the natural ground by the realizer
    int wayLevel = 0;                ///< the way's STANDING level at the door (world micro: the paving's surface + 1)
    int padY = 0;                    ///< the pad's top cube (world y) the forge levels the footprint to
    int threshold = 0;               ///< the ground floor's standing level (world micro) = padY * 9 + the style's offset
    bool hasTerrace = false;         ///< the parcel is graded to one level before anything is built on it (terrain mode)
    int terraceY = 0;                ///< that level: the top cube (world y) - the median of the parcel's ground when its turn came
    // increment D: the door's APPROACH - the cells door + normal * k (k = 1..n) and the STANDING level each offers once
    // the doorsteps are laid (planDoorstepLevels from the threshold to the way). The last cell is the first PAVED one
    // (the way - the anchor: its own level, or one sill on its edge). Empty: no seat, or no paved cell within 6 cells.
    struct ApproachCell { int x = 0, z = 0, level = 0; };   ///< settlement-local cubes; world micro
    bool hasApproach = false;
    std::vector<ApproachCell> approach;
    // M5b-2 (D44 a; D50-D52 PROPOSED): the PARCEL - the conquered subset of `rect`, as settlement-local cube rects
    // whose union is the mask. `rect` stays the nominal toft and the parcel's bound (the growth blocks it). Empty =
    // the rect (a record written before M5b-2).
    std::vector<Rect> parcel;
    bool inParcel(int x, int z) const;   ///< settlement-local cubes
    int parcelArea() const;              ///< cells in the mask
    /// The parcel as ONE rect (D51 as revised): the single rect of `parcel`; the nominal toft when the record
    /// carries none or more than one. The planner's terrace, projectMap and through it the realizer all read THIS.
    Rect parcelRect() const;
};

/// M5b-2 (the audit of 7cbdf70b, claim 5; closes O21 d): the terrace of one parcel - THE rule, called by the planner
/// (over its own ground) and by the realizer's "terracing parcel" unit (over the chunks' terrain top). The parcel is
/// graded to the MEDIAN of its ground; a skirt of `skirt` cells ramps one cube a cell back to the ground beyond.
/// `cells` holds every cell of the parcel grown by the skirt with its target top cube (x outer, z inner), changed
/// or not. `groundAt` is read for every parcel cell (the median) and every skirt cell, never written. ok = false:
/// a parcel thinner than 2 cells, or no sampler. Frame: whatever `parcel` and `groundAt` share.
struct ParcelTerrace {
    bool ok = false;
    int grade = 0;
    struct Cell { int x = 0, z = 0, target = 0; };
    std::vector<Cell> cells;
};
ParcelTerrace planParcelTerrace(const Rect& parcel, int skirt, const std::function<int(int, int)>& groundAt);

struct MapArea {
    int id = 0;
    std::string landuse;   ///< "market" | "square" | "farmland" | "graveyard" | "croft" (M5c-2)
    Rect rect;
    int lot = -1;          ///< M5c-2: the lot a croft belongs to (-1 = none)
};

/// M5d-1 (D59-D61 PROPOSED): the WALL as map data - one closed ring of cube cells (the band's centreline, settlement-
/// local, 4-connected, the last cell adjacent to the first; it MAY leave the site rect), the gates where another way
/// crosses it, and the stretches left open over water. `present` false with a `refusal` = the tier asks for a wall
/// and none could be planned. JSON: the ring is written as its CORNERS (`nodes`) and expanded on read, so the ring
/// always starts at its first corner.
struct MapWall {
    struct Pt { int x = 0, z = 0; };
    struct Gate { int x = 0, z = 0; int way = -1; int width = 0; };   ///< the ring cell the way crosses at; the opening's width (cubes)
    struct Gap { int x = 0, z = 0; int cells = 0; };                  ///< a run of ring cells in water: its first cell (ring order), its length
    /// M5d-3 (D64 as refined, PROPOSED): a mural tower at one of the wall's ROUTED corners. One entry per corner;
    /// `built` false = not built, and `skipped` says why. The drum's cells are towerFootprintCells(bbox(), shape).
    struct Tower {
        int x = 0, z = 0;              ///< the drum's centre cell (the corner, or pushed outward from it)
        int cornerX = 0, cornerZ = 0;  ///< the routed corner's ring cell - inside the drum
        int size = 0;                  ///< the drum's bounding square (cubes): the tier's walls.tower_size
        char doorSide = 'S';           ///< TowerForge's door side: N = +z, S = -z, E = +x, W = -x
        int doorX = 0, doorZ = 0;      ///< the doorway's rim cell
        int outX = 0, outZ = 0;        ///< the cell outside the doorway, inside the ring: where the tower is entered from
        bool built = false;
        std::string skipped;
        Rect bbox() const { return Rect{x - size / 2, z - size / 2, size, size}; }
    };
    bool present = false;
    std::string refusal;
    int margin = 0;                 ///< the tier's walls.margin the route kept from every lot (cubes)
    std::vector<Pt> ring;
    std::vector<Gate> gates;
    std::vector<Gap> waterGaps;
    std::vector<Tower> towers;      ///< M5d-3: one entry per routed corner (empty when the tier's walls.towers is false)
    float bakeMs = 0.0f;            ///< planWall's own wall time (diagnostic; never written to JSON, never hashed)
    std::vector<Pt> corners() const;                               ///< the ring's turning cells, in ring order
    static std::vector<Pt> expand(const std::vector<Pt>& corners); ///< the ring through those corners (axis-aligned legs)
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
    MapWall wall;              ///< M5d-1: the circuit wall (absent on a tier without `walls`)
    /// D69 (c1): where a way crosses fordable water - the way, its centre-line cells in the water (in the way's
    /// order) and the kind ("ford"). Written to JSON only when there is one.
    struct Crossing { int way = -1; std::string kind = "ford"; std::vector<std::pair<int, int>> cells; };
    std::vector<Crossing> crossings;
    int entryNode = -1;        ///< the node where the first arriving road meets the site (-1 = none)
    float bakeMs = 0.0f;       ///< the planner's wall time (diagnostic, never hashed)
    std::string refusal;       ///< non-empty = the planner refused the site, with the reason
    std::string fordNote;      ///< O38 (e): non-empty = fordable creek cells were not forded because a ford's ramp does not fit there
    std::string marketNote;    ///< M5e-1: non-empty = the tier asks for a market and the map carries none, or moved it - why
    int marketLane = 0;        ///< M5e-1 (D67 PROPOSED): the through lane a way keeps clear across the market (cubes; the tier's lane width); 0 = the way's full width
    int droppedLots = 0;       ///< lots dropped for unbuildable ground (surfaced, like droppedPlots)
    float buildableFraction = -1.0f;   ///< the site analysis' buildable fraction (diagnostic; -1 = not analysed) - a refusal reports HOW unbuildable the site was
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
