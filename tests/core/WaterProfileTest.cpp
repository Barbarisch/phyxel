#include <gtest/gtest.h>

#include "core/HydrologyMap.h"
#include "core/WaterBodyIndex.h"
#include "core/WaterProfile.h"

#include <cmath>
#include <limits>

// Water Appearance v4, W1 — the per-body profile PIPE (docs/Water.md).
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
constexpr float kPi = 3.14159265358979f;

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

// REPLACES EnergyFormulaIsPreserved, which pinned the old `log2(areaCells+1)/10` proxy. W3 removes
// that formula on purpose (its normaliser was eyeballed and it was blind to wind heading), so the
// pin is deliberately retired rather than discovered broken — and what replaces it asserts the
// property the old formula COULD NOT express.
TEST(WaterProfileTest, EnergyIsFetchLimitedNotAreaBased) {
    BakedFixture f;
    std::vector<float> out;
    buildHydroUpload(f.hydro, &f.bodies, {}, out);

    const WaterBodyIndex::Body* lake = f.bodies.bodyAt((kLakeX + 0.5f) * 10.0f, (kLakeZ + 0.5f) * 10.0f);
    const WaterBodyIndex::Body* pond = f.bodies.bodyAt((kPondX + 0.5f) * 10.0f, (kPondZ + 0.5f) * 10.0f);
    ASSERT_NE(lake, nullptr);
    ASSERT_NE(pond, nullptr);

    // Ocean is open water: unlimited fetch, fully developed. Unchanged from before.
    EXPECT_FLOAT_EQ(out[texel(f.hydro, kOceanX, kOceanZ) + 1], 1.0f);

    // These fixture bodies are TINY (10 m cells, tens of metres across). Under the old area proxy
    // the lake scored 0.37 of a full ocean swell; physically a 36 m puddle carries essentially no
    // swell at all, and the new curve says so.
    const float lakeE = out[texel(f.hydro, kLakeX, kLakeZ) + 1];
    const float pondE = out[texel(f.hydro, kPondX, kPondZ) + 1];
    EXPECT_LT(lakeE, 0.10f) << "a few-tens-of-metres body cannot build a swell";
    EXPECT_GT(lakeE, pondE) << "more fetch -> more developed";
    EXPECT_GT(lakeE, 0.0f);
}

// THE discriminating property: the same body builds a different sea depending on WIND HEADING.
// No area-based formula can produce this — it is why fetch replaced area.
TEST(WaterProfileTest, TheSameBodyGetsDifferentEnergyByWindHeading) {
    // 10x2 cells at 128 m: 1280 m along X, 256 m along Z.
    WaterBodyIndex::Body b;
    b.cls = WaterBodyIndex::Class::Lake;
    b.areaCells = 20;
    b.bboxMin = {0, 0};
    b.bboxMax = {9, 1};
    b.volumeEst = 30.0f * 20.0f * 128.0f * 128.0f;   // deep enough that turbidity is irrelevant here

    WaterWind along; along.speedMs = kReferenceWindMs; along.dirRadians = 0.0f;          // +X, long axis
    WaterWind across; across.speedMs = kReferenceWindMs; across.dirRadians = kPi * 0.5f; // +Z, short axis

    const float eAlong  = deriveWaterProfile(&b, 128.0f, along).waveEnergy;
    const float eAcross = deriveWaterProfile(&b, 128.0f, across).waveEnergy;
    EXPECT_GT(eAlong, eAcross * 1.5f)
        << "wind along a 5:1 lake must build a materially bigger sea than wind across it";
}

