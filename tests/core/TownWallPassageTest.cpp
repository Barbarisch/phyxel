#include <gtest/gtest.h>

#include <functional>
#include <set>
#include <tuple>

#include "core/TowerForge.h"
#include "core/TownWall.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// A GATE IS A HOLE YOU CAN WALK THROUGH — or it is a decorative notch.
//
// The tower shipped as a solid drum because it was checked geometrically ("is
// there a cone on top?") instead of functionally ("can an agent climb it?").
// The circuit wall was checked exactly the same way: gate width, gate/street
// alignment, no tower planted in a gateway. Nobody ever walked through one.
//
// This is the missing half. The wall is rasterized the way SettlementBuildService
// stamps it — solid columns from grade, gate columns starting spec.gateClearCubes
// up so the wall bridges the road — and a TraversalProbe walks the engine's own
// agent from OUTSIDE the circuit to INSIDE it. Control: seal the gates and the
// same walk must fail.
//
// The clear height is spec data (TownWallSpec::gateClearCubes), not a constant
// living privately in the stamper, precisely so this test measures the opening
// the settlement actually builds.
//
// NOTE ON PROBE BOUNDS: a whole city circuit is ~90 million micro cells, which a
// flood fill will not finish. Every walk below is bounded to a tight corridor
// around the feature under test. That is not a shortcut — a passage through THIS
// gate is exactly the claim — but it does mean the bounds must always leave the
// wall itself the only thing between start and goal.
// ============================================================================

namespace {

TownWallSpec citySpec() {
    TownWallSpec s;
    s.enabled = true;
    s.heightCubes = 7;
    s.thicknessCubes = 2;
    s.gateWidthCubes = 5;
    s.marginCubes = 2;
    s.towers = true;
    s.towerSize = 4;
    return s;
}

Rect site() { return Rect{0, 0, 120, 90}; }
std::vector<Rect> crossroadStreets() {
    return {Rect{0, 42, 120, 7},    // main street, full width, along X
            Rect{56, 0, 7, 90}};    // cross street, full depth, along Z
}

constexpr int kMicro = 9;
constexpr int kGradeCube = 1;       ///< flat test terrain: solid below cube y=1

/// The circuit as MICRO occupancy, matching the stamper: every band cell is a solid
/// column of cubes from grade to grade+height; a gate cell's column starts
/// gateClearCubes higher, leaving the passage open beneath it.
struct WalledWorld {
    std::set<std::pair<int, int>> band, gates;
    int height = 0, clearCubes = 0;
    bool sealGates = false;         ///< control: brick the gateways up

    WalledWorld(const TownWallPlan& p, const TownWallSpec& spec, bool seal = false)
        : height(spec.heightCubes), clearCubes(spec.gateClearCubes), sealGates(seal) {
        for (const auto& r : p.runs)
            for (int x = r.band.x; x < r.band.x1(); ++x)
                for (int z = r.band.z; z < r.band.z1(); ++z) band.insert({x, z});
        for (const auto& g : p.gates)
            for (int x = g.opening.x; x < g.opening.x1(); ++x)
                for (int z = g.opening.z; z < g.opening.z1(); ++z) gates.insert({x, z});
        // Towers are solid mass here — they must not be the thing that lets the agent
        // through, and treating them as solid is the conservative choice.
        for (const auto& t : p.towers)
            for (const auto& c : towerFootprintCells(t, spec.towerShape))
                band.insert({t.x + c.x, t.z + c.y});
    }

    bool solidAt(int mx, int my, int mz) const {
        const int cx = (mx >= 0 ? mx : mx - kMicro + 1) / kMicro;
        const int cz = (mz >= 0 ? mz : mz - kMicro + 1) / kMicro;
        const int cy = (my >= 0 ? my : my - kMicro + 1) / kMicro;
        if (cy < kGradeCube) return true;                      // the ground
        const bool isGate = gates.count({cx, cz}) > 0;
        if (isGate && !sealGates) {
            const int y0 = kGradeCube + clearCubes;
            return cy >= y0 && cy < kGradeCube + height;        // the lintel overhead
        }
        if (!band.count({cx, cz}) && !isGate) return false;
        return cy < kGradeCube + height;
    }

