// MatSimCoreTest — docs/MaterialSimPlan.md §6a, the P1a red tests for the CPU reference material
// cellular automaton. Every test prints its numbers when it fails, and the load-bearing ones run
// a CONTROL that must fail the same comparator (a leak, a one-tick shift, a perturbation), which
// proves the comparator can see the defect it guards against.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    // Asleep = quiet (nothing moved, no head changed) on two consecutive ticks (both block offsets).
    // §6d: before the pressure rule this counted moves only; the head field can still be settling
    // on a tick that moves nothing.
    int asleepAt = -1;
    uint64_t lastEligible = 0;
    bool prevQuiet = false;
    for (int t = 1; t <= limit; ++t) {
        const StepStats st = s.step(1);
        lastEligible = st.lastTickEligible;
        if (st.lastTickQuiet() && prevQuiet) { asleepAt = t; break; }
        prevQuiet = st.lastTickQuiet();
    }
    ASSERT_NE(asleepAt, -1) << "still " << lastEligible << " eligible blocks at tick " << limit;
    std::printf("[MatSimSleep] uneven basin asleep at tick %d (%.2f s at 60 Hz)\n", asleepAt, asleepAt / 60.0);
    // Asleep means asleep: the next 100 ticks evaluate nothing and move nothing.
    const StepStats after = s.step(100);
    EXPECT_EQ(after.eligibleBlocks, 0u);
    EXPECT_EQ(after.evaluatedBlocks, 0u) << "asleep basin still evaluated blocks";
    // Control: a perturbation must wake it. Two ticks: the dropped cell sits in the box's top
    // layer, so on one of the two block offsets its block reaches outside the box and cannot move.
    s.setCell({6, 5, 6}, Mat::Water, s.full());
    EXPECT_GT(s.step(2).eligibleBlocks, 0u) << "a dropped cell did not wake the basin";
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

// ---------------------------------------------------------------------------------------------
// L2 - pressure (§6d). Communicating vessels: water in a U-tube stands at ONE level in both arms.
// P1 measured the opposite live (the far arm stuck at the connector's height, the box asleep with
// the arms 3 m apart). Prediction, written before the rule existed: with pressure on, both arms
// end within max(1/4 cell, 3 cm) of each other and of the volume-derived level, at every cell
// size, and the box comes to rest; with pressure off (the control) the far arm stays empty.

namespace {
struct UTube {
    int leftW, rightW;   // arm widths (m); the divider is 1 m thick
    int totalW() const { return leftW + 1 + rightW; }
};
// Micro-coordinate world: closed box x 0..W, y 0..6, z 0..1 m; divider x in [leftW, leftW+1),
// y in [1, 6) m, leaving a 1 m connector along the floor.
MatSimCore uTube(const UTube& u, int cpm, double leftFill, Config cfg = {}) {
    auto world = [u](int mx, int my, int) {
        return (mx >= 9 * u.leftW && mx < 9 * (u.leftW + 1) && my >= 9) ? Occ::Solid : Occ::Empty;
    };
    MatSimCore s;
    std::string why;
    EXPECT_TRUE(s.create({0, 0, 0}, {u.totalW() * cpm, 6 * cpm, cpm}, cpm, world, &why, cfg)) << why;
    s.fillBox(Mat::Water, {0, 0, 0}, {u.totalW() * cpm, cpm, cpm}, 1.0);                  // connector
    s.fillBox(Mat::Water, {0, cpm, 0}, {u.leftW * cpm, int(leftFill * cpm), cpm}, 1.0);   // left arm
    return s;
}
// Water height (m) above the connector in the arm spanning world-cell x in [x0, x1); 1 m deep.
double armHeight(const MatSimCore& s, int x0, int x1) {
    const int cpm = s.cellsPerM();
    uint64_t units = 0;
    for (int y = cpm; y < s.size().y; ++y)
        for (int z = 0; z < s.size().z; ++z)
            for (int x = x0; x < x1; ++x) {
                const Cell c = s.at({x, y, z});
                if (cellMaterial(c) == Mat::Water) units += cellFill(c);
            }
    const double m3 = double(units) / double(s.full()) / double(cpm * cpm * cpm);
    return m3 / (double(x1 - x0) / cpm);
}
// At rest = nothing moved and no head changed on two consecutive ticks (both block offsets).
int runToRest(MatSimCore& s, int limit) {
    bool prev = false;
    for (int t = 1; t <= limit; ++t) {
        const bool q = s.step(1).lastTickQuiet();
        if (q && prev) return t;
        prev = q;
    }
    return -1;
}
} // namespace

TEST(MatSimPressure, UTubeArmsLevel) {
    const UTube tubes[] = {{2, 2}, {1, 3}};
    for (const UTube& u : tubes)
        for (int cpm : {1, 3, 9}) {
            MatSimCore s = uTube(u, cpm, 4.0);
            const double want = 4.0 * u.leftW / double(u.leftW + u.rightW);   // m above the connector
            const int limit = cpm == 9 ? 12000 : 9000;
            const int restAt = runToRest(s, limit);
            const double hl = armHeight(s, 0, u.leftW * cpm);
            const double hr = armHeight(s, (u.leftW + 1) * cpm, u.totalW() * cpm);
            const double tol = std::max(0.25 / cpm, 0.03);
            std::printf("[MatSimPressure] U-tube %d|%d m at 1/%d m: left %.3f m, right %.3f m (level %.3f), "
                        "rest at tick %d (%.1f s)\n",
                        u.leftW, u.rightW, cpm, hl, hr, want, restAt, restAt / 60.0);
            EXPECT_NE(restAt, -1) << "never came to rest in " << limit << " ticks";
            EXPECT_NEAR(hl, hr, tol) << "arms not level at 1/" << cpm << " m";
            EXPECT_NEAR(hr, want, tol) << "right arm off the volume level at 1/" << cpm << " m";
            EXPECT_EQ(s.ledger(), s.expectedLedger());
        }
}