// ⚑OCEAN SWELL MUST RESPOND TO WIND (solution-auditor, 2026-08-03). W3's first build short-
// circuited Ocean to the struct default waveEnergy = 1.0, inherited from W1's "oceans are fully
// developed" shortcut. That is right about FETCH — an ocean has effectively unlimited fetch — but
// it also froze the ocean's AMPLITUDE, so the one body the user most associates with a gale was
// the one body whose swell ignored the wind entirely. Unlimited fetch means tanh -> 1, not
// scale -> 1: the (U_A/U_A_ref)^2 term still applies.
TEST(WaterProfileTest, OceanSwellRespondsToWind) {
    WaterBodyIndex::Body ocean;
    ocean.cls = WaterBodyIndex::Class::Ocean;
    ocean.areaCells = 100000;
    ocean.bboxMin = {0, 0};
    ocean.bboxMax = {999, 999};
    ocean.volumeEst = 200.0f * 100000.0f * 128.0f * 128.0f;

    WaterWind calm; calm.speedMs = 3.0f;
    WaterWind mid;  mid.speedMs  = kReferenceWindMs;
    WaterWind gale; gale.speedMs = 15.0f;

    const float eCalm = deriveWaterProfile(&ocean, 128.0f, calm).waveEnergy;
    const float eMid  = deriveWaterProfile(&ocean, 128.0f, mid).waveEnergy;
    const float eGale = deriveWaterProfile(&ocean, 128.0f, gale).waveEnergy;

    EXPECT_NEAR(eMid, 1.0f, 1e-3f) << "the reference sea is still exactly 1 — existing oceans unchanged";
    EXPECT_GT(eGale, eMid)  << "a gale must raise the ocean swell";
    EXPECT_LT(eCalm, eMid)  << "a light air must lower it";
}

TEST(WaterProfileTest, RoughnessIsDerivedFromWindAndIsBodyIndependent) {
    // Cox-Munk slope has no fetch term, so two very different bodies under the same wind must get
    // the SAME roughness. Pinning this stops a future edit from quietly inventing a fetch link.
    WaterBodyIndex::Body big;  big.cls = WaterBodyIndex::Class::Lake;
    big.areaCells = 5000; big.bboxMin = {0, 0}; big.bboxMax = {99, 99};
    big.volumeEst = 40.0f * 5000.0f * 128.0f * 128.0f;
    WaterBodyIndex::Body tiny; tiny.cls = WaterBodyIndex::Class::Pond;
    tiny.areaCells = 1; tiny.bboxMin = {0, 0}; tiny.bboxMax = {0, 0};
    tiny.volumeEst = 1.0f * 128.0f * 128.0f;

    WaterWind calm;  calm.speedMs = 0.5f;
    WaterWind gale;  gale.speedMs = 15.0f;
    EXPECT_FLOAT_EQ(deriveWaterProfile(&big, 128.0f, calm).roughness,
                    deriveWaterProfile(&tiny, 128.0f, calm).roughness);
    EXPECT_LT(deriveWaterProfile(&big, 128.0f, calm).roughness,
              deriveWaterProfile(&big, 128.0f, gale).roughness) << "calm must be smoother than gale";
}

// W1 asserted "turbidity 0 AND roughness 1 everywhere". W2 deliberately breaks the first half —
// turbidity is now derived — so this test was CHANGED ON PURPOSE, not discovered broken. What
// survives is the half W2 does not touch: roughness stays neutral until W3, and dry land is never
// assigned water optics.
// W1 asserted roughness == 1 because nothing derived it. W3 now derives it from wind — and at the
// DEFAULT wind (the Beaufort-4 mid-point the shipped constants were authored to) it still comes out
// exactly 1. That identity is the reason W3 does not silently restyle every existing world.
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

// ── W3: fetch geometry ────────────────────────────────────────────────────────────────────────
// Fetch is the distance wind blows over open water before reaching a point. These test the pure
// geometry only — how fetch becomes a wave height is the grounded part and lands separately.
namespace {
// A body spanning cells [0..n-1] in each axis, i.e. n cells wide.
glm::ivec2 lo() { return {0, 0}; }
}  // namespace

