#include <gtest/gtest.h>

#include "core/HydrologyMap.h"
#include "core/WaterBodyIndex.h"
#include "core/WaterProfile.h"

#include <cmath>
#include <limits>

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

// W1 asserted "turbidity 0 AND roughness 1 everywhere". W2 deliberately breaks the first half —
// turbidity is now derived — so this test was CHANGED ON PURPOSE, not discovered broken. What
// survives is the half W2 does not touch: roughness stays neutral until W3, and dry land is never
// assigned water optics.
TEST(WaterProfileTest, RoughnessStaysNeutralUntilW3AndDryColumnsAreClear) {
    BakedFixture f;
    std::vector<float> out;
    buildHydroUpload(f.hydro, &f.bodies, {}, out);

    for (size_t i = 0; i < f.hydro.levels().size(); ++i) {
        ASSERT_FLOAT_EQ(out[i * kHydroTexelFloats + 3], 1.0f)
            << "roughness must still be 1 (W3 owns it), cell " << i;
        if (!wetLevel(out[i * kHydroTexelFloats]))
            ASSERT_FLOAT_EQ(out[i * kHydroTexelFloats + 2], 0.0f)
                << "dry land must carry no turbidity, cell " << i;
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

// waterProfileAt is THE shared query: the surface reads its profile per pixel from the texture,
// but the underwater overlay is a fullscreen pass that needs ONE profile for "the water the camera
// is in". These must be the same function or a lake reads murky from above and clear from below.
TEST(WaterProfileTest, ProfileAtWorldColumnMatchesThePackedTexture) {
    BakedFixture f;
    std::vector<float> out;
    buildHydroUpload(f.hydro, &f.bodies, {}, out);

    struct Probe { int cx, cz; const char* what; };
    for (const Probe& p : {Probe{kOceanX, kOceanZ, "ocean"}, Probe{kLakeX, kLakeZ, "lake"},
                           Probe{kPondX, kPondZ, "pond"},   Probe{kDryX, kDryZ, "dry land"}}) {
        const WaterProfile wp =
            waterProfileAt(&f.bodies, (p.cx + 0.5f) * 10.0f, (p.cz + 0.5f) * 10.0f, 10.0f);
        const size_t t = texel(f.hydro, p.cx, p.cz);
        EXPECT_FLOAT_EQ(wp.waveEnergy, out[t + 1]) << "energy disagrees with the texture at " << p.what;
        EXPECT_FLOAT_EQ(wp.turbidity, out[t + 2]) << "turbidity disagrees at " << p.what;
        EXPECT_FLOAT_EQ(wp.roughness, out[t + 3]) << "roughness disagrees at " << p.what;
    }
}

// ── W2: turbidity varies by body ──────────────────────────────────────────────────────────────
// These are the red-before-green tests for W2. Against the W1 build (turbidity 0 for everything)
// every ordering assertion below fails, because "clearer than" cannot hold when both sides are 0.
//
// Bodies are constructed directly rather than baked: WaterBodyIndex::Body is a plain struct, and
// this is a test OF THE MAPPING, so feeding it exact depths beats hoping a fixture's geography
// happens to produce them.
namespace {
constexpr float kCell = 128.0f;   // the shipped hydrology cell size (water_bake_info)

WaterBodyIndex::Body bodyOfDepth(WaterBodyIndex::Class cls, int areaCells, float meanDepth) {
    WaterBodyIndex::Body b;
    b.cls = cls;
    b.areaCells = areaCells;
    // volumeEst is Sum((level - terrain) * cellSize^2), so mean depth = volumeEst/(area*cellSize^2)
    b.volumeEst = meanDepth * static_cast<float>(areaCells) * kCell * kCell;
    return b;
}
}  // namespace

TEST(WaterProfileTest, ShallowBodiesAreTurbidAndDeepOnesAreClear) {
    const WaterProfile shallow =
        deriveWaterProfile(&bodyOfDepth(WaterBodyIndex::Class::Pond, 3, 1.0f), kCell);
    const WaterProfile deep =
        deriveWaterProfile(&bodyOfDepth(WaterBodyIndex::Class::Lake, 400, 40.0f), kCell);

    EXPECT_GT(shallow.turbidity, deep.turbidity)
        << "a 1 m pond must read murkier than a 40 m lake (Carlson trophic ordering)";
    EXPECT_NEAR(shallow.turbidity, 1.0f, 1e-5f) << "at/below kTurbidDepth = fully turbid";
    EXPECT_NEAR(deep.turbidity, 0.0f, 1e-5f)    << "at/above kClearDepth = fully clear";
}

TEST(WaterProfileTest, OceanIsTheClearWaterEndpoint) {
    // Open ocean is the CLEAREST natural water (Jerlov type I, Secchi 30-50 m) — it reads opaque
    // because it is deep, via Beer-Lambert, not because it is dirty. A shallow ocean shelf column
    // must therefore still be clear: the class short-circuits before the depth proxy.
    const WaterProfile ocean =
        deriveWaterProfile(&bodyOfDepth(WaterBodyIndex::Class::Ocean, 5000, 1.5f), kCell);
    EXPECT_FLOAT_EQ(ocean.turbidity, 0.0f);
}

TEST(WaterProfileTest, TurbidityIsMonotonicInDepthAndStaysInRange) {
    float prev = 2.0f, first = -1.0f, last = -1.0f;
    for (float depth : {0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 15.0f, 20.0f, 50.0f}) {
        const WaterProfile p =
            deriveWaterProfile(&bodyOfDepth(WaterBodyIndex::Class::Lake, 100, depth), kCell);
        EXPECT_GE(p.turbidity, 0.0f) << "depth " << depth;
        EXPECT_LE(p.turbidity, 1.0f) << "depth " << depth;
        EXPECT_LE(p.turbidity, prev) << "turbidity must never RISE with depth (depth " << depth << ")";
        prev = p.turbidity;
        if (first < 0.0f) first = p.turbidity;
        last = p.turbidity;
    }
    // ⚑THIS assertion is what makes the test a FALSIFIER rather than a guard. Monotonicity and
    // in-range are both trivially satisfied by "turbidity is always 0" — the exact un-implemented
    // state — so without a STRICT decrease across the range this test passes on a stub. Verified:
    // it did exactly that before the mapping landed.
    EXPECT_GT(first, last) << "turbidity must actually FALL across the depth range, not be flat";
}

TEST(WaterProfileTest, DegenerateBodiesDoNotProduceNaNTurbidity) {
    // A zero-area or zero-volume body would divide by zero in the mean-depth step.
    for (const auto& b : {bodyOfDepth(WaterBodyIndex::Class::Pond, 0, 0.0f),
                          bodyOfDepth(WaterBodyIndex::Class::Lake, 10, 0.0f)}) {
        const WaterProfile p = deriveWaterProfile(&b, kCell);
        EXPECT_FALSE(std::isnan(p.turbidity)) << "NaN turbidity from a degenerate body";
        EXPECT_GE(p.turbidity, 0.0f);
        EXPECT_LE(p.turbidity, 1.0f);
    }
    const WaterProfile z = deriveWaterProfile(&bodyOfDepth(WaterBodyIndex::Class::Lake, 10, 5.0f), 0.0f);
    EXPECT_FALSE(std::isnan(z.turbidity)) << "NaN turbidity from cellSize 0";

    // The isfinite(volumeEst) guard had no falsifier until now (solution-auditor, 2026-08-03): the
    // other degenerate cases all trip the denom>0 check instead, so removing isfinite() left every
    // test green. A non-finite volume must read CLEAR, never NaN — one NaN here reaches the
    // hydrology texture and then every water pixel of that body.
    for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity()}) {
        WaterBodyIndex::Body b = bodyOfDepth(WaterBodyIndex::Class::Lake, 10, 5.0f);
        b.volumeEst = bad;
        const WaterProfile p = deriveWaterProfile(&b, kCell);
        EXPECT_FALSE(std::isnan(p.turbidity)) << "non-finite volumeEst produced NaN turbidity";
        EXPECT_FLOAT_EQ(p.turbidity, 0.0f) << "an unmeasurable body must read CLEAR, not murky";
    }
}

TEST(WaterProfileTest, ProfileAtIsNeutralWithoutABodyIndex) {
    // A world with no hydrology bake has no bodies at all — the honest answer is the neutral
    // profile, not a crash and not a guess.
    const WaterProfile wp = waterProfileAt(nullptr, 123.0f, 456.0f, 128.0f);
    EXPECT_FLOAT_EQ(wp.turbidity, 0.0f);
    EXPECT_FLOAT_EQ(wp.roughness, 1.0f);
    EXPECT_FLOAT_EQ(wp.waveEnergy, 1.0f);
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
