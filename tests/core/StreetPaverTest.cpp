#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <unordered_map>

#include <glm/glm.hpp>

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

// ── M0 ("site prep reordered"): spur-only paving from a REALIZED door ───────────────
// The street is stamped first (its columns + planned surfaces are the input); a spur planned
// afterwards meets the nearest street column AT ITS SURFACE and never re-emits a street column.
// ── M0 row 2b (m0_l4_engine.txt, town: "2 planned from 5 realized doors, 3 too steep") ──
// A terraced lot: the pad sits 3 cubes (27 micro) above the street. The straight spur's
// grade tube is cap = maxStepUp / halfWidth = 2 micro per micro cell, so the run needs
// >= 14 cells. From the cell OUTSIDE the door (z cube 6 -> 14 micro off the street edge)
// it plans; from the forge's marker one cell nearer (z cube 7 -> 5 micro) it is "too
// steep" - the M0 regression. And a failed spur must say WHY, with its numbers (RED: the
// planner counts it and says nothing).
TEST(StreetPaverTest, DoorSpurFromATerracePadIsReportedWithItsNumbers) {
    auto pad = [](int mx, int mz) {   // pad (cube 19) south of the street, plain (16) elsewhere
        (void)mx;
        const int cz = mz >= 0 ? mz / 9 : -((-mz + 8) / 9);
        return cz <= 7 ? (19 + 1) * 9 : (16 + 1) * 9;
    };
    std::vector<glm::ivec4> stamped;   // the street, already paved level at 153 (z cubes 8..12)
    for (int x = 0; x < 20 * 9; ++x)
        for (int z = 8 * 9; z < 13 * 9; ++z) stamped.push_back({x, 153, z, 153});
    const DoorAnchor outsideDoor{5 * 9 + 4, 6 * 9 + 4, pad(5 * 9 + 4, 6 * 9 + 4)};
    const auto ok = planDoorSpurs({outsideDoor}, {}, stamped, pad, AgentBox{}, "Gravel");
    EXPECT_EQ(ok.spursPlanned, 1);
    EXPECT_EQ(ok.spursFailed, 0);
    EXPECT_FALSE(ok.columns.empty());
    EXPECT_TRUE(ok.spurFailures.empty());
    const DoorAnchor marker{5 * 9 + 4, 7 * 9 + 4, pad(5 * 9 + 4, 7 * 9 + 4)};
    const auto bad = planDoorSpurs({marker}, {}, stamped, pad, AgentBox{}, "Gravel");
    EXPECT_EQ(bad.spursPlanned, 0);
    EXPECT_EQ(bad.spursFailed, 1);
    ASSERT_EQ(bad.spurFailures.size(), 1u) << "a failed spur is reported, not just counted";
    const std::string& why = bad.spurFailures[0];
    EXPECT_NE(why.find("too steep"), std::string::npos) << why;
    EXPECT_NE(why.find("drop 27"), std::string::npos) << why;   // 180 -> 153
    EXPECT_NE(why.find("6 cells"), std::string::npos) << why;   // micro z 67 -> 72
    EXPECT_NE(why.find("cap 2"), std::string::npos) << why;
}

