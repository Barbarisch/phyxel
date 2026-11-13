#pragma once

// ============================================================================
// StreetPaver — street paving as REAL geometry (lay_street_network #39 slice 2 /
// place_path #24 cut-cell closure). Turn a settlement's logical street rects +
// building door anchors into a PAVING PLAN: every micro column of every street
// (and a spur from each door to the nearest street) gets a target walkable
// surface, graded so a character can walk the whole network (planTerrainPath's
// slope-limited lower envelope along the street centerline, LEVEL cross-section
// across the width — a graded road, not a terrain-hugging ribbon).
//
// CUT columns (target surface BELOW the terrain top) are INCLUDED in the plan —
// the applier removes the terrain cubes above the surface, then paves. This
// closes the honest "cut_cells_unpaved" gap: the old ribbon stamper skipped
// them, leaving unwalkable terrain bulges at ~6% of hill transitions.
//
// PURE: terrain injected as a micro sampler; unit-testable with no engine
// (StreetPaverTest L2 coverage/continuity + L3 TraversalProbe end-to-end walk).
// Units: micro = 1/9 m. Conventions match the proven build_settlement stamper:
// a column's paving occupies micro rows [terrain top face .. surface] inclusive
// (>= 1 micro thick), so the walk top is surface+1 uniformly.
// ============================================================================

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "core/BuildingProgram.h"  // Rect
#include "core/PathPlanner.h"      // DoorAnchor, AgentBox

