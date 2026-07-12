#include <gtest/gtest.h>

#include <unordered_map>

#include "core/StreetPaver.h"
#include "core/TraversalProbe.h"

using namespace Phyxel::Core;

// ============================================================================
// StreetPaver — streets become REAL walkable geometry (#39 slice 2, #24 cut
// closure). L2: the plan COVERS every street column (cut cells included — the
// old ribbon stamper skipped them, leaving unwalkable terrain bulges), grading
// keeps every along-network step <= the agent's step-up, building interiors are
// never paved, spurs meet the street's surface. L3: a TraversalProbe walks the
// paved main street END TO END across a ridge that blocks the unpaved terrain.
// RED baseline: the fill/level-only stub (the old stamper's behavior) fails
// coverage + the end-to-end walk at the ridge's cut columns.
// ============================================================================

namespace {
// Cube-resolution test terrain: flat plain at cube height 16 with a 2-cube ridge crossing the
// street at cube x 10..11 (running along z). Micro top FACE = (h+1)*9.
int cubeHeight(int cx) { return (cx >= 10 && cx <= 11) ? 18 : 16; }
int groundMicro(int mx, int mz) {
    (void)mz;
    const int cx = mx >= 0 ? mx / 9 : -((-mx + 8) / 9);
    return (cubeHeight(cx) + 1) * 9;
}
long long key(int x, int z) { return (static_cast<long long>(x) << 32) ^ (z & 0xffffffffLL); }

// One street: cubes x 0..19, width 5 (z 8..12), settlement origin (0,0).
std::vector<Rect> testStreet() { return {Rect{0, 8, 20, 5}}; }

PavingPlan planIt(const std::vector<DoorAnchor>& doors = {},
                  const std::vector<glm::ivec4>& footprints = {}) {
    return planStreetPaving(testStreet(), {0, 0}, doors, footprints, groundMicro, AgentBox{},
                            "Gravel");
}

std::unordered_map<long long, PavedColumn> indexPlan(const PavingPlan& p) {
    std::unordered_map<long long, PavedColumn> m;
    for (const auto& c : p.columns) m[key(c.x, c.z)] = c;
    return m;
}
}  // namespace

// THE coverage invariant (RED on the fill/level-only stub): every micro column of the street rect
// is in the plan — including the CUT columns on the ridge. A skipped column is an unwalkable
// terrain bulge in the middle of the road.
TEST(StreetPaverTest, PlanCoversEveryStreetColumnIncludingCuts) {
    const auto plan = planIt();
    ASSERT_TRUE(plan.ok);
    const auto m = indexPlan(plan);
    int missing = 0;
    for (int mx = 0; mx < 20 * 9; ++mx)
        for (int mz = 8 * 9; mz < 13 * 9; ++mz)
            if (!m.count(key(mx, mz))) ++missing;
    EXPECT_EQ(missing, 0) << missing << " street columns unpaved (cut cells skipped?)";
    EXPECT_GT(plan.cutCols, 0) << "fixture must actually exercise cut columns (ridge)";
}

// Grading: every adjacent pair of paved columns steps <= the agent step-up (the walkability
// invariant measured on the plan; the centerline is planTerrainPath-graded, the cross-section
// LEVEL, junctions first-writer — so no seam may exceed step-up).
TEST(StreetPaverTest, AdjacentPavedColumnsStayWithinStepUp) {
    const auto plan = planIt();
    ASSERT_TRUE(plan.ok);
    const auto m = indexPlan(plan);
    const AgentBox box;
    int worst = 0;
    for (const auto& c : plan.columns)
        for (const auto& d : {std::pair{1, 0}, std::pair{0, 1}}) {
            auto it = m.find(key(c.x + d.first, c.z + d.second));
            if (it == m.end()) continue;
            worst = std::max(worst, std::abs(it->second.surface - c.surface));
        }
    EXPECT_LE(worst, box.maxStepUpMicro);
}

// V7: a building footprint's INTERIOR is never paved (perimeter/door cells are allowed).
TEST(StreetPaverTest, BuildingInteriorIsNeverPaved) {
    // building at cubes (4..9, 11..16) — overlaps the street's north edge
    const std::vector<glm::ivec4> fp = {{4, 11, 6, 6}};   // (x, z, w, d)
    const auto plan = planIt({}, fp);
    ASSERT_TRUE(plan.ok);
    for (const auto& c : plan.columns) {
        const int cbx = c.x / 9, cbz = c.z / 9;
        EXPECT_FALSE(cbx >= 5 && cbx <= 8 && cbz >= 12 && cbz <= 15)
            << "paved inside a building interior at cube (" << cbx << "," << cbz << ")";
    }
}

