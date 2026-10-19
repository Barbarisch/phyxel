#include <gtest/gtest.h>

#include <algorithm>
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

// Auditor 2026-10-19: the update is checked against its closed form. A 3-cell west->east chain
// (base at sea, A, B) on a 3x1 grid: after ONE step with no uplift, A = (hA + f_A h_base)/(1+f_A)
// with f_A = K dt A_A^m / cellU (A_A = 2 cells drain through A), and B = (hB + f_B A')/(1+f_B).
TEST(MacroErosionTest, OneStepMatchesTheClosedFormOnAThreeCellChain) {
    // 5 x 3 grid: the grid BORDER is always an outlet in PriorityFlood (the first version of this
    // test used a 3 x 1 grid, so every cell was base level and nothing eroded). The middle row is
    // base(0,1 border, sea) <- A(1,1) <- B(2,1); every other cell is a 1000 u wall so routing is
    // forced west; (3,1) and (4,1) are part of the wall.
    const int w = 5, h = 3;
    MacroErosionParams p;
    p.steps = 1; p.dt = 1000.0f; p.k = 1e-5f; p.m = 0.5f; p.upliftMax = 0.0f; p.maxSlope = 0.0f;
    p.cellU = kCell; p.seaLevel = kSea;
    std::vector<float> h0(w * h, kSea + 1000.0f);
    const int base = 1 * w + 0, A = 1 * w + 1, B = 1 * w + 2;
    h0[base] = kSea; h0[A] = kSea + 100.0f; h0[B] = kSea + 150.0f;
    const auto r = runMacroErosion(h0, w, h, {}, p);
    const float area = kCell * kCell;
    // The interior wall cell (3,1) has B as its lowest neighbour and drains through it, so the
    // chain's drainage areas are 3 cells at A and 2 at B (the first version expected 2 and 1).
    const float fA = p.k * p.dt * std::pow(3.0f * area, p.m) / kCell;
    const float fB = p.k * p.dt * std::pow(2.0f * area, p.m) / kCell;
    const float expA = (h0[A] + fA * h0[base]) / (1.0f + fA);
    const float expB = (h0[B] + fB * expA) / (1.0f + fB);
    ASSERT_EQ(r.receiver[A], base) << "A must drain to the base cell";
    ASSERT_EQ(r.receiver[B], A) << "B must drain through A";
    EXPECT_NEAR(r.drainageArea[A], 3.0f * area, 1.0f) << "A must drain 3 cells (B + the wall cell behind it)";
    EXPECT_NEAR(r.drainageArea[B], 2.0f * area, 1.0f) << "B must drain 2 cells";
    EXPECT_NEAR(r.height[base], kSea, 1e-4f) << "base level moved";
    EXPECT_NEAR(r.height[A], expA, 1e-3f) << "A is not the closed-form value";
    EXPECT_NEAR(r.height[B], expB, 1e-3f) << "B is not the closed-form value";
    EXPECT_LT(r.height[A], h0[A]);
}

