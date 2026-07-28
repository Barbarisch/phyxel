#include "core/SettlementWalkability.h"

#include <algorithm>
#include <climits>
#include <sstream>

namespace Phyxel {
namespace Core {

namespace {

/// Chebyshev distance in the walk plane (x/z) plus vertical separation. Used to find
/// the closest approach of the two floods; x/z dominate because the pinch we hunt is
/// a horizontal squeeze (a fence line, a wall, a plot gap), not a step.
inline int gapMicro(const glm::ivec3& a, const glm::ivec3& b) {
    return std::max({std::abs(a.x - b.x), std::abs(a.y - b.y), std::abs(a.z - b.z)});
}

}  // namespace

int freeWidthMicro(const std::function<bool(int, int, int)>& occupied, const AgentBox& box,
                   glm::ivec3 at, char axis, int maxScanMicro) {
    TraversalProbe probe(occupied, box);
    // The measurement is only meaningful standing somewhere the agent can actually be.
    if (!probe.fits(at.x, at.y, at.z) || !probe.supported(at.x, at.y, at.z)) return 0;

    const int dx = (axis == 'x' || axis == 'X') ? 1 : 0;
    const int dz = dx ? 0 : 1;

    // Walk out both ways from `at` until the box stops fitting/being supported. The
    // agent's own footprint is already accounted for by fits(), so the count of
    // accepting CENTRE columns understates the physical gap by the box width -- we
    // report the free span (centres + the box), which is what "corridor width" means.
    int span = 1;
    for (int s = -1; s <= 1; s += 2) {
        for (int k = 1; k <= maxScanMicro; ++k) {
            const int x = at.x + dx * s * k, z = at.z + dz * s * k;
            if (!probe.fits(x, at.y, z) || !probe.supported(x, at.y, z)) break;
            ++span;
        }
    }
    return span + 2 * box.halfWidthMicro;
}

WalkabilityReport checkRoutes(const std::function<bool(int, int, int)>& occupied,
                              const AgentBox& box, const std::vector<WalkRoute>& routes,
                              glm::ivec3 boundLo, glm::ivec3 boundHi, bool diagnose) {
    WalkabilityReport rep;
    TraversalProbe probe(occupied, box);

    for (const auto& r : routes) {
        RouteResult res;
        res.label = r.label;

        const glm::ivec3 goalLo(r.to.x - r.goalRadiusMicro, r.to.y - r.goalHeightMicro,
                                r.to.z - r.goalRadiusMicro);
        const glm::ivec3 goalHi(r.to.x + r.goalRadiusMicro, r.to.y + r.goalHeightMicro,
                                r.to.z + r.goalRadiusMicro);

        res.walkable = probe.reachable(r.from, goalLo, goalHi, boundLo, boundHi);
        if (res.walkable) {
            ++rep.walkable;
            rep.routes.push_back(res);
            continue;
        }

        // ---- FAILED: locate the pinch --------------------------------------------
        // Flooding from both ends is what makes this a diagnosis instead of a verdict.
        // Distinguish "the endpoints are bad probe input" from "the town is broken"
        // FIRST -- otherwise an unsupported start reports as a walkability defect and
        // the validator sends us chasing a generator bug that isn't there. This part is
        // cheap, so it runs even when `diagnose` is off.
        res.startUnsupported = (probe.settle(r.from.x, r.from.y, r.from.z, boundLo.y) == INT_MIN);
        res.goalUnsupported = (probe.settle(r.to.x, r.to.y, r.to.z, boundLo.y) == INT_MIN);

        if (!diagnose) {
            ++rep.blocked;
            rep.routes.push_back(res);
            continue;
        }

        const auto fromSet = probe.flood(r.from, boundLo, boundHi);
        const auto goalSet = probe.flood(r.to, boundLo, boundHi);
        res.reachedFromStart = static_cast<int>(fromSet.size());
        res.reachedFromGoal = static_cast<int>(goalSet.size());

        if (!fromSet.empty() && !goalSet.empty()) {
            // Closest approach of the two sets. O(n*m) is fine at settlement scale for a
            // FAILING route (this runs only on failure) and keeps the answer exact --
            // an approximate pinch would point at the wrong placer.
            int best = INT_MAX;
            for (const auto& a : fromSet) {
                for (const auto& b : goalSet) {
                    const int g = gapMicro(a, b);
                    if (g < best) {
                        best = g;
                        res.pinchFrom = a;
                        res.pinchTo = b;
                        if (best <= 1) break;   // adjacent: can't do better
                    }
                }
                if (best <= 1) break;
            }
            res.pinchGapMicro = best;
        }

        ++rep.blocked;
        rep.routes.push_back(res);
    }
    return rep;
}

std::string WalkabilityReport::summary() const {
    std::ostringstream os;
    os << walkable << " walkable / " << blocked << " BLOCKED";
    for (const auto& r : routes) {
        if (r.walkable) continue;
        os << "\n  BLOCKED " << r.label;
        if (r.startUnsupported) os << " [start does not settle -- bad probe input, not a town defect]";
        if (r.goalUnsupported) os << " [goal does not settle -- bad probe input, not a town defect]";
        os << "\n    reachable set: " << r.reachedFromStart << " from start, " << r.reachedFromGoal
           << " from goal";
        if (r.pinchGapMicro >= 0) {
            os << "\n    pinch: gap " << r.pinchGapMicro << " micro between ("
               << r.pinchFrom.x << "," << r.pinchFrom.y << "," << r.pinchFrom.z << ") and ("
               << r.pinchTo.x << "," << r.pinchTo.y << "," << r.pinchTo.z << ")"
               << "  [cube (" << (r.pinchFrom.x / 9) << "," << (r.pinchFrom.y / 9) << ","
               << (r.pinchFrom.z / 9) << ")]";
        }
    }
    return os.str();
}

}  // namespace Core
}  // namespace Phyxel