TEST(StreetPaverTest, DoorSpursMeetTheStampedStreetAndNeverRepaveIt) {
    const auto street = planIt();   // the ridge street, no spurs
    ASSERT_TRUE(street.ok);
    std::vector<glm::ivec4> stamped;
    for (const auto& c : street.columns) stamped.push_back({c.x, c.surface, c.z, c.surface});
    const auto sidx = indexPlan(street);
    // A door 3 cubes north of the street's north edge (street z 8..12 -> edge micro row 116).
    const DoorAnchor door{5 * 9 + 4, 16 * 9 + 4, groundMicro(5 * 9 + 4, 16 * 9 + 4)};
    const auto spurs = planDoorSpurs({door}, {}, stamped, groundMicro, AgentBox{}, "Gravel");
    ASSERT_TRUE(spurs.ok);
    EXPECT_EQ(spurs.spursPlanned, 1);
    EXPECT_EQ(spurs.spursFailed, 0);
    ASSERT_FALSE(spurs.columns.empty()) << "a door 3 cubes off the street needs a spur";
    for (const auto& c : spurs.columns)
        EXPECT_FALSE(sidx.count(key(c.x, c.z))) << "street column re-emitted at " << c.x << "," << c.z;
    // The spur's last column before the street (z = 117, just north of row 116) meets the
    // street's planned surface at that x.
    bool met = false;
    for (const auto& c : spurs.columns)
        if (c.z == 13 * 9 && sidx.count(key(c.x, 13 * 9 - 1)))
            met = met || (c.surface == sidx.at(key(c.x, 13 * 9 - 1)).surface);
    EXPECT_TRUE(met) << "no spur column meets the street at its surface";
    // A door standing ON the street is trivially connected: no spur columns, still planned.
    const DoorAnchor onStreet{5 * 9 + 4, 10 * 9 + 4, groundMicro(5 * 9 + 4, 10 * 9 + 4)};
    const auto trivial = planDoorSpurs({onStreet}, {}, stamped, groundMicro, AgentBox{}, "Gravel");
    EXPECT_TRUE(trivial.ok);
    EXPECT_EQ(trivial.spursPlanned, 1);
    EXPECT_TRUE(trivial.columns.empty());
    // No street at all -> nothing to meet, not OK, nothing emitted.
    const auto none = planDoorSpurs({door}, {}, {}, groundMicro, AgentBox{}, "Gravel");
    EXPECT_FALSE(none.ok);
}

// ============================================================================
// M5b-1 (the M5b design check of 2026-11-06, red test (g)): the WAY-RIBBON paver - one paver for every map
// way at the tier's width. Rig: one way of width 5 over the ridge terrain above, cells (2,2) -> (12,2) ->
// (20,10): a straight leg across the 2-cube ridge (cut columns) and a DIAGONAL leg. Predictions in
// pm_chain88.txt. RED on the stub (0 columns, ok = false).
// ============================================================================
namespace {
std::vector<WayRibbon> testWay() { return {WayRibbon{{{2, 2}, {12, 2}, {20, 10}}, 5}}; }
// perpendicular distance (cubes) from a cell centre to the way's polyline
float distToWay(float cx, float cz) {
    float best = 1e9f;
    // chain 88: a reference into testWay()'s temporary dangled (0 cells due); the vector is held
    const std::vector<WayRibbon> ways = testWay();
    const auto& w = ways.front().cells;
    for (size_t k = 0; k + 1 < w.size(); ++k) {
        const glm::vec2 a(w[k].x + 0.5f, w[k].y + 0.5f), b(w[k + 1].x + 0.5f, w[k + 1].y + 0.5f), p(cx, cz), ab = b - a;
        const float t = glm::dot(ab, ab) > 0.0f ? std::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f) : 0.0f;
        best = std::min(best, glm::length(p - (a + ab * t)));
    }
    return best;
}
}  // namespace