namespace Phyxel {
namespace Core {

struct PavedColumn {
    int  x = 0;             ///< micro column coord (world, east)
    int  z = 0;             ///< micro column coord (world, north)
    int  surface = 0;       ///< target surface (micro): paving occupies [ground/base .. surface]
    bool cut = false;       ///< terrain top is ABOVE surface: remove cubes >= surface/9 first
};

struct PavingPlan {
    bool ok = false;
    std::string material = "Cobblestone";  ///< paving material (tier data; must exist in materials.json)
    std::vector<PavedColumn> columns;      ///< deduped micro columns, deterministic order
    long levelCols = 0;                    ///< surface == terrain top face (thin cap)
    long fillCols = 0;                     ///< surface above terrain (causeway fill)
    long cutCols = 0;                      ///< surface below terrain (cut, then cap)
    int  spursPlanned = 0;                 ///< door spurs graded
    int  spursFailed = 0;                  ///< door spurs too steep (surfaced, not silent)
    /// M0 row 2b: one line per failed spur - door (x,z,S0) -> street (x,z,SG), run length,
    /// drop, grade cap and the planner's reason - so a "too steep" is never a bare count.
    std::vector<std::string> spurFailures;
};

/// Plan paving for a street network. `streets` are settlement-local street rects (cubes);
/// `originCubes` = the settlement origin (ox, oz) mapping them to world; `doors` are world-micro
/// door anchors (one per building, on its street-facing wall); `buildingFootprints` are world-cube
/// building rects whose INTERIOR (inset 1 cube) must never be paved (V7 — paths route around
/// buildings, meeting the door at the perimeter). `groundMicroAt(mx, mz)` = terrain top FACE in
/// micro at a micro column. Streets are graded along their long axis on the centerline
/// (planTerrainPath) and broadcast LEVEL across the width; spurs run door -> nearest street column,
/// meeting the STREET's planned surface (not raw terrain). First writer wins at junctions (street
/// surfaces dominate spur ends). Deterministic.
PavingPlan planStreetPaving(const std::vector<Rect>& streets, glm::ivec2 originCubes,
                            const std::vector<DoorAnchor>& doors,
                            const std::vector<glm::ivec4>& buildingFootprints,  // (x, z, w, d) world cubes
                            const std::function<int(int, int)>& groundMicroAt,
                            const AgentBox& box, const std::string& material);

/// M5b-1 (the M5b design check of 2026-11-06, item 3a): ONE paver for every map way. A way is a
/// polyline of settlement cells (world cubes here) with a paved width; each consecutive pair is
/// graded along its centreline (planTerrainPath, the slope-limited lower envelope) and the surface
/// is broadcast LEVEL across the width perpendicular to travel (±width/2, the hamlet's agent-wide
/// ribbon generalized to the tier's street width). Diagonal legs are paved as a ribbon round the
/// rasterized centre line, never as an axis-aligned rect. First writer wins at junctions.
/// Deterministic. StreetPaverTest: coverage to the width, no column beyond it, step-up across
/// adjacent columns, a TraversalProbe walk end to end across a diagonal leg.
struct WayRibbon {
    std::vector<glm::ivec2> cells;   ///< the way's node cells in order (world cubes; >= 2)
    int widthCubes = 1;              ///< the paved width (cubes; the program's street / lane width)
};
PavingPlan planWayPaving(const std::vector<WayRibbon>& ways,
                         const std::function<int(int, int)>& groundMicroAt,
                         const AgentBox& box, const std::string& material);

/// M0 (docs/WorldForgeV2Plan.md §5 M0, "site prep reordered"): door spurs ONLY, planned AFTER
/// the buildings exist so each spur runs from the REALIZED door (the recorded front-door
/// marker), never from a footprint midpoint, and a refused lot gets no spur at all.
/// `streetColumns` are the street unit's stamped columns as (x, startRow, z, surface) in world
/// micro — the spur meets the nearest one AT ITS PLANNED SURFACE (first writer wins: no street
/// column is re-emitted). Doors already on a street column or within halfWidth+1 of one are
/// trivially connected (their threshold cell is paved, counted as a planned spur). Columns
/// inside a building-footprint interior are never paved. Deterministic.
PavingPlan planDoorSpurs(const std::vector<DoorAnchor>& doors,
                         const std::vector<glm::ivec4>& buildingFootprints,  // (x, z, w, d) world cubes
                         const std::vector<glm::ivec4>& streetColumns,       // (x, startRow, z, surface) micro
                         const std::function<int(int, int)>& groundMicroAt,
                         const AgentBox& box, const std::string& material);

/// The DOORSTEPS (the realizer's "doorsteps" unit, M5b-1 chain 98): a stoop of steps from a door's threshold down
/// to the ground outside it, one cell per step along the door's outward normal. `groundFeet[k]` is the standing
/// level the ground offers in the (k + 1)-th cell outside the door (INT_MIN = no ground found); returns, per cell,
/// the standing level the step fill must raise that cell to (INT_MIN = no fill). Every riser between consecutive
/// standing levels, the threshold included, must be <= `stepUpMicro`. Deterministic; pure.
std::vector<int> planDoorstepFills(int thresholdFeet, const std::vector<int>& groundFeet, int stepUpMicro);

/// M5b-1 (chains 101 / 105, DESIGN DECISION): a lot that fronts a graded way is BUILT AT THE WAY'S LEVEL at its door -
/// the SMALLEST pad cube whose threshold (pad top + `thresholdAbovePadMicro`) stands at or above the way's standing
/// level `wayFeetMicro` less one micro (T - W in [-1, 7]): a walker outside a door whose head room is the carve's
/// 18 micro cannot stand HIGHER than the threshold + 1 (the lintel), so the way is never above the threshold by more
/// than one; the descent from the threshold to the way (<= 7) is the doorsteps' (planDoorstepLevels). The ways are
/// graded (a causeway over a dip, a cutting through a rise) while the natural ground under the lot is not.
int padCubeForStreet(int wayFeetMicro, int thresholdAbovePadMicro, int stepUpMicro);

/// The DOORSTEPS as LEVELS (chain 105): the standing level each approach cell outside the door must take so the walk
/// from the threshold `thresholdFeet` reaches the first PAVED cell (the way, an anchor that is never touched) with
/// every riser <= `stepUpMicro` and no cell above the previous level (the lintel). `groundFeet[i]` is the (i + 1)-th
/// cell's standing level as found (INT_MIN = no ground), `paved[i]` whether it is a paved column. Returns per cell the
/// target standing level (INT_MIN = leave as found): below the ground it is a CUT, above it a FILL. Without a paved
/// cell in reach the descent rule of planDoorstepFills stands (fills only). If the unpaved cells cannot bring the
/// descent within a step of the way, the way's EDGE cell takes one sill (the one exception that touches paving).
std::vector<int> planDoorstepLevels(int thresholdFeet, const std::vector<int>& groundFeet, const std::vector<bool>& paved,
                                    int stepUpMicro);

/// The late STREET SWEEP's selection (M0 row "street sweep protects cells owned by placed objects", register O9): of
/// the road band's cube cells, the ones the sweep clears. Never a cell inside a building's footprint (x, z, w, d in
/// cubes, any height - chain 103: a setback-0 doorway cube), never a cell inside a placed object's cube AABB
/// (inclusive min / max - the well on the way's verge and the yard props are placed BEFORE the sweep and are voxels
/// in the same chunks). The sweep exists for construction debris; debris lying inside an object's box is kept with it.
struct SweepPlan {
    std::vector<glm::ivec3> cells;
    int keptForBuildings = 0;
    int keptForObjects = 0;
};
SweepPlan planStreetSweep(const std::vector<glm::ivec3>& band, const std::vector<glm::ivec4>& footprints,
                          const std::vector<std::pair<glm::ivec3, glm::ivec3>>& objectBoxes);

}  // namespace Core
}  // namespace Phyxel
