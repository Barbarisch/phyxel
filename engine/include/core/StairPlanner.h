#pragma once

// ============================================================================
// StairPlanner — the ONE source of truth for stair geometry (Structure Gen v2).
//
// A stair must be physically CLIMBABLE, not merely present. The planner turns a
// stair well (footprint + floor-to-floor rise) into a concrete set of solid
// micro-blocks (treads + landings) with a guaranteed walkable riser, and reports
// whether it actually fits. Both the realizer (to BUILD) and the validator (to
// GATE) call this, so the gate measures exactly what gets built (no drift). See
// docs/BuildKnownIssues.md KI-4.
//
// Forms:
//   * Switchback — two half-flights + a mid-landing, 180° turn. Compact square
//     shaft, repeats cleanly up a tower; the turn + landing break the vertical
//     stack so floors don't fill each other's headroom. Default for buildings.
//   * Straight   — a single flight along the longer axis. Walkable per-flight if
//     the run is long enough, but stacked straight wells form a solid headroom-less
//     column (the KI-4 failure) — the validator flags that separately.
//
// Grounding: riser ≤ the character's step-up (caller passes maxStepMicro = the
// engine character's m_maxStepHeight); the planner prefers the comfort riser
// (IRC R311.7.5.1, ~0.2 m) and only steepens (up to the cap) to make a flight fit.
// Units: micro = 1/9 m; cubes for the well footprint.
// ============================================================================

#include <string>
#include <vector>

namespace Phyxel {
namespace Core {

enum class StairForm { Straight, Switchback };

StairForm   stairFormFromString(const std::string& s);   ///< unknown -> Switchback
std::string stairFormToString(StairForm f);

/// A solid block to fill, in the well's LOCAL micro frame: x in [0, wellW*9),
/// z in [0, wellD*9), and solid from y=base (the lower walkable) up by height h.
struct StairSolid { int x, y, z, w, h, d; };

struct StairPlan {
    std::vector<StairSolid> solids;   ///< treads + landings (fill with floor material)
    int  holeX = 0, holeZ = 0;        ///< XZ extent (local micro) to cut in the UPPER
    int  holeW = 0, holeD = 0;        ///< floor slab so the flight emerges
    int  maxRiserMicro = 0;           ///< worst riser across flights (gate: ≤ maxStepMicro)
    int  topMicro = 0;                ///< height the flight reaches (== riseMicro when ok)
    bool ok = false;                  ///< fits the well AND every riser ≤ maxStepMicro
    std::string error;                ///< why it doesn't fit (when !ok)
};

/// Plan a stair filling a wellW×wellD (cubes) well rising riseMicro (floor-to-floor),
/// keeping risers ≤ maxStepMicro. Geometry is local to the well's lower-walkable origin.
StairPlan planStair(int wellW, int wellD, int riseMicro, StairForm form, int maxStepMicro);

/// Worst-case head clearance (micro) a character has when emerging off the LOWER flight onto the
/// shared intermediate floor, given the UPPER flight stacked above it (offset up by lower.topMicro,
/// and by upperDx/upperDz micro in the well plane). Scans the combined plan solids for footholds at
/// the intermediate floor and measures the open air above each. A solid-pillar stack returns ~0; a
/// thin-tread stack returns the full inter-floor gap. This is the REAL clearance the validator gates
/// on — geometry-driven, not a form label. Returns >= charHeightMicro means clear.
int stackedEmergenceClearance(const StairPlan& lower, const StairPlan& upper,
                              int upperDxMicro, int upperDzMicro,
                              int wellWcubes, int wellDcubes, int charHeightMicro);

}  // namespace Core
}  // namespace Phyxel