TEST(MatSimPressure, ControlWithoutPressureTheFarArmStaysEmpty) {
    Config off;
    off.pressure = false;
    MatSimCore s = uTube({2, 2}, 3, 4.0, off);
    runToRest(s, 9000);
    const double hr = armHeight(s, 9, 15);
    std::printf("[MatSimPressure] control (pressure off) at 1/3 m: right arm %.3f m\n", hr);
    EXPECT_LT(hr, 0.05) << "without the pressure rule the far arm should not rise (P1's failure)";
}

TEST(MatSimPressure, SleepEqualsDenseInAUTube) {
    // The head is read from a far source, so a sleeping brick must wake when the far surface
    // moves. Dense and sleeping runs must agree on every tick, including the head field.
    Config dense;
    dense.sleep = false;
    MatSimCore a = uTube({2, 2}, 3, 4.0, dense);
    MatSimCore b = uTube({2, 2}, 3, 4.0);
    const IVec3 lo{0, 0, 0};
    const IVec3 hi{a.size().x - 1, a.size().y - 1, a.size().z - 1};
    uint64_t skipped = 0;
    for (int t = 1; t <= 2400; ++t) {
        const StepStats sa = a.step(1);
        const StepStats sb = b.step(1);
        skipped += sa.evaluatedBlocks - sb.evaluatedBlocks;
        IVec3 w;
        ASSERT_FALSE(firstDiff(a, b, lo, hi, w))
            << "tick " << t << " world cell (" << w.x << "," << w.y << "," << w.z << "): dense "
            << cellStr(a.at(w)) << " vs sleep " << cellStr(b.at(w));
        ASSERT_EQ(a.headPtr(), b.headPtr()) << "head pointers differ at tick " << t;
        ASSERT_EQ(a.head(), b.head()) << "heads differ at tick " << t;
    }
    std::printf("[MatSimPressure] U-tube dense == sleep for 2400 ticks; %llu block evaluations skipped\n",
                (unsigned long long)skipped);
}

TEST(MatSimPressure, FourVesselsShareOneLevel) {
    // Stress (count): four chambers of widths 1, 2, 1, 3 m, joined by one 1 m floor connector
    // under 1 m dividers; only the first is filled, 5 m tall. Every chamber must end at the one
    // volume-derived level, at 1 and 1/3 m.
    const int widths[4] = {1, 2, 1, 3};
    int x0[4], W = 0;
    for (int k = 0; k < 4; ++k) { x0[k] = W; W += widths[k] + (k < 3 ? 1 : 0); }
    auto world = [&](int mx, int my, int) {
        if (my < 9) return Occ::Empty;
        for (int k = 0; k < 3; ++k) {
            const int d = 9 * (x0[k] + widths[k]);
            if (mx >= d && mx < d + 9) return Occ::Solid;
        }
        return Occ::Empty;
    };
    for (int cpm : {1, 3}) {
        MatSimCore s;
        std::string why;
        ASSERT_TRUE(s.create({0, 0, 0}, {W * cpm, 7 * cpm, cpm}, cpm, world, &why)) << why;
        s.fillBox(Mat::Water, {0, 0, 0}, {W * cpm, cpm, cpm}, 1.0);
        s.fillBox(Mat::Water, {0, cpm, 0}, {widths[0] * cpm, 5 * cpm, cpm}, 1.0);
        const double want = 5.0 * widths[0] / 7.0;
        const int restAt = runToRest(s, 20000);
        const double tol = std::max(0.25 / cpm, 0.03);
        std::string line;
        for (int k = 0; k < 4; ++k) {
            const double h = armHeight(s, x0[k] * cpm, (x0[k] + widths[k]) * cpm);
            char b[48];
            std::snprintf(b, sizeof(b), " %.3f", h);
            line += b;
            EXPECT_NEAR(h, want, tol) << "chamber " << k << " at 1/" << cpm << " m";
        }
        std::printf("[MatSimPressure] four vessels at 1/%d m:%s m (level %.3f), rest at tick %d (%.1f s)\n", cpm,
                    line.c_str(), want, restAt, restAt / 60.0);
        EXPECT_NE(restAt, -1);
        EXPECT_EQ(s.ledger(), s.expectedLedger());
    }
}

TEST(MatSimPressure, BoxTooTallForTheHeadFieldIsRefused) {
    // Boundary: the head word holds (height + 1) x FULL below 2^30 - 1. At 1 m cells that is
    // 5,752 cells (5,753 x 186,624 = 1,073,647,872); one more must be refused, at create and by a
    // switch, never wrap.
    MatSimCore s;
    std::string why;
    EXPECT_TRUE(s.create({0, 0, 0}, {1, 5752, 1}, 1, openAir, &why)) << why;
    EXPECT_FALSE(s.create({0, 0, 0}, {1, 5753, 1}, 1, openAir, &why)) << "a box too tall for the head field was accepted";
    EXPECT_NE(why.find("pressure"), std::string::npos) << why;
    ASSERT_TRUE(s.create({0, 0, 0}, {3, 17256, 3}, 3, openAir, &why)) << why;   // 5,752 m at 1/3 m
    EXPECT_TRUE(s.setCellSize(1, openAir, &why)) << why;
    ASSERT_TRUE(s.create({0, 0, 0}, {3, 17259, 3}, 3, openAir, &why)) << why;   // 5,753 m at 1/3 m
    EXPECT_FALSE(s.setCellSize(1, openAir, &why)) << "a switch to a box too tall for the head field was accepted";
    EXPECT_NE(why.find("pressure"), std::string::npos) << why;
    EXPECT_EQ(s.cellsPerM(), 3) << "a refused switch changed the box";
}