// A door spur is planned from the door to the nearest street column and its junction carries the
// STREET's surface (first-writer wins), so spur and street meet at one level.
TEST(StreetPaverTest, DoorSpurReachesTheStreetAtItsSurface) {
    // door 3 cubes south of the street, on flat ground
    DoorAnchor door{5 * 9 + 4, 5 * 9 + 4, (16 + 1) * 9};
    const auto plan = planIt({door});
    ASSERT_TRUE(plan.ok);
    EXPECT_EQ(plan.spursPlanned, 1);
    EXPECT_EQ(plan.spursFailed, 0);
    const auto m = indexPlan(plan);
    EXPECT_TRUE(m.count(key(door.x, door.z))) << "spur must start at the door column";
    // the junction column (clamped door onto the street rect) keeps the street's graded surface
    auto it = m.find(key(door.x, 8 * 9));
    ASSERT_NE(it, m.end());
    // street here is flat plain: surface == terrain top face
    EXPECT_EQ(it->second.surface, (16 + 1) * 9);
}

// L3 (RED on the stub): a TraversalProbe walks the paved street END TO END across the ridge. On
// bare terrain the ridge is an 18-micro wall (> 4-micro step-up); with full paving the cut caps
// ramp over it at <= step-up per cell. Occupancy: paved column = solid up to `surface` (cut
// columns lose their terrain cubes >= surface/9 first); unpaved = terrain.
TEST(StreetPaverTest, ProbeWalksThePavedStreetEndToEnd) {
    const auto plan = planIt();
    ASSERT_TRUE(plan.ok);
    const auto m = indexPlan(plan);
    auto occupied = [&](int x, int y, int z) {
        auto it = m.find(key(x, z));
        if (it != m.end()) return y <= it->second.surface;   // terrain-or-base + paving cap
        return y < groundMicro(x, z);
    };
    const AgentBox box;
    TraversalProbe probe(occupied, box);
    const int zMid = 10 * 9 + 4;                             // street centerline row
    auto feetAt = [&](int mx) {
        auto it = m.find(key(mx, zMid));
        return (it != m.end() ? it->second.surface : groundMicro(mx, zMid) - 1) + 1;
    };
    const glm::ivec3 start{4, feetAt(4), zMid};
    const int gx = 20 * 9 - 5;
    const glm::ivec3 goalLo{gx - 2, feetAt(gx) - 9, zMid - 4}, goalHi{gx + 2, feetAt(gx) + 9, zMid + 4};
    const glm::ivec3 boundLo{0, 16 * 9 - 9, 8 * 9}, boundHi{20 * 9, 20 * 9 + 18, 13 * 9};
    EXPECT_TRUE(probe.reachable(start, goalLo, goalHi, boundLo, boundHi))
        << "the paved main street must be walkable end to end (cut columns owed?)";
}

// A door AT the street edge (urban setback-0 row houses open straight onto the street) is
// trivially CONNECTED — not a failed spur. RED (live find, town tier 2026-07-09: 11/21 flush
// doors reported "too steep" on flat ground — planTerrainPath rejects the zero-length run).
TEST(StreetPaverTest, DoorOnTheStreetEdgeCountsAsConnected) {
    DoorAnchor door{10 * 9 + 4, 10 * 9 + 4, (16 + 1) * 9};   // inside the street band itself
    const auto plan = planIt({door});
    ASSERT_TRUE(plan.ok);
    EXPECT_EQ(plan.spursFailed, 0) << "a flush door must not read as a failed spur";
    EXPECT_EQ(plan.spursPlanned, 1);
}

// Degenerate inputs stay honest.
TEST(StreetPaverTest, EmptyStreetsReturnNotOk) {
    const auto plan = planStreetPaving({}, {0, 0}, {}, {}, groundMicro, AgentBox{}, "Gravel");
    EXPECT_FALSE(plan.ok);
    EXPECT_TRUE(plan.columns.empty());
}