// A 1-D channel run to steady state against the analytic slope-area relation (W&T 1999 eq.,
// S = (U/K)^(1/n) A^(-m/n) with n = 1): a single west->east chain of N interior cells with uniform
// uplift drains to a sea outlet at the west; at steady state each cell stands
// min(talus*dx, (U/K) A_i^(-m) dx) above its receiver, A_i = (N - i + 1) cells. Converged when the
// max per-cell change over the last 50 steps is < 0.1 % of the relief. Agreement within 2 %
// MEASURES that the solver reproduces the stream-power equilibrium (auditor 2026-10-19) and
// explains the uniform-uplift rig's 2.6x of the paper's hmax: the fit is theirs, the equation is
// the physics, and the equation's relief for a 48-cell path at 512 m is several km.
TEST(MacroErosionTest, OneDimensionalChainReachesTheAnalyticSteadyState) {
    const int N = 48, w = N + 2, h = 3;   // border columns + wall rows are outlets/walls
    std::vector<float> h0(w * h, kSea + 1.0e5f), uplift(w * h, 0.0f);
    for (int x = 0; x < w; ++x) { h0[0 * w + x] = kSea + 1.0e5f; h0[2 * w + x] = kSea + 1.0e5f; }
    for (int x = 0; x < w; ++x) { h0[1 * w + x] = kSea + 1.0f + 0.01f * x; uplift[1 * w + x] = 1.0f; }
    h0[1 * w + 0] = kSea;            // the sea outlet (border, west)
    h0[1 * w + (w - 1)] = kSea + 1.0e5f;   // the east end is a wall, not an outlet
    MacroErosionParams g;
    g.dt = 2.5e5f; g.k = 5.61e-7f; g.m = 0.5f; g.upliftMax = 5.0e-4f; g.maxSlope = 0.577f;
    g.cellU = kCell; g.seaLevel = kSea;
    g.steps = 600;
    const auto a = runMacroErosion(h0, w, h, uplift, g);
    g.steps = 650;
    const auto b = runMacroErosion(h0, w, h, uplift, g);
    const float area = kCell * kCell;
    float maxDelta = 0.0f, relief = 0.0f, worstRel = 0.0f;
    float analytic = kSea;
    for (int i = 1; i <= N; ++i) {
        const int c = 1 * w + i;
        maxDelta = std::max(maxDelta, std::fabs(b.height[c] - a.height[c]));
        relief = std::max(relief, b.height[c] - kSea);
        const float Ai = static_cast<float>(N - i + 1) * area;
        const float slope = std::min(g.maxSlope, (g.upliftMax / g.k) * std::pow(Ai, -g.m));
        analytic += slope * kCell;
        ASSERT_EQ(b.receiver[c], c - 1) << "cell " << i << " does not drain west";
        worstRel = std::max(worstRel, std::fabs(b.height[c] - analytic) / (analytic - kSea));
    }
    std::printf("[ M1c 1-D ] steady state: relief %.0f m, analytic %.0f m, worst relative error %.2f %%, max |dh| over 600->650 steps %.3f m\n",
                relief, analytic - kSea, 100.0f * worstRel, maxDelta);
    EXPECT_LT(maxDelta, 0.001f * relief) << "the chain has not converged";
    EXPECT_LT(worstRel, 0.02f) << "the steady-state profile departs from the analytic slope-area relation";
}

// A cell at the bottom of a depression (below the filled surface) is NOT filled by the solve
// and NOT eroded: it only takes its uplift (the first version raised it to its receiver every step).
TEST(MacroErosionTest, ALakeCellIsNeitherErodedNorFilledBySolveOnlyUplifted) {
    const int w = 5, h = 5;
    std::vector<float> h0(w * h, kSea + 200.0f), uplift(w * h, 1.0f);
    for (int x = 0; x < w; ++x) { h0[x] = kSea; h0[(h - 1) * w + x] = kSea; }   // sea rows top/bottom
    h0[2 * w + 2] = kSea + 120.0f;   // the pit, 80 u below its rim
    MacroErosionParams p;
    p.steps = 3; p.dt = 1000.0f; p.k = 1e-5f; p.m = 0.5f; p.upliftMax = 1e-3f; p.maxSlope = 0.0f;
    p.cellU = kCell; p.seaLevel = kSea;
    const auto r = runMacroErosion(h0, w, h, uplift, p);
    const float pit = r.height[2 * w + 2];
    EXPECT_GT(r.filled[2 * w + 2], pit + 1.0f) << "the pit is not a lake on the filled surface";
    EXPECT_NEAR(pit, kSea + 120.0f + 3.0f * p.dt * p.upliftMax, 1e-3f)
        << "the lake cell was eroded or filled by the solve (expected uplift only)";
}