TEST(MatSimPressure, HoleUnderWaterFills) {
    // Audit R2: a hole (an emptied cell) deep in still water, or an air pocket under a ceiling
    // beside it, must fill; the box must never come to rest with water hanging over a not-full
    // cell. At every cell size, in a 3 x 4 x 3 m basin filled 3 m deep.
    for (int cpm : {1, 3, 9}) {
        auto shelf = [](int mx, int my, int mz) {   // a 1 m shelf (ceiling) at 1-2 m height over one corner
            return (my >= 9 && my < 18 && mx < 9 && mz < 9) ? Occ::Solid : Occ::Empty;
        };
        MatSimCore s;
        std::string why;
        ASSERT_TRUE(s.create({0, 0, 0}, {3 * cpm, 4 * cpm, 3 * cpm}, cpm, shelf, &why)) << why;
        s.fillBox(Mat::Water, {0, 0, 0}, {3 * cpm, 3 * cpm, 3 * cpm}, 1.0);
        s.setCell({2 * cpm, cpm / 2, 2 * cpm}, kEmpty, 0);                     // a hole near the floor
        s.setCell({2 * cpm - 1, 2 * cpm, cpm}, Mat::Water, s.full() / 3);       // a part-full cell mid-depth
        s.setCell({cpm / 2, cpm - 1, cpm / 2}, kEmpty, 0);                     // an air pocket under the shelf
        const int restAt = runToRest(s, 12000);
        EXPECT_NE(restAt, -1) << "cpm " << cpm << ": never came to rest";
        int hanging = 0;
        IVec3 first{-1, -1, -1};
        for (int y = 0; y + 1 < s.size().y; ++y)
            for (int z = 0; z < s.size().z; ++z)
                for (int x = 0; x < s.size().x; ++x) {
                    const Cell c = s.at({x, y, z}), up = s.at({x, y + 1, z});
                    if (cellMaterial(c) == kSolid || cellMaterial(up) != Mat::Water) continue;
                    if (cellFill(c) + s.pressureDeadband() < s.full()) {
                        if (!hanging) first = {x, y, z};
                        ++hanging;
                    }
                }
        std::printf("[MatSimPressure] hole under water at 1/%d m: rest at tick %d, %d cells under water not full\n",
                    cpm, restAt, hanging);
        EXPECT_EQ(hanging, 0) << "cpm " << cpm << ": water hangs over a not-full cell, first at (" << first.x
                              << "," << first.y << "," << first.z << ") fill " << cellFill(s.at(first));
        EXPECT_EQ(s.ledger(), s.expectedLedger());
    }
}

TEST(MatSimPressure, WideUTubeRestsLevelAtEverySize) {
    // The live rig's U-tube half (found live 2026-11-04): a 16 x 8 x 7 m box, a 1 m divider at x 8 m
    // open below 1 m, the 8 m-wide left arm filled 5 m deep, the 7 m right arm empty. The first
    // pressure rule (one source per cell, chosen with a per-hop cost) left the arms 2-3 cm apart at
    // every size (a bias that grows with vessel width) and never came to rest at 1/3 m: a full cell
    // that saw the far, higher arm pushed up while its neighbour saw only the surface above it, so
    // water circulated. Prediction: both arms at the volume level within two deadbands + 3 mm, and
    // at rest, at 1 and 1/3 m (1/9 m is the integration test's: 1.4 M cell-ticks per second here).
    auto world = [](int mx, int my, int) { return (mx >= 72 && mx < 81 && my >= 9) ? Occ::Solid : Occ::Empty; };
    for (int cpm : {1, 3}) {
        MatSimCore s;
        std::string why;
        ASSERT_TRUE(s.create({0, 0, 0}, {16 * cpm, 8 * cpm, 7 * cpm}, cpm, world, &why)) << why;
        s.fillBox(Mat::Water, {0, 0, 0}, {8 * cpm, 5 * cpm, 7 * cpm}, 1.0);
        const int restAt = runToRest(s, 24000);
        // Heights above the floor, the connector row included, 7 m deep arms.
        const double hl = armHeight(s, 0, 8 * cpm) / 7.0 + 1.0, hr = armHeight(s, 9 * cpm, 16 * cpm) / 7.0 + 1.0;
        const double want = (8.0 * 5.0 * 7.0 - 7.0) / (15.0 * 7.0) + 0.0;   // minus the connector cell under the divider
        const double tol = 1.0 / (32.0 * cpm) + 0.003;   // two deadbands (FULL/32) + 3 mm: no width-dependent bias
        std::printf("[MatSimPressure] wide U-tube at 1/%d m: left %.3f m, right %.3f m (level %.3f), rest at tick %d (%.1f s)\n",
                    cpm, hl, hr, want, restAt, restAt / 60.0);
        EXPECT_NE(restAt, -1) << "never came to rest at 1/" << cpm << " m";
        EXPECT_NEAR(hl, hr, tol) << "arms not level at 1/" << cpm << " m";
        EXPECT_NEAR(hr, want, tol);
        EXPECT_EQ(s.ledger(), s.expectedLedger());
    }
}