    std::function<bool(int, int, int)> occ() const {
        return [this](int x, int y, int z) { return solidAt(x, y, z); };
    }
};

/// Feet-level micro pose at the centre of cube (cx,cz), standing on grade.
glm::ivec3 standing(int cx, int cz) {
    return {cx * kMicro + 4, kGradeCube * kMicro, cz * kMicro + 4};
}

/// A corridor through one gate: outside -> band -> inside, bounded tight enough to
/// flood. `out` is the start cube, `in` the goal cube, both on the gate centreline.
struct GateCorridor {
    glm::ivec3 start, goal, lo, hi;
};

GateCorridor corridorThrough(const WallGate& g, const Rect& outer, int wallHeight) {
    const int cx = (g.opening.x + g.opening.x1() - 1) / 2;
    const int cz = (g.opening.z + g.opening.z1() - 1) / 2;
    int outX = cx, outZ = cz, inX = cx, inZ = cz;
    switch (g.side) {
        case 'S': outZ = outer.z - 3;      inZ = outer.z + 6;      break;
        case 'N': outZ = outer.z1() + 2;   inZ = outer.z1() - 7;   break;
        case 'W': outX = outer.x - 3;      inX = outer.x + 6;      break;
        default:  outX = outer.x1() + 2;   inX = outer.x1() - 7;   break;
    }
    GateCorridor c;
    c.start = standing(outX, outZ);
    c.goal  = standing(inX, inZ);
    const int loX = std::min(outX, inX) - 4, hiX = std::max(outX, inX) + 5;
    const int loZ = std::min(outZ, inZ) - 4, hiZ = std::max(outZ, inZ) + 5;
    c.lo = {loX * kMicro, 0, loZ * kMicro};
    c.hi = {hiX * kMicro, (kGradeCube + wallHeight + 2) * kMicro, hiZ * kMicro};
    return c;
}

}  // namespace

// Calibration first: the probe must agree that this flat world is walkable at all,
// and that a solid band cell is not. Without this a "cannot pass" result could mean
// the harness is wrong rather than the wall.
TEST(TownWallPassageTest, ProbeCalibrationOnTheTestTerrain) {
    const auto p = planTownWall(site(), crossroadStreets(), {}, citySpec());
    ASSERT_TRUE(p.ok) << p.refusal;
    const WalledWorld w(p, citySpec());
    TraversalProbe probe(w.occ(), AgentBox{});

    const auto open = standing(60, 45);                        // middle of the site
    EXPECT_TRUE(probe.fits(open.x, open.y, open.z)) << "the agent cannot stand on open ground";
    EXPECT_TRUE(probe.supported(open.x, open.y, open.z));

    // A wall cell away from any gate must be solid where the agent's body would go.
    const Rect& o = p.outerBound;
    int solidProbes = 0;
    for (int z = o.z + 6; z < o.z1() - 6 && solidProbes == 0; ++z)
        if (w.band.count({o.x, z}) && !w.gates.count({o.x, z})) {
            const auto in = standing(o.x, z);
            EXPECT_FALSE(probe.fits(in.x, in.y, in.z))
                << "a curtain-wall cell is not solid — the wall is not a wall";
            ++solidProbes;
        }
    EXPECT_EQ(solidProbes, 1);
}

// THE ACCEPTANCE TEST: walk from open country, through a gate, into the town.
TEST(TownWallPassageTest, AnAgentWalksThroughEveryGateIntoTheTown) {
    const auto spec = citySpec();
    const auto p = planTownWall(site(), crossroadStreets(), {}, spec);
    ASSERT_TRUE(p.ok) << p.refusal;
    ASSERT_FALSE(p.gates.empty());

    const WalledWorld w(p, spec);
    TraversalProbe probe(w.occ(), AgentBox{});

    for (const auto& g : p.gates) {
        const auto c = corridorThrough(g, p.outerBound, spec.heightCubes);
        ASSERT_TRUE(probe.fits(c.start.x, c.start.y, c.start.z))
            << "no standing room outside the " << g.side << " gate";
        ASSERT_TRUE(probe.fits(c.goal.x, c.goal.y, c.goal.z))
            << "no standing room inside the " << g.side << " gate";

        EXPECT_TRUE(probe.reachable(c.start, c.goal - glm::ivec3(3, 0, 3),
                                    c.goal + glm::ivec3(3, 4, 3), c.lo, c.hi))
            << "an agent cannot walk through the " << g.side
            << " gate into the town — the gateway is a decorative notch, not a passage";
    }
}

