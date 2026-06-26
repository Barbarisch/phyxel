#include <gtest/gtest.h>

#include <cmath>

#include "core/SiteAnalysis.h"

using namespace Phyxel::Core;

// ============================================================================
// analyze_site (#01) L2 — buildability classification measured against synthetic
// terrain FIXTURES (no live engine): flat ground, a step cliff, a cone hill, and
// water. The slope must reflect real neighbour deltas (a cliff is TooSteep, not
// Flat) — the keystone Phases 2-4 depend on.
// ============================================================================

// Flat ground -> every cell Flat, buildableFraction 1.0.
TEST(SiteAnalysisTest, FlatGroundIsAllBuildable) {
    auto flat = [](int, int) { return 16; };
    auto m = analyzeSite(8, 8, /*maxBuildableSlope=*/3, flat);
    for (const auto& c : m.cells) EXPECT_EQ(c.cls, Buildability::Flat);
    EXPECT_DOUBLE_EQ(m.buildableFraction(), 1.0);
}

// THE invariant (red on the slope=0 stub): a STEP CLIFF (one half low, one half high) must classify
// the cells straddling the step as TooSteep — not Flat. A settlement can't sit on a cliff face.
TEST(SiteAnalysisTest, StepCliffIsTooSteep) {
    // left half ground=16, right half ground=26 (a 10-cube cliff at x=4)
    auto cliff = [](int x, int) { return x < 4 ? 16 : 26; };
    auto m = analyzeSite(8, 8, /*maxBuildableSlope=*/3, cliff);
    // cells on either side of the step (x=3 and x=4) see a 10-cube neighbour delta -> TooSteep
    EXPECT_EQ(m.at(3, 4).cls, Buildability::TooSteep) << "the cliff edge read as buildable";
    EXPECT_EQ(m.at(4, 4).cls, Buildability::TooSteep);
    // far from the step it's flat
    EXPECT_EQ(m.at(0, 4).cls, Buildability::Flat);
    EXPECT_EQ(m.at(7, 4).cls, Buildability::Flat);
    EXPECT_LT(m.buildableFraction(), 1.0) << "a cliff site should not be 100% buildable";
}

// A hill with a FLAT TOP (a plateau — the realistic "buildable hilltop"): the top is Flat, the gentle
// skirt is SlopeOk (5-cube/ring), a sheer drop is TooSteep. Distinguishes the three non-water classes.
TEST(SiteAnalysisTest, HilltopBuildableSkirtSlopeOkScarpTooSteep) {
    // ring = chebyshev distance from centre (4,4). ring 0-1 = flat plateau at 30; then -5/ring (gentle
    // SlopeOk skirt); but a sheer SCARP at the outer edge (ring 4) drops 10 -> TooSteep.
    auto hill = [](int x, int z) {
        int ring = std::max(std::abs(x - 4), std::abs(z - 4));
        if (ring <= 1) return 30;          // flat top
        if (ring == 2) return 25;          // skirt
        if (ring == 3) return 20;          // skirt
        return 5;                          // ring 4 outer: a sheer 15-cube scarp
    };
    auto m = analyzeSite(9, 9, /*maxBuildableSlope=*/6, hill);
    EXPECT_EQ(m.at(4, 4).cls, Buildability::Flat)    << "the hilltop plateau should be buildable";
    EXPECT_EQ(m.at(2, 4).cls, Buildability::SlopeOk) << "the gentle skirt should be slope-ok";  // slope 5
    EXPECT_EQ(m.at(0, 4).cls, Buildability::TooSteep)<< "the sheer scarp should be too steep";  // slope 15
    EXPECT_GT(m.buildableFraction(), 0.0);
    EXPECT_LT(m.buildableFraction(), 1.0);
}

// Water cells are classified Water regardless of slope (you don't build in the lake).
TEST(SiteAnalysisTest, WaterIsNotBuildable) {
    auto flat = [](int, int) { return 16; };
    auto water = [](int x, int) { return x >= 4; };   // right half is water
    auto m = analyzeSite(8, 8, 3, flat, water);
    EXPECT_EQ(m.at(5, 5).cls, Buildability::Water);
    EXPECT_EQ(m.at(1, 5).cls, Buildability::Flat);
    EXPECT_DOUBLE_EQ(m.buildableFraction(), 0.5) << "half the site is water";
}