// DIAGNOSTIC (disabled; run with --gtest_also_run_disabled_tests): where the pressure rule's
// throughput goes in the 2|2 U-tube at 1/3 m. Per sample: right arm height, pressurised holes,
// free-surface cells and how many of them stand within the deadband of the head (the absorbers
// holes are routed to), distinct route sources.
TEST(MatSimDiag, DISABLED_UTubeThroughput) {
    for (int cpm : {1, 3}) {
        MatSimCore s = uTube({2, 2}, cpm, 4.0);
        const IVec3 n = s.size();
        const uint32_t F = s.full(), db = s.pressureDeadband();
        std::printf("[diag] cpm %d F %u db %u K %u\n", cpm, F, db, s.orificeK());
        for (int t = 0; t <= 2000; ++t) {
            if (t % (cpm == 1 ? 5 : 25) == 0) {
                int holes = 0, surf = 0, absorb = 0, partialClosed = 0;
                uint32_t headMax = 0;
                std::vector<uint32_t> srcs;
                for (size_t i = 0; i < s.head().size(); ++i) headMax = std::max(headMax, s.head()[i] & kHeadMask);
                const auto& Q = s.headPtr();
                const auto& Hh = s.head();
                const auto& M = s.headMaxPtr();
                for (size_t i = 0; i < Q.size(); ++i) {
                    const uint32_t hv = Hh[i];
                    if (M[i] != kNoHead && (M[i] >> 8) == i) {   // a free surface
                        ++surf;
                        if (Q[i] == (uint32_t(i) << 8)) ++absorb;   // its own route source = an absorber
                    }
                    if (Q[i] != kNoHead && std::find(srcs.begin(), srcs.end(), Q[i] >> 8) == srcs.end()) srcs.push_back(Q[i] >> 8);
                    (void)hv;
                }
                for (int y = 0; y < n.y; ++y)
                    for (int z = 0; z < n.z; ++z)
                        for (int x = 0; x < n.x; ++x) {
                            const Cell c = s.at({x, y, z});
                            if (cellMaterial(c) == kSolid || cellFill(c) >= F) continue;
                            // closed above: solid or liquid over it
                            const Cell up = y + 1 < n.y ? s.at({x, y + 1, z}) : makeCell(kSolid, 0);
                            if (cellFill(c) > 0 && (cellMaterial(up) == kSolid || cellFill(up) > 0)) ++partialClosed;
                        }
                (void)holes; (void)headMax;
                std::printf("[diag] t %4d right %.3f left %.3f surf %d absorbers %d routeSrcs %zu partialClosed %d\n", t,
                            armHeight(s, 3 * cpm, 5 * cpm), armHeight(s, 0, 2 * cpm), surf, absorb, srcs.size(), partialClosed);
            }
            if (s.step(1).lastTickQuiet() && t > 10) { std::printf("[diag] quiet at %d\n", t); break; }
        }
    }
}

TEST(MatSimDiag, DISABLED_UTubeSlice) {
    // Prints the z = 1 slice of the 2|2 U-tube at 1/3 m: fill in tenths ('#' full, '.' empty,
    // 'S' solid) and the route hops of each cell, on four consecutive ticks.
    const char* env = std::getenv("MATSIM_DIAG_CPM");
    const int cpm = env ? std::atoi(env) : 3;
    MatSimCore s = uTube({2, 2}, cpm, 4.0);
    const IVec3 n = s.size();
    const uint32_t F = s.full();
    const int zs = cpm / 2;
    for (int t = 0; t <= 303; ++t) {
        if (t >= 300) {
            std::printf("[slice] tick %d (z=1): fill tenths | route hops\n", t);
            for (int y = n.y - 1; y >= 0; --y) {
                std::string a, b;
                for (int x = 0; x < n.x; ++x) {
                    const Cell cell = s.at({x, y, zs});
                    const size_t i = size_t(x) + size_t(n.x) * (size_t(zs) + size_t(n.z) * size_t(y));
                    if (cellMaterial(cell) == kSolid) { a += 'S'; b += "  S"; continue; }
                    const uint32_t f = cellFill(cell);
                    a += f == 0 ? '.' : (f >= F ? '#' : char('0' + std::min<uint32_t>(9, f * 10 / F)));
                    if (cpm == 1) std::printf("[slice]   x%d y%d fill %u/%u head %u closed %d\n", x, y, f, F, s.head()[i] & kHeadMask, (s.head()[i] & kHeadClosed) ? 1 : 0);
                    const uint32_t q = s.headPtr()[i];
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "%3d", q == kNoHead ? -1 : int(q & 0xFFu));
                    b += buf;
                }
                std::printf("[slice] y%2d %s   %s\n", y, a.c_str(), b.c_str());
            }
        }
        s.step(1);
    }
}

