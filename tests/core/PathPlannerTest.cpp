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

// TEETH (grading does the work, not the headroom carve): take the REAL plan but flatten every cell's
// surface to the start elevation, then stamp+walk. The carved corridor still tunnels through the cliff,
// but the agent never gains elevation, so it cannot reach the high-plateau goal box. Proves the probe
// success in StraightRampMakesCliffWalkable depends on the grader's surfaceY values, not the tunnel.
TEST(PathPlannerTest, FlatStampDoesNotReachRaisedGoal) {
    const Cliff t{27, 45, 40};
    const int zc = 11;
    const glm::ivec3 start(12, t.Hlow, zc), goal(68, t.Hhigh, zc);
    const glm::ivec3 lo(0, 0, 0), hi(80, 80, 22);
    auto ground = [&](int x, int z) { return t.at(x, z); };
    PathPlan plan = planStraightRamp(ground, start, goal, kAgent);
    ASSERT_TRUE(plan.ok) << plan.reason;
    for (auto& c : plan.cells) c.surfaceY = start.y;   // strip the grading, keep the route + carve
    const Stamped s = stamp(t, plan, zc);
    TraversalProbe walk([&](int x, int y, int z) { return s.occ(x, y, z); }, kAgent);
    EXPECT_FALSE(walk.reachable(start, goal - glm::ivec3(2, 1, 2), goal + glm::ivec3(2, 1, 2), lo, hi))
        << "an ungraded (flat) path reached the raised goal — the cliff proof rests on the carve, not the grade";
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

// ============================================================================
// Terrain-following grade (3c redesign) — a settlement path must HUG the ground, not cut a straight
// line between the anchor heights and dive under the hill between them. planTerrainPath follows a
// terraced hill with only a shallow step-smoothing cut, where planStraightRamp would tunnel through it.
// ============================================================================
namespace {
// A terraced HILL rising in whole-cube (9-micro) steps to a mid peak then back down; doors sit at the
// equal-height feet. Bare terrain is impassable (9-micro risers > step-up).
struct Hill {
    int base, x0, x1;
    int at(int x, int /*z*/) const {
        if (x <= x0 || x >= x1) return base;
        const int up = (x - x0) / 9, down = (x1 - x) / 9;
        return base + 9 * std::min(up, down);
    }
};
// Cut-stamp occupancy: corridor cells (perpendicular ±hw at exact surfaceY) REPLACE the column (solid
// below S, terrain above removed); off-corridor is bare terrain.
struct HillStamp {
    Hill t; std::unordered_map<long long,int> cor;
    static long long key(int x,int z){ return (static_cast<long long>(x)<<32)^(z&0xffffffffLL); }
    void stamp(const PathPlan& p,int hw){
        const auto& cs=p.cells;
        for(size_t i=0;i<cs.size();++i){
            bool tX=false,tZ=false;
            if(i+1<cs.size()){tX|=cs[i+1].x!=cs[i].x;tZ|=cs[i+1].z!=cs[i].z;}
            if(i>0){tX|=cs[i].x!=cs[i-1].x;tZ|=cs[i].z!=cs[i-1].z;}
            if(!tX&&!tZ)tX=true;
            if(tX)for(int d=-hw;d<=hw;++d)cor[key(cs[i].x,cs[i].z+d)]=cs[i].surfaceY;
            if(tZ)for(int d=-hw;d<=hw;++d)cor[key(cs[i].x+d,cs[i].z)]=cs[i].surfaceY;
        }
    }
    bool occ(int x,int y,int z)const{ auto it=cor.find(key(x,z)); return y<(it!=cor.end()?it->second:t.at(x,z)); }
};
int maxCutVsTerrain(const PathPlan& p,const Hill& t){ int m=0; for(const auto&c:p.cells) m=std::max(m,t.at(c.x,c.z)-c.surfaceY); return m; }
}  // namespace

TEST(PathPlannerTest, TerrainPathHugsHillWhereStraightTunnels) {
    const Hill t{18, 10, 90};                       // terraced hill, peak ~+36 micro at x=50, feet at 18
    const glm::ivec3 start(10, 18, 11), goal(90, 18, 11);
    auto ground = [&](int x, int z) { return t.at(x, z); };

    const PathPlan tf = planTerrainPath(ground, start, goal, kAgent);
    ASSERT_TRUE(tf.ok) << tf.reason;
    EXPECT_LE(tf.maxRiser, kAgent.maxStepUpMicro);
    // Hugs the terrain: cut is only local step-smoothing (within ~one cube), not a tunnel.
    EXPECT_LE(tf.maxCutMicro, 9) << "terrain-following path cut too deep — not hugging the surface";

    // CONTRAST: the linear ramp dives under the hill (cut ~= the hill height) — the bug this fixes.
    const PathPlan lin = planStraightRamp(ground, start, goal, kAgent);
    ASSERT_TRUE(lin.ok) << lin.reason;
    EXPECT_GE(maxCutVsTerrain(lin, t), 30) << "linear ramp should tunnel through the hill (the contrast)";
    EXPECT_LT(tf.maxCutMicro, maxCutVsTerrain(lin, t) / 2) << "terrain-following must cut far less than linear";

    // TEETH: bare terraced hill blocks travel (9-micro risers); the graded path makes it walkable.
    const glm::ivec3 lo(0,0,0), hi(100,80,30);
    TraversalProbe bare([&](int x,int y,int z){ return y < t.at(x,z); }, kAgent);
    ASSERT_FALSE(bare.reachable(start, goal-glm::ivec3(2,1,2), goal+glm::ivec3(2,1,2), lo, hi))
        << "bare terraced hill must block the far foot (else no teeth)";
    HillStamp w{t,{}}; w.stamp(tf, kAgent.halfWidthMicro);
    TraversalProbe walk([&](int x,int y,int z){ return w.occ(x,y,z); }, kAgent);
    EXPECT_TRUE(walk.reachable(start, goal-glm::ivec3(2,1,2), goal+glm::ivec3(2,1,2), lo, hi))
        << "the terrain-following path should be walkable foot-to-foot over the hill";
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

// ============================================================================
// Switchback (3b) — when a connection is too steep for a straight run, planSwitchback folds the route
// back and forth to gain length, keeping every riser <= step-up. The character climbs it; flights are
// wall-separated so the climb genuinely uses the switchback.
// ============================================================================
namespace {
// Flat ground at terrainTop; the switchback is a raised structure (fill) above it. The planner emits
// the EXACT walkable ground (plan.surface = flight bands + flat landing squares), so the test stamps it
// verbatim — no inference. Adjacent flights (>= Wf apart in z) stay separated by their climb-difference
// (a retaining wall); flat landing squares let the footprint turn the corner.
struct SwitchWorld {
    int terrainTop;
    std::unordered_map<long long, int> surf;
    static long long key(int x, int z) { return (static_cast<long long>(x) << 32) ^ (z & 0xffffffffLL); }
    void stamp(const PathPlan& plan) {
        for (const auto& c : plan.surface) surf[key(c.x, c.z)] = c.surfaceY;
    }
    bool occ(int x, int y, int z) const {
        auto it = surf.find(key(x, z));
        return y < (it != surf.end() ? it->second : terrainTop);
    }
};
}  // namespace

// A straight run can't do it, but a switchback can — and the character walks the switchback end to end.
TEST(PathPlannerTest, SwitchbackClimbsWhereStraightCannot) {
    const int base = 9, target = 45;                   // climb 36 micro (4 cubes)
    const glm::ivec3 start(20, base, 20);
    auto ground = [&](int, int) { return base; };      // flat terrain

    // Straight run over the same horizontal room (8 micro) is too steep -> ok=false (motivates the fold).
    const PathPlan straight = planStraightRamp(ground, start, glm::ivec3(28, target, 20), kAgent);
    ASSERT_FALSE(straight.ok) << "straight run should be too steep here (else the switchback isn't needed)";

    const PathPlan plan = planSwitchback(ground, start, target, kAgent, /*flightRun=*/10, /*lateralBudget=*/20);
    ASSERT_TRUE(plan.ok) << "switchback should fit within the budget: " << plan.reason;
    EXPECT_LE(plan.maxRiser, kAgent.maxStepUpMicro);
    ASSERT_GE(plan.cells.size(), 2u);
    EXPECT_EQ(plan.cells.front().surfaceY, base);
    EXPECT_EQ(plan.cells.back().surfaceY, target);

    // The route begins/ends on flat aprons; the probe starts on the entry apron and aims for the exit one.
    const PathCell entry = plan.cells.front(), term = plan.cells.back();
    const glm::ivec3 entryFeet(entry.x, base, entry.z);
    const glm::ivec3 lo(0, 0, 0), hi(60, 80, 60);

    // TEETH: on bare flat terrain the raised terminus is unreachable (you can't climb 36 micro of air).
    SwitchWorld bareW{base, {}};
    TraversalProbe bare([&](int x, int y, int z) { return bareW.occ(x, y, z); }, kAgent);
    ASSERT_FALSE(bare.reachable(entryFeet, glm::ivec3(term.x - 2, target - 1, term.z - 2),
                               glm::ivec3(term.x + 2, target + 1, term.z + 2), lo, hi))
        << "the elevated terminus must be unreachable without the switchback";

    SwitchWorld w{base, {}};
    w.stamp(plan);
    TraversalProbe walk([&](int x, int y, int z) { return w.occ(x, y, z); }, kAgent);
    EXPECT_TRUE(walk.reachable(entryFeet, glm::ivec3(term.x - 2, target - 1, term.z - 2),
                              glm::ivec3(term.x + 2, target + 1, term.z + 2), lo, hi))
        << "the character should climb the switchback from base to the terminus";
}

// Every riser along the folded route is within the step-up (the walkability invariant), end-to-end.
TEST(PathPlannerTest, SwitchbackRisersWithinStepUp) {
    auto ground = [&](int, int) { return 9; };
    const PathPlan plan = planSwitchback(ground, glm::ivec3(20, 9, 20), 45, kAgent, 10, 20);
    ASSERT_TRUE(plan.ok) << plan.reason;
    for (size_t i = 1; i < plan.cells.size(); ++i)
        EXPECT_LE(std::abs(plan.cells[i].surfaceY - plan.cells[i - 1].surfaceY), kAgent.maxStepUpMicro)
            << "riser at route cell " << i << " exceeds step-up";
}

// TEETH (degradation): too little lateral room for the needed folds -> ok=false, not a broken path.
TEST(PathPlannerTest, SwitchbackReportsInsufficientLateralRoom) {
    auto ground = [&](int, int) { return 9; };
    // climb 36 at gradeCap 2 over flightRun 8 needs 3 flights * 5 wide = 15 lateral; budget 8 is too small.
    const PathPlan plan = planSwitchback(ground, glm::ivec3(20, 9, 20), 45, kAgent, 8, /*lateralBudget=*/8);
    EXPECT_FALSE(plan.ok) << "must report when there isn't enough lateral room for switchbacks";
}
