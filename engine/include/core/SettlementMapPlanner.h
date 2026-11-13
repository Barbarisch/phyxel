#pragma once

// ============================================================================
// WorldForge v2 M5a (docs/WorldForgeV2Plan.md §M5): the HEADLESS settlement planner. Given a
// site (tier + era program, size, seed), the meso heightfield and water through injected
// samplers, and the roads arriving at the site, it bakes the SettlementMap — a pure function
// of its inputs (no chunk, no world; the realizer runs it with the generator's columns, the
// tests with synthetic fields; SettlementMapTest pins that two samplers of the same field in a
// different evaluation order give the identical map).
//
// Cluster tiers (the hamlet): INTEREST-MAP GROWTH (Emilien 2012's forms; the parameters from
// the program data where grounded, REASONED where not - each labelled at its definition in
// SettlementMapPlanner.cpp): seat the lot of highest interest, connect it by a town-local path
// (the M4 cost model: slope aversion squared, water blocked, the reuse discount on cells already
// on a way), update the maps, repeat to the tier's building count. Street tiers (village, town,
// city): the main-street scenario as built (chooseStreetAxis + planMainStreetLayout /
// planCityLayout) emitted as the map, so every tier is realized from the map (D41) while the
// growth planner proper is proven on the hamlet first; M5b-e grow the street tiers.
// ============================================================================

#include <functional>
#include <string>
#include <vector>

#include "core/PathPlanner.h"
#include "core/SettlementLayout.h"
#include "core/SettlementMap.h"
#include "core/SettlementProgram.h"
#include "core/SettlementWalkability.h"
#include "core/TraversalProbe.h"

namespace Phyxel {
namespace Core {

class RoomProgramRegistry;

struct SettlementMapInput {
    const SettlementTierPreset* tier = nullptr;   ///< the scenario: the tier's program data (required)
    const RoomProgramRegistry* rooms = nullptr;   ///< natural footprints by typology (null = an 8 x 6 stand-in, tests only)
    std::string era = "medieval";
    unsigned seed = 1;
    int W = 0, D = 0;                             ///< the site rect (cubes); local origin (0,0)
    int originX = 0, originY = 16, originZ = 0;   ///< the world min corner (recorded, not used by the planner)
    /// LOCAL (x,z) -> the surface height (cubes): the sheet column headless, the ground scan in a loaded world.
    std::function<int(int, int)> heightAt;
    /// LOCAL (x,z) -> standing or channel water (M0: unbuildable; a path never crosses it).
    std::function<bool(int, int)> waterAt;
    struct Entry { int x = 0, z = 0; int cls = 1; };
    std::vector<Entry> entries;                   ///< where the arriving roads meet the site rect (LOCAL), by road class
    int maxRelief = 6;                            ///< the buildable relief cap over a lot (cubes) - analyzeSite's rule
    AgentBox agent;                               ///< the walk corridor the paths are sized for
    float reuse = 0.5f;                           ///< the M4 reuse discount on cells already on a way (routeReuseFraction)
    int lotCap = 400;                             ///< the M5 stress cap: more lots than this are refused with the reason
};

/// Bake the map. Never throws; a refused site carries `refusal` and whatever was planned before it.
SettlementMap planSettlementMap(const SettlementMapInput& in);

/// The tiers AS BUILT before the growth - the cluster tier before M5a (selectBuildablePlots on the flattest
/// 12 x 12 windows, the natural footprint centred, facing +Z, NO ways - the paths were an MST of the realized
/// doors, planned after the build) and, since M5b-1, the street tiers' main-street scenario (chooseStreetAxis +
/// planMainStreetLayout / planCityLayout emitted as the map): the CONTROLS SettlementMapTest measures the
/// growth planner against (EveryLotTouchesAWay, CityAxesFollowTerrain).
SettlementMap planSettlementMapAsBuilt(const SettlementMapInput& in);

/// L3 on the MAP before any voxel: the lots' footprints stand as solid blocks on flat ground
/// (`wallHeightCubes` tall), everything else is free, and the existing walkability gate
/// (checkRoutes, the TraversalProbe) walks from every lot's door-approach cell to the entry
/// node. Connectivity through the map's free space, not slopes (the terrain is flattened in
/// the stand-in; the post-voxel gate measures the realized ground).
WalkabilityReport mapWalkabilityStandIn(const SettlementMap& map, const AgentBox& box, int wallHeightCubes = 4);

/// The realizer's projection of the map onto the layout structs the build units consume:
/// lots -> plots (+ assigned plots with footprint, typology, street side, setback), axis-aligned
/// ways with a paved width -> street rects, the market area -> the square.
struct MapProjection {
    SettlementLayout layout;
    MainStreetLayout msl;
    bool mainStreet = false;   ///< true when the map carries a primary way (the street tiers)
    int primaryWay = -1;       ///< M5b-1: the primary way's id (msl.mainStreet is its bounding box)
    glm::ivec2 wellCell{0, 0}; ///< M5b-1: the village well's cell - the primary's middle node's verge (settlement-local)
    bool hasWell = false;
};
MapProjection projectMap(const SettlementMap& map);

/// The realizer's path network from the map's ways (cluster tiers): a graded terrain path per
/// consecutive node pair of every way (world cube cells in `wayCells`), plus a spur from each
/// REALIZED door anchor to the nearest way node. Replaces the door-to-door MST of the hamlet as
/// built, so what is paved is the map.
SettlementPaths pathsForMapWays(const std::vector<std::vector<glm::ivec2>>& wayCells, const std::vector<DoorAnchor>& doors,
                                const std::function<int(int, int)>& groundMicroAt, const AgentBox& box);

/// Testing hook: the town-local connection's slope aversion (the M4 cost model's 15); a negative value restores it.
/// SettlementMapTest.WaysAvoidTheSteepBand sets 0 as its control - the ways then climb the bench.
void setPathSlopeAversionForTesting(float aversion);

/// The door-approach cell of a lot (settlement-local cubes): one cube outside the footprint's
/// front wall at its centre, on the side the lot faces.
glm::ivec2 lotDoorApproach(const MapLot& lot);

}  // namespace Core
}  // namespace Phyxel