// L2: every micro column of every cell within half the width of the centre line is in the plan; no
// column lies more than half the width + 1 cube away; adjacent paved columns stay within the step-up.
TEST(StreetPaverTest, WayRibbonCoversTheCentrelineToItsWidth) {
    const PavingPlan plan = planWayPaving(testWay(), groundMicro, AgentBox{}, "Gravel");
    const auto m = indexPlan(plan);
    int cellsDue = 0, cellsMissing = 0, columnsBeyond = 0, stepBreaks = 0;
    for (int cz = 0; cz < 16; ++cz)
        for (int cx = 0; cx < 24; ++cx) {
            const float d = distToWay(cx + 0.5f, cz + 0.5f);
            // inside the ribbon: the capsule's half-width is 2.5 cubes (22.5 micro); a cell whose centre is within
            // 1.5 has every corner column within 2.21 (chain 88's 2.0 left a cell's far corners at 2.71 - outside)
            if (d > 1.5f) continue;
            ++cellsDue;
            bool whole = true;
            for (int i = 0; i < 9 && whole; ++i) for (int j = 0; j < 9; ++j) if (!m.count(key(cx * 9 + i, cz * 9 + j))) { whole = false; break; }
            if (!whole) ++cellsMissing;
        }
    for (const auto& c : plan.columns) {
        if (distToWay((c.x + 0.5f) / 9.0f, (c.z + 0.5f) / 9.0f) > 3.5f) ++columnsBeyond;
        for (const auto& d : {std::pair<int, int>{1, 0}, std::pair<int, int>{0, 1}}) {
            auto it = m.find(key(c.x + d.first, c.z + d.second));
            if (it != m.end() && std::abs(it->second.surface - c.surface) > AgentBox{}.maxStepUpMicro) {
                if (++stepBreaks <= 6) std::printf("[ M5b ]   step break at (%d,%d) %d -> (%d,%d) %d\n", c.x, c.z, c.surface, it->second.x, it->second.z, it->second.surface);
            }
        }
    }
    std::printf("[ M5b ] way ribbon: ok %d, %zu columns; %d cells due, %d not wholly paved; %d columns beyond the width; %d step breaks\n",
                plan.ok ? 1 : 0, plan.columns.size(), cellsDue, cellsMissing, columnsBeyond, stepBreaks);
    EXPECT_TRUE(plan.ok);
    EXPECT_GT(cellsDue, 40) << "the rig measures nothing";
    EXPECT_EQ(cellsMissing, 0) << "a cell inside the ribbon is not wholly paved";
    EXPECT_EQ(columnsBeyond, 0) << "a paved column lies beyond the way's width";
    EXPECT_EQ(stepBreaks, 0) << "adjacent paved columns differ by more than the step-up";
}

// L3: a TraversalProbe walks the ribbon from the first cell to the last, across the ridge's cut and
// round the diagonal leg. CONTROL: the same walk on the bare terrain is blocked by the 18-micro ridge.
TEST(StreetPaverTest, ProbeWalksTheWayRibbonAcrossADiagonal) {
    const PavingPlan plan = planWayPaving(testWay(), groundMicro, AgentBox{}, "Gravel");
    ASSERT_TRUE(plan.ok) << "no plan: the paver is not built";
    const auto m = indexPlan(plan);
    auto occupiedPaved = [&](int x, int y, int z) {
        auto it = m.find(key(x, z));
        if (it != m.end()) return y <= it->second.surface;
        return y < groundMicro(x, z);
    };
    auto occupiedBare = [&](int x, int y, int z) { return y < groundMicro(x, z); };
    const AgentBox box;
    auto feet = [&](int mx, int mz, bool paved) {
        auto it = m.find(key(mx, mz));
        return ((paved && it != m.end()) ? it->second.surface : groundMicro(mx, mz) - 1) + 1;
    };
    const int sx = 2 * 9 + 4, sz = 2 * 9 + 4, gx = 20 * 9 + 4, gz = 10 * 9 + 4;
    const glm::ivec3 boundLo{0, 16 * 9 - 9, 0}, boundHi{24 * 9, 20 * 9 + 18, 16 * 9};
    TraversalProbe paved(occupiedPaved, box);
    const bool walks = paved.reachable({sx, feet(sx, sz, true), sz}, {gx - 2, feet(gx, gz, true) - 9, gz - 2}, {gx + 2, feet(gx, gz, true) + 9, gz + 2}, boundLo, boundHi);
    TraversalProbe bare(occupiedBare, box);
    const bool bareWalks = bare.reachable({sx, feet(sx, sz, false), sz}, {gx - 2, feet(gx, gz, false) - 9, gz - 2}, {gx + 2, feet(gx, gz, false) + 9, gz + 2}, boundLo, boundHi);
    std::printf("[ M5b ] way ribbon walk: paved %s, bare terrain (the control) %s\n", walks ? "WALKABLE" : "BLOCKED", bareWalks ? "walkable" : "blocked");
    EXPECT_TRUE(walks) << "the ribbon is not walkable end to end";
    EXPECT_FALSE(bareWalks) << "the bare terrain walks too: the ridge does not discriminate";
}

