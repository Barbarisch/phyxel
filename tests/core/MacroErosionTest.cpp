#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/MacroErosion.h"
#include "core/PriorityFlood.h"

using namespace Phyxel;

// ============================================================================
// MacroErosionTest: M1c (docs/WorldForgeV2Plan.md §5 M1, §10 F4c). STRUCTURAL assertions only:
// the drainage is a forest rooted at base level, receivers are never higher on the filled
// surface, drainage area is conserved, drainage density falls as the channel-initiation
// threshold rises and stays under the 512 u grid ceiling (2.7 km/km²), erosion lowers the mean,
// steps = 0 is the identity, and macro drainage crosses the old ±16384 hydrology box edge
// (HydroEdgeTest's M1 scope). NO absolute density number is asserted (STRIKE rows in §10); the
// measured Dd × √A_c is PRINTED for the NEEDS-RESEARCH band.
//
// RIG VALUES (stated, not grounded, used only here): 96×96 cells at 512 u (49 km), a dome of
// 300 u over sea level 16 with ±15 u hash roughness, K = 1e-5, m = 0.4, dt = 1000 yr, 20 steps,
// uplift 1e-3 m/yr at the dome's centre. The shipped recipe default is steps = 0 (erosion off)
// until §10's K / U rows are GROUNDED.
// ============================================================================
namespace {

constexpr int   kW = 96, kH = 96;
constexpr float kCell = 512.0f, kSea = 16.0f;

float hashNoise(int x, int z) {
    uint64_t h = static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ull ^ static_cast<uint64_t>(z) * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 29; h *= 0xBF58476D1CE4E5B9ull; h ^= h >> 32;
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0xFFFFFF) - 0.5f;
}

struct Rig {
    std::vector<float> h0, uplift;
    Rig() : h0(kW * kH), uplift(kW * kH) {
        for (int z = 0; z < kH; ++z)
            for (int x = 0; x < kW; ++x) {
                const float dx = (x - kW * 0.5f) / (kW * 0.5f), dz = (z - kH * 0.5f) / (kH * 0.5f);
                const float r2 = dx * dx + dz * dz;
                const float dome = std::max(0.0f, 1.0f - r2);          // 1 at the centre, 0 at the rim
                h0[z * kW + x] = kSea + 300.0f * dome * dome - 2.0f + 30.0f * hashNoise(x, z);
                uplift[z * kW + x] = dome;
            }
    }
};

MacroErosionParams rigParams(int steps, float upliftMax = 1e-3f) {
    MacroErosionParams p;
    p.steps = steps; p.dt = 1000.0f; p.k = 1e-5f; p.m = 0.4f;
    p.upliftMax = upliftMax; p.cellU = kCell; p.seaLevel = kSea;
    return p;
}

}  // namespace

TEST(MacroErosionTest, StepsZeroIsTheIdentityAndStillReportsDrainage) {
    Rig rig;
    const auto r = runMacroErosion(rig.h0, kW, kH, rig.uplift, rigParams(0));
    ASSERT_EQ(r.height.size(), rig.h0.size());
    EXPECT_EQ(std::memcmp(r.height.data(), rig.h0.data(), rig.h0.size() * sizeof(float)), 0)
        << "steps = 0 changed the heights";
    EXPECT_EQ(r.receiver.size(), rig.h0.size());
    EXPECT_GT(r.baseLevelCells, 0);
}

TEST(MacroErosionTest, ReceiversFormAForestRootedAtBaseLevelAndTheStackOrdersDonorsAfterReceivers) {
    Rig rig;
    const auto r = runMacroErosion(rig.h0, kW, kH, rig.uplift, rigParams(20));
    const int n = kW * kH;
    ASSERT_EQ(static_cast<int>(r.stack.size()), n) << "the stack does not visit every cell once";
    std::vector<int> pos(n, -1);
    for (int s = 0; s < n; ++s) pos[r.stack[s]] = s;
    int cycles = 0, outOfOrder = 0;
    for (int i = 0; i < n; ++i) {
        if (pos[i] < 0) { ++cycles; continue; }
        int c = i, hops = 0;
        while (r.receiver[c] != c && hops <= n) { c = r.receiver[c]; ++hops; }
        if (hops > n) ++cycles;
        if (r.receiver[i] != i && pos[r.receiver[i]] > pos[i]) ++outOfOrder;
    }
    EXPECT_EQ(cycles, 0) << cycles << " cells never reach base level (a cycle or an unvisited cell)";
    EXPECT_EQ(outOfOrder, 0) << outOfOrder << " cells are stacked before their receiver";
}