TEST(WaterProfileTest, FetchIsAnisotropicForAnElongatedBody) {
    // THE point of fetch: the same lake builds a different sea depending on wind heading. A model
    // that only used area (the shipped log2(area) proxy) cannot express this at all.
    const glm::ivec2 mn = lo(), mx{9, 1};   // 1280 m x 256 m
    const float along  = fetchAlongWind(mn, mx, 128.0f, 0.0f);         // +X, the long axis
    const float across = fetchAlongWind(mn, mx, 128.0f, kPi * 0.5f);   // +Z, the short axis
    EXPECT_NEAR(along, 1280.0f, 0.01f);
    EXPECT_NEAR(across, 256.0f, 0.01f);
    EXPECT_GT(along, across * 4.0f) << "a 5:1 lake must give a >4x fetch difference by heading";
}

// ⚑THE TEST THAT CAUGHT A REAL BUG (solution-auditor, 2026-08-03). The first implementation
// computed the bbox's SUPPORT WIDTH (|w*ux| + |d*uz|, the projection/shadow extent) instead of the
// longest CHORD along the wind — and fetch is a chord: the distance wind actually travels over
// water. The two coincide only at the cardinal angles AND at the rectangle's own diagonal angle,
// which for a SQUARE is exactly 45 degrees — the one non-cardinal angle the original suite tested.
// An elongated body at an oblique, non-diagonal angle is the case that separates them.
TEST(WaterProfileTest, FetchOnAnElongatedBodyAtAnObliqueAngleIsTheTrueChord) {
    const glm::ivec2 mn{0, 0}, mx{9, 1};        // 1280 m x 256 m
    // True longest interior chord along u = min(w/|ux|, d/|uz|):
    //   30 deg -> min(1280/0.8660, 256/0.5000) = min(1478.0, 512.0) = 512.0
    //   60 deg -> min(1280/0.5000, 256/0.8660) = min(2560.0, 295.6) = 295.6
    // The support-width formula gives 1236.5 and 861.7 — 141% and 191% too high.
    EXPECT_NEAR(fetchAlongWind(mn, mx, 128.0f, kPi / 6.0f), 512.0f, 1.0f);
    EXPECT_NEAR(fetchAlongWind(mn, mx, 128.0f, kPi / 3.0f), 295.6f, 1.0f);
}

TEST(WaterProfileTest, FetchAcrossASquareAtFortyFiveDegreesIsItsDiagonal) {
    // Square body, 4 cells (512 m) a side. At 45 degrees the longest chord IS the diagonal.
    // ⚑Kept, but note this case is DEGENERATE as a correctness check: for a square, 45 degrees is
    // the diagonal angle, the one oblique angle where the (wrong) support width and the (right)
    // chord agree. It passed against the buggy formula too. The elongated-oblique test above is
    // what actually discriminates; this one only pins the square case.
    const glm::ivec2 mn = lo(), mx{3, 3};
    const float diag = fetchAlongWind(mn, mx, 128.0f, kPi * 0.25f);
    EXPECT_NEAR(diag, 512.0f * std::sqrt(2.0f), 0.5f);
    EXPECT_GT(diag, fetchAlongWind(mn, mx, 128.0f, 0.0f)) << "diagonal must exceed the axis extent";
}

TEST(WaterProfileTest, FetchOfASingleCellIsOneCell) {
    // A one-cell pond must not report zero fetch (inclusive bounds) nor a whole cell-grid's worth.
    EXPECT_NEAR(fetchAlongWind({5, 5}, {5, 5}, 128.0f, 0.0f), 128.0f, 0.01f);
}

TEST(WaterProfileTest, FetchIsSymmetricUnderWindReversal) {
    // Wind from the east and wind from the west cross the same water.
    const glm::ivec2 mn = lo(), mx{9, 3};
    for (float a : {0.0f, 0.7f, 1.3f, 2.5f}) {
        EXPECT_NEAR(fetchAlongWind(mn, mx, 128.0f, a),
                    fetchAlongWind(mn, mx, 128.0f, a + kPi), 0.01f) << "angle " << a;
    }
}

