#include <gtest/gtest.h>

#include <unordered_map>

#include "core/PathPlanner.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// PathPlanner L3 — a graded path between two anchors over terrain must be WALKABLE
// by the engine character, not merely "drawn". A raw cube step (9 micro) exceeds the
// 4-micro step-up, so a cliff is impassable on bare terrain (the teeth). planStraightRamp
// must regrade the run into <= 4-micro risers; a TraversalProbe then walks it end-to-end.
// ============================================================================

namespace {
const AgentBox kAgent{2, 16, 4};  // halfWidth 2, height 16, step-up 4 — the engine character

// A full-width cliff: terrain top is Hlow for x < cliffX, Hhigh for x >= cliffX (all z).
struct Cliff {
    int Hlow, Hhigh, cliffX;
    int at(int x, int /*z*/) const { return x < cliffX ? Hlow : Hhigh; }
};

// Bare-terrain occupancy: solid below the terrain top.
bool bareOcc(const Cliff& t, int x, int y, int z) { return y < t.at(x, z); }

// Path-stamped occupancy: along the carved corridor (path z +/- halfWidth+1), the ramp surface
// OVERRIDES terrain — solid below surfaceY (fill), head-room cleared above (cut through the cliff).
// Off the corridor, bare terrain.
struct Stamped {
    Cliff t;
    std::unordered_map<int, int> xToSurf;  // path centre x -> surfaceY
    int zc, halfW, clearance;
    bool occ(int x, int y, int z) const {
        auto it = xToSurf.find(x);
        if (it != xToSurf.end() && z >= zc - halfW && z <= zc + halfW) {
            const int s = it->second;
            if (y < s) return true;              // ramp body (fill)
            if (y < s + clearance) return false; // carved head-room (cut through terrain)
        }
        return y < t.at(x, z);                   // bare terrain elsewhere
    }
};

Stamped stamp(const Cliff& t, const PathPlan& plan, int zc) {
    Stamped s; s.t = t; s.zc = zc; s.halfW = kAgent.halfWidthMicro; s.clearance = kAgent.heightMicro;
    for (const auto& c : plan.cells) s.xToSurf[c.x] = c.surfaceY;
    return s;
}
}  // namespace

// TEETH + GREEN in one: on bare terrain the cliff blocks the character (step 18 > 4); after stamping
// the graded ramp, the character walks low plateau -> ramp -> high plateau.
TEST(PathPlannerTest, StraightRampMakesCliffWalkable) {
    const Cliff t{27, 45, 40};                 // 18-micro (2-cube) cliff at x=40, full width
    const int zc = 11;
    const glm::ivec3 start(12, t.Hlow, zc), goal(68, t.Hhigh, zc);
    const glm::ivec3 lo(0, 0, 0), hi(80, 80, 22);

    // RED control: bare terrain is NOT traversable across the cliff.
    TraversalProbe bare([&](int x, int y, int z) { return bareOcc(t, x, y, z); }, kAgent);
    ASSERT_FALSE(bare.reachable(start, goal - glm::ivec3(2, 1, 2), goal + glm::ivec3(2, 1, 2), lo, hi))
        << "bare cliff should block the character (this test has no teeth otherwise)";

    auto ground = [&](int x, int z) { return t.at(x, z); };
    const PathPlan plan = planStraightRamp(ground, start, goal, kAgent);
    ASSERT_TRUE(plan.ok) << "planner should grade a long-enough run: " << plan.reason;
    EXPECT_LE(plan.maxRiser, kAgent.maxStepUpMicro) << "a riser exceeds the character step-up";

    const Stamped s = stamp(t, plan, zc);
    TraversalProbe walk([&](int x, int y, int z) { return s.occ(x, y, z); }, kAgent);
    EXPECT_TRUE(walk.reachable(start, goal - glm::ivec3(2, 1, 2), goal + glm::ivec3(2, 1, 2), lo, hi))
        << "the graded ramp should be walkable end to end";
}

// Endpoints keep their anchor grade, and every riser is within the step-up (the walkability invariant).
TEST(PathPlannerTest, RampHonoursAnchorsAndStepUp) {
    const Cliff t{27, 45, 40};
    const glm::ivec3 start(12, 27, 11), goal(68, 45, 11);
    auto ground = [&](int x, int z) { return t.at(x, z); };
    const PathPlan plan = planStraightRamp(ground, start, goal, kAgent);
    ASSERT_TRUE(plan.ok) << plan.reason;
    ASSERT_GE(plan.cells.size(), 2u);
    EXPECT_EQ(plan.cells.front().surfaceY, start.y);
    EXPECT_EQ(plan.cells.back().surfaceY, goal.y);
    for (size_t i = 1; i < plan.cells.size(); ++i)
        EXPECT_LE(std::abs(plan.cells[i].surfaceY - plan.cells[i - 1].surfaceY), kAgent.maxStepUpMicro)
            << "riser at cell " << i << " exceeds step-up";
}

// TEETH: a run too SHORT to absorb the elevation at <= step-up must be reported (ok=false), not faked
// as a walkable path. 4-cell run, 18-micro rise needs >= 5 steps -> infeasible.
TEST(PathPlannerTest, TooShortRunReportsTooSteep) {
    const Cliff t{27, 45, 40};
    const glm::ivec3 start(30, 27, 11), goal(34, 45, 11);  // only 4 micro of run
    auto ground = [&](int x, int z) { return t.at(x, z); };
    const PathPlan plan = planStraightRamp(ground, start, goal, kAgent);
    EXPECT_FALSE(plan.ok) << "a too-short steep run must not claim a walkable straight path";
}