// M5b-1 chain 98 (the first village's gate, read as a vertical section): the way graded a cube BELOW the toft left a
// 9-micro face between the second cell outside the door (the street, 892) and the first (the toft's apron, 901); the
// stoop looked at the first cell only - its riser to the threshold (903) was 2, "no step" - and the face stayed. The
// stoop must carry the descent all the way to the ground: every riser <= the step-up, wherever the drop lies.
TEST(StreetPaverTest, DoorstepsStoopTheWholeDescentToTheStreet) {
    const int kStep = 4;
    // the hall house of chain 98: threshold 903; cells outward: 901 (apron), 892, 892, 892, 892, 892 (the street)
    const auto apron = Phyxel::Core::planDoorstepFills(903, {901, 892, 892, 892, 892, 892}, kStep);
    ASSERT_EQ(apron.size(), 6u);
    EXPECT_EQ(apron[0], INT_MIN) << "the apron needs no fill (riser 2 to the threshold)";
    EXPECT_EQ(apron[1], 897) << "the second cell must be raised to within 4 of the apron";
    EXPECT_EQ(apron[2], 893) << "the third to within 4 of the second";
    EXPECT_EQ(apron[3], INT_MIN) << "893 -> 892 is a riser of 1: no fill";
    // the croft of chain 93 (stooped, walkable): threshold 869; cells outward 864, 869 (a bump), 864
    const auto simple = Phyxel::Core::planDoorstepFills(869, {864, 869, 864}, kStep);
    EXPECT_EQ(simple[0], 865);
    EXPECT_EQ(simple[1], INT_MIN);
    // every riser along the stooped approach <= the step-up, on both cases (the invariant, not the numbers)
    for (const auto& c : {std::pair<int, std::vector<int>>{903, {901, 892, 892, 892, 892, 892}}, {869, {864, 869, 864}}}) {
        const auto f = Phyxel::Core::planDoorstepFills(c.first, c.second, kStep);
        int prev = c.first;
        for (size_t k = 0; k < c.second.size(); ++k) {
            const int feet = f[k] != INT_MIN ? f[k] : c.second[k];
            EXPECT_LE(prev - feet, kStep) << "riser from cell " << k << " (" << feet << ") up to " << prev << " exceeds the step-up";
            prev = feet;
        }
    }
}

// M5b-1 chain 101 (DESIGN DECISION, stated in the ledger): a lot fronting a graded way is built at the way's level at
// its door. The relation: the pad cube whose threshold (pad top + 14 micro for the slab builds measured live) lands
// within one step (4 micro) of the way's standing level. Live cases: the croft whose way stood at 878 against a
// threshold of 869 (pad 95) -> pad 96, threshold 878; the hall house whose way was cut to 892 against 903 (pad 98 ->
// 896); a way at 1363 -> pad 150, threshold 1364.
TEST(StreetPaverTest, PadCubeForStreetPutsTheThresholdAtOrAboveTheWay) {
    // chain 105: a way ABOVE the threshold by more than one puts the walker's head into the lintel (the carve is 18,
    // the agent 17) - the threshold stands at or above the way less one, and within a cell of stoop plus a sill (+7)
    const int kAbove = 12, kStep = 4;   // chain 107: threshold = pad * 9 + 12 on the requested-pad builds measured live
    for (int wayFeet : {878, 892, 1363, 870, 855, 1427, 882, 9, 0, -5}) {
        const int pad = Phyxel::Core::padCubeForStreet(wayFeet, kAbove, kStep);
        const int threshold = pad * 9 + kAbove;
        EXPECT_GE(threshold, wayFeet - 1) << "way " << wayFeet << ": the threshold sinks below the way (the lintel case)";
        EXPECT_LE(threshold, wayFeet + 7) << "way " << wayFeet << ": the threshold stands beyond a stoop's reach above the way";
    }
    EXPECT_EQ(Phyxel::Core::padCubeForStreet(883, kAbove, kStep), 97);    // threshold 885, the village's cutting lots
    EXPECT_EQ(Phyxel::Core::padCubeForStreet(892, kAbove, kStep), 98);    // 894, 2 above
    EXPECT_EQ(Phyxel::Core::padCubeForStreet(1363, kAbove, kStep), 150);  // 1362 = the way less one
    EXPECT_EQ(Phyxel::Core::padCubeForStreet(1396, kAbove, kStep), 154);  // 1398, the hall_house @13477
}

