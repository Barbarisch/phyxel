#include <gtest/gtest.h>

#include <set>

#include "core/TownWall.h"

using namespace Phyxel::Core;

// ============================================================================
// Circuit wall (place_town_wall #42, CityForgePlan M7). The invariants that
// make a wall a wall rather than decoration:
//   * the band CLOSES — no accidental gap you can walk through
//   * every street reaching the edge gets a gate at least as wide as itself
//     (walling a road in severs the settlement; the planner must never do it
//     silently)
//   * the band never lands on a building the allocator already placed
// RED baseline: planTownWall returns ok=false / "not implemented".
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

/// The site the city planner would hand us, with a main street spanning it E-W
/// and a cross street spanning it N-S (the crossroads form).
Rect site() { return Rect{0, 0, 120, 90}; }
std::vector<Rect> crossroadStreets() {
    return {Rect{0, 42, 120, 7},    // main street, full width, along X
            Rect{56, 0, 7, 90}};    // cross street, full depth, along Z
}

bool overlaps(const Rect& a, const Rect& b) {
    return a.x < b.x1() && b.x < a.x1() && a.z < b.z1() && b.z < a.z1();
}

/// Every cube cell covered by the plan's runs, minus its gates.
std::set<std::pair<int, int>> wallCells(const TownWallPlan& p) {
    std::set<std::pair<int, int>> cells;
    for (const auto& r : p.runs)
        for (int x = r.band.x; x < r.band.x1(); ++x)
            for (int z = r.band.z; z < r.band.z1(); ++z) cells.insert({x, z});
    for (const auto& g : p.gates)
        for (int x = g.opening.x; x < g.opening.x1(); ++x)
            for (int z = g.opening.z; z < g.opening.z1(); ++z) cells.erase({x, z});
    return cells;
}

}  // namespace

TEST(TownWallTest, TheCircuitClosesAroundTheSite) {
    const auto p = planTownWall(site(), crossroadStreets(), {}, citySpec());
    ASSERT_TRUE(p.ok) << p.refusal;

    // The band encloses the site with the spec's margin, and is thickness deep.
    EXPECT_EQ(p.innerBound.x, site().x - 2);
    EXPECT_EQ(p.innerBound.w, site().w + 4);
    EXPECT_EQ(p.outerBound.x, p.innerBound.x - 2);
    EXPECT_EQ(p.outerBound.w, p.innerBound.w + 4);

    // CLOSURE: walk the outer ring; every cell is wall or gate — no third option.
    const auto cells = wallCells(p);
    std::set<std::pair<int, int>> gateCells;
    for (const auto& g : p.gates)
        for (int x = g.opening.x; x < g.opening.x1(); ++x)
            for (int z = g.opening.z; z < g.opening.z1(); ++z) gateCells.insert({x, z});

    const Rect& o = p.outerBound;
    int holes = 0;
    for (int x = o.x; x < o.x1(); ++x)
        for (int z = o.z; z < o.z1(); ++z) {
            const bool onRing = x < o.x + 2 || x >= o.x1() - 2 || z < o.z + 2 || z >= o.z1() - 2;
            if (!onRing) continue;                       // interior, not part of the band
            if (!cells.count({x, z}) && !gateCells.count({x, z})) ++holes;
        }
    EXPECT_EQ(holes, 0) << holes << " cells of the circuit are neither wall nor gate";
}

