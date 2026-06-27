#include "core/PathPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace Phyxel {
namespace Core {

namespace {
// Grade an ORDERED route of cells (already 4-connected) so the surface rises evenly from startY at
// cells.front() to goalY at cells.back(), every riser <= maxStep. Spreads the +/-1 remainder evenly
// (Bresenham) so unit risers are scattered, never bunched. Returns false (and leaves the plan not-ok)
// if the route is too short to absorb the change at <= maxStep. Fills surfaceY + maxRiser.
bool gradeRoute(std::vector<PathCell>& cells, int startY, int goalY, int maxStep, int& maxRiserOut) {
    const int N = static_cast<int>(cells.size());
    const int n = N - 1;  // risers
    const long delta = static_cast<long>(goalY) - startY;
    if (N < 2 || std::llabs(delta) > static_cast<long>(n) * maxStep) return false;

    const int base = static_cast<int>(delta / n);
    const int rem = static_cast<int>(delta - static_cast<long>(base) * n);
    const int extra = (rem > 0) - (rem < 0);
    const int adjRem = std::abs(rem);
    int cum = startY, acc = 0;
    maxRiserOut = 0;
    cells[0].surfaceY = startY;
    for (int i = 1; i < N; ++i) {
        int step = base;
        acc += adjRem;
        if (acc >= n) { acc -= n; step += extra; }
        cum += step;
        cells[i].surfaceY = cum;
        maxRiserOut = std::max(maxRiserOut, std::abs(step));
    }
    cells[N - 1].surfaceY = goalY;  // guard exact arrival
    return true;
}
}  // namespace

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

    // Grade cap < the bare step-up: the footprint samples corners at +/-halfWidth, so to step on/off the
    // ramp from flat ground the cell halfWidth ahead must be within the step-up -> grade <= step-up/half-
    // width. (A grade-up-to-step-up ramp clears the per-step rule but the footprint can't sit on it.)
    const int gradeCap = std::max(1, box.maxStepUpMicro / std::max(1, box.halfWidthMicro));
    if (!gradeRoute(p.cells, startMicro.y, goalMicro.y, gradeCap, p.maxRiser)) {
        p.ok = false;
        p.reason = "too steep for a straight run (needs switchbacks)";
        p.cells.clear();
        return p;
    }
    p.ok = true;
    return p;
}

PathPlan planSwitchback(const std::function<int(int, int)>& /*groundMicroAt*/,
                        glm::ivec3 startMicro, int targetSurfaceY, const AgentBox& box,
                        int flightRunMicro, int lateralBudgetMicro) {
    PathPlan p;
    const int hw = box.halfWidthMicro;
    const int Wf = 2 * hw + 1;                    // flight width so the footprint fits within one flight
    const int R = flightRunMicro;                 // horizontal run per flight (cells along X)
    const int apron = hw + 1;                     // flat tread (>= footprint deep) at both ends, so the
                                                  // character stands flush before/after the climb
    const long dy = static_cast<long>(targetSurfaceY) - startMicro.y;
    // Grade cap < the bare step-up (see planStraightRamp): a flight must be enterable from a flat
    // landing/apron, so the cell halfWidth ahead is within the step-up -> grade <= step-up/halfWidth.
    const int gradeCap = std::max(1, box.maxStepUpMicro / std::max(1, hw));

    if (R < 1 || Wf < 1) { p.reason = "degenerate flight geometry"; return p; }
    const long perFlight = static_cast<long>(R) * gradeCap;   // most a flight can climb at the grade cap
    const int numFlights = perFlight > 0 ? static_cast<int>((std::llabs(dy) + perFlight - 1) / perFlight) : 0;
    const int flights = std::max(1, numFlights);
    const int lateralNeeded = flights * Wf;
    if (lateralNeeded > lateralBudgetMicro) {
        p.reason = "not enough lateral room for switchbacks";
        return p;
    }

    // Per-flight boundary heights, the climb spread evenly across the flights (each <= R*gradeCap).
    std::vector<int> yb(flights + 1);
    for (int i = 0; i <= flights; ++i)
        yb[i] = startMicro.y + static_cast<int>(std::llround(static_cast<double>(i) * dy / flights));
    yb[flights] = targetSurfaceY;

    const int x0 = startMicro.x, z0 = startMicro.z;
    auto key = [](int x, int z) { return (static_cast<long long>(x) << 32) ^ (z & 0xffffffffLL); };
    std::unordered_map<long long, int> ground;
    auto band = [&](int x, int zc, int h) { for (int z = zc - hw; z <= zc + hw; ++z) ground[key(x, z)] = h; };

    // Start apron: a flat run at yb[0] in band 0, leading INTO flight 0 (so the route begins on flush
    // flat ground the character can settle on).
    for (int k = apron; k >= 1; --k) { p.cells.push_back({x0 - k, z0, yb[0]}); band(x0 - k, z0, yb[0]); }

    for (int i = 0; i < flights; ++i) {
        const int zc = z0 + i * Wf;
        const bool fwd = (i % 2 == 0);
        const int xFrom = fwd ? x0 : x0 + R;
        const int xTo   = fwd ? x0 + R : x0;   // turn end (reaches yb[i+1])
        const int dir   = fwd ? 1 : -1;

        std::vector<PathCell> flight;          // centre-line, graded yb[i] -> yb[i+1] at <= gradeCap
        for (int x = xFrom; x != xTo + dir; x += dir) flight.push_back({x, zc, 0});
        int fr = 0;
        if (!gradeRoute(flight, yb[i], yb[i + 1], gradeCap, fr)) {
            p.reason = "switchback flight too short for its climb";  // shouldn't happen given `flights`
            return p;
        }
        p.maxRiser = std::max(p.maxRiser, fr);
        for (const auto& c : flight) { p.cells.push_back(c); band(c.x, c.z, c.surfaceY); }  // flat-width tread

        // Landing platform at the turn: flat at yb[i+1], placed BEYOND the flight end (outward in `dir`)
        // so it never overwrites the still-climbing flight, and spanning band i -> band i+1 in z so the
        // footprint turns the corner on level ground. The next flight starts back at xTo in band i+1
        // (both at yb[i+1] -> flush). The climb-difference between non-adjacent flights stays a wall.
        if (i < flights - 1) {
            const int zNext = z0 + (i + 1) * Wf;
            for (int k = 1; k <= apron; ++k) {
                const int lx = xTo + dir * k;
                for (int z = zc - hw; z <= zNext + hw; ++z) ground[key(lx, z)] = yb[i + 1];
            }
            // route markers across the flat turn (surfaceY continuity for the riser invariant)
            for (int z = zc + 1; z <= zNext; ++z) p.cells.push_back({xTo + dir, z, yb[i + 1]});
        }
    }

    // End apron: flat run at the target, leading OUT of the last flight (outward beyond its turn end).
    {
        const bool lastFwd = ((flights - 1) % 2 == 0);
        const int exitX = lastFwd ? x0 + R : x0, edir = lastFwd ? 1 : -1;
        const int zcLast = z0 + (flights - 1) * Wf;
        for (int k = 1; k <= apron; ++k) {
            p.cells.push_back({exitX + edir * k, zcLast, targetSurfaceY});
            band(exitX + edir * k, zcLast, targetSurfaceY);
        }
    }

    p.surface.reserve(ground.size());
    for (const auto& kv : ground)
        p.surface.push_back({static_cast<int>(kv.first >> 32),
                             static_cast<int>(static_cast<int32_t>(kv.first & 0xffffffffLL)), kv.second});
    p.ok = true;
    return p;
}

}  // namespace Core
}  // namespace Phyxel
