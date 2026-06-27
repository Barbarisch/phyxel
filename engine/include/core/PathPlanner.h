#pragma once

// ============================================================================
// PathPlanner — place_path (#24) routing + grade/step logic (the ⚠️ engine gap).
// Grade a route between two anchors over terrain into a WALKABLE surface: the walk
// top rises at most box.maxStepUpMicro per cell, so the engine character (AgentBox
// step-up 4 micro ≈ 0.44 m) can actually walk it. A raw cube-resolution path is NOT
// walkable — 1 cube = 9 micro > the 4-micro step-up, so even gentle cube hills block
// the character — therefore the path REGRADES (cut/fill) into a gentle ramp/steps.
//
// PURE: the caller injects the micro terrain sampler so this is unit-testable against
// synthetic terrain with NO live engine; the L3 proof stamps the plan into an
// occupancy and walks it with a TraversalProbe (the honest "can the character use it").
// Units: micro = 1/9 m (1 cube = 9 micro), matching MicroCanvas + TraversalProbe.
// ============================================================================

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/TraversalProbe.h"  // AgentBox

namespace Phyxel {
namespace Core {

struct PathCell {
    int x = 0;         ///< micro coord (east)
    int z = 0;         ///< micro coord (north)
    int surfaceY = 0;  ///< walkable top (micro): solid below, head-room cleared above
};

struct PathPlan {
    bool ok = false;
    std::string reason;            ///< why it failed (e.g. "too steep for a straight run")
    std::vector<PathCell> cells;   ///< ordered start -> goal (centre line; THE walk route). The step-up
                                   ///< invariant (maxRiser) is measured along THIS, cell to cell.
    std::vector<PathCell> surface; ///< the exact ground cells to FILL (flight bands + flat landings +
                                   ///< aprons). NOT uniformly step-bounded: adjacent cells of different
                                   ///< flights differ by a flight's climb — intentional RETAINING WALLS
                                   ///< the character never steps across (it detours via the landings).
                                   ///< Populated for switchbacks; empty for the 1-wide straight ramp
                                   ///< (caller carves the centre line to width).
    int maxRiser = 0;              ///< largest |step| along the ROUTE (`cells`) (<= step-up when ok)
};

/// Grade a STRAIGHT run (4-connected micro line) from `startMicro` to `goalMicro` over terrain into a
/// walkable ramp: each consecutive cell's surface rises by <= box.maxStepUpMicro. The anchors keep
/// their own surface Y (the building/door grade); intermediate cells are regraded (cut/fill) so the
/// elevation change is spread evenly at <= step-up per cell. Feasible iff the run is long enough to
/// absorb the change ((cells-1)*maxStepUp >= |Δelev|); else ok=false with a reason (needs switchbacks).
/// `groundMicroAt(x,z)` = terrain top in micro (informational; the straight ramp connects the anchor
/// elevations). Deterministic.
PathPlan planStraightRamp(const std::function<int(int, int)>& groundMicroAt,
                          glm::ivec3 startMicro, glm::ivec3 goalMicro, const AgentBox& box);

/// SWITCHBACK climb: when a connection is too steep for a straight run, fold the route back and forth
/// (flights stacked along +Z, each running along X) to gain enough length that every riser stays
/// <= box.maxStepUpMicro. Climbs from `startMicro` to elevation `targetSurfaceY` (the route only needs
/// the target HEIGHT and sign — its horizontal TERMINUS is `plan.cells.back()`, which the caller
/// connects onward; this decouples the fold geometry from the goal's exact XZ). Flights are
/// (2*halfWidth+1) wide so the character footprint fits within one flight; adjacent flights differ by
/// a flight's climb (a retaining wall) so the only way up is the switchback. `flightRunMicro` = the
/// horizontal run per flight (along X); needs `numFlights*(2*halfWidth+1) <= lateralBudgetMicro` or
/// ok=false (not enough lateral room — graceful degradation). Deterministic.
PathPlan planSwitchback(const std::function<int(int, int)>& groundMicroAt,
                        glm::ivec3 startMicro, int targetSurfaceY, const AgentBox& box,
                        int flightRunMicro, int lateralBudgetMicro);

// --- Settlement path network (3c) -------------------------------------------------------------------

struct DoorAnchor {
    int x = 0;         ///< micro coord of the building's threshold
    int z = 0;
    int surfaceY = 0;  ///< ground height (micro) at the threshold (where the path must meet the door)
};

struct SettlementPaths {
    std::vector<PathPlan> paths;                      ///< a graded ramp per CONNECTED edge
    int edges = 0;                                    ///< edges attempted (a spanning tree -> doors-1)
    int connected = 0;                                ///< edges successfully graded (paths.size())
    std::vector<std::pair<int, int>> failedEdges;     ///< door index pairs too steep for a straight ramp
};

/// Connect a settlement's building doors into a walkable network: a minimum spanning tree over the
/// doors (by horizontal distance), each edge graded into a walkable ramp over the terrain via
/// planStraightRamp. On real CUBE-resolution terrain adjacent columns differ by >= a cube (9 micro) >
/// the step-up, so doors at different heights are NOT walkable between on bare ground — the graded
/// ramps cut/fill the connection down to <= step-up risers. Edges too steep for a straight ramp are
/// reported in failedEdges (a switchback-routing follow-up), not silently dropped. Deterministic.
SettlementPaths planSettlementPaths(const std::vector<DoorAnchor>& doors,
                                    const std::function<int(int, int)>& groundMicroAt,
                                    const AgentBox& box);

}  // namespace Core
}  // namespace Phyxel