// ── W3: fetch-limited wave growth (SMB/CERC) and wind-driven slope (Cox & Munk) ───────────────
// Expected values are the GROUNDING PASS's own independently-derived sanity figures, which this
// implementation reproduces: at Beaufort-4 wind a 500 m pond reaches Hs 0.13 m, a 6 km lake
// 0.36 m, the shipped 0.9 m swell needs ~69 km of fetch, and the fully-developed cap is 1.57 m.
// Energy is the tanh term, so Hs = 0.283 * energy * U_A^2/g with U_A(6.7) = 7.3677.
TEST(WaterProfileTest, FetchLimitedEnergyMatchesTheSmbCurve) {
    const float U = kReferenceWindMs;   // 6.7 m/s, Beaufort 4 mid-point
    EXPECT_NEAR(fetchLimitedEnergy(500.0f,   U), 0.082685f, 1e-4f) << "500 m pond";
    EXPECT_NEAR(fetchLimitedEnergy(6000.0f,  U), 0.231080f, 1e-4f) << "6 km lake";
    EXPECT_NEAR(fetchLimitedEnergy(69000.0f, U), 0.575964f, 1e-4f) << "69 km -> the shipped 0.9 m swell";

    // The same numbers expressed as significant wave height, which is what the grounding pass
    // actually quoted — checking the curve in the units it was sourced in, not just the ratio.
    const float uA = 0.71f * std::pow(U, 1.23f);
    auto hs = [&](float F) { return 0.283f * fetchLimitedEnergy(F, U) * uA * uA / 9.81f; };
    EXPECT_NEAR(hs(500.0f),   0.13f, 0.01f);
    EXPECT_NEAR(hs(6000.0f),  0.36f, 0.01f);
    EXPECT_NEAR(hs(69000.0f), 0.90f, 0.01f);
}

TEST(WaterProfileTest, FetchLimitedEnergyIsBoundedAndMonotonic) {
    const float U = kReferenceWindMs;
    // Bounded [0,1] BY CONSTRUCTION — this is why the SMB tanh term is used directly instead of a
    // ratio against a separately-sourced fully-developed height, which would exceed 1 at big fetch.
    EXPECT_FLOAT_EQ(fetchLimitedEnergy(0.0f, U), 0.0f);
    EXPECT_NEAR(fetchLimitedEnergy(1.0e9f, U), 1.0f, 1e-4f) << "unlimited fetch -> fully developed";
    float prev = -1.0f;
    for (float F : {10.0f, 100.0f, 1000.0f, 10000.0f, 100000.0f, 1000000.0f}) {
        const float e = fetchLimitedEnergy(F, U);
        EXPECT_GE(e, 0.0f); EXPECT_LE(e, 1.0f);
        EXPECT_GT(e, prev) << "energy must rise strictly with fetch (F=" << F << ")";
        prev = e;
    }
}

TEST(WaterProfileTest, StrongerWindNeedsMoreFetchToFullyDevelop) {
    // X = g*F/U_A^2, so at a FIXED fetch a stronger wind is LESS developed. This is the physical
    // behaviour that a pure area/size proxy cannot express at all.
    const float F = 6000.0f;
    EXPECT_GT(fetchLimitedEnergy(F, 4.0f), fetchLimitedEnergy(F, 6.7f));
    EXPECT_GT(fetchLimitedEnergy(F, 6.7f), fetchLimitedEnergy(F, 15.0f));
}

TEST(WaterProfileTest, DegenerateWindAndFetchAreZeroNotNaN) {
    for (float F : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()})
        EXPECT_FLOAT_EQ(fetchLimitedEnergy(F, kReferenceWindMs), 0.0f);
    for (float U : {0.0f, -3.0f, std::numeric_limits<float>::quiet_NaN()})
        EXPECT_FLOAT_EQ(fetchLimitedEnergy(6000.0f, U), 0.0f) << "no wind = no wind-sea";
}

