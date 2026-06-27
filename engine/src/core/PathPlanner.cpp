#include "core/PathPlanner.h"

#include <cmath>
#include <cstdlib>

namespace Phyxel {
namespace Core {

PathPlan planStraightRamp(const std::function<int(int, int)>& /*groundMicroAt*/,
                          glm::ivec3 startMicro, glm::ivec3 goalMicro, const AgentBox& box) {
    PathPlan p;

    // 4-connected straight run start -> goal over the micro XZ grid (a Bresenham-style staircase: one
    // unit x/z step at a time, kept as close to the straight line as possible). Diagonals are split so
    // the TraversalProbe's axis-aligned moves can follow every cell.
    const int sx = (goalMicro.x > startMicro.x) - (goalMicro.x < startMicro.x);
    const int sz = (goalMicro.z > startMicro.z) - (goalMicro.z < startMicro.z);
    const int adx = std::abs(goalMicro.x - startMicro.x);
    const int adz = std::abs(goalMicro.z - startMicro.z);

    int cx = startMicro.x, cz = startMicro.z, dox = 0, doz = 0;
    p.cells.push_back({cx, cz, 0});
    while (cx != goalMicro.x || cz != goalMicro.z) {
        bool stepX;
        if (cx == goalMicro.x)      stepX = false;
        else if (cz == goalMicro.z) stepX = true;
        else stepX = (static_cast<long>(dox + 1) * adz <= static_cast<long>(doz + 1) * adx);
        if (stepX) { cx += sx; ++dox; } else { cz += sz; ++doz; }
        p.cells.push_back({cx, cz, 0});
    }

    const int N = static_cast<int>(p.cells.size());
    const int n = N - 1;  // number of risers
    const long delta = static_cast<long>(goalMicro.y) - startMicro.y;
    const int maxStep = box.maxStepUpMicro;
    if (N < 2 || std::llabs(delta) > static_cast<long>(n) * maxStep) {
        p.ok = false;
        p.reason = "too steep for a straight run (needs switchbacks)";
        return p;
    }

    // Spread the elevation change as evenly as possible across the risers (a near-constant gentle
    // grade, not a steep ramp-then-flat): `base` per riser, plus a +/-1 remainder distributed EVENLY
    // (Bresenham) so the unit adjustments are scattered, never bunched. Each riser magnitude stays
    // <= maxStep whenever the run is long enough (the feasibility check above), so the ramp is walkable
    // AND the footprint never straddles a steep face.
    const int base = static_cast<int>(delta / n);            // truncates toward zero
    const int rem  = static_cast<int>(delta - static_cast<long>(base) * n);  // |rem| < n, sign of delta
    const int extra = (rem > 0) - (rem < 0);
    const int adjRem = std::abs(rem);
    int cum = startMicro.y, acc = 0;
    p.cells[0].surfaceY = startMicro.y;
    for (int i = 1; i < N; ++i) {
        int step = base;
        acc += adjRem;
        if (acc >= n) { acc -= n; step += extra; }  // place one adjustment every ~n/adjRem risers
        cum += step;
        p.cells[i].surfaceY = cum;
        p.maxRiser = std::max(p.maxRiser, std::abs(step));
    }
    p.cells[N - 1].surfaceY = goalMicro.y;  // guard exact arrival at the anchor grade
    p.ok = true;
    return p;
}

}  // namespace Core
}  // namespace Phyxel