TEST(TownWallTest, EveryStreetReachingTheEdgeGetsAGateWideEnoughForIt) {
    const auto streets = crossroadStreets();
    const auto p = planTownWall(site(), streets, {}, citySpec());
    ASSERT_TRUE(p.ok) << p.refusal;

    // The crossroads form reaches all four sides -> four gates.
    EXPECT_EQ(p.gates.size(), 4u) << "a crossroads city needs a gate on every side";
    std::set<char> sides;
    for (const auto& g : p.gates) {
        sides.insert(g.side);
        const int w = (g.side == 'N' || g.side == 'S') ? g.opening.w : g.opening.d;
        EXPECT_GE(w, 5) << "gate on side " << g.side << " is narrower than the spec minimum";
        EXPECT_GE(w, g.streetWidth) << "gate on side " << g.side << " pinches its own street";
    }
    EXPECT_EQ(sides.size(), 4u);

    // Each gate must actually LINE UP with its street, not merely exist.
    for (const auto& g : p.gates) {
        bool aligned = false;
        for (const auto& s : streets) {
            const bool alongX = s.w >= s.d;
            if ((g.side == 'N' || g.side == 'S') && !alongX &&
                g.opening.x <= s.x && g.opening.x1() >= s.x1()) aligned = true;
            if ((g.side == 'E' || g.side == 'W') && alongX &&
                g.opening.z <= s.z && g.opening.z1() >= s.z1()) aligned = true;
        }
        EXPECT_TRUE(aligned) << "gate on side " << g.side << " does not span its street";
    }
}

TEST(TownWallTest, AGatelessCircuitIsRefusedNotSilentlyBuilt) {
    // A street reaching the edge that the planner cannot gate (gate wider than the
    // side allows) must REFUSE — never strangle the road.
    TownWallSpec s = citySpec();
    s.gateWidthCubes = 400;                       // impossible on a 120x90 site
    const auto p = planTownWall(site(), crossroadStreets(), {}, s);
    EXPECT_FALSE(p.ok);
    EXPECT_FALSE(p.refusal.empty()) << "refused without saying why";
}

TEST(TownWallTest, TheBandNeverLandsOnABuilding) {
    // A building sitting where the band would run must be caught, not paved over.
    const Rect intruder{-3, 40, 6, 8};            // straddles the west band
    const auto bad = planTownWall(site(), crossroadStreets(), {intruder}, citySpec());
    EXPECT_FALSE(bad.ok) << "the wall would have been stamped through a building";

    // Buildings inside the site are fine — the band sits outside them by construction.
    const Rect inside{10, 10, 8, 6};
    const auto good = planTownWall(site(), crossroadStreets(), {inside}, citySpec());
    ASSERT_TRUE(good.ok) << good.refusal;
    for (const auto& r : good.runs)
        EXPECT_FALSE(overlaps(r.band, inside)) << "band overlaps an interior building";
}

TEST(TownWallTest, CornerTowersStandAtTheCorners) {
    const auto p = planTownWall(site(), crossroadStreets(), {}, citySpec());
    ASSERT_TRUE(p.ok) << p.refusal;
    ASSERT_EQ(p.towers.size(), 4u) << "a circuit wants a tower at each corner";
    const Rect& o = p.outerBound;
    for (const auto& t : p.towers) {
        const bool atX = (t.x == o.x) || (t.x1() == o.x1());
        const bool atZ = (t.z == o.z) || (t.z1() == o.z1());
        EXPECT_TRUE(atX && atZ) << "tower at (" << t.x << "," << t.z << ") is not on a corner";
    }
    // Towers must not block a gate.
    for (const auto& t : p.towers)
        for (const auto& g : p.gates)
            EXPECT_FALSE(overlaps(t, g.opening)) << "a tower was planted in a gateway";
}

TEST(TownWallTest, DisabledSpecPlansNothingAndSaysSo) {
    TownWallSpec off;                              // enabled = false
    const auto p = planTownWall(site(), crossroadStreets(), {}, off);
    EXPECT_FALSE(p.ok);
    EXPECT_TRUE(p.runs.empty());
    EXPECT_FALSE(p.refusal.empty());
}

TEST(TownWallTest, DeterministicForTheSameSite) {
    const auto a = planTownWall(site(), crossroadStreets(), {}, citySpec());
    const auto b = planTownWall(site(), crossroadStreets(), {}, citySpec());
    ASSERT_EQ(a.runs.size(), b.runs.size());
    ASSERT_EQ(a.gates.size(), b.gates.size());
    for (size_t i = 0; i < a.gates.size(); ++i) {
        EXPECT_EQ(a.gates[i].side, b.gates[i].side);
        EXPECT_EQ(a.gates[i].opening.x, b.gates[i].opening.x);
        EXPECT_EQ(a.gates[i].opening.z, b.gates[i].opening.z);
    }
}