// Control: brick up the gateways and the SAME walk must fail on every side. Without
// this the result above could be passing because the agent slipped around the wall
// inside the corridor bounds rather than through the gate.
TEST(TownWallPassageTest, TheProofFailsWhenTheGatesAreSealed) {
    const auto spec = citySpec();
    const auto p = planTownWall(site(), crossroadStreets(), {}, spec);
    ASSERT_TRUE(p.ok) << p.refusal;

    const WalledWorld w(p, spec, /*sealGates=*/true);
    TraversalProbe probe(w.occ(), AgentBox{});

    for (const auto& g : p.gates) {
        const auto c = corridorThrough(g, p.outerBound, spec.heightCubes);
        EXPECT_FALSE(probe.reachable(c.start, c.goal - glm::ivec3(3, 0, 3),
                                     c.goal + glm::ivec3(3, 4, 3), c.lo, c.hi))
            << "the agent got through a SEALED " << g.side << " gate — the passage test "
               "proves nothing, and the wall has a hole somewhere other than its gates";
    }
}

// The clear height is a contract with the agent, not a magic number in the stamper.
TEST(TownWallPassageTest, TheGateLintelClearsTheAgentsHeight) {
    const TownWallSpec spec = citySpec();
    const AgentBox box;
    EXPECT_GE(spec.gateClearCubes * kMicro, box.heightMicro)
        << "the default gate is lower than the agent is tall";

    // And it is honoured: with a one-cube lintel the walk must fail, which is what
    // proves the passage above depends on this number rather than ignoring it.
    TownWallSpec squat = spec;
    squat.gateClearCubes = 1;                                   // 9 micro < 16
    const auto p = planTownWall(site(), crossroadStreets(), {}, squat);
    ASSERT_TRUE(p.ok) << p.refusal;
    const WalledWorld w(p, squat);
    TraversalProbe probe(w.occ(), AgentBox{});
    const auto c = corridorThrough(p.gates.front(), p.outerBound, squat.heightCubes);
    EXPECT_FALSE(probe.reachable(c.start, c.goal - glm::ivec3(3, 0, 3),
                                 c.goal + glm::ivec3(3, 4, 3), c.lo, c.hi))
        << "the agent ducked under a 9-micro lintel — the probe is not enforcing headroom";
}

// INTEGRATION — the corner tower must be ENTERABLE FROM THE TOWN.
//
// TowerForgeTest proves the tower is climbable in an empty box; TownWallTest proves
// the circuit closes. Neither sees the seam between them, which is where the tower's
// doorway could open straight into the curtain wall it stands on. This walks one
// agent the whole way: town ground -> tower doorway -> top chamber.
TEST(TownWallPassageTest, TheCornerTowerIsEnteredFromInsideTheTown) {
    TownWallSpec spec = citySpec();
    spec.towerSize = 9;                     // the live city value; 4 is below planTower's floor
    const auto p = planTownWall(site(), crossroadStreets(), {}, spec);
    ASSERT_TRUE(p.ok) << p.refusal;
    ASSERT_FALSE(p.towers.empty());

    // The south-west tower, doored the way SettlementBuildService doors it: facing in.
    const Rect tw = p.towers.front();
    TowerSpec ts;
    ts.shape = spec.towerShape;
    ts.heightCubes = spec.heightCubes + spec.towerExtraHeight;
    ts.storeyCubes = 3;
    ts.arrowLoops = true;
    ts.doorSide = (tw.z <= p.outerBound.z + 1) ? 'N' : 'S';
    const TowerPlan tp = planTower(tw, ts);
    ASSERT_TRUE(tp.ok) << "the live city's tower size cannot be made usable: " << tp.refusal;

    // Curtain + towers, but THIS tower rasterized from its real plan instead of solid.
    WalledWorld w(p, spec);
    for (const auto& c : towerFootprintCells(tw, spec.towerShape))
        w.band.erase({tw.x + c.x, tw.z + c.y});
    std::set<std::tuple<int, int, int>> towerSolid;
    const int baseM = kGradeCube * kMicro;
    auto fill = [&](int cx, int cz, int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int mx = 0; mx < kMicro; ++mx)
                for (int mz = 0; mz < kMicro; ++mz)
                    towerSolid.insert({(tw.x + cx) * kMicro + mx, baseM + y,
                                       (tw.z + cz) * kMicro + mz});
    };
    for (const auto& r : tp.walls) fill(r.cx, r.cz, r.fromMicroY, r.toMicroY);
    for (const auto& pl : tp.plates) fill(pl.cx, pl.cz, pl.yMicro, pl.yMicro + pl.thicknessMicro);

    auto occ = [&w, &towerSolid](int x, int y, int z) {
        return w.solidAt(x, y, z) || towerSolid.count({x, y, z}) > 0;
    };
    TraversalProbe probe(occ, AgentBox{});

    // Bound the flood to the tower and a few cubes of town around it.
    const glm::ivec3 lo{(tw.x - 4) * kMicro, 0, (tw.z - 4) * kMicro};
    const glm::ivec3 hi{(tw.x1() + 4) * kMicro, (kGradeCube + ts.heightCubes + 2) * kMicro,
                        (tw.z1() + 4) * kMicro};
    // A townsman on open ground just inside the circuit, off the tower's north face.
    const glm::ivec3 start = standing((tw.x + tw.x1()) / 2, tw.z1() + 2);
    ASSERT_TRUE(probe.fits(start.x, start.y, start.z)) << "no standing room beside the tower";

    const glm::ivec3 doorFeet{tw.x * kMicro + tp.doorFeetMicro.x, baseM + tp.doorFeetMicro.y,
                              tw.z * kMicro + tp.doorFeetMicro.z};
    EXPECT_TRUE(probe.reachable(start, doorFeet - glm::ivec3(4, 2, 4),
                                doorFeet + glm::ivec3(4, 6, 4), lo, hi))
        << "the corner tower's doorway cannot be reached from inside the town — the stair, "
           "the floors and the arrow loops are all sealed behind a door nobody can walk to";

    const glm::ivec3 topFeet{tw.x * kMicro + tp.topFeetMicro.x, baseM + tp.topFeetMicro.y,
                             tw.z * kMicro + tp.topFeetMicro.z};
    EXPECT_TRUE(probe.reachable(start, topFeet - glm::ivec3(5, 3, 5),
                                topFeet + glm::ivec3(5, 6, 5), lo, hi))
        << "a townsman cannot climb the corner tower from the street";
}

