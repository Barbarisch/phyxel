// MatSimCoreTest — docs/MaterialSimPlan.md §6a, the P1a red tests for the CPU reference material
// cellular automaton. Every test prints its numbers when it fails, and the load-bearing ones run
// a CONTROL that must fail the same comparator (a leak, a one-tick shift, a perturbation), which
// proves the comparator can see the defect it guards against.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/matsim/MatSimCore.h"

using namespace Phyxel::Core::MatSim;

namespace {

// Geometry is written in MICRO (1/9 m) world coordinates so the same world can be run at any
// cell size. A lab box's outside is solid, so the default world is open air.
Occ openAir(int, int, int) { return Occ::Empty; }

// The first world cell where two cores differ inside the inclusive world-cell box [lo, hi];
// returns false if none.
bool firstDiff(const MatSimCore& a, const MatSimCore& b, IVec3 lo, IVec3 hi, IVec3& where) {
    for (int y = lo.y; y <= hi.y; ++y)
        for (int z = lo.z; z <= hi.z; ++z)
            for (int x = lo.x; x <= hi.x; ++x)
                if (a.at({x, y, z}) != b.at({x, y, z})) { where = {x, y, z}; return true; }
    return false;
}

std::string cellStr(Cell c) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "mat=%u fill=%u", cellMaterial(c), cellFill(c));
    return buf;
}

// Signed per-material difference ledger - expected; returns the first non-zero material.
bool ledgerDelta(const MatSimCore& s, int& mat, long long& delta) {
    const Ledger got = s.ledger();
    const Ledger& want = s.expectedLedger();
    for (int m = 0; m < 256; ++m)
        if (got[m] != want[m]) {
            mat = m;
            delta = static_cast<long long>(got[m]) - static_cast<long long>(want[m]);
            return true;
        }
    return false;
}

// A dam-break scene at any cell size: a 4 x 3 x 4 m closed box, `material` filling x < 1 m to
// 2 m high. Returns the created core.
MatSimCore damBreak(int cpm, uint8_t material, Config cfg = {}) {
    MatSimCore s;
    std::string why;
    EXPECT_TRUE(s.create({0, 0, 0}, {4 * cpm, 3 * cpm, 4 * cpm}, cpm, openAir, &why, cfg)) << why;
    s.fillBox(material, {0, 0, 0}, {cpm, 2 * cpm, 4 * cpm}, 1.0);
    return s;
}

