#include <gtest/gtest.h>

#include "core/HydrologyMap.h"
#include "core/WaterBodyIndex.h"
#include "core/WaterProfile.h"

#include <cmath>

// Water Appearance v4, W1 — the per-body profile PIPE (docs/WaterAppearanceV4.md).
//
// W1 is deliberately invisible: derivation returns the neutral profile, so these tests are contract
// tests on the packing + a REGRESSION PIN on the one value that already shipped (wave energy). They
// are not the red-before-green proof for the feature — that is the L4 positive control (forcing a
// turbidity override must move pixels, which is impossible on the pre-W1 build because the channel
// does not exist). Stated plainly rather than dressed up.
//
// The energy pin is red-verified by MUTATION: changing the /10.0f normaliser or the 0.15f floor in
// deriveWaterProfile turns EnergyFormulaIsPreserved red.
//
// Fixture geography is shared with WaterBodyIndexTest (12×12 cells, cellSize 10, sea level 0):
//  - OCEAN: x=0 column at height −5, floods to sea level 0, touches the bake boundary.
//  - LAKE:  3×4 basin (x∈[4,6], z∈[4,7]) at height 10, outlet at 25 → 12 cells, fills to 25.
//  - POND:  2 cells (2,9)-(3,9) at height 5, outlet at 15 → fills to 15.