// M5b-1 chain 105: the pads moved down to their ways and the SETBACK cells kept their natural height - a 15-micro wall
// in front of the village hall house's door, a 4-micro bump before the butcher's that put the walker's head into the
// lintel. The stoop as levels: the unpaved cells between the door and the first paved cell (the way) take the line
// from the threshold down to the way, a cut where the ground stands above it, a fill where below, never above the
// threshold; the way is never touched except for one sill on its edge when the unpaved cells cannot reach it.
TEST(StreetPaverTest, DoorstepLevelsCutAndFillTheApproachToTheWay) {
    const int kStep = 4;
    // the village hall house (threshold 887 after the pad rule; the setback cell at 900, the way at 883 from the next)
    const auto cutting = Phyxel::Core::planDoorstepLevels(887, {900, 883, 883, 883}, {false, true, true, true}, kStep);
    ASSERT_EQ(cutting.size(), 4u);
    EXPECT_EQ(cutting[0], 883) << "the setback cell must be CUT down to the way's level (riser 4 from the threshold)";
    EXPECT_EQ(cutting[1], INT_MIN) << "the way is never touched";
    // the butcher: threshold 1409, the setback cell 1413 (the lintel case), the way at 1409
    const auto bump = Phyxel::Core::planDoorstepLevels(1409, {1413, 1409, 1409}, {false, true, true}, kStep);
    EXPECT_EQ(bump[0], 1409) << "a cell above the threshold is cut flat (the lintel)";
    EXPECT_EQ(bump[1], INT_MIN);
    // the apron of chain 98 with the way from the second cell: 903 -> 899 on the apron, the way at 892 is 7 below ->
    // one sill on the way's edge (the exception)
    const auto apron = Phyxel::Core::planDoorstepLevels(903, {901, 892, 892}, {false, true, true}, kStep);
    EXPECT_EQ(apron[0], 899);
    EXPECT_EQ(apron[1], 895) << "the way's edge takes one sill when the unpaved cells cannot reach it";
    EXPECT_EQ(apron[2], INT_MIN);
    // chain 109: a way 22 below the door with no unpaved cell between takes NO sill (one step cannot bridge it)
    const auto cliff = Phyxel::Core::planDoorstepLevels(1409, {1387, 1387, 1386}, {true, true, true}, kStep);
    EXPECT_EQ(cliff[0], INT_MIN) << "an 18-micro sill is a wall on the way, not a step";
    // no paved cell in reach: the descent rule (fills as planDoorstepFills) - and a bump above the threshold is cut
    const auto open = Phyxel::Core::planDoorstepLevels(903, {901, 892, 892, 892}, {false, false, false, false}, kStep);
    EXPECT_EQ(open[0], INT_MIN); EXPECT_EQ(open[1], 897); EXPECT_EQ(open[2], 893); EXPECT_EQ(open[3], INT_MIN);
    const auto bumpOpen = Phyxel::Core::planDoorstepLevels(1398, {1404, 1404, 1395}, {false, false, false}, kStep);
    EXPECT_EQ(bumpOpen[0], 1398) << "a cell above the threshold is cut flat even without a way in reach (the lintel)";
    EXPECT_EQ(bumpOpen[1], 1398);
    EXPECT_EQ(bumpOpen[2], INT_MIN) << "1395 is within a step below 1398";
    // the invariant on every case: no target above the threshold, every riser along the resulting line <= the step
    for (const auto& c : {std::tuple<int, std::vector<int>, std::vector<bool>>{887, {900, 883, 883, 883}, {false, true, true, true}},
                          {1409, {1413, 1409, 1409}, {false, true, true}}, {903, {901, 892, 892}, {false, true, true}}}) {
        const auto t = Phyxel::Core::planDoorstepLevels(std::get<0>(c), std::get<1>(c), std::get<2>(c), kStep);
        int prev = std::get<0>(c);
        for (size_t i = 0; i < std::get<1>(c).size(); ++i) {
            const int feet = t[i] != INT_MIN ? t[i] : std::get<1>(c)[i];
            EXPECT_LE(feet, std::get<0>(c)) << "a cell stands above the threshold";
            EXPECT_LE(std::abs(prev - feet), kStep) << "riser at cell " << i << " (" << prev << " -> " << feet << ")";
            prev = feet;
        }
    }
}

