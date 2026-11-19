#include <cstdio>
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

// O39 (pm_chain495.txt; the audit's condition): a WAY's grade - at most `risePerCell` micro of rise per 9 micro columns
// (one cell), so the walker graph (which links adjacent CELLS up to its 4-micro riser) can climb it; and, when the two
// ends lie further apart in height than the leg is long, a CUT to that grade instead of a refusal.
TEST(PathPlannerTest, AWaysGradeRisesAtMostItsRiserPerCell) {
    const auto worstPerCell = [](const PathPlan& p) {
        int worst = 0;
        for (size_t i = 0; i + 9 < p.cells.size(); ++i) worst = std::max(worst, std::abs(p.cells[i + 9].surfaceY - p.cells[i].surfaceY));
        return worst;
    };
    // (1) a one-cube fall in the ground half way along a 90-column leg (the creek city's south street)
    const auto step = [](int x, int) { return x < 45 ? 343 : 334; };
    const glm::ivec3 a(0, 343, 4), b(89, 334, 4);
    const PathPlan steep = planTerrainPath(step, a, b, kAgent);           // the CONTROL: the cap as before (2 micro a column)
    const PathPlan way = planTerrainPath(step, a, b, kAgent, 4);
    ASSERT_TRUE(steep.ok); ASSERT_TRUE(way.ok);
    std::printf("[ O39 ] a one-cube fall: the old cap's worst rise over 9 columns %d micro; the way's grade %d; the way's deepest cut %d%s", worstPerCell(steep), worstPerCell(way), way.maxCutMicro, "\n");
    EXPECT_GT(worstPerCell(steep), 4) << "the control does not show the defect: the rig measures nothing";
    EXPECT_LE(worstPerCell(way), 4) << "a way rises more than the walker graph's riser inside one cell";
    EXPECT_EQ(way.cells.front().surfaceY, 343); EXPECT_EQ(way.cells.back().surfaceY, 334);
    EXPECT_TRUE(way.reason.empty()) << "a leg long enough for its fall was cut to grade: " << way.reason;
    // (amended before chain 507, pm_chain495.txt: I asserted "never filled above the ground" and it is false - on the
    // tube path the ramp comes down AFTER the fall, on fill. That is the planner's existing behaviour at any cap; the
    // deepest fill is printed, and bounded here by the fall itself.)
    int deepestFill = 0;
    for (const auto& c : way.cells) deepestFill = std::max(deepestFill, c.surfaceY - step(c.x, c.z));
    std::printf("[ O39 ] a one-cube fall: the way's deepest FILL above the ground %d micro%s", deepestFill, "\n");
    EXPECT_LE(deepestFill, 9) << "the way stands above its ground by more than the fall it ramps";
    // (2) ends further apart than the leg allows: 18 micro over 18 columns (the grade allows 8) - a CUT, not a refusal
    const auto bank = [](int x, int) { return x < 9 ? 334 : 352; };
    const glm::ivec3 lo(0, 334, 4), hi(17, 352, 4);
    const PathPlan cut = planTerrainPath(bank, lo, hi, kAgent, 4);
    ASSERT_TRUE(cut.ok) << "a way leg too steep end to end is refused (it was then left unpaved): " << cut.reason;
    EXPECT_FALSE(cut.reason.empty()) << "the cut to grade is not said";
    EXPECT_EQ(cut.cells.front().surfaceY, 334) << "the LOW end moved";
    EXPECT_LE(worstPerCell(cut), 4);
    for (size_t i = 1; i < cut.cells.size(); ++i) EXPECT_LE(std::abs(cut.cells[i].surfaceY - cut.cells[i - 1].surfaceY), 1) << "a riser inside the cut at column " << i;
    std::printf("[ O39 ] ends 18 micro apart over 18 columns: high end cut from 352 to %d (deepest cut %d micro), reason '%s'%s", cut.cells.back().surfaceY, cut.maxCutMicro, cut.reason.c_str(), "\n");
    // (amended before chain 508: the deepest cut is at the bank's FOOT, column 9 - 18 micro - not at the end, 14; I had
    // asserted they were equal)
    EXPECT_GE(cut.maxCutMicro, 352 - cut.cells.back().surfaceY);
    EXPECT_LE(cut.maxCutMicro, 18) << "a cut deeper than the bank is high";
    EXPECT_GE(cut.maxCutMicro, 18 - 8) << "the high end was not cut down to the grade";
    // the old cap on the same ends REFUSES (the control for the branch): 18 micro over 17 steps at 2 a column is reachable,
    // so use ends the old cap cannot reach either: 60 micro over 18 columns
    const auto cliff = [](int x, int) { return x < 9 ? 334 : 394; };
    EXPECT_FALSE(planTerrainPath(cliff, glm::ivec3(0, 334, 4), glm::ivec3(17, 394, 4), kAgent).ok) << "the old path does not refuse a cliff: the control is void";
    EXPECT_TRUE(planTerrainPath(cliff, glm::ivec3(0, 334, 4), glm::ivec3(17, 394, 4), kAgent, 4).ok);
}