// ⚑THE RED TEST FOR A BUG THE L4 FOUND. The first W3 build scaled the swell by
// fetchLimitedEnergy, the FRACTION of a fully-developed sea. That fraction FALLS as wind rises
// (X = gF/U_A^2), so a gale made waves smaller — backwards. Absolute Hs rises because of the
// U_A^2/g factor the fraction discards. On the buggy build this test failed: at a fixed 6 km fetch
// the fraction went 0.231 (6.7 m/s) -> 0.087 (15 m/s).
TEST(WaterProfileTest, StrongerWindMakesBiggerWavesAtFixedFetch) {
    const float F = 6000.0f;
    const float calm = waveHeightScale(F, 3.0f);
    const float mid  = waveHeightScale(F, kReferenceWindMs);
    const float gale = waveHeightScale(F, 15.0f);
    EXPECT_GT(gale, mid)  << "a gale must build a BIGGER sea than a moderate breeze";
    EXPECT_GT(mid,  calm) << "a moderate breeze must build a bigger sea than a light air";
    // And the fraction alone genuinely moves the other way — this pins WHY the scale is needed,
    // so a future edit cannot "simplify" back to the fraction without turning this red.
    EXPECT_LT(fetchLimitedEnergy(F, 15.0f), fetchLimitedEnergy(F, kReferenceWindMs));
}

TEST(WaterProfileTest, WaveHeightScaleIsUnityForTheReferenceSea) {
    // A fully-developed sea at the reference wind IS the sea the shipped amplitude was authored
    // to, so its multiplier must be exactly 1 — this is what keeps existing oceans unchanged.
    EXPECT_NEAR(waveHeightScale(1.0e9f, kReferenceWindMs), 1.0f, 1e-3f);
    // A 6 km lake at the reference wind is fetch-limited to the same fraction as before, since the
    // wind ratio is 1 there.
    EXPECT_NEAR(waveHeightScale(6000.0f, kReferenceWindMs), 0.231080f, 1e-4f);
}

TEST(WaterProfileTest, WaveHeightScaleIsClampedAndNeverNaN) {
    EXPECT_LE(waveHeightScale(1.0e9f, 40.0f), kMaxWaveHeightScale) << "storm must not blow past the clamp";
    EXPECT_FLOAT_EQ(waveHeightScale(0.0f, 10.0f), 0.0f);
    EXPECT_FLOAT_EQ(waveHeightScale(1000.0f, 0.0f), 0.0f);
    EXPECT_FALSE(std::isnan(waveHeightScale(std::numeric_limits<float>::quiet_NaN(), 6.7f)));
}

TEST(WaterProfileTest, WindRoughnessIsUnityAtTheReferenceWind) {
    // The identity that keeps W3 from silently restyling every existing world: at the wind the
    // shipped constants were authored to, the ripple slope scale is exactly 1.
    EXPECT_FLOAT_EQ(windRoughness(kReferenceWindMs), 1.0f);
}

TEST(WaterProfileTest, WindRoughnessFollowsCoxMunkSlope) {
    // sqrt(mss(U)/mss(6.7)) with mss = 0.003 + 0.00512*U.
    EXPECT_NEAR(windRoughness(0.0f),  0.283585f, 1e-4f) << "dead calm -> near-mirror micro-slope";
    EXPECT_NEAR(windRoughness(2.0f),  0.595753f, 1e-4f);
    EXPECT_NEAR(windRoughness(10.0f), 1.205374f, 1e-4f);
    EXPECT_NEAR(windRoughness(15.0f), 1.462594f, 1e-4f);
    EXPECT_GT(windRoughness(15.0f), windRoughness(0.0f) * 5.0f)
        << "calm-to-gale must span a visible range, not a nudge";
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