TEST(MatSimDiag, DISABLED_AbsorberBandSweep) {
    // 2|2 U-tube; prints ticks to 50% / 90% of the transfer, the arm gap at the end, rest tick.
    const char* only = std::getenv("MATSIM_DIAG_CPM");
    for (int cpm : {3, 9}) {
        if (only && std::atoi(only) != cpm) continue;
        MatSimCore s = uTube({2, 2}, cpm, 4.0);
        int t50 = -1, t90 = -1, rest = -1;
        bool prev = false;
        const int limit = cpm == 9 ? 12000 : 4000;
        int t = 1;
        for (; t <= limit; ++t) {
            const bool q = s.step(1).lastTickQuiet();
            if (t % 10 == 0 || q) {
                const double hr = armHeight(s, 3 * cpm, 5 * cpm);
                if (t50 < 0 && hr >= 1.0) t50 = t;
                if (t90 < 0 && hr >= 1.8) t90 = t;
            }
            if (q && prev) { rest = t; break; }
            prev = q;
        }
        const double hl = armHeight(s, 0, 2 * cpm), hr = armHeight(s, 3 * cpm, 5 * cpm);
        std::printf("[sweep] band %s cpm %d: t50 %d t90 %d rest %d (ran %d) left %.4f right %.4f gap %.4f m\n",
                    std::getenv("MATSIM_ABS_DIV") ? std::getenv("MATSIM_ABS_DIV") : "db", cpm, t50, t90, rest,
                    t > limit ? limit : t, hl, hr, hl - hr);
    }
}