// AUDIT (2026-08-28) — the wall-walk. Merlons and crenels are stamped along the top
// course, which is only worth anything if a defender can STAND behind them. Nothing in
// the circuit plan provides a stair, ramp or tower doorway onto the walk, so this
// measures the honest current state: the parapet is decoration on an unreachable
// surface. Logged in StructurePipelineGaps.md; when access ships, this expectation
// flips to EXPECT_TRUE and the gap closes.
TEST(TownWallPassageTest, AuditTheWallWalkIsCurrentlyUnreachable) {
    const auto spec = citySpec();
    const auto p = planTownWall(site(), crossroadStreets(), {}, spec);
    ASSERT_TRUE(p.ok) << p.refusal;
    const WalledWorld w(p, spec);
    TraversalProbe probe(w.occ(), AgentBox{});

    // A point on the inner course of the west run, clear of both corner towers.
    const Rect& o = p.outerBound;
    // Clear of both the corner tower and the west gate (which the cross street opens
    // at mid-depth — picking the run's midpoint lands inside the gateway).
    const int walkX = o.x + 1, walkZ = o.z + 8;
    ASSERT_TRUE(w.band.count({walkX, walkZ}));
    ASSERT_FALSE(w.gates.count({walkX, walkZ}));

    const glm::ivec3 onWalk{walkX * kMicro + 4, (kGradeCube + spec.heightCubes) * kMicro,
                            walkZ * kMicro + 4};
    EXPECT_TRUE(probe.fits(onWalk.x, onWalk.y, onWalk.z))
        << "there is not even standing room on the wall-walk";

    const glm::ivec3 start = standing(walkX + 5, walkZ);        // inside the town, at its foot
    ASSERT_TRUE(probe.fits(start.x, start.y, start.z));
    const glm::ivec3 lo{(walkX - 3) * kMicro, 0, (walkZ - 8) * kMicro};
    const glm::ivec3 hi{(walkX + 9) * kMicro, (kGradeCube + spec.heightCubes + 3) * kMicro,
                        (walkZ + 8) * kMicro};

    const bool canGetUp = probe.reachable(start, onWalk - glm::ivec3(4, 0, 6),
                                          onWalk + glm::ivec3(4, 6, 6), lo, hi);
    EXPECT_FALSE(canGetUp)
        << "the wall-walk became reachable — good news, but this audit expectation is now "
           "stale: flip it to EXPECT_TRUE and close the gap in StructurePipelineGaps.md";
}