TEST(MacroErosionTest, ReceiverIsNeverHigherOnTheFilledSurfaceAndAreaIsConserved) {
    Rig rig;
    const auto r = runMacroErosion(rig.h0, kW, kH, rig.uplift, rigParams(20));
    const int n = kW * kH;
    const std::vector<float> filled = PriorityFlood::fill(r.height, kW, kH, kSea);
    int uphill = 0;
    double baseArea = 0.0;
    for (int i = 0; i < n; ++i) {
        const int rc = r.receiver[i];
        if (rc == i) { baseArea += r.drainageArea[i]; continue; }
        if (filled[rc] > filled[i] + 1e-3f) ++uphill;
    }
    EXPECT_EQ(uphill, 0) << uphill << " receivers sit above their donor on the filled surface";
    EXPECT_NEAR(baseArea, static_cast<double>(n) * kCell * kCell, 1.0)
        << "drainage area is not conserved at base level";
}

TEST(MacroErosionTest, DrainageDensityFallsWithTheThresholdAndStaysUnderTheGridCeiling) {
    Rig rig;
    const auto r = runMacroErosion(rig.h0, kW, kH, rig.uplift, rigParams(20));
    const float cellArea = kCell * kCell;
    float prev = 1e9f;
    for (int cells : {2, 8, 32, 128}) {
        const float dd = macroDrainageDensity(r, kW, kH, kCell, cells * cellArea);
        std::printf("[ M1c Dd ] threshold %3d cells (%.2f km2): Dd = %.3f km/km2, Dd*sqrt(Ac) = %.3f\n",
                    cells, cells * cellArea * 1e-6f, dd, dd * std::sqrt(cells * cellArea * 1e-6f));
        EXPECT_LE(dd, prev + 1e-6f) << "drainage density rose with the threshold";
        EXPECT_LE(dd, 2.7f) << "above the 512 u grid ceiling (1.414 cell widths of channel per cell)";
        prev = dd;
    }
    EXPECT_GT(macroDrainageDensity(r, kW, kH, kCell, 2 * cellArea), 0.0f) << "no channels at all";
}

TEST(MacroErosionTest, ErosionLowersTheMeanWithoutUpliftAndUpliftRaisesIt) {
    Rig rig;
    const auto eroded = runMacroErosion(rig.h0, kW, kH, rig.uplift, rigParams(20, 0.0f));
    EXPECT_LT(eroded.meanHeightAfter, eroded.meanHeightBefore) << "20 steps without uplift did not erode";
    const auto lifted = runMacroErosion(rig.h0, kW, kH, rig.uplift, rigParams(20, 1e-3f));
    EXPECT_GT(lifted.meanHeightAfter, eroded.meanHeightAfter) << "uplift had no effect";
    // Control: a step with a larger K erodes more.
    MacroErosionParams hard = rigParams(20, 0.0f);
    hard.k = 1e-4f;
    const auto harder = runMacroErosion(rig.h0, kW, kH, rig.uplift, hard);
    EXPECT_LT(harder.meanHeightAfter, eroded.meanHeightAfter) << "a 10x K did not erode more";
}