// Run `ticks` steps asserting the ledger after every tick; returns the first failing tick or -1.
long runLedgerChecked(MatSimCore& s, int ticks, int& mat, long long& delta) {
    for (int t = 1; t <= ticks; ++t) {
        s.step(1);
        if (ledgerDelta(s, mat, delta)) return t;
    }
    return -1;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// L1 — nothing happens until a box is created.

TEST(MatSimDefaults, InertUntilCreated) {
    MatSimCore s;
    EXPECT_FALSE(s.created());
    const StepStats st = s.step(10);
    EXPECT_EQ(st.ticks, 0);
    EXPECT_EQ(s.tick(), 0u);
    const FillResult f = s.fillBox(Mat::Water, {0, 0, 0}, {4, 4, 4}, 1.0);
    EXPECT_EQ(f.unitsWritten, 0u);
    for (uint64_t v : s.ledger()) EXPECT_EQ(v, 0u);
    EXPECT_TRUE(s.cells().empty());
}

TEST(MatSimDefaults, FullUnitsNestExactly) {
    EXPECT_EQ(fullUnits(9), 256u);
    EXPECT_EQ(fullUnits(3), 6912u);
    EXPECT_EQ(fullUnits(1), 186624u);
    EXPECT_EQ(fullUnits(2), 0u);
    EXPECT_EQ(fullUnits(3), 27u * fullUnits(9));
    EXPECT_EQ(fullUnits(1), 27u * fullUnits(3));
}

TEST(MatSimCreate, RefusesBadInput) {
    MatSimCore s;
    std::string why;
    EXPECT_FALSE(s.create({0, 0, 0}, {4, 4, 4}, 2, openAir, &why)) << "cells_per_m 2 accepted";
    EXPECT_FALSE(why.empty());
    EXPECT_FALSE(s.create({0, 0, 0}, {0, 4, 4}, 3, openAir, &why)) << "zero extent accepted";
    EXPECT_FALSE(s.create({0, 0, 0}, {300, 300, 300}, 9, openAir, &why)) << "27 M cells accepted";
    auto unknownCorner = [](int mx, int my, int mz) {
        return (mx == 5 && my == 5 && mz == 5) ? Occ::Unknown : Occ::Empty;
    };
    EXPECT_FALSE(s.create({0, 0, 0}, {4, 4, 4}, 3, unknownCorner, &why)) << "box with UNKNOWN accepted";
    EXPECT_NE(why.find("unknown"), std::string::npos) << why;
    EXPECT_FALSE(s.created()) << "a refused create left a partial box";
    EXPECT_FALSE(s.createMetres(0.1, 0, 0, 1, 1, 1, 3, openAir, &why)) << "off-lattice min accepted";
    EXPECT_TRUE(s.createMetres(-1.0 / 3.0, 0, 2.0 / 3.0, 1, 1, 1, 3, openAir, &why)) << why;
    EXPECT_EQ(s.origin(), (IVec3{-1, 0, 2}));
}

// ---------------------------------------------------------------------------------------------
// L2 — mass is exact.

TEST(MatSimLedger, ExactOver10kTicks) {
    const int cpms[] = {1, 3, 9};
    const uint8_t mats[] = {Mat::Water, Mat::Sand};
    for (int cpm : cpms)
        for (uint8_t mat : mats) {
            MatSimCore s = damBreak(cpm, mat);
            const int ticks = (cpm == 9) ? 3000 : 10000;
            int bad = 0;
            long long delta = 0;
            const long failTick = runLedgerChecked(s, ticks, bad, delta);
            EXPECT_EQ(failTick, -1) << "cpm=" << cpm << " material=" << int(mat)
                                    << ": material " << bad << " off by " << delta
                                    << " units at tick " << failTick;
            EXPECT_EQ(s.ledger()[mat], static_cast<uint64_t>(cpm * 2 * cpm * 4 * cpm) * s.full());
        }
}

TEST(MatSimLedger, ControlLeakIsSeen) {
    Config cfg;
    cfg.testLeakOneUnit = true;
    MatSimCore s = damBreak(3, Mat::Water, cfg);
    // The scene starts full-column, so gravity has nothing to do until the front moves; give the
    // leak a top cell with room below it.
    s.setCell({6, 5, 6}, Mat::Water, s.full());
    int bad = 0;
    long long delta = 0;
    const long failTick = runLedgerChecked(s, 200, bad, delta);
    ASSERT_NE(failTick, -1) << "the ledger did not see a deliberately leaked unit";
    EXPECT_EQ(bad, Mat::Water);
    EXPECT_EQ(delta, -1) << "the ledger saw a loss of " << delta << ", not exactly one unit";
}

// ---------------------------------------------------------------------------------------------
// L2 — the resolution switch is an exact conserving remap.

namespace {
// A 3 x 3 x 3 m box with a 1 m pillar and a thin 1/9 m shelf (solid at 1 m cells, mostly open at
// 1/9) and a deterministic scatter of water at 1 m cells.
Occ pillarAndShelf(int mx, int my, int mz) {
    if (mx >= 9 && mx < 18 && mz >= 9 && mz < 18 && my < 18) return Occ::Solid;   // pillar
    if (my == 20 && mx >= 18 && mz < 9) return Occ::Solid;                         // shelf
    return Occ::Empty;
}
} // namespace

TEST(MatSimRemap, RoundTripIsExact) {
    MatSimCore s;
    std::string why;
    ASSERT_TRUE(s.create({0, 0, 0}, {3, 3, 3}, 1, pillarAndShelf, &why)) << why;
    for (int y = 0; y < 3; ++y)
        for (int z = 0; z < 3; ++z)
            for (int x = 0; x < 3; ++x) {
                const uint32_t f = cellHash(x, y, z, 0, 7) % (s.full() + 1);
                s.setCell({x, y, z}, Mat::Water, f);   // refused on solid cells, by design
            }
    const std::vector<Cell> before = s.cells();
    const Ledger l0 = s.ledger();
    const int path[] = {3, 9, 3, 1};
    for (int cpm : path) {
        ASSERT_TRUE(s.setCellSize(cpm, pillarAndShelf, &why)) << "-> " << cpm << ": " << why;
        const Ledger l = s.ledger();
        for (int m = 0; m < 256; ++m)
            ASSERT_EQ(l[m], l0[m]) << "after switching to " << cpm << " cells/m, material " << m
                                   << " off by " << (long long)l[m] - (long long)l0[m] << " units";
        EXPECT_EQ(s.ledger(), s.expectedLedger());
    }
    ASSERT_EQ(s.cells().size(), before.size());
    for (size_t i = 0; i < before.size(); ++i)
        ASSERT_EQ(s.cells()[i], before[i]) << "cell " << i << " out of place after the round trip: "
                                           << cellStr(s.cells()[i]) << " vs " << cellStr(before[i]);
}

TEST(MatSimRemap, SplitIsBottomUp) {
    MatSimCore s;
    std::string why;
    ASSERT_TRUE(s.create({0, 0, 0}, {1, 1, 1}, 1, openAir, &why)) << why;
    ASSERT_TRUE(s.setCell({0, 0, 0}, Mat::Water, fullUnits(1) / 2));
    ASSERT_TRUE(s.setCellSize(3, openAir, &why)) << why;
    const uint32_t F = fullUnits(3);
    for (int y = 0; y < 3; ++y)
        for (int z = 0; z < 3; ++z)
            for (int x = 0; x < 3; ++x) {
                const Cell c = s.at({x, y, z});
                const uint32_t want = (y == 0) ? F : (y == 1 ? F / 2 : 0);
                EXPECT_EQ(cellFill(c), want) << "child (" << x << "," << y << "," << z << ") " << cellStr(c);
                EXPECT_EQ(cellMaterial(c), want ? Mat::Water : kEmpty) << cellStr(c);
            }
}

TEST(MatSimRemap, MergeRefusesMixedMaterials) {
    MatSimCore s;
    std::string why;
    ASSERT_TRUE(s.create({0, 0, 0}, {3, 3, 3}, 3, openAir, &why)) << why;
    s.setCell({0, 0, 0}, Mat::Water, 10);
    s.setCell({1, 0, 0}, Mat::Sand, 10);
    const std::vector<Cell> before = s.cells();
    EXPECT_FALSE(s.setCellSize(1, openAir, &why)) << "a merge of water + sand into one cell was accepted";
    EXPECT_FALSE(why.empty());
    EXPECT_EQ(s.cellsPerM(), 3);
    EXPECT_EQ(s.cells(), before) << "a refused switch changed the grid";
}

// ---------------------------------------------------------------------------------------------
// L2 — sleep: a still basin goes fully asleep, and skipping never changes the result.

namespace {
MatSimCore unevenBasin(Config cfg = {}) {
    MatSimCore s;
    std::string why;
    EXPECT_TRUE(s.create({0, 0, 0}, {12, 6, 12}, 3, openAir, &why, cfg)) << why;   // 4 x 2 x 4 m
    s.fillBox(Mat::Water, {0, 0, 0}, {12, 2, 12}, 1.0);   // a 2/3 m layer
    s.fillBox(Mat::Water, {0, 2, 0}, {4, 3, 4}, 1.0);     // plus a 4/3 x 1 x 4/3 m mound in a corner
    return s;
}
} // namespace

TEST(MatSimSleep, StillBasinReachesZeroEligible) {
    MatSimCore s = unevenBasin();
    const int limit = 600;   // prediction: asleep within 10 s of simulated time at 60 Hz
    // Asleep = nothing eligible on two consecutive ticks (both block offsets).
    int asleepAt = -1;
    uint64_t lastEligible = 0, prevEligible = 1;
    for (int t = 1; t <= limit; ++t) {
        lastEligible = s.step(1).lastTickEligible;
        if (lastEligible == 0 && prevEligible == 0) { asleepAt = t; break; }
        prevEligible = lastEligible;
    }
    ASSERT_NE(asleepAt, -1) << "still " << lastEligible << " eligible blocks at tick " << limit;
    std::printf("[MatSimSleep] uneven basin asleep at tick %d (%.2f s at 60 Hz)\n", asleepAt, asleepAt / 60.0);
    // Asleep means asleep: the next 100 ticks evaluate nothing and move nothing.
    const StepStats after = s.step(100);
    EXPECT_EQ(after.eligibleBlocks, 0u);
    EXPECT_EQ(after.evaluatedBlocks, 0u) << "asleep basin still evaluated blocks";
    // Control: a perturbation must wake it.
    s.setCell({6, 5, 6}, Mat::Water, s.full());
    EXPECT_GT(s.step(1).lastTickEligible, 0u) << "a dropped cell did not wake the basin";
}

namespace {
// Obstacles + water + sand, for the dense-vs-sleep and determinism comparisons.
Occ steppedRoom(int mx, int my, int mz) {
    if (mx >= 18 && mx < 27 && my < 9) return Occ::Solid;                 // a 1 m step
    if (mz >= 27 && mz < 30 && my < 15 && mx < 30) return Occ::Solid;     // a low wall, 1/3 m thick
    return Occ::Empty;
}
MatSimCore mixedScene(bool sleep, uint32_t seed = 1) {
    Config cfg;
    cfg.sleep = sleep;
    cfg.seed = seed;
    MatSimCore s;
    std::string why;
    // 40 x 9 x 40 cells (5+ bricks a side) so bricks far from the action can actually sleep.
    EXPECT_TRUE(s.create({-3, 0, -2}, {40, 9, 40}, 3, steppedRoom, &why, cfg)) << why;
    s.fillBox(Mat::Water, {-3, 0, -2}, {4, 6, 9}, 1.0);
    s.fillBox(Mat::Sand, {6, 4, 3}, {3, 4, 3}, 1.0);
    s.fillBox(Mat::Water, {3, 6, 8}, {2, 2, 2}, 0.5);
    return s;
}
} // namespace

TEST(MatSimSleep, SkippingEqualsDense) {
    MatSimCore dense = mixedScene(false);
    MatSimCore sleepy = mixedScene(true);
    const IVec3 lo = dense.origin();
    const IVec3 hi{lo.x + dense.size().x - 1, lo.y + dense.size().y - 1, lo.z + dense.size().z - 1};
    uint64_t skipped = 0;
    for (int t = 1; t <= 1500; ++t) {
        const StepStats a = dense.step(1);
        const StepStats b = sleepy.step(1);
        skipped += a.evaluatedBlocks - b.evaluatedBlocks;
        IVec3 w;
        ASSERT_FALSE(firstDiff(dense, sleepy, lo, hi, w))
            << "sleep changed the result at tick " << t << ", world cell (" << w.x << "," << w.y << ","
            << w.z << "): dense " << cellStr(dense.at(w)) << " vs sleep " << cellStr(sleepy.at(w));
        ASSERT_EQ(a.eligibleBlocks, b.eligibleBlocks) << "tick " << t;
    }
    EXPECT_GT(skipped, 0u) << "sleep never skipped a block, so this test proved nothing";
    std::printf("[MatSimSleep] dense == sleep over 1500 ticks; %llu block evaluations skipped\n",
                (unsigned long long)skipped);
    // Edits under a (mostly) sleeping scene must wake exactly what dense would move: a drop of
    // water far from the action, then a wall raised mid-run through resolidify, then removed.
    bool wall = false;
    auto world = [&wall](int mx, int my, int mz) {
        if (wall && mx >= 45 && mx < 54 && my < 27 && mz >= 0 && mz < 90) return Occ::Solid;
        return steppedRoom(mx, my, mz);
    };
    auto compareFor = [&](int ticks, const char* phase) {
        for (int t = 1; t <= ticks; ++t) {
            dense.step(1);
            sleepy.step(1);
            IVec3 w;
            ASSERT_FALSE(firstDiff(dense, sleepy, lo, hi, w))
                << phase << ": sleep changed the result " << t << " ticks in, world cell (" << w.x << ","
                << w.y << "," << w.z << "): dense " << cellStr(dense.at(w)) << " vs sleep " << cellStr(sleepy.at(w));
        }
    };
    dense.setCell({30, 8, 30}, Mat::Water, dense.full());
    sleepy.setCell({30, 8, 30}, Mat::Water, sleepy.full());
    compareFor(300, "after a drop into a sleeping corner");
    wall = true;
    dense.resolidify(world);
    sleepy.resolidify(world);
    compareFor(300, "after resolidify raised a wall");
    wall = false;
    dense.resolidify(world);
    sleepy.resolidify(world);
    compareFor(300, "after resolidify removed it");
    EXPECT_EQ(sleepy.ledger(), sleepy.expectedLedger());
    // Control: the same comparator must see a single perturbed cell.
    sleepy.setCell({0, 8, 0}, Mat::Water, 1);
    IVec3 w;
    EXPECT_TRUE(firstDiff(dense, sleepy, lo, hi, w)) << "the comparator missed a one-unit change";
}

// ---------------------------------------------------------------------------------------------
// L2 — determinism and world alignment.

TEST(MatSimDeterminism, TwoRunsBitIdentical) {
    MatSimCore a = mixedScene(true);
    MatSimCore b = mixedScene(true);
    a.step(800);
    b.step(800);
    ASSERT_EQ(a.cells().size(), b.cells().size());
    for (size_t i = 0; i < a.cells().size(); ++i)
        ASSERT_EQ(a.cells()[i], b.cells()[i]) << "first differing cell index " << i;
    // Control: a different seed must give a different run (the hash really is used).
    MatSimCore c = mixedScene(true, 99);
    c.step(800);
    EXPECT_NE(a.cells(), c.cells()) << "seed has no effect: choices are not hashed";
}

namespace {
// A closed room defined in WORLD micro coordinates; everything outside it is solid. Boxes of any
// placement that contain the room must simulate it identically in world space.
constexpr int kRoomLo[3] = {-7, 2, 4};     // world CELLS at 3 cells/m
constexpr int kRoomHi[3] = {10, 11, 19};   // inclusive
Occ room(int mx, int my, int mz) {
    const int c[3] = {mx / 3 - (mx < 0 && mx % 3 ? 1 : 0), my / 3 - (my < 0 && my % 3 ? 1 : 0),
                      mz / 3 - (mz < 0 && mz % 3 ? 1 : 0)};
    for (int i = 0; i < 3; ++i)
        if (c[i] < kRoomLo[i] || c[i] > kRoomHi[i]) return Occ::Solid;
    if (c[0] >= 0 && c[0] <= 2 && c[1] <= 5) return Occ::Solid;   // an interior wall stub
    return Occ::Empty;
}
MatSimCore roomIn(IVec3 origin, IVec3 size) {
    MatSimCore s;
    std::string why;
    EXPECT_TRUE(s.create(origin, size, 3, room, &why)) << why;
    s.fillBox(Mat::Water, {-7, 2, 4}, {5, 8, 6}, 1.0);
    s.fillBox(Mat::Sand, {4, 6, 10}, {3, 5, 3}, 1.0);
    return s;
}
} // namespace

TEST(MatSimWorldAlignment, ShiftedBoxesAgreeInWorldSpace) {
    const IVec3 lo{kRoomLo[0], kRoomLo[1], kRoomLo[2]};
    const IVec3 hi{kRoomHi[0], kRoomHi[1], kRoomHi[2]};
    const IVec3 tight{lo.x, lo.y, lo.z};
    const IVec3 tightSize{hi.x - lo.x + 1, hi.y - lo.y + 1, hi.z - lo.z + 1};
    struct Case { IVec3 origin, size; const char* name; };
    const Case cases[] = {
        {{lo.x - 4, lo.y - 2, lo.z - 6}, {tightSize.x + 10, tightSize.y + 4, tightSize.z + 8}, "even offset"},
        {{lo.x - 3, lo.y - 1, lo.z - 5}, {tightSize.x + 7, tightSize.y + 3, tightSize.z + 9}, "odd offset"},
    };
    for (const Case& cs : cases) {
        MatSimCore a = roomIn(tight, tightSize);
        MatSimCore b = roomIn(cs.origin, cs.size);
        for (int t = 1; t <= 600; ++t) {
            a.step(1);
            b.step(1);
            IVec3 w;
            ASSERT_FALSE(firstDiff(a, b, lo, hi, w))
                << cs.name << ": box placement changed the result at tick " << t << ", world cell ("
                << w.x << "," << w.y << "," << w.z << "): " << cellStr(a.at(w)) << " vs " << cellStr(b.at(w));
        }
        EXPECT_GT(a.ledger()[Mat::Water], 0u);
    }
    // Control: the comparator must see a one-tick shift.
    MatSimCore a = roomIn(tight, tightSize);
    MatSimCore b = roomIn(cases[1].origin, cases[1].size);
    a.step(30);
    b.step(31);
    IVec3 w;
    EXPECT_TRUE(firstDiff(a, b, lo, hi, w)) << "the comparator missed a one-tick shift";
}

// ---------------------------------------------------------------------------------------------
// L2 — powder stands at its grounded angle of repose.

namespace {
// Collapse a 4 x 12 x 4-cell column of `mat` on an 8 x 4 x 8 m floor at 3 cells/m and measure the
// rest slope along x through the centre. Returns false (with a message) if it never rests.
bool pileSlope(uint8_t mat, double& slope, std::string& msg) {
    MatSimCore s;
    std::string why;
    if (!s.create({0, 0, 0}, {24, 12, 24}, 3, openAir, &why)) { msg = why; return false; }
    s.fillBox(mat, {10, 0, 10}, {4, 12, 4}, 1.0);
    int asleepAt = -1;
    uint64_t prev = 1;
    for (int t = 1; t <= 6000; ++t) {
        const uint64_t e = s.step(1).lastTickEligible;
        if (e == 0 && prev == 0) { asleepAt = t; break; }
        prev = e;
    }
    if (asleepAt < 0) { msg = "never came to rest in 6000 ticks"; return false; }
    const double F = s.full();
    std::vector<double> h(24, 0.0);
    for (int x = 0; x < 24; ++x) {
        for (int zz = 11; zz <= 12; ++zz)
            for (int y = 0; y < 12; ++y) {
                const Cell c = s.at({x, y, zz});
                if (cellMaterial(c) == mat) h[x] += cellFill(c) / F;
            }
        h[x] *= 0.5;
    }
    int peak = 0;
    for (int x = 1; x < 24; ++x) if (h[x] > h[peak]) peak = x;
    int edge = peak;
    while (edge + 1 < 24 && h[edge + 1] > 0.0) ++edge;
    const int run = edge - peak;
    std::string prof;
    for (double v : h) { char b[16]; std::snprintf(b, sizeof(b), "%.2f ", v); prof += b; }
    // The slope is the MEDIAN per-cell drop along the flank. The peak-to-toe average folds in the
    // flat crest (two equal peak columns) and the partial-height toe cell, which are quantisation,
    // not the angle; it is printed too.
    std::vector<double> drops;
    for (int x = peak; x < edge; ++x) drops.push_back(h[x] - h[x + 1]);
    std::sort(drops.begin(), drops.end());
    slope = drops.empty() ? 0.0 : drops[drops.size() / 2];
    const double endpoint = run ? (h[peak] - h[edge]) / run : 0.0;
    char head[200];
    std::snprintf(head, sizeof(head),
                  "%s: rest at tick %d, peak %.2f cells, flank run %d, median drop %.3f (peak-to-toe %.3f); profile ",
                  materialTable()[mat].name.c_str(), asleepAt, h[peak], run, slope, endpoint);
    msg = head + prof;
    return run >= 3;
}
} // namespace

TEST(MatSimPowder, PileStandsAtRepose) {
    // Each powder must rest within +-0.03 of its own grounded tan(theta), tight enough that sand
    // (0.675) can't pass at snow's angle (0.781). The audit showed a +-1-cell drop tolerance could.
    struct P { uint8_t mat; double deg; };
    const P powders[] = {{Mat::Sand, 34.0}, {Mat::Snow, 38.0}, {Mat::Gravel, 45.0}};
    double prevSlope = 0.0;
    for (const P& p : powders) {
        double slope = 0.0;
        std::string msg;
        ASSERT_TRUE(pileSlope(p.mat, slope, msg)) << msg;
        const double want = std::tan(p.deg * 3.14159265358979 / 180.0);
        std::printf("[MatSimPowder] %s (want tan %.0f = %.3f)\n", msg.c_str(), p.deg, want);
        EXPECT_NEAR(slope, want, 0.03) << msg;
        EXPECT_GT(slope, prevSlope) << "piles must steepen with the angle of repose: " << msg;
        prevSlope = slope;
    }
}

// ---------------------------------------------------------------------------------------------
// L2 — edits under the simulation: trapped fill stays in the ledger; unknown is solid + counted.

TEST(MatSimEdits, ResolidifyTrapsAndReleases) {
    bool wall = false;
    auto world = [&wall](int mx, int my, int mz) {
        return (wall && mx < 9 && my < 9 && mz < 9) ? Occ::Solid : Occ::Empty;
    };
    MatSimCore s;
    std::string why;
    ASSERT_TRUE(s.create({0, 0, 0}, {6, 6, 6}, 3, world, &why)) << why;
    s.fillBox(Mat::Water, {0, 0, 0}, {6, 1, 6}, 1.0);
    const Ledger l0 = s.ledger();
    wall = true;
    s.resolidify(world);
    EXPECT_EQ(s.trappedUnits(), 9ull * s.full()) << "3 x 1 x 3 water cells inside the new solid";
    EXPECT_EQ(s.ledger(), l0) << "resolidify lost or made mass";
    s.step(50);
    EXPECT_EQ(s.ledger(), l0);
    wall = false;
    s.resolidify(world);
    EXPECT_EQ(s.trappedUnits(), 0u);
    EXPECT_EQ(s.ledger(), l0);
    EXPECT_EQ(s.ledger(), s.expectedLedger());
}

TEST(MatSimEdits, UnknownIsSolidAndCounted) {
    bool fog = false;
    auto world = [&fog](int mx, int, int) {
        return (fog && mx >= 9) ? Occ::Unknown : Occ::Empty;
    };
    MatSimCore s;
    std::string why;
    ASSERT_TRUE(s.create({0, 0, 0}, {6, 3, 3}, 3, world, &why)) << why;
    EXPECT_EQ(s.unknownCells(), 0u);
    fog = true;
    s.resolidify(world);
    EXPECT_EQ(s.unknownCells(), 3u * 3u * 3u) << "x >= 1 m is unknown: 3 x 3 x 3 cells";
    EXPECT_EQ(cellMaterial(s.at({4, 1, 1})), kSolid) << "an unknown cell must be solid";
    s.fillBox(Mat::Water, {0, 0, 2}, {3, 3, 1}, 1.0);   // a 1 m x 1 m x 1/3 m slab beside the fog
    s.step(200);
    EXPECT_EQ(s.ledger(), s.expectedLedger());
    // The fog lifts: nothing may have gone into the unknown part, and the count drops to zero.
    fog = false;
    s.resolidify(world);
    EXPECT_EQ(s.unknownCells(), 0u);
    EXPECT_EQ(s.trappedUnits(), 0u) << "water was frozen inside unknown cells";
    uint64_t inFog = 0, total = 0;
    for (int y = 0; y < 3; ++y)
        for (int z = 0; z < 3; ++z)
            for (int x = 0; x < 6; ++x) {
                const Cell c = s.at({x, y, z});
                if (cellMaterial(c) != Mat::Water) continue;
                total += cellFill(c);
                if (x >= 3) inFog += cellFill(c);
            }
    EXPECT_EQ(inFog, 0u) << "water entered unknown space";
    EXPECT_EQ(total, 9ull * s.full()) << "the slab spread but must all still be in the known part";
}

// ---------------------------------------------------------------------------------------------
// L2 — liquid leaves a ledge. Rule 2 (level) only moves water between SUPPORTED cells, so
// without rule 3 applying to liquids (theta = 0) water would stand on a ledge's lip forever.

TEST(MatSimLiquid, PoursOffLedge) {
    // 3 x 3 x 2 m box at 3 cells/m; a 1 m-high ledge under x < 1 m; a pool of water on it.
    auto ledge = [](int mx, int my, int) { return (mx < 9 && my < 9) ? Occ::Solid : Occ::Empty; };
    MatSimCore s;
    std::string why;
    ASSERT_TRUE(s.create({0, 0, 0}, {9, 9, 6}, 3, ledge, &why)) << why;
    s.fillBox(Mat::Water, {0, 3, 0}, {3, 2, 6}, 1.0);   // 2 cells deep on the ledge top
    s.step(600);
    uint64_t below = 0, onLedge = 0;
    for (int y = 0; y < 9; ++y)
        for (int z = 0; z < 6; ++z)
            for (int x = 0; x < 9; ++x) {
                const Cell c = s.at({x, y, z});
                if (cellMaterial(c) != Mat::Water) continue;
                (y < 3 ? below : onLedge) += cellFill(c);
            }
    std::printf("[MatSimLiquid] after 600 ticks: %.1f%% of the water below the ledge top\n",
                100.0 * double(below) / double(below + onLedge));
    EXPECT_GT(below, onLedge) << "water stayed on the ledge: " << onLedge << " units up, " << below << " below";
    EXPECT_EQ(s.ledger(), s.expectedLedger());
}

// ---------------------------------------------------------------------------------------------
// Stress — random scenes, many seeds: the ledger is exact on EVERY tick, sleep equals dense on
// EVERY tick, through live cell-size switches (refused merges must change nothing).

TEST(MatSimStress, RandomScenesLedgerSleepAndSwitch) {
    uint64_t refusedMerges = 0, switches = 0;
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        // Random metre-scale pillars and sub-metre shelves, fixed in world space.
        auto world = [seed](int mx, int my, int mz) {
            const int cx = mx / 9 - (mx < 0 && mx % 9 ? 1 : 0), cz = mz / 9 - (mz < 0 && mz % 9 ? 1 : 0);
            const uint32_t h = cellHash(cx, 0, cz, 0, seed);
            if (h % 7 == 0 && my < int(9 + h % 18)) return Occ::Solid;
            if (h % 11 == 1 && my == 13) return Occ::Solid;
            return Occ::Empty;
        };
        Config sleepy, dense;
        sleepy.seed = dense.seed = seed;
        dense.sleep = false;
        MatSimCore a, b;
        std::string why;
        const IVec3 o{-3 * int(seed), 0, 2 - int(seed)};   // negative and odd origins included
        ASSERT_TRUE(a.create(o, {6, 4, 6}, 1, world, &why, sleepy)) << why;
        ASSERT_TRUE(b.create(o, {6, 4, 6}, 1, world, &why, dense)) << why;
        ASSERT_TRUE(a.setCellSize(3, world, &why)) << why;
        ASSERT_TRUE(b.setCellSize(3, world, &why)) << why;
        const uint8_t mats[] = {Mat::Water, Mat::Sand, Mat::Gravel};
        for (int k = 0; k < 10; ++k) {
            const uint32_t h = cellHash(k, 1, 2, 3, seed);
            const IVec3 lo{a.origin().x + int(h % 15), int((h >> 4) % 9), a.origin().z + int((h >> 8) % 15)};
            const IVec3 sz{1 + int((h >> 12) % 4), 1 + int((h >> 16) % 3), 1 + int((h >> 20) % 4)};
            const uint8_t m = mats[(h >> 24) % 3];
            const double f = 0.25 + 0.75 * double((h >> 26) % 4) / 3.0;
            a.fillBox(m, lo, sz, f);
            b.fillBox(m, lo, sz, f);
        }
        const int sizes[] = {9, 3, 1, 3, 9, 3};
        for (int phase = 0; phase < 6; ++phase) {
            for (int t = 0; t < 120; ++t) {
                a.step(1);
                b.step(1);
                int bad = 0;
                long long d = 0;
                ASSERT_FALSE(ledgerDelta(a, bad, d)) << "seed " << seed << " cpm " << a.cellsPerM() << " tick "
                                                     << a.tick() << ": material " << bad << " off by " << d;
                ASSERT_EQ(a.cells(), b.cells()) << "seed " << seed << ": sleep != dense at tick " << a.tick();
            }
            const std::vector<Cell> before = a.cells();
            const Ledger lb = a.ledger();
            const bool okA = a.setCellSize(sizes[phase], world, &why);
            const bool okB = b.setCellSize(sizes[phase], world, &why);
            ASSERT_EQ(okA, okB);
            ++switches;
            if (!okA) {
                ++refusedMerges;
                ASSERT_EQ(a.cells(), before) << "a refused switch changed the grid: " << why;
            }
            ASSERT_EQ(a.ledger(), lb) << "seed " << seed << ": switch to " << sizes[phase] << " changed the ledger";
            ASSERT_EQ(a.ledger(), a.expectedLedger());
        }
    }
    std::printf("[MatSimStress] 6 seeds x 720 ticks: ledger exact and sleep == dense every tick; "
                "%llu switches, %llu refused (mixed-material merges)\n",
                (unsigned long long)switches, (unsigned long long)refusedMerges);
}