// Convergence as a RATE (auditor: the earlier |d(50->200)| < |d(0->50)| was a weak inequality).
// The MAX per-cell change is dominated by single cells whose routing flips (a lake cell becoming a
// channel jumps by hundreds of metres: measured 758 m of 4414 m at 150->200), so the rate is the
// 95th percentile of |dh| over 50-step windows. MEASURED on the dome rig: p95 = 1.64 % of the relief
// at 150->200, so the paper's "converges after around 200 time steps" does NOT hold here at 1 %
// (their step count is their setup's); the assertion is that the rate keeps FALLING window over
// window and is under 1 % by 300->350. The 1-D test above is the exact-convergence check.
TEST(MacroErosionTest, GroundedConfigurationConvergenceRateFallsAndIsUnderOnePercentBy350Steps) {
    Rig rig;
    MacroErosionParams g;
    g.dt = 2.5e5f; g.k = 5.61e-7f; g.m = 0.5f; g.upliftMax = 5.0e-4f; g.maxSlope = 0.577f;
    g.cellU = kCell; g.seaLevel = kSea;
    auto p95Between = [&](int s0, int s1, float& relief, float& maxDelta) {
        g.steps = s0;
        const auto a = runMacroErosion(rig.h0, kW, kH, rig.uplift, g);
        g.steps = s1;
        const auto b = runMacroErosion(rig.h0, kW, kH, rig.uplift, g);
        std::vector<float> deltas(kW * kH);
        relief = 0.0f;
        for (int i = 0; i < kW * kH; ++i) {
            deltas[i] = std::fabs(b.height[i] - a.height[i]);
            relief = std::max(relief, b.height[i] - kSea);
        }
        std::sort(deltas.begin(), deltas.end());
        maxDelta = deltas.back();
        return deltas[static_cast<size_t>(deltas.size() * 0.95)];
    };
    float rel1, max1, rel2, max2, rel3, max3;
    const float p1 = p95Between(100, 150, rel1, max1);
    const float p2 = p95Between(150, 200, rel2, max2);
    const float p3 = p95Between(300, 350, rel3, max3);
    std::printf("[ M1c converge ] p95 |dh| per 50 steps: 100->150 %.1f m (%.2f %%), 150->200 %.1f m (%.2f %%), 300->350 %.1f m (%.2f %%); max at 300->350 %.1f m; relief %.0f m\n",
                p1, 100.0f * p1 / rel1, p2, 100.0f * p2 / rel2, p3, 100.0f * p3 / rel3, max3, rel3);
    EXPECT_LT(p2, p1) << "the convergence rate did not fall from 100->150 to 150->200";
    EXPECT_LT(p3, p2) << "the convergence rate did not fall from 150->200 to 300->350";
    EXPECT_LT(p3, 0.01f * rel3) << "95 % of cells still move by more than 1 % of the relief at 300->350";
}

// Uniform uplift on the rig's 49 km extent vs the paper's hmax = 2.244 u/k (its 50 x 50 km setup):
// printed, and asserted only to the ORDER (within 3x) — the dome rig read 2.1-2.5x the fit and the
// auditor asked for this control before "the fit does not transfer" is accepted.
TEST(MacroErosionTest, UniformUpliftReliefIsWithinAFactorOfThreeOfThePaperFit) {
    std::vector<float> h0(kW * kH, kSea + 1.0f), uplift(kW * kH, 1.0f);
    for (int z = 0; z < kH; ++z)
        for (int x = 0; x < kW; ++x) {
            if (x == 0 || z == 0 || x == kW - 1 || z == kH - 1) h0[z * kW + x] = kSea;   // sea rim
            else h0[z * kW + x] += 2.0f * hashNoise(x, z);
        }
    MacroErosionParams g;
    g.steps = 200; g.dt = 2.5e5f; g.k = 5.61e-7f; g.m = 0.5f; g.upliftMax = 5.0e-4f; g.maxSlope = 0.577f;
    g.cellU = kCell; g.seaLevel = kSea;
    const auto r = runMacroErosion(h0, kW, kH, uplift, g);
    float maxAbove = 0.0f;
    for (float v : r.height) maxAbove = std::max(maxAbove, v - kSea);
    const float fit = 2.244f * g.upliftMax / g.k;
    std::printf("[ M1c uniform ] max relief %.0f m vs paper fit %.0f m (ratio %.2f)\n", maxAbove, fit, maxAbove / fit);
    EXPECT_GT(maxAbove, fit / 3.0f) << "far below the paper's fit: erosion too strong or an area-unit error";
    EXPECT_LT(maxAbove, fit * 3.0f) << "far above the paper's fit: erosion too weak or an area-unit error";
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