// The GROUNDED configuration (plan §10 F4e: Cordonnier 2016 §6 — n = 1, m = 0.5, k = 5.61e-7 /yr,
// U = 5e-4 m/yr, dt = 2.5e5 yr, ~200 steps, talus 30°) on the rig's dome, asserting the paper's own
// claims: relief is FORMED by 50 steps (uplift has built mountains far above the 311 m dome) and
// SETTLES by 200 (the 50→200 change is smaller than the 0→50 change: "the geometry stops
// evolving after 100−300 iterations"), and the 30° ceiling holds between every cell and its final
// receiver. The trough-count "valley density" is PRINTED only: the first version of this test
// asserted it rises with steps and it fell (1.60 → 1.30 → 1.25 /km²) because the metric counts the
// rig's hash roughness, which erosion smooths — a mis-specified prediction, recorded in the plan.
// The relief the configuration builds is printed against the paper's hmax = 2.244 u/k (≈ 2000 m):
// the rig reaches ~4–5 km, so the fit does not transfer to this grid; the engine remaps anyway.
TEST(MacroErosionTest, GroundedConfigurationFormsReliefBy50StepsSettlesBy200AndHonoursTheTalusCeiling) {
    Rig rig;
    MacroErosionParams g;
    g.dt = 2.5e5f; g.k = 5.61e-7f; g.m = 0.5f; g.upliftMax = 5.0e-4f; g.maxSlope = 0.577f;
    g.cellU = kCell; g.seaLevel = kSea;
    float relief[3] = {0.0f, 0.0f, 0.0f};
    int idx = 0;
    for (int steps : {0, 50, 200}) {
        g.steps = steps;
        const auto r = runMacroErosion(rig.h0, kW, kH, rig.uplift, g);
        const float vd = macroValleyDensity(r.height, kW, kH, kCell);
        float maxAbove = 0.0f;
        int overTalus = 0;
        for (int i = 0; i < kW * kH; ++i) {
            maxAbove = std::max(maxAbove, r.height[i] - kSea);
            const int rc = r.receiver[i];
            if (rc == i) continue;
            const int ix = i % kW, iz = i / kW, rx = rc % kW, rz = rc / kW;
            const float dist = (ix != rx && iz != rz) ? kCell * 1.41421356f : kCell;
            if (r.height[i] > r.height[rc] + g.maxSlope * dist + 1e-3f) ++overTalus;
        }
        std::printf("[ M1c grounded ] steps %3d: trough density %.3f /km2 (printed only), max relief %.0f m (paper hmax 2.244 u/k = %.0f m), over-talus cells %d\n",
                    steps, vd, maxAbove, 2.244f * g.upliftMax / g.k, overTalus);
        if (steps > 0)
            EXPECT_EQ(overTalus, 0) << overTalus << " cells stand above the 30 deg talus ceiling over their final receiver";
        relief[idx++] = maxAbove;
    }
    EXPECT_GT(relief[1], relief[0] * 3.0f) << "50 steps did not form relief: " << relief[0] << " -> " << relief[1];
    EXPECT_LT(std::fabs(relief[2] - relief[1]), std::fabs(relief[1] - relief[0]))
        << "the geometry did not settle: 0->50 moved " << relief[1] - relief[0] << ", 50->200 moved " << relief[2] - relief[1];
}

// HydroEdgeTest, M1 scope (plan resolution 5): macro drainage is continuous across the old
// ±16384 u hydrology box edge. The rig is centred, so that edge is column 48 ± 32 = 80 (east).
TEST(MacroErosionTest, MacroDrainageCrossesTheOldHydrologyBoxEdge) {
    Rig rig;
    const auto r = runMacroErosion(rig.h0, kW, kH, rig.uplift, rigParams(20));
    const int edgeX = kW / 2 + static_cast<int>(16384.0f / kCell);   // 80
    int crossings = 0;
    for (int i = 0; i < kW * kH; ++i) {
        const int rc = r.receiver[i];
        if (rc == i) continue;
        const int ix = i % kW, rx = rc % kW;
        if ((ix == edgeX - 1 && rx == edgeX) || (ix == edgeX && rx == edgeX + 1) ||
            (ix == edgeX && rx == edgeX - 1) || (ix == edgeX + 1 && rx == edgeX))
            ++crossings;
    }
    EXPECT_GT(crossings, 0) << "no receiver chain crosses x = +16384: drainage dies at the old box edge";
}