TEST(MatSimStress, DegenerateInputsAndChurn) {
    MatSimCore s;
    std::string why;
    ASSERT_TRUE(s.create({0, 0, 0}, {1, 1, 1}, 9, openAir, &why)) << why;   // a single cell
    EXPECT_EQ(s.fillBox(Mat::Water, {0, 0, 0}, {1, 1, 1}, std::nan("")).unitsWritten, 0u);
    EXPECT_EQ(s.fillBox(Mat::Water, {5, 5, 5}, {2, 2, 2}, 1.0).cellsWritten, 0u) << "fill outside the box";
    EXPECT_EQ(s.fillBox(kSolid, {0, 0, 0}, {1, 1, 1}, 1.0).cellsWritten, 0u) << "SOLID is not a fill material";
    EXPECT_FALSE(s.setCell({0, 0, 0}, Mat::Water, s.full() + 1)) << "over-full cell accepted";
    EXPECT_EQ(s.fillBox(Mat::Water, {0, 0, 0}, {1, 1, 1}, 7.0).unitsWritten, s.full()) << "fraction clamps to 1";
    s.step(100);
    EXPECT_EQ(s.ledger()[Mat::Water], s.full());
    EXPECT_FALSE(s.setCellSize(1, openAir, &why)) << "a 1/9 m box off the 1 m lattice was remapped";
    for (int i = 0; i < 1000; ++i) {   // create / clear churn
        ASSERT_TRUE(s.create({i, -i, 0}, {4, 4, 4}, 3, openAir, &why)) << why;
        s.fillBox(Mat::Sand, {i, -i, 0}, {4, 1, 4}, 1.0);
        s.step(2);
        ASSERT_EQ(s.ledger()[Mat::Sand], 16ull * s.full());
        s.clear();
        ASSERT_FALSE(s.created());
    }
}

