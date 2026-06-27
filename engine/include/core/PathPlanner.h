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
    std::vector<PathCell> cells;   ///< ordered start -> goal (centre line)
    int maxRiser = 0;              ///< largest |surface step| along the path (<= step-up when ok)
};

/// Grade a STRAIGHT run (4-connected micro line) from `startMicro` to `goalMicro` over terrain into a
/// walkable ramp: each consecutive cell's surface rises by <= box.maxStepUpMicro. The anchors keep
/// their own surface Y (the building/door grade); intermediate cells are regraded (cut/fill) so the
/// elevation change is spread evenly at <= step-up per cell. Feasible iff the run is long enough to
/// absorb the change ((cells-1)*maxStepUp >= |Δelev|); else ok=false with a reason (needs switchbacks
/// — a later increment). `groundMicroAt(x,z)` = terrain top in micro (informational; the straight ramp
/// connects the anchor elevations). Deterministic.
PathPlan planStraightRamp(const std::function<int(int, int)>& groundMicroAt,
                          glm::ivec3 startMicro, glm::ivec3 goalMicro, const AgentBox& box);

}  // namespace Core
}  // namespace Phyxel
