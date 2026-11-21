// MatSimLattice8Test -- docs/VoxelFoundationPlan.md F6.1 (prediction: docs/evidence/voxfound_f6_prediction.txt E1-E7):
// MatSim on the world's 1/8 m lattice (cells of 1, 1/2, 1/4, 1/8 m), every world a VoxFound VoxelGrid of 1/8 m voxels
// read through matSimOccupancy. The same invariants MatSimCoreTest holds on the 1/9 lattice: an exact ledger, an exact
// remap, determinism, sleep = dense, watertight walls, repose, communicating vessels.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "core/matsim/MatSimCore.h"
#include "core/voxfound/MatSimWorld.h"
#include "core/voxfound/VoxelGrid.h"

using namespace Phyxel::Core::MatSim;
using Phyxel::Core::VoxFound::VoxelGrid;
using Phyxel::Core::VoxFound::matSimOccupancy;

namespace {

constexpr int kL = 8;   // the world lattice: 8 voxels per metre

Config cfg8(Config c = {}) {
    c.lattice = kL;
    return c;
}

// An empty grid of `m` metres a side (in 1/8 m voxels), with an optional solid predicate in voxel coordinates.
template <class F>
VoxelGrid gridOf(glm::ivec3 metres, F solid) {
    VoxelGrid g;
    g.resize(metres * kL);
    for (int x = 0; x < g.dim.x; ++x)
        for (int y = 0; y < g.dim.y; ++y)
            for (int z = 0; z < g.dim.z; ++z)
                if (solid(x, y, z)) g.set({x, y, z}, 1);
    return g;
}
VoxelGrid emptyGrid(glm::ivec3 metres) {
    return gridOf(metres, [](int, int, int) { return false; });
}

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

uint32_t hash3(int x, int y, int z) {
    uint32_t h = uint32_t(x) * 73856093u ^ uint32_t(y) * 19349663u ^ uint32_t(z) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

int runToRest(MatSimCore& s, int limit) {
    bool prev = false;
    for (int t = 1; t <= limit; ++t) {
        const bool q = s.step(1).lastTickQuiet();
        if (q && prev) return t;
        prev = q;
    }
    return -1;
}

}  // namespace

// E1: the lattice's sizes and units
TEST(MatSimLattice8, FullUnitsNestByEight) {
    EXPECT_EQ(fullUnits(8, kL), 256u);
    EXPECT_EQ(fullUnits(4, kL), 2048u);
    EXPECT_EQ(fullUnits(2, kL), 16384u);
    EXPECT_EQ(fullUnits(1, kL), 131072u);
    for (int c : {4, 2, 1}) EXPECT_EQ(fullUnits(c, kL), 8u * fullUnits(2 * c, kL)) << c;
    EXPECT_EQ(fullUnits(3, kL), 0u);
    EXPECT_EQ(fullUnits(9, kL), 0u);
    EXPECT_EQ(fullUnits(2), 0u) << "the default lattice stays 9";
    EXPECT_EQ(fullUnits(9), 256u);
    const VoxelGrid g = emptyGrid({1, 1, 1});
    MatSimCore s;
    std::string why;
    EXPECT_FALSE(s.create({0, 0, 0}, {3, 3, 3}, 3, matSimOccupancy(g), &why, cfg8())) << "3 cells/m on lattice 8";
    EXPECT_FALSE(s.create({0, 0, 0}, {2, 2, 2}, 2, matSimOccupancy(g), &why)) << "2 cells/m on lattice 9";
    Config bad;
    bad.lattice = 7;
    EXPECT_FALSE(s.create({0, 0, 0}, {1, 1, 1}, 1, matSimOccupancy(g), &why, bad)) << "lattice 7";
    ASSERT_TRUE(s.create({0, 0, 0}, {2, 2, 2}, 2, matSimOccupancy(g), &why, cfg8())) << why;
    EXPECT_EQ(s.full(), 16384u);
    // the grid's edge: a box reaching past it reads unknown occupancy and is refused
    EXPECT_FALSE(s.create({0, 0, 0}, {3, 2, 2}, 2, matSimOccupancy(g), &why, cfg8())) << "a box past the grid";
}

// E2: the ledger exact every tick -- a dam break at every size, every mobile material
TEST(MatSimLattice8, LedgerExactDamBreakEverySize) {
    const VoxelGrid g = emptyGrid({4, 3, 4});
    for (int cpm : {1, 2, 4, 8})
        for (uint8_t mat : {Mat::Water, Mat::Sand, Mat::Snow, Mat::Gravel}) {
            MatSimCore s;
            std::string why;
            ASSERT_TRUE(s.create({0, 0, 0}, {4 * cpm, 3 * cpm, 4 * cpm}, cpm, matSimOccupancy(g), &why, cfg8())) << why;
            s.fillBox(mat, {0, 0, 0}, {cpm, 2 * cpm, 4 * cpm}, 1.0);
            const int ticks = cpm == 8 ? 3000 : 10000;
            int bad = -1, m = 0;
            long long d = 0;
            for (int t = 1; t <= ticks && bad < 0; ++t) {
                s.step(1);
                if (ledgerDelta(s, m, d)) bad = t;
            }
            EXPECT_EQ(bad, -1) << "cpm " << cpm << " material " << int(mat) << ": material " << m << " off by " << d;
            EXPECT_EQ(s.ledger()[mat], uint64_t(cpm) * 2 * cpm * 4 * cpm * s.full());
        }
}

// E3: the remap 1 -> 2 -> 4 -> 8 -> 4 -> 2 -> 1 exact, with a pillar and a 1/8 m shelf; a split is bottom-up
TEST(MatSimLattice8, RemapRoundTripExactAndBottomUp) {
    const VoxelGrid g = gridOf({3, 3, 3}, [](int x, int y, int z) {
        if (x >= 8 && x < 16 && z >= 8 && z < 16 && y < 16) return true;   // a 1 m pillar, 2 m tall
        return y == 18 && x >= 16 && z < 8;                                 // a 1/8 m shelf
    });
    MatSimCore s;
    std::string why;
    ASSERT_TRUE(s.create({0, 0, 0}, {3, 3, 3}, 1, matSimOccupancy(g), &why, cfg8())) << why;
    for (int y = 0; y < 3; ++y)
        for (int z = 0; z < 3; ++z)
            for (int x = 0; x < 3; ++x)
                s.setCell({x, y, z}, Mat::Water, hash3(x, y, z) % s.full() + 1);   // one material: every merge is exact
    const Ledger l0 = s.ledger();
    ASSERT_EQ(l0, s.expectedLedger());
    for (int cpm : {2, 4, 8, 4, 2, 1}) {
        ASSERT_TRUE(s.setCellSize(cpm, matSimOccupancy(g), &why)) << "-> " << cpm << ": " << why;
        ASSERT_EQ(s.ledger(), l0) << "switching to " << cpm << " cells/m changed the ledger (trapped "
                                  << s.trappedUnits() << ")";
        s.step(20);
        ASSERT_EQ(s.ledger(), l0) << "stepping at " << cpm << " cells/m changed the ledger";
    }
    std::printf("[MatSimLattice8] remap 1->2->4->8->4->2->1 exact; trapped %llu units\n",
                (unsigned long long)s.trappedUnits());
    // bottom-up: half a 1 m cell of water splits into the four lower 1/2 m children, full
    const VoxelGrid e = emptyGrid({1, 1, 1});
    MatSimCore h;
    ASSERT_TRUE(h.create({0, 0, 0}, {1, 1, 1}, 1, matSimOccupancy(e), &why, cfg8())) << why;
    ASSERT_TRUE(h.setCell({0, 0, 0}, Mat::Water, fullUnits(1, kL) / 2));
    ASSERT_TRUE(h.setCellSize(2, matSimOccupancy(e), &why)) << why;
    const uint32_t F = fullUnits(2, kL);
    for (int z = 0; z < 2; ++z)
        for (int x = 0; x < 2; ++x) {
            EXPECT_EQ(cellFill(h.at({x, 0, z})), F) << "lower child " << x << "," << z;
            EXPECT_EQ(cellFill(h.at({x, 1, z})), 0u) << "upper child " << x << "," << z;
        }
}

namespace {
// A stepped room in 1/8 m voxels at 1/4 m cells: a 1 m step and a low 1/4 m wall; box cells (-3, 0, -2) + 40 x 9 x 40.
const glm::ivec3 kRoomOrigin{-6, 0, -4};   // world voxels of the grid's (0,0,0) = box origin cells x 2
VoxelGrid steppedRoomGrid() {
    VoxelGrid g;
    g.resize({80, 18, 80});
    for (int x = 0; x < 80; ++x)
        for (int y = 0; y < 18; ++y)
            for (int z = 0; z < 80; ++z) {
                const int wx = x + kRoomOrigin.x, wz = z + kRoomOrigin.z;
                const bool step = wx >= 16 && wx < 24 && y < 8;
                const bool wall = wz >= 24 && wz < 26 && y < 12 && wx < 26;
                if (step || wall) g.set({x, y, z}, 1);
            }
    return g;
}
MatSimCore mixedScene(const VoxelGrid& g, bool sleep, uint32_t seed = 1) {
    Config c;
    c.sleep = sleep;
    c.seed = seed;
    MatSimCore s;
    std::string why;
    EXPECT_TRUE(s.create({-3, 0, -2}, {40, 9, 40}, 4, matSimOccupancy(g, kRoomOrigin), &why, cfg8(c))) << why;
    s.fillBox(Mat::Water, {-3, 0, -2}, {4, 6, 9}, 1.0);
    s.fillBox(Mat::Sand, {6, 4, 3}, {3, 4, 3}, 1.0);
    s.fillBox(Mat::Water, {3, 6, 8}, {2, 2, 2}, 0.5);
    return s;
}
}  // namespace

// E4: determinism (and the seed's control) and sleep = dense
TEST(MatSimLattice8, DeterministicAndSleepEqualsDense) {
    const VoxelGrid g = steppedRoomGrid();
    MatSimCore a = mixedScene(g, true), b = mixedScene(g, true), c = mixedScene(g, true, 99);
    a.step(800);
    b.step(800);
    c.step(800);
    EXPECT_EQ(a.cells(), b.cells()) << "two runs differ";
    EXPECT_NE(a.cells(), c.cells()) << "the seed has no effect";
    MatSimCore dense = mixedScene(g, false), sleepy = mixedScene(g, true);
    uint64_t skipped = 0;
    for (int t = 1; t <= 800; ++t) {
        const StepStats x = dense.step(1), y = sleepy.step(1);
        skipped += x.evaluatedBlocks - y.evaluatedBlocks;
        ASSERT_EQ(dense.cells(), sleepy.cells()) << "sleep changed the result at tick " << t;
    }
    EXPECT_GT(skipped, 0u) << "sleep never skipped a block";
    EXPECT_EQ(sleepy.ledger(), sleepy.expectedLedger());
    std::printf("[MatSimLattice8] two runs bit-identical; dense == sleep over 800 ticks, %llu evaluations skipped\n",
                (unsigned long long)skipped);
}

// E5: a one-voxel-thick diagonal wall (edge-joined 1/8 m voxels) holds water and sand at every size
TEST(MatSimLattice8, DiagonalVoxelWallIsWatertight) {
    const VoxelGrid g = gridOf({4, 3, 4}, [](int x, int, int z) { return x + z == 24; });
    int ran = 0;
    for (int cpm : {1, 2, 4, 8})
        for (uint8_t mat : {Mat::Water, Mat::Sand}) {
            const int k = kL / cpm;
            MatSimCore s;
            std::string why;
            ASSERT_TRUE(s.create({0, 0, 0}, {4 * cpm, 3 * cpm, 4 * cpm}, cpm, matSimOccupancy(g), &why, cfg8())) << why;
            uint64_t near0 = 0;
            for (int y = 0; y < 2 * cpm; ++y)
                for (int z = 0; z < 4 * cpm; ++z)
                    for (int x = 0; x < 4 * cpm; ++x)
                        if ((x * k + k - 1) + (z * k + k - 1) < 24 && s.setCell({x, y, z}, mat, s.full())) near0 += s.full();
            if (near0 == 0) continue;   // 1 m cells: the wall's cells leave no whole cell on the near side
            s.step(cpm == 8 ? 600 : 1500);
            uint64_t far = 0;
            for (int y = 0; y < 3 * cpm; ++y)
                for (int z = 0; z < 4 * cpm; ++z)
                    for (int x = 0; x < 4 * cpm; ++x) {
                        const Cell c = s.at({x, y, z});
                        if (x * k + z * k > 24 && cellMaterial(c) == mat) far += cellFill(c);
                    }
            std::printf("[MatSimLattice8] diagonal voxel wall at 1/%d m, material %d: %llu units on the near side, %llu "
                        "through\n", cpm, int(mat), (unsigned long long)near0, (unsigned long long)far);
            ++ran;
            EXPECT_EQ(far, 0u) << "cpm " << cpm << " material " << int(mat) << ": " << far << " of " << near0
                               << " units through the wall";
            EXPECT_EQ(s.ledger(), s.expectedLedger());
        }
    EXPECT_EQ(ran, 8) << "every size and material must have had a near side to fill";
}

// E6: a sand pile stands at its angle of repose (34 deg) at 1/8 m, within MatSimCoreTest's +-0.03
TEST(MatSimLattice8, SandPileStandsAtRepose) {
    const VoxelGrid g = emptyGrid({3, 2, 3});   // 24 x 16 x 24 voxels = cells at 8 cells/m
    MatSimCore s;
    std::string why;
    ASSERT_TRUE(s.create({0, 0, 0}, {24, 12, 24}, 8, matSimOccupancy(g), &why, cfg8())) << why;
    s.fillBox(Mat::Sand, {10, 0, 10}, {4, 12, 4}, 1.0);
    int asleepAt = -1;
    uint64_t prev = 1;
    for (int t = 1; t <= 6000; ++t) {
        const uint64_t e = s.step(1).lastTickEligible;
        if (e == 0 && prev == 0) { asleepAt = t; break; }
        prev = e;
    }
    ASSERT_GE(asleepAt, 0) << "never came to rest";
    const double F = s.full();
    std::vector<double> h(24, 0.0);
    for (int x = 0; x < 24; ++x) {
        for (int zz = 11; zz <= 12; ++zz)
            for (int y = 0; y < 12; ++y) {
                const Cell c = s.at({x, y, zz});
                if (cellMaterial(c) == Mat::Sand) h[x] += cellFill(c) / F;
            }
        h[x] *= 0.5;
    }
    int peak = 0;
    for (int x = 1; x < 24; ++x) if (h[x] > h[peak]) peak = x;
    int edge = peak;
    while (edge + 1 < 24 && h[edge + 1] > 0.0) ++edge;
    std::vector<double> drops;
    for (int x = peak; x < edge; ++x) drops.push_back(h[x] - h[x + 1]);
    std::sort(drops.begin(), drops.end());
    const double slope = drops.empty() ? 0.0 : drops[drops.size() / 2], want = std::tan(34.0 * 3.14159265358979 / 180.0);
    std::printf("[MatSimLattice8] sand at 1/8 m: rest at tick %d, peak %.2f cells, run %d, median drop %.3f (want %.3f)\n",
                asleepAt, h[peak], edge - peak, slope, want);
    EXPECT_GE(edge - peak, 3);
    EXPECT_NEAR(slope, want, 0.03);
    EXPECT_EQ(s.ledger(), s.expectedLedger());
}

// E7: a U-tube's arms level (communicating vessels) at 1/2, 1/4 and 1/8 m
TEST(MatSimLattice8, UTubeArmsLevel) {
    const int leftW = 2, rightW = 2, totalW = leftW + 1 + rightW;
    // a closed box 5 x 6 x 1 m; the divider x in [2, 3) m, y >= 1 m, leaving a 1 m connector on the floor
    const VoxelGrid g = gridOf({totalW, 6, 1}, [&](int x, int y, int) { return x >= 8 * leftW && x < 8 * (leftW + 1) && y >= 8; });
    for (int cpm : {2, 4, 8}) {
        MatSimCore s;
        std::string why;
        ASSERT_TRUE(s.create({0, 0, 0}, {totalW * cpm, 6 * cpm, cpm}, cpm, matSimOccupancy(g), &why, cfg8())) << why;
        s.fillBox(Mat::Water, {0, 0, 0}, {totalW * cpm, cpm, cpm}, 1.0);
        s.fillBox(Mat::Water, {0, cpm, 0}, {leftW * cpm, 4 * cpm, cpm}, 1.0);
        const int limit = cpm == 8 ? 12000 : 9000;
        const int restAt = runToRest(s, limit);
        auto arm = [&](int x0, int x1) {
            uint64_t units = 0;
            for (int y = cpm; y < s.size().y; ++y)
                for (int z = 0; z < s.size().z; ++z)
                    for (int x = x0; x < x1; ++x) {
                        const Cell c = s.at({x, y, z});
                        if (cellMaterial(c) == Mat::Water) units += cellFill(c);
                    }
            const double m3 = double(units) / s.full() / double(cpm * cpm * cpm);
            return m3 / (double(x1 - x0) / cpm);   // the arm is 1 m deep
        };
        const double hl = arm(0, leftW * cpm), hr = arm((leftW + 1) * cpm, totalW * cpm);
        const double want = 4.0 * leftW / double(leftW + rightW), tol = std::max(0.25 / cpm, 0.03);
        std::printf("[MatSimLattice8] U-tube 2|2 m at 1/%d m: left %.3f m, right %.3f m (level %.3f), rest at tick %d\n",
                    cpm, hl, hr, want, restAt);
        EXPECT_NE(restAt, -1) << "never came to rest in " << limit << " ticks at 1/" << cpm << " m";
        EXPECT_NEAR(hl, hr, tol) << "arms not level at 1/" << cpm << " m";
        EXPECT_NEAR(hr, want, tol) << "right arm off the volume level at 1/" << cpm << " m";
        EXPECT_EQ(s.ledger(), s.expectedLedger());
    }
}