// ---------------------------------------------------------------------------------------------
// L2 â€” a wall of cells that only touch at edges is still a wall. A staircased diagonal wall
// (x + z = const, solid cubes meeting edge to edge) must hold water and sand on its side, at every
// cell size. Found by the P1a audit: level grouped cells across a block's corner and slide moved
// fill diagonally with no open path between the cells.

TEST(MatSimWalls, StaircaseDiagonalWallIsWatertight) {
    const int cpms[] = {1, 3, 9};
    const uint8_t mats[] = {Mat::Water, Mat::Sand};
    for (int cpm : cpms)
        for (uint8_t mat : mats) {
            const int k = 9 / cpm;   // micro per cell
            // A 4 x 3 x 4 m box; wall cells where cx + cz == 3 * cpm (one cell thick, edge-joined).
            auto wall = [cpm, k](int mx, int, int mz) {
                const int cx = mx / k, cz = mz / k;
                return (cx + cz == 3 * cpm) ? Occ::Solid : Occ::Empty;
            };
            MatSimCore s;
            std::string why;
            ASSERT_TRUE(s.create({0, 0, 0}, {4 * cpm, 3 * cpm, 4 * cpm}, cpm, wall, &why)) << why;
            // Fill the near side (cx + cz < 3 cpm) to 2 m with the material.
            uint64_t near0 = 0;
            for (int y = 0; y < 2 * cpm; ++y)
                for (int z = 0; z < 4 * cpm; ++z)
                    for (int x = 0; x < 4 * cpm; ++x)
                        if (x + z < 3 * cpm && s.setCell({x, y, z}, mat, s.full())) near0 += s.full();
            const int ticks = cpm == 9 ? 600 : 1500;
            s.step(ticks);
            uint64_t far = 0;
            IVec3 firstFar{-1, -1, -1};
            for (int y = 0; y < 3 * cpm; ++y)
                for (int z = 0; z < 4 * cpm; ++z)
                    for (int x = 0; x < 4 * cpm; ++x) {
                        const Cell c = s.at({x, y, z});
                        if (x + z > 3 * cpm && cellMaterial(c) == mat && cellFill(c)) {
                            if (!far) firstFar = {x, y, z};
                            far += cellFill(c);
                        }
                    }
            EXPECT_EQ(far, 0u) << "cpm " << cpm << " material " << int(mat) << ": " << far << " of " << near0
                               << " units crossed the staircase wall; first at (" << firstFar.x << ","
                               << firstFar.y << "," << firstFar.z << ")";
            EXPECT_EQ(s.ledger(), s.expectedLedger());
        }
}

