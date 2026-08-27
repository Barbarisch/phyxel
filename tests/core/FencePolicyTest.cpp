#include <gtest/gtest.h>

#include "core/FenceBuilder.h"
#include "core/SettlementLayout.h"

using namespace Phyxel::Core;

// ============================================================================
// Fence policy (CityForgePlan M3, user-settled 2026-08-27): city cores are not
// fenced crofts, flush urban rows are not caged, not every plot is fenced, and
// the gate lands on the FRONT DOOR, not the geometric centre of the side.
// RED baseline: shouldFencePlot always true; fenceGateWindowAt = centred.
// ============================================================================

namespace {
FencePolicy corePolicy() {
    FencePolicy p;
    p.hasCore = true;
    p.coreCu = 80;
    p.coreCv = 80;
    p.coreRing = 24;
    p.fraction = 1.0;
    return p;
}
// A comfortable plot: building inset 2 cubes from every boundary.
Rect plotAt(int x, int z) { return Rect{x, z, 12, 12}; }
Rect insetFootprint(const Rect& p, int by) {
    return Rect{p.x + by, p.z + by, p.w - 2 * by, p.d - 2 * by};
}
}  // namespace

TEST(FencePolicyTest, CoreRingPlotsAreNeverFenced) {
    const FencePolicy pol = corePolicy();
    const Rect inCore = plotAt(70, 70);         // centre (76,76): Chebyshev 4 <= 24
    const Rect outCore = plotAt(10, 10);        // centre (16,16): Chebyshev 64 > 24
    EXPECT_FALSE(shouldFencePlot(0, 7, inCore, insetFootprint(inCore, 2), pol));
    EXPECT_TRUE(shouldFencePlot(0, 7, outCore, insetFootprint(outCore, 2), pol));
}

TEST(FencePolicyTest, FlushBuildingsGoUnfenced) {
    FencePolicy pol;                             // no core, fraction 1.0
    const Rect plot = plotAt(10, 10);
    // Building flush to the plot's street edge (gap 0 on one side) -> unfenced.
    const Rect flush{plot.x, plot.z + 2, plot.w - 4, plot.d - 4};
    EXPECT_FALSE(shouldFencePlot(0, 7, plot, flush, pol));
    // Comfortable clearance on every side -> fenced.
    EXPECT_TRUE(shouldFencePlot(0, 7, plot, insetFootprint(plot, 1), pol));
}

TEST(FencePolicyTest, FractionIsSeededDeterministicAndProportional) {
    FencePolicy pol;
    pol.fraction = 0.5;
    const Rect plot = plotAt(10, 10);
    const Rect fp = insetFootprint(plot, 2);
    int fenced = 0;
    for (int i = 0; i < 200; ++i) fenced += shouldFencePlot(i, 7, plot, fp, pol) ? 1 : 0;
    EXPECT_GT(fenced, 60) << "fraction 0.5 fenced almost nothing";
    EXPECT_LT(fenced, 140) << "fraction 0.5 fenced almost everything";
    for (int i = 0; i < 50; ++i)
        EXPECT_EQ(shouldFencePlot(i, 7, plot, fp, pol), shouldFencePlot(i, 7, plot, fp, pol));
    // fraction 1.0 keeps the legacy everything-fenced behaviour for eligible plots
    pol.fraction = 1.0;
    for (int i = 0; i < 50; ++i) EXPECT_TRUE(shouldFencePlot(i, 7, plot, fp, pol));
}

TEST(FencePolicyTest, GateWindowFollowsTheDoor) {
    // Run of 21 cubes -> runLenMicro = 20*9+1 = 181. Door near the run's low end at cube 3.
    const int runLen = 20 * 9 + 1, gateW = 2;
    int lo = -1, hi = -1;
    ASSERT_TRUE(fenceGateWindowAt(runLen, gateW, 3 * 9 + 4, lo, hi));
    // The gate must be cube-aligned and its window must CONTAIN the preferred centre.
    EXPECT_EQ(lo % 9, 0);
    EXPECT_EQ(hi - lo, gateW * 9);
    EXPECT_LE(lo, 3 * 9 + 4);
    EXPECT_GT(hi, 3 * 9 + 4);

    // Door hanging past the run start: clamps flush to the start, never negative.
    ASSERT_TRUE(fenceGateWindowAt(runLen, gateW, -7, lo, hi));
    EXPECT_EQ(lo, 0);
    EXPECT_EQ(hi, gateW * 9);

    // Door past the far end: clamps so the whole gate stays inside the run's cube span.
    ASSERT_TRUE(fenceGateWindowAt(runLen, gateW, runLen + 40, lo, hi));
    EXPECT_EQ(hi, ((runLen + 8) / 9) * 9);

    // Run too short for the gate: refused.
    EXPECT_FALSE(fenceGateWindowAt(9, 2, 4, lo, hi));
}