TEST(MatSimDiag, DISABLED_HoleStallCauses) {
    // Every closed, non-full liquid cell (a hole) classified by its 6 face neighbours, block parity
    // ignored: A no full same-liquid neighbour; B full neighbours, none with fewer route hops;
    // C a full neighbour with fewer hops but the head excess too small; D a valid donor exists.
    const char* env = std::getenv("MATSIM_DIAG_CPM");
    const int cpm = env ? std::atoi(env) : 3;
    MatSimCore s = uTube({2, 2}, cpm, 4.0);
    const IVec3 n = s.size();
    const uint32_t F = s.full(), db = s.pressureDeadband();
    auto idx = [&](int x, int y, int z) { return size_t(x) + size_t(n.x) * (size_t(z) + size_t(n.z) * size_t(y)); };
    const int every = cpm == 9 ? 250 : 50;
    for (int t = 0; t <= (cpm == 9 ? 3000 : 600); ++t) {
        if (t % every == 0 && t > 0) {
            int A = 0, B = 0, C = 0, D = 0;
            uint64_t deficit = 0;
            for (int y = 0; y < n.y; ++y)
                for (int z = 0; z < n.z; ++z)
                    for (int x = 0; x < n.x; ++x) {
                        const Cell c = s.at({x, y, z});
                        if (cellMaterial(c) == kSolid || cellFill(c) == 0 || cellFill(c) >= F) continue;
                        const size_t i = idx(x, y, z);
                        if (!(s.head()[i] & kHeadClosed)) continue;
                        deficit += F - cellFill(c);
                        const uint32_t ag = s.headPtr()[i] & 0xFFu;
                        const uint32_t ex = uint32_t(y) * F + cellFill(c);
                        bool full = false, fewer = false, valid = false;
                        const int d[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
                        for (auto& o : d) {
                            const int X = x + o[0], Y = y + o[1], Z = z + o[2];
                            if (X < 0 || Y < 0 || Z < 0 || X >= n.x || Y >= n.y || Z >= n.z) continue;
                            const Cell nc = s.at({X, Y, Z});
                            if (cellMaterial(nc) != cellMaterial(c) || cellFill(nc) + db < F) continue;
                            full = true;
                            const size_t j = idx(X, Y, Z);
                            if ((s.headPtr()[j] & 0xFFu) >= ag) continue;
                            fewer = true;
                            const uint32_t hd = s.head()[j] & kHeadMask;
                            if (hd == 0 || hd - 1 <= ex + 2 * db) continue;
                            valid = true;
                        }
                        (valid ? D : fewer ? C : full ? B : A)++;
                    }
            std::printf("[stall] cpm %d t %4d right %.3f holes %d (deficit %.1f cells): A nofull %d B nofewer %d C noexcess %d D canmove %d\n",
                        cpm, t, armHeight(s, 3 * cpm, 5 * cpm), A + B + C + D, double(deficit) / F, A, B, C, D);
        }
        s.step(1);
    }
}

TEST(MatSimDiag, DISABLED_WhatStillMoves) {
    // 2|2 U-tube at 1/9 m: after 9000 ticks, every cell that changes over 4 ticks, with its state.
    const int cpm = 9;
    MatSimCore s = uTube({2, 2}, cpm, 4.0);
    const IVec3 n = s.size();
    const uint32_t F = s.full();
    s.step(9000);
    for (int t = 0; t < 4; ++t) {
        std::vector<Cell> before(size_t(n.x) * n.y * n.z);
        for (int y = 0; y < n.y; ++y) for (int z = 0; z < n.z; ++z) for (int x = 0; x < n.x; ++x)
            before[size_t(x) + size_t(n.x) * (size_t(z) + size_t(n.z) * size_t(y))] = s.at({x, y, z});
        const StepStats st = s.step(1);
        int shown = 0, changed = 0;
        for (int y = 0; y < n.y; ++y) for (int z = 0; z < n.z; ++z) for (int x = 0; x < n.x; ++x) {
            const size_t i = size_t(x) + size_t(n.x) * (size_t(z) + size_t(n.z) * size_t(y));
            const Cell c = s.at({x, y, z});
            if (c == before[i]) continue;
            ++changed;
            if (shown++ < 12)
                std::printf("[moves] tick %d x%d y%d z%d fill %u -> %u /%u closed %d hops %u head %u E %u\n", 9001 + t, x, y, z,
                            cellFill(before[i]), cellFill(c), F, (s.head()[i] & kHeadClosed) ? 1 : 0, s.headPtr()[i] & 0xFFu,
                            s.head()[i] & kHeadMask, uint32_t(y) * F + cellFill(c));
        }
        std::printf("[moves] tick %d: %d cells changed, eligible %llu, head changes %llu\n", 9001 + t, changed,
                    (unsigned long long)st.lastTickEligible, (unsigned long long)st.lastTickHeadChanges);
    }
}

// ---------------------------------------------------------------------------------------------
// 6j — the thin-film floor (docs/MaterialSimPlan.md §6j). Water on a solid surface does not spread
// thinner than Config::filmFloor (fraction of a full cell): a film below it coalesces into beads of
// at least the floor instead of tiling the surface. The ledge "specks" of §6i were a uniform CA film
// lifted one particle per cell.

namespace {
struct FilmScan {
    int wet = 0;
    uint32_t minFill = 0xFFFFFFFFu;
};
FilmScan scanWater(const MatSimCore& s) {
    FilmScan r;
    for (int y = 0; y < s.size().y; ++y)
        for (int z = 0; z < s.size().z; ++z)
            for (int x = 0; x < s.size().x; ++x) {
                const Cell c = s.at({x, y, z});
                if (cellMaterial(c) != Mat::Water || !cellFill(c)) continue;
                ++r.wet;
                r.minFill = std::min(r.minFill, cellFill(c));
            }
    return r;
}
} // namespace

TEST(MatSimFilm, SpillStopsAtTheFilmFloor) {
    // A 5 x 0.67 x 5 m flat box at 1/3 m, one full cell of water on the floor in the middle.
    // Prediction: with no floor it spreads over many cells to a sliver depth (the control); with a
    // 1/4-cell floor it stops at 4 cells of exactly 1/4 (one 2x2 level, then nothing is allowed).
    for (float floorF : {0.0f, 0.25f}) {
        Config cfg;
        cfg.filmFloor = floorF;
        MatSimCore s;
        std::string why;
        ASSERT_TRUE(s.create({0, 0, 0}, {15, 2, 15}, 3, openAir, &why, cfg)) << why;
        ASSERT_TRUE(s.setCell({7, 0, 7}, Mat::Water, s.full()));
        s.step(900);
        const FilmScan r = scanWater(s);
        std::printf("[MatSimFilm] floor %.2f: %d wet cells, thinnest %.4f of a cell\n", floorF, r.wet,
                    double(r.minFill) / double(s.full()));
        EXPECT_EQ(s.ledger(), s.expectedLedger());
        if (floorF == 0.0f) {
            EXPECT_GT(r.wet, 4) << "control: the unfloored CA should spread the cell thin";
            EXPECT_LT(r.minFill, s.full() / 4) << "control";
        } else {
            EXPECT_LE(r.wet, 4);
            EXPECT_GE(r.minFill, uint32_t(floorF * s.full())) << "a film thinner than the floor";
        }
    }
}

TEST(MatSimFilm, AThinFilmBeadsUp) {
    // A uniform film already laid down (as a settle deposit can): 1/20 of a cell over a 3 x 3 m patch
    // of floor at 1/3 m. Prediction: with a 1/4-cell floor it coalesces (no level is allowed below
    // the floor) into at most a third of the cells; the CA conserves every unit; the control leaves
    // it alone.
    for (float floorF : {0.0f, 0.25f}) {
        Config cfg;
        cfg.filmFloor = floorF;
        MatSimCore s;
        std::string why;
        ASSERT_TRUE(s.create({0, 0, 0}, {9, 2, 9}, 3, openAir, &why, cfg)) << why;
        s.fillBox(Mat::Water, {0, 0, 0}, {9, 1, 9}, 0.05);
        s.step(600);
        const FilmScan r = scanWater(s);
        std::printf("[MatSimFilm] film 0.05 on a 9x9 floor, floor %.2f: %d wet cells, thinnest %.4f\n", floorF, r.wet,
                    double(r.minFill) / double(s.full()));
        EXPECT_EQ(s.ledger(), s.expectedLedger());
        if (floorF == 0.0f) EXPECT_EQ(r.wet, 81) << "control";
        else EXPECT_LE(r.wet, 81 / 3) << "the film did not bead";
    }
}

TEST(MatSimFilm, LedgeDripsAreNotSlivers) {
    // PoursOffLedge's scene: 2 cells of water on a 1 m ledge at 1/3 m. Count, every tick, the
    // airborne slivers (water cells under 1/4 full with empty air below). Prediction: the floor
    // run makes under 1/10 of the control's sliver-cell-ticks and still pours most water off.
    auto ledge = [](int mx, int my, int) { return (mx < 9 && my < 9) ? Occ::Solid : Occ::Empty; };
    long long slivers[2] = {};
    for (int run = 0; run < 2; ++run) {
        Config cfg;
        cfg.filmFloor = run ? 0.25f : 0.0f;
        MatSimCore s;
        std::string why;
        ASSERT_TRUE(s.create({0, 0, 0}, {9, 9, 6}, 3, ledge, &why, cfg)) << why;
        s.fillBox(Mat::Water, {0, 3, 0}, {3, 2, 6}, 1.0);
        const uint32_t q = s.full() / 4;
        uint64_t below = 0, onLedge = 0;
        for (int t = 0; t < 600; ++t) {
            s.step(1);
            for (int y = 1; y < 9; ++y)
                for (int z = 0; z < 6; ++z)
                    for (int x = 0; x < 9; ++x) {
                        const Cell c = s.at({x, y, z});
                        if (cellMaterial(c) != Mat::Water || !cellFill(c) || cellFill(c) >= q) continue;
                        if (cellMaterial(s.at({x, y - 1, z})) == kEmpty) {
                            if (run && slivers[run] < 40 && std::getenv("MATSIM_FILM_TRACE"))
                                std::printf("  sliver t%d (%d,%d,%d) %.3f\n", t, x, y, z, double(cellFill(c)) / s.full());
                            ++slivers[run];
                        }
                    }
        }
        for (int y = 0; y < 9; ++y)
            for (int z = 0; z < 6; ++z)
                for (int x = 0; x < 9; ++x) {
                    const Cell c = s.at({x, y, z});
                    if (cellMaterial(c) == Mat::Water) (y < 3 ? below : onLedge) += cellFill(c);
                }
        std::printf("[MatSimFilm] ledge, floor %.2f: %lld airborne sliver-cell-ticks, %.1f%% below the ledge\n",
                    cfg.filmFloor, slivers[run], 100.0 * double(below) / double(below + onLedge));
        EXPECT_GT(below, onLedge);
        EXPECT_EQ(s.ledger(), s.expectedLedger());
    }
    EXPECT_GT(slivers[0], 0) << "control: the unfloored ledge should drip slivers";
    EXPECT_LT(slivers[1] * 10, slivers[0]);
}

TEST(MatSimFilm, UTubesStillLevelWithTheFloor) {
    // The floor must not touch a body of water: only a layer resting on solid with a mean under the
    // floor is gathered. Prediction: the 2|2 and 1|3 U-tubes and the wide U-tube rest level to the
    // same tolerances as without it, at 1 and 1/3 m, ledger exact.
    Config cfg;
    cfg.filmFloor = 0.25f;
    const UTube tubes[] = {{2, 2}, {1, 3}};
    for (const UTube& u : tubes)
        for (int cpm : {1, 3}) {
            MatSimCore s = uTube(u, cpm, 4.0, cfg);
            const double want = 4.0 * u.leftW / double(u.leftW + u.rightW);
            const int restAt = runToRest(s, 9000);
            const double hl = armHeight(s, 0, u.leftW * cpm), hr = armHeight(s, (u.leftW + 1) * cpm, u.totalW() * cpm);
            const double tol = std::max(0.25 / cpm, 0.03);
            std::printf("[MatSimFilm] U-tube %d|%d at 1/%d m with the floor: left %.3f, right %.3f (level %.3f), rest %d\n",
                        u.leftW, u.rightW, cpm, hl, hr, want, restAt);
            EXPECT_NE(restAt, -1);
            EXPECT_NEAR(hl, hr, tol);
            EXPECT_NEAR(hr, want, tol);
            EXPECT_EQ(s.ledger(), s.expectedLedger());
        }
    auto world = [](int mx, int my, int) { return (mx >= 72 && mx < 81 && my >= 9) ? Occ::Solid : Occ::Empty; };
    for (int cpm : {1, 3}) {
        MatSimCore s;
        std::string why;
        ASSERT_TRUE(s.create({0, 0, 0}, {16 * cpm, 8 * cpm, 7 * cpm}, cpm, world, &why, cfg)) << why;
        s.fillBox(Mat::Water, {0, 0, 0}, {8 * cpm, 5 * cpm, 7 * cpm}, 1.0);
        const int restAt = runToRest(s, 12000);
        // The floor's invariant: once the arms have levelled it never acts again. Rest itself is the
        // old rules' (seed 1 at 1/3 m lands in a pressure-rule cycle that a fresh head field breaks
        // with or without the floor: MatSimDiag.DISABLED_FilmWideUTubeMoves, plan §6j).
        const uint64_t g0 = s.filmGathers(), h0 = s.filmHeld();
        s.step(4000);
        const double hl = armHeight(s, 0, 8 * cpm) / 7.0 + 1.0, hr = armHeight(s, 9 * cpm, 16 * cpm) / 7.0 + 1.0;
        const double want = (8.0 * 5.0 * 7.0 - 7.0) / (15.0 * 7.0);
        const double tol = 1.0 / (32.0 * cpm) + 0.003;
        std::printf("[MatSimFilm] wide U-tube at 1/%d m with the floor: left %.3f, right %.3f (level %.3f), rest %d, "
                    "film acts after: %llu\n", cpm, hl, hr, want, restAt,
                    (unsigned long long)(s.filmGathers() - g0 + s.filmHeld() - h0));
        EXPECT_EQ(s.filmGathers(), g0) << "the floor gathered a layer of a levelled body";
        EXPECT_EQ(s.filmHeld(), h0) << "the floor held a slide in a levelled body";
        EXPECT_NEAR(hl, hr, tol);
        EXPECT_NEAR(hr, want, tol);
        EXPECT_EQ(s.ledger(), s.expectedLedger());
    }
}

TEST(MatSimDiag, DISABLED_FilmWideUTubeMoves) {
    // 6j diagnosis: the wide U-tube at 1/3 m with a 1/4 floor; every cell that changes over 4 ticks after 24000.
    Config cfg;
    cfg.filmFloor = 0.25f;
    if (const char* e = std::getenv("MATSIM_FILM")) cfg.filmFloor = float(std::atof(e));
    if (const char* e = std::getenv("MATSIM_SEED")) cfg.seed = uint32_t(std::atoi(e));
    auto world = [](int mx, int my, int) { return (mx >= 72 && mx < 81 && my >= 9) ? Occ::Solid : Occ::Empty; };
    const int cpm = 3;
    if (std::getenv("MATSIM_REST_ONLY")) {
        MatSimCore r;
        std::string w;
        ASSERT_TRUE(r.create({0, 0, 0}, {16 * cpm, 8 * cpm, 7 * cpm}, cpm, world, &w, cfg)) << w;
        r.fillBox(Mat::Water, {0, 0, 0}, {8 * cpm, 5 * cpm, 7 * cpm}, 1.0);
        const int restAt = runToRest(r, 12000);
        const uint64_t g0 = r.filmGathers(), h0 = r.filmHeld();
        r.step(12000);
        std::printf("[film-moves] floor %.2f seed %u: rest at %d; film gathers %llu then %llu more, held %llu then %llu more\n",
                    cfg.filmFloor, cfg.seed, restAt, (unsigned long long)g0, (unsigned long long)(r.filmGathers() - g0),
                    (unsigned long long)h0, (unsigned long long)(r.filmHeld() - h0));
        // Hand the final state to a core WITHOUT the floor: does the old rule set rest from here?
        // Control: the same handoff WITH the floor (a fresh core resets the head field and the tick).
        for (float fl : {0.f, cfg.filmFloor}) {
            Config off = cfg;
            off.filmFloor = fl;
            MatSimCore o;
            ASSERT_TRUE(o.create({0, 0, 0}, {16 * cpm, 8 * cpm, 7 * cpm}, cpm, world, &w, off)) << w;
            for (int y = 0; y < r.size().y; ++y)
                for (int z = 0; z < r.size().z; ++z)
                    for (int x = 0; x < r.size().x; ++x) {
                        const Cell c = r.at({x, y, z});
                        if (cellMaterial(c) != kSolid) o.setCell({x, y, z}, cellMaterial(c), cellFill(c));
                    }
            std::printf("[film-moves] the same state handed to a fresh core, floor %.2f: rest at %d\n", fl, runToRest(o, 12000));
        }
        return;
    }
    MatSimCore s;
    std::string why;
    ASSERT_TRUE(s.create({0, 0, 0}, {16 * cpm, 8 * cpm, 7 * cpm}, cpm, world, &why, cfg)) << why;
    s.fillBox(Mat::Water, {0, 0, 0}, {8 * cpm, 5 * cpm, 7 * cpm}, 1.0);
    s.step(24000);
    const IVec3 n = s.size();
    const uint32_t F = s.full();
    for (int t = 0; t < 4; ++t) {
        std::vector<Cell> before(size_t(n.x) * n.y * n.z);
        for (int y = 0; y < n.y; ++y) for (int z = 0; z < n.z; ++z) for (int x = 0; x < n.x; ++x)
            before[size_t(x) + size_t(n.x) * (size_t(z) + size_t(n.z) * size_t(y))] = s.at({x, y, z});
        const StepStats st = s.step(1);
        int shown = 0, changed = 0;
        for (int y = 0; y < n.y; ++y) for (int z = 0; z < n.z; ++z) for (int x = 0; x < n.x; ++x) {
            const size_t i = size_t(x) + size_t(n.x) * (size_t(z) + size_t(n.z) * size_t(y));
            const Cell c = s.at({x, y, z});
            if (c == before[i]) continue;
            ++changed;
            if (shown++ < 16)
                std::printf("[film-moves] tick %d x%d y%d z%d fill %u -> %u /%u below %s\n", t, x, y, z, cellFill(before[i]),
                            cellFill(c), F, cellStr(s.at({x, y - 1, z})).c_str());
        }
        std::printf("[film-moves] tick %d: %d cells changed, eligible %llu, head changes %llu\n", t, changed,
                    (unsigned long long)st.lastTickEligible, (unsigned long long)st.lastTickHeadChanges);
    }
}

// 6k cost: when nothing changed anywhere on the last tick (no move, no head change, no edit), the head field's inputs are
// unchanged, so the pressure pass would rebuild the same field: sleep skips it (and, quiet for two ticks, the block loop).
// Measured: at 1/9 m the pressure pass is most of a resting box's ~260 ms Debug tick. Dense and sleeping runs must agree
// on cells, heads and head pointers every tick: to rest, through an edit that wakes it, and back to rest.
TEST(MatSimSleep, QuietTicksSkipThePressurePassExactly) {
    Config dense;
    dense.sleep = false;
    MatSimCore a = unevenBasin(dense), b = unevenBasin();
    const IVec3 lo{0, 0, 0};
    const IVec3 hi{a.size().x - 1, a.size().y - 1, a.size().z - 1};
    uint64_t skippedA = 0, skippedB = 0;
    for (int t = 1; t <= 1500; ++t) {
        if (t == 900) {   // the edit: a cell dropped into the sleeping basin
            ASSERT_TRUE(a.setCell({6, 5, 6}, Mat::Water, a.full()));
            ASSERT_TRUE(b.setCell({6, 5, 6}, Mat::Water, b.full()));
        }
        const StepStats sa = a.step(1), sb = b.step(1);
        skippedA += sa.pressurePassesSkipped;
        skippedB += sb.pressurePassesSkipped;
        IVec3 w;
        ASSERT_FALSE(firstDiff(a, b, lo, hi, w)) << "tick " << t << " world cell (" << w.x << "," << w.y << "," << w.z << ")";
        ASSERT_EQ(a.headPtr(), b.headPtr()) << "head pointers differ at tick " << t;
        ASSERT_EQ(a.head(), b.head()) << "heads differ at tick " << t;
    }
    std::printf("[MatSimSleep] quiet-tick skip: dense == sleep for 1500 ticks; pressure passes skipped %llu (dense %llu)\n",
                (unsigned long long)skippedB, (unsigned long long)skippedA);
    EXPECT_EQ(skippedA, 0u) << "the dense run must never skip";
    EXPECT_GT(skippedB, 100u) << "the sleeping basin must skip the pass once at rest";
}
