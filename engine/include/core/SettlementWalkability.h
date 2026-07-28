#pragma once

// ============================================================================
// SettlementWalkability — the settlement-scale L3 gate.
//
// A generated town must be walkable BY CONSTRUCTION: every route a resident
// actually walks (street -> their door -> their room) has to admit the character
// box on the COMPOSED settlement occupancy — buildings AND fences AND paving AND
// ground together — not merely on a plot diagram where the rectangles don't
// overlap. Runtime unstick heuristics are safety nets; their firing rate is a
// defect signal, not a fix. This is the instrument that turns that directive into
// a falsifiable measurement.
//
// The point of this module over a bare TraversalProbe call is DIAGNOSIS. A bool
// says the town is broken; a generator defect needs a coordinate. On failure we
// flood from BOTH ends and report where the two reachable sets come closest --
// the pinch -- plus the free width there, so the fix targets the placer that made
// it instead of a hypothesis.
//
// PURE: occupancy injected as a micro sampler. No ChunkManager, no engine, no
// render loop -- unit-testable headless against the same pure planners the
// build_settlement handler composes.
//
// Units: micro = 1/9 m throughout (positions are AGENT FEET, world micro).
// ============================================================================

#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/TraversalProbe.h"

namespace Phyxel {
namespace Core {

/// The minimum free width a walkable corridor must offer, in CUBES.
///
/// GROUNDED in the agent, not chosen for grid convenience: the character box is
/// `AgentBox::halfWidthMicro` = 2 micro either side of centre (0.25 m half-width,
/// matching AnimatedVoxelCharacter's m_originalHalfWidth), so the box spans 5
/// micro and bare passage needs only 5. Two cubes (18 micro) is the BUILT
/// requirement rather than the geometric minimum because settlement boundaries are
/// planned per-cube while fences/walls consume micro rows off both faces, so a
/// nominal 1-cube gap is not 9 micro of air in practice. It matches the gate width
/// the fence stamper already uses (gateW = 2 cubes).
constexpr int kMinCorridorWidthCubes = 2;
constexpr int kMinCorridorWidthMicro = kMinCorridorWidthCubes * 9;

/// One route a resident must be able to walk. `from`/`to` are agent FEET in world
/// micro; the goal is a box around `to` (a room centre is an area, not a pixel).
struct WalkRoute {
    std::string label;            ///< human-readable ("plot 7 croft: street -> interior")
    glm::ivec3  from{0};
    glm::ivec3  to{0};
    int goalRadiusMicro = 4;      ///< goal half-extent in x/z
    int goalHeightMicro = 9;      ///< goal half-extent in y (a story of slack)
};

struct RouteResult {
    std::string label;
    bool walkable = false;

    /// Diagnosis (only meaningful when !walkable):
    glm::ivec3 pinchFrom{0};      ///< closest point of the start-side reachable set
    glm::ivec3 pinchTo{0};        ///< closest point of the goal-side reachable set
    int  pinchGapMicro = -1;      ///< Chebyshev gap between them; -1 = not computed
    int  reachedFromStart = 0;    ///< flood sizes -- a tiny start set means SEALED AT THE START
    int  reachedFromGoal = 0;

    /// Input problems, reported distinctly so a bad probe setup is never mistaken
    /// for a town defect (the failure mode that makes a validator lie).
    bool startUnsupported = false;  ///< `from` doesn't settle onto anything
    bool goalUnsupported = false;   ///< `to` doesn't settle onto anything
};

struct WalkabilityReport {
    std::vector<RouteResult> routes;
    int walkable = 0;
    int blocked = 0;
    bool ok() const { return blocked == 0; }
    /// Multi-line, one entry per BLOCKED route with its pinch coords -- built for
    /// a test failure message / a build response, so a defect arrives located.
    std::string summary() const;
};

/// Walk every route on the composed occupancy. `occupied(x,y,z)` is the settlement's
/// micro solidity (buildings + fences + paving + ground). Deterministic.
///
/// `diagnose` controls the FAILURE path only: locating the pinch costs two exhaustive
/// floods per blocked route, which is the dominant cost when many routes fail. Pass
/// false when a caller only needs the verdict (e.g. a negative control that EXPECTS
/// everything blocked); leave it true whenever a failure would need investigating.
WalkabilityReport checkRoutes(const std::function<bool(int, int, int)>& occupied,
                              const AgentBox& box, const std::vector<WalkRoute>& routes,
                              glm::ivec3 boundLo, glm::ivec3 boundHi, bool diagnose = true);

/// Free width (micro) of the passage through `at`, measured ACROSS `axis` ('x' or 'z'):
/// how many consecutive micro columns centred on `at` accept the box (fits + supported).
/// This is the measurement `kMinCorridorWidthMicro` is stated in -- it turns "the corridor
/// is too narrow" into a number. Returns 0 if the box doesn't fit at `at` itself.
int freeWidthMicro(const std::function<bool(int, int, int)>& occupied, const AgentBox& box,
                   glm::ivec3 at, char axis, int maxScanMicro = 64);

}  // namespace Core
}  // namespace Phyxel
