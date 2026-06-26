#include <gtest/gtest.h>

#include <cmath>
#include <iostream>

#include "core/SiteAnalysis.h"
#include "core/WorldGenerator.h"

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

// THE invariant (red on the relief=0 stub): a STEP CLIFF (one half low, one half high) must classify
// the cells whose footprint WINDOW straddles the step as TooSteep — not Flat (you can't seat a building
// half on a cliff). window=1 (tight) here so far-from-step cells are clearly Flat.
TEST(SiteAnalysisTest, StepCliffIsTooSteep) {
    auto cliff = [](int x, int) { return x < 4 ? 16 : 26; };   // 10-cube cliff at x=4
    auto m = analyzeSite(8, 8, /*maxRelief=*/3, cliff, /*waterAt=*/{}, /*flatRelief=*/1, /*window=*/1);
    EXPECT_EQ(m.at(3, 4).cls, Buildability::TooSteep) << "the cliff edge read as buildable";  // relief 10
    EXPECT_EQ(m.at(4, 4).cls, Buildability::TooSteep);
    EXPECT_EQ(m.at(0, 4).cls, Buildability::Flat);   // window doesn't reach the step
    EXPECT_EQ(m.at(7, 4).cls, Buildability::Flat);
    EXPECT_LT(m.buildableFraction(), 1.0) << "a cliff site should not be 100% buildable";
}

// The three non-water classes via footprint relief (window=1): a flat plateau (relief 0 -> Flat), a
// gentle skirt (relief 4 -> SlopeOk at maxRelief 6), and a sheer cliff edge (relief 20 -> TooSteep).
TEST(SiteAnalysisTest, PlateauFlatSkirtSlopeOkCliffTooSteep) {
    auto terrain = [](int x, int) {
        if (x <= 3) return 30;             // flat plateau
        if (x <= 6) return 30 - 2 * (x - 3); // 28,26,24 gentle skirt
        return 4;                          // x>=7: sheer drop to low ground (a cliff edge at x=6->7)
    };
    auto m = analyzeSite(10, 10, /*maxRelief=*/6, terrain, /*waterAt=*/{}, /*flatRelief=*/1, /*window=*/1);
    EXPECT_EQ(m.at(1, 4).cls, Buildability::Flat)     << "the plateau should be buildable";     // relief 0
    EXPECT_EQ(m.at(5, 4).cls, Buildability::SlopeOk)  << "the gentle skirt should be slope-ok"; // relief 4
    EXPECT_EQ(m.at(7, 4).cls, Buildability::TooSteep) << "the cliff edge should be too steep";  // relief 20
    EXPECT_GT(m.buildableFraction(), 0.0);
    EXPECT_LT(m.buildableFraction(), 1.0);
}

// REAL terrain (not synthetic): drive analyzeSite from WorldGenerator's actual surface height for
// Perlin (rolling hills) vs Mountains (steep). The classification must reflect the real algorithm —
// rolling hills are mostly buildable; mountains are meaningfully LESS buildable. This validates the
// keystone against the engine's own terrain generator, encoded, no live engine.
TEST(SiteAnalysisTest, RealTerrainHillsMoreBuildableThanMountains) {
    Phyxel::WorldGenerator hills(Phyxel::WorldGenerator::GenerationType::Perlin, 1234u);
    Phyxel::WorldGenerator mtns(Phyxel::WorldGenerator::GenerationType::Mountains, 1234u);
    // footprint-scale window (8-cube footprint -> window 4); maxRelief 6 = up to 6 cubes of cut/fill ok.
    const int W = 48, D = 48, maxRelief = 6, window = 4;
    auto hHills = [&](int x, int z) { return hills.sampleSurface(x, z).surfaceY; };
    auto hMtns  = [&](int x, int z) { return mtns.sampleSurface(x, z).surfaceY; };
    const auto mapHills = analyzeSite(W, D, maxRelief, hHills, {}, 1, window);
    const auto mapMtns  = analyzeSite(W, D, maxRelief, hMtns,  {}, 1, window);
    auto stats = [](const BuildabilityMap& m) {
        int hmin = 9999, hmax = -9999, rmax = 0;
        for (const auto& c : m.cells) { hmin = std::min(hmin, c.height); hmax = std::max(hmax, c.height); rmax = std::max(rmax, c.relief); }
        std::cout << " height[" << hmin << "," << hmax << "] maxRelief=" << rmax
                  << " buildable=" << m.buildableFraction() << "\n";
    };
    std::cout << "  Perlin hills:"; stats(mapHills);
    std::cout << "  Mountains:  "; stats(mapMtns);
    EXPECT_GT(mapHills.buildableFraction(), 0.7)
        << "Perlin rolling hills should be mostly buildable";
    EXPECT_LT(mapMtns.buildableFraction(), mapHills.buildableFraction() - 0.1)
        << "Mountains should be MEANINGFULLY less buildable than rolling hills (footprint relief)";
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