namespace Phyxel {
namespace {

float fixtureHeight(float x, float z) {
    const int cx = static_cast<int>(std::floor(x / 10.0f));
    const int cz = static_cast<int>(std::floor(z / 10.0f));
    if (cx == 0) return -5.0f;                                    // ocean strip (boundary column)
    if (cx >= 4 && cx <= 6 && cz >= 4 && cz <= 7) return 10.0f;   // lake basin
    if (cz == 5 && cx >= 7 && cx <= 11) return 25.0f;             // lake outlet channel (dry)
    if ((cx == 2 || cx == 3) && cz == 9) return 5.0f;             // pond basin
    if (cz == 9 && cx >= 4 && cx <= 11) return 15.0f;             // pond outlet channel (dry)
    return 50.0f;
}

struct BakedFixture {
    HydrologyMap hydro;
    WaterBodyIndex bodies;
    BakedFixture()
        : hydro(fixtureHeight, 0.0f, 0.0f, 12, 12, 10.0f, /*seaLevel=*/0.0f),
          bodies(hydro, fixtureHeight) {}
};

// Index of the first float of cell (cx, cz) in a buildHydroUpload result.
size_t texel(const HydrologyMap& h, int cx, int cz) {
    return (static_cast<size_t>(cz) * h.cellsX() + cx) * kHydroTexelFloats;
}

bool wetLevel(float level) { return level > HydrologyMap::NO_WATER * 0.5f; }

// Cells inside each body, by the fixture's geography.
constexpr int kOceanX = 0, kOceanZ = 6;
constexpr int kLakeX = 5,  kLakeZ = 5;
constexpr int kPondX = 2,  kPondZ = 9;
constexpr int kDryX = 9,   kDryZ = 1;   // the 50-high plateau

}  // namespace

TEST(WaterProfileTest, PacksFourFloatsPerCellWithLevelInRed) {
    BakedFixture f;
    std::vector<float> out;
    buildHydroUpload(f.hydro, &f.bodies, {}, out);

    // Stride is the whole point of W1: the texture went RG32F -> RGBA32F so turbidity and roughness
    // have somewhere to live (the sea shader's push block is exactly full at 128 B).
    ASSERT_EQ(out.size(), f.hydro.levels().size() * 4u);
    ASSERT_EQ(kHydroTexelFloats, 4);

    for (size_t i = 0; i < f.hydro.levels().size(); ++i)
        ASSERT_FLOAT_EQ(out[i * kHydroTexelFloats], f.hydro.levels()[i])
            << "R channel must be the basin level verbatim, cell " << i;
}

TEST(WaterProfileTest, EnergyFormulaIsPreserved) {
    BakedFixture f;
    std::vector<float> out;
    buildHydroUpload(f.hydro, &f.bodies, {}, out);

    // Sanity: the fixture really is shaped the way the expectations below assume.
    const WaterBodyIndex::Body* lake = f.bodies.bodyAt((kLakeX + 0.5f) * 10.0f, (kLakeZ + 0.5f) * 10.0f);
    const WaterBodyIndex::Body* pond = f.bodies.bodyAt((kPondX + 0.5f) * 10.0f, (kPondZ + 0.5f) * 10.0f);
    ASSERT_NE(lake, nullptr);
    ASSERT_NE(pond, nullptr);
    ASSERT_EQ(lake->areaCells, 12);
    ASSERT_EQ(pond->areaCells, 2);

    // LITERALS, not a re-implementation of the formula — a test that recomputes the expression it
    // is checking cannot fail when the expression changes.
    //   ocean  -> short-circuits to full energy
    //   lake   -> log2(12+1)/10 = 0.37004397
    //   pond   -> log2( 2+1)/10 = 0.15849625  (above the 0.15 floor, so the floor is NOT what pins it)
    EXPECT_FLOAT_EQ(out[texel(f.hydro, kOceanX, kOceanZ) + 1], 1.0f);
    EXPECT_NEAR(out[texel(f.hydro, kLakeX, kLakeZ) + 1], 0.37004397f, 1e-6f);
    EXPECT_NEAR(out[texel(f.hydro, kPondX, kPondZ) + 1], 0.15849625f, 1e-6f);
}

TEST(WaterProfileTest, W1DerivationIsNeutralEverywhere) {
    BakedFixture f;
    std::vector<float> out;
    buildHydroUpload(f.hydro, &f.bodies, {}, out);

    // The no-regression contract for W1: nothing derived may move turbidity or roughness off the
    // values that reproduce today's shading exactly. When W2/W3 land, THIS test is the one that
    // legitimately changes — and it must be changed deliberately, not discovered broken.
    for (size_t i = 0; i < f.hydro.levels().size(); ++i) {
        ASSERT_FLOAT_EQ(out[i * kHydroTexelFloats + 2], 0.0f) << "turbidity must be 0 in W1, cell " << i;
        ASSERT_FLOAT_EQ(out[i * kHydroTexelFloats + 3], 1.0f) << "roughness must be 1 in W1, cell " << i;
    }
}

TEST(WaterProfileTest, DryColumnsKeepFullEnergyAndNeutralProfile) {
    BakedFixture f;
    std::vector<float> out;
    buildHydroUpload(f.hydro, &f.bodies, {}, out);

    const size_t t = texel(f.hydro, kDryX, kDryZ);
    ASSERT_FALSE(wetLevel(out[t])) << "fixture plateau should be dry";
    EXPECT_FLOAT_EQ(out[t + 1], 1.0f);   // unchanged from the pre-refactor loop
    EXPECT_FLOAT_EQ(out[t + 2], 0.0f);
    EXPECT_FLOAT_EQ(out[t + 3], 1.0f);
}

TEST(WaterProfileTest, OverrideReachesEveryWetColumnAndOnlyWetColumns) {
    BakedFixture f;
    std::vector<float> out;
    WaterLookOverride ovr;
    ovr.active = true;
    ovr.turbidity = 0.8f;
    ovr.roughness = 0.2f;
    buildHydroUpload(f.hydro, &f.bodies, ovr, out);

    int wet = 0, dry = 0;
    for (size_t i = 0; i < f.hydro.levels().size(); ++i) {
        const float* px = &out[i * kHydroTexelFloats];
        if (wetLevel(px[0])) {
            ++wet;
            ASSERT_FLOAT_EQ(px[2], 0.8f) << "override must reach wet cell " << i;
            ASSERT_FLOAT_EQ(px[3], 0.2f);
        } else {
            ++dry;
            ASSERT_FLOAT_EQ(px[2], 0.0f) << "override must NOT touch dry cell " << i;
            ASSERT_FLOAT_EQ(px[3], 1.0f);
        }
    }
    EXPECT_GT(wet, 0) << "fixture must contain water or the test proves nothing";
    EXPECT_GT(dry, 0) << "fixture must contain dry land or the 'only wet' half proves nothing";
}

TEST(WaterProfileTest, NullBodyIndexFallsBackToFullEnergy) {
    BakedFixture f;
    std::vector<float> out;
    buildHydroUpload(f.hydro, nullptr, {}, out);   // worlds baked before the body index existed

    for (size_t i = 0; i < f.hydro.levels().size(); ++i)
        ASSERT_FLOAT_EQ(out[i * kHydroTexelFloats + 1], 1.0f)
            << "no body index -> pre-tangible-water-F behaviour (full energy), cell " << i;
}

}  // namespace Phyxel