// ---------------------------------------------------------------------------------------------
// L2 â€” trapped contents survive a resolution round trip (the audit found no test reached the
// split/merge trapped paths).

TEST(MatSimRemap, TrappedContentsSurviveRoundTrip) {
    bool wall = false;
    auto world = [&wall](int mx, int my, int mz) {
        return (wall && mx >= 9 && mx < 18 && my < 9 && mz < 9) ? Occ::Solid : Occ::Empty;
    };
    MatSimCore s;
    std::string why;
    ASSERT_TRUE(s.create({0, 0, 0}, {3, 3, 3}, 1, world, &why)) << why;
    s.fillBox(Mat::Water, {0, 0, 0}, {3, 1, 3}, 0.75);
    wall = true;
    s.resolidify(world);   // the 1 m cell (1,0,0) turns solid around 0.75 of water
    ASSERT_EQ(s.trappedUnits(), s.full() * 3 / 4);
    const Ledger l0 = s.ledger();
    const int path[] = {3, 9, 3, 1};
    for (int cpm : path) {
        ASSERT_TRUE(s.setCellSize(cpm, world, &why)) << "-> " << cpm << ": " << why;
        ASSERT_EQ(s.ledger(), l0) << "switch to " << cpm << " changed the ledger (trapped "
                                  << s.trappedUnits() << ")";
        s.step(30);
        ASSERT_EQ(s.ledger(), l0) << "stepping at " << cpm << " changed the ledger";
    }
    EXPECT_EQ(s.ledger(), s.expectedLedger());
}
