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
    std::vector<PathCell> cells;   ///< ordered start -> goal (centre line; the walk route)
    std::vector<PathCell> surface; ///< full walkable ground (flight bands + flat landing squares); the
                                   ///< exact cells to fill. Populated for switchbacks; empty for the
                                   ///< 1-wide straight ramp (caller carves the centre line to width).
    int maxRiser = 0;              ///< largest |surface step| along the route (<= step-up when ok)
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

}  // namespace Core
}  // namespace Phyxel
