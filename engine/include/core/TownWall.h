#pragma once

// ============================================================================
// TownWall — the circuit wall (place_town_wall #42, CityForgePlan M7).
//
// A walled circuit is the single strongest "this is a city, not a big village"
// signal a settlement can carry, and it is a PURE planning problem: given the
// site rect and the street network, where does the wall band run, where must it
// open, and where do the towers stand.
//
// THE RULE THAT MATTERS: a street that reaches the edge MUST get a gate. Walling
// a road in is not a cosmetic defect — it severs the settlement from the world
// (and from WorldForge's inter-settlement roads). The planner refuses to emit a
// circuit it cannot gate rather than quietly strangling a street.
//
// The band sits OUTSIDE the built site (site + margin), so the wall can never
// land on a plot the burgage allocator already filled; a caller passing building
// footprints gets an explicit overlap check rather than a surprise.
//
// Coordinates are settlement-local CUBES, matching SettlementLayout's Rects.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/BuildingProgram.h"   // Rect

namespace Phyxel {
namespace Core {

/// Tier data (`walls` in settlement_program.json). enabled=false => no circuit.
struct TownWallSpec {
    bool        enabled = false;
    int         heightCubes = 7;      ///< wall height above grade
    int         thicknessCubes = 2;   ///< band thickness (>=2 gives a walkable top)
    int         gateWidthCubes = 5;   ///< minimum gate opening; widened to the street
    int         marginCubes = 2;      ///< clear gap between the built site and the band
    bool        towers = true;        ///< corner towers
    int         towerSize = 4;
    int         towerExtraHeight = 3;
    /// "round" (default) | "square". Mural towers on a curtain wall are USUALLY round after
    /// the 12th century — a drum sheds missiles and has no corner to undermine, and Conwy
    /// (this spec's own grounding) carries 21 drum towers. Square remains available for the
    /// earlier form. NB a tower HOUSE is rectangular; that is a different building.
    std::string towerShape = "round";
    /// "parapet" (default) | "conical". The two attested tops, split regionally rather than
    /// chosen by taste: English/Welsh drums take a crenellated parapet with the roof flat
    /// behind it (Conwy, Caernarfon, Beaumaris); French and German towers take the conical
    /// "pepperpot" roof (Carcassonne, the Loire châteaux). A bare cylinder is neither.
    std::string towerCap = "parapet";
    bool        crenellations = true; ///< merlon/crenel parapet on the wall top
    std::string material = "StoneBricks";
};

/// One straight stretch of the band, in cube coords. Gates are carved out of these
/// by the caller (they are reported separately so a realizer can leave them AIR).
struct WallRun {
    Rect band;             ///< the run's cube footprint (thickness x length)
    char side = 'S';       ///< 'S' -z | 'N' +z | 'W' -x | 'E' +x
};

struct WallGate {
    Rect opening;          ///< the full-thickness opening cut through the band
    char side = 'S';
    int  streetWidth = 0;  ///< the street this gate serves (0 = a spec-minimum gate)
};

struct TownWallPlan {
    bool ok = false;
    std::string refusal;          ///< why no circuit was planned (never silent)
    Rect innerBound;              ///< the clear area the band encloses (site + margin)
    Rect outerBound;              ///< the band's outer extent
    std::vector<WallRun>  runs;
    std::vector<WallGate> gates;
    std::vector<Rect>     towers; ///< corner towers (cube footprints)
};

/// Plan a circuit wall around `site` (the settlement rect, settlement-local cubes).
///
/// `streets` are the settlement's street rects: every street that reaches the site
/// edge gets a gate on that side, at least as wide as the street itself. `footprints`
/// (may be empty) are the building footprints — the band must not touch one, and if
/// it does the plan REFUSES rather than stamping a wall through a house.
///
/// Deterministic; pure. Returns ok=false with a reason when the spec is disabled,
/// the site is too small to enclose, or the band would hit a building.
TownWallPlan planTownWall(const Rect& site, const std::vector<Rect>& streets,
                          const std::vector<Rect>& footprints, const TownWallSpec& spec);

/// The cube cells a tower actually occupies inside its bounding rect — "square" fills it,
/// "round" keeps the inscribed disc. Pure and shared, so the stamper and the tests agree on
/// what round MEANS instead of each deciding separately (offsets are rect-relative).
std::vector<glm::ivec2> towerFootprintCells(const Rect& bbox, const std::string& shape);

} // namespace Core
} // namespace Phyxel
