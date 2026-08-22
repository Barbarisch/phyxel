// C1 of docs/ContinuousLodPlan.md — the shared LOD metric.
//
// PHASE 0 (characterization) FIRST: the engine's ten LOD systems had ZERO test
// coverage (solution-auditor, 2026-07-29) — flipping s_shadowFrustumCull, or
// setting m_charLod1Distance to 3500, left all 3080 tests green. Re-homing them
// without pinning current behaviour would be unverifiable by construction.
// So: pin the legacy numbers, then prove the new metric reproduces them exactly
// at the reference config and scales correctly away from it.

#include <gtest/gtest.h>
#include "core/LodService.h"
#include "graphics/RenderCoordinator.h"

using Phyxel::Core::LodService;

namespace {
// Reference humanoid height used to convert the character distance thresholds
// into pixel budgets. ~1.8 m is a standing adult; the engine's capsule is in
// world units where 1 cube = 1 m (resources/structure_styles.json:1).
constexpr float kCharacterHeight = 1.8f;

// The legacy hand-tuned character thresholds (RenderCoordinator.h:405-407).
constexpr float kLegacyLod1 = 35.0f;
constexpr float kLegacyLod2 = 80.0f;
constexpr float kLegacyCull = 400.0f;
} // namespace

// ---------------------------------------------------------------------------
// PHASE 0 — characterization. These pin what the engine does TODAY. If someone
// retunes a threshold, these fail and force the change to be deliberate.
// ---------------------------------------------------------------------------
TEST(LodCharacterizationTest, CharacterLodDefaultsAreUnchanged) {
    Phyxel::Graphics::RenderCoordinator::CharacterLodDefaults d;
    EXPECT_FLOAT_EQ(d.lod1Distance, kLegacyLod1);
    EXPECT_FLOAT_EQ(d.lod2Distance, kLegacyLod2);
    EXPECT_FLOAT_EQ(d.cullDistance, kLegacyCull);
}

TEST(LodCharacterizationTest, LegacyThresholdsSelectExpectedLevels) {
    // Exact legacy semantics (RenderCoordinator.h:408-412): strictly-greater
    // comparisons on SQUARED distance.
    auto legacyLevel = [](float dist) {
        const float d2 = dist * dist;
        if (d2 > kLegacyLod2 * kLegacyLod2) return 2;
        if (d2 > kLegacyLod1 * kLegacyLod1) return 1;
        return 0;
    };
    EXPECT_EQ(legacyLevel(0.0f), 0);
    EXPECT_EQ(legacyLevel(34.9f), 0);
    EXPECT_EQ(legacyLevel(35.0f), 0);   // boundary is NOT inclusive
    EXPECT_EQ(legacyLevel(35.1f), 1);
    EXPECT_EQ(legacyLevel(79.9f), 1);
    EXPECT_EQ(legacyLevel(80.0f), 1);
    EXPECT_EQ(legacyLevel(80.1f), 2);
}

// ---------------------------------------------------------------------------
// The metric itself.
// ---------------------------------------------------------------------------
TEST(LodServiceTest, ProjectedPixelsMatchesHandDerivedValue) {
    auto v = LodService::referenceView();
    // pixelScale = 900*0.5 / tan(22.5deg) = 450 / 0.41421356 = 1086.4...
    EXPECT_NEAR(LodService::pixelScale(v), 1086.396f, 0.05f);
    // A 1.8 m character at 35 m: 1.8 * 1086.4 / 35 = 55.87 px
    EXPECT_NEAR(LodService::projectedPixels(kCharacterHeight, kLegacyLod1, v), 55.87f, 0.05f);
}

TEST(LodServiceTest, DistanceForPixelsInvertsProjectedPixels) {
    auto v = LodService::makeView(1440.0f, 60.0f);
    for (float dist : {5.0f, 35.0f, 250.0f, 2048.0f}) {
        const float px = LodService::projectedPixels(kCharacterHeight, dist, v);
        EXPECT_NEAR(LodService::distanceForPixels(kCharacterHeight, px, v), dist, dist * 1e-3f);
    }
}

// THE POINT OF C1: a pixel budget derived from a legacy distance must reproduce
// that exact distance at the reference config -- so re-homing changes nothing
// for the config the numbers were tuned at.
TEST(LodServiceTest, LegacyDistancesRoundTripExactlyAtReferenceConfig) {
    auto ref = LodService::referenceView();
    for (float legacy : {kLegacyLod1, kLegacyLod2, kLegacyCull}) {
        const float budget = LodService::pixelBudgetForLegacyDistance(kCharacterHeight, legacy);
        const float back = LodService::distanceForPixels(kCharacterHeight, budget, ref);
        EXPECT_NEAR(back, legacy, legacy * 1e-4f)
            << "re-homing must be a no-op at the config the threshold was tuned at";
    }
}

// ...and AWAY from the reference config it must actually differ -- otherwise the
// whole exercise is cosmetic. Doubling viewport height doubles pixels per world
// unit, so a given pixel budget is reached twice as far away.
TEST(LodServiceTest, HigherResolutionHoldsDetailProportionallyFarther) {
    const float budget = LodService::pixelBudgetForLegacyDistance(kCharacterHeight, kLegacyLod1);

    const float at900 = LodService::distanceForPixels(
        kCharacterHeight, budget, LodService::makeView(900.0f, 45.0f));
    const float at1800 = LodService::distanceForPixels(
        kCharacterHeight, budget, LodService::makeView(1800.0f, 45.0f));

    EXPECT_NEAR(at900, kLegacyLod1, 0.01f);
    EXPECT_NEAR(at1800, kLegacyLod1 * 2.0f, 0.05f)
        << "2x viewport height must push the LOD1 distance out 2x";
}

TEST(LodServiceTest, NarrowerFovHoldsDetailFarther) {
    const float budget = LodService::pixelBudgetForLegacyDistance(kCharacterHeight, kLegacyLod1);
    const float wide = LodService::distanceForPixels(
        kCharacterHeight, budget, LodService::makeView(900.0f, 90.0f));
    const float narrow = LodService::distanceForPixels(
        kCharacterHeight, budget, LodService::makeView(900.0f, 30.0f));
    EXPECT_GT(narrow, wide)
        << "zooming in (narrow FOV) magnifies, so detail must be held farther out";
}

// The engine today is BLIND to both of the above. This documents the defect the
// legacy path has and that C1 fixes -- it is the reason C1 exists.
TEST(LodServiceTest, LegacyPathIsBlindToResolutionAndFov) {
    auto legacyLevel = [](float dist) {
        const float d2 = dist * dist;
        if (d2 > kLegacyLod2 * kLegacyLod2) return 2;
        if (d2 > kLegacyLod1 * kLegacyLod1) return 1;
        return 0;
    };
    // Same world distance, wildly different views -> legacy gives the same
    // answer, because it never sees the view at all.
    EXPECT_EQ(legacyLevel(60.0f), 1);
    const float pxTiny = LodService::projectedPixels(kCharacterHeight, 60.0f,
                                                     LodService::makeView(480.0f, 100.0f));
    const float pxHuge = LodService::projectedPixels(kCharacterHeight, 60.0f,
                                                     LodService::makeView(2160.0f, 20.0f));
    EXPECT_GT(pxHuge / pxTiny, 10.0f)
        << "the same character covers >10x the pixels across these views, yet the "
           "legacy world-unit threshold returns the identical LOD level";
}

// ---------------------------------------------------------------------------
// levelForDistance — the power-of-two cut (plan §2.4).
// ---------------------------------------------------------------------------
TEST(LodServiceTest, LevelForDistanceIsMonotonicInDistance) {
    auto v = LodService::referenceView();
    int prev = -1;
    for (float d = 8.0f; d < 4096.0f; d *= 1.5f) {
        const int lvl = LodService::levelForDistance(1.0f, d, 2.0f, v);
        EXPECT_GE(lvl, prev) << "level must not get FINER as distance grows (d=" << d << ")";
        prev = lvl;
    }
}

TEST(LodServiceTest, LevelForDistanceRespectsMaxLevel) {
    auto v = LodService::referenceView();
    EXPECT_LE(LodService::levelForDistance(1.0f, 1e9f, 2.0f, v, 5), 5);
    EXPECT_EQ(LodService::levelForDistance(1.0f, 0.01f, 2.0f, v, 5), 0)
        << "point blank must stay at the finest level";
}

TEST(LodServiceTest, FadeWeightClampsAndInterpolates) {
    EXPECT_FLOAT_EQ(LodService::fadeWeight(100.0f, 10.0f, 20.0f), 1.0f);
    EXPECT_FLOAT_EQ(LodService::fadeWeight(1.0f, 10.0f, 20.0f), 0.0f);
    EXPECT_NEAR(LodService::fadeWeight(15.0f, 10.0f, 20.0f), 0.5f, 1e-5f);
}

// ---------------------------------------------------------------------------
// C1 wiring: the screen-space correction applied to the CHARACTER thresholds.
// The correction is a pure scale on the legacy distances, so it is verifiable
// without a Vulkan device.
// ---------------------------------------------------------------------------
// NOTE (solution-auditor 2026-07-29): this used to be a LOCAL COPY of the scale
// formula. That made the tests unfalsifiable for the thing that matters most —
// mutating the real updateLodView() to `m_lodViewScale = 1.0f` (disabling the
// entire feature) left all 27 tests GREEN. Tests now call the PRODUCTION function
// that RenderCoordinator itself delegates to, so that mutation is caught.
namespace {
float viewScale(float viewportHeight, float fovYDeg) {
    return LodService::viewScaleVsReference(viewportHeight, fovYDeg);
}
} // namespace

TEST(LodServiceTest, ViewScaleIsExactlyOneAtReferenceConfig) {
    // THE safety property: turning C1 on must change NOTHING at the config the
    // 35/80/400 thresholds were hand-tuned at.
    EXPECT_NEAR(viewScale(LodService::kReferenceViewportHeight,
                          LodService::kReferenceFovYDegrees), 1.0f, 1e-6f);
}

TEST(LodServiceTest, ViewScaleTracksResolutionAndFov) {
    EXPECT_NEAR(viewScale(1800.0f, 45.0f), 2.0f, 1e-4f);   // 2x height -> 2x
    EXPECT_NEAR(viewScale(450.0f, 45.0f), 0.5f, 1e-4f);    // half height -> half
    EXPECT_GT(viewScale(900.0f, 30.0f), 1.0f);             // zoom in -> hold detail farther
    EXPECT_LT(viewScale(900.0f, 90.0f), 1.0f);             // wide angle -> shed sooner
}

TEST(LodServiceTest, ScaledCharacterThresholdsMatchLegacyAtReferenceConfig) {
    const float k = viewScale(LodService::kReferenceViewportHeight,
                              LodService::kReferenceFovYDegrees);
    Phyxel::Graphics::RenderCoordinator::CharacterLodDefaults d;
    EXPECT_FLOAT_EQ(d.lod1Distance * k, kLegacyLod1);
    EXPECT_FLOAT_EQ(d.lod2Distance * k, kLegacyLod2);
    EXPECT_FLOAT_EQ(d.cullDistance * k, kLegacyCull);
}


// ---------------------------------------------------------------------------
// Falsifiability guards: exercise the SHIPPED selection logic, not a copy of it.
// RenderCoordinator::lodForDistanceSq delegates to characterLodLevel, so breaking
// either is caught here.
// ---------------------------------------------------------------------------
TEST(LodServiceTest, CharacterLodLevelReproducesLegacyAtUnitScale) {
    auto lvl = [](float d) {
        return LodService::characterLodLevel(d * d, kLegacyLod1, kLegacyLod2, 1.0f);
    };
    EXPECT_EQ(lvl(34.9f), 0);
    EXPECT_EQ(lvl(35.0f), 0);   // strictly-greater boundary preserved
    EXPECT_EQ(lvl(35.1f), 1);
    EXPECT_EQ(lvl(80.0f), 1);
    EXPECT_EQ(lvl(80.1f), 2);
}

TEST(LodServiceTest, CharacterLodLevelScalesWithTheView) {
    // At 2x the reference viewport height the same character is twice as big on
    // screen, so LOD1 must not kick in until twice as far away.
    const float k = LodService::viewScaleVsReference(1800.0f, 45.0f);
    ASSERT_NEAR(k, 2.0f, 1e-4f);
    EXPECT_EQ(LodService::characterLodLevel(60.0f * 60.0f, kLegacyLod1, kLegacyLod2, 1.0f), 1)
        << "at reference scale, 60u is already LOD1";
    EXPECT_EQ(LodService::characterLodLevel(60.0f * 60.0f, kLegacyLod1, kLegacyLod2, k), 0)
        << "at 2x resolution the same 60u character must still be full detail";
}

TEST(LodServiceTest, ViewScaleOfOneDisablesTheCorrection) {
    // Pins that the A/B path (s_screenSpaceLod=false -> scale 1.0) is
    // byte-identical to legacy selection.
    for (float d : {1.0f, 34.9f, 35.1f, 79.9f, 80.1f, 500.0f}) {
        const float d2 = d * d;
        const int legacy = (d2 > kLegacyLod2 * kLegacyLod2) ? 2
                         : (d2 > kLegacyLod1 * kLegacyLod1) ? 1 : 0;
        EXPECT_EQ(LodService::characterLodLevel(d2, kLegacyLod1, kLegacyLod2, 1.0f), legacy)
            << "scale 1.0 must reproduce legacy exactly at d=" << d;
    }
}

// ---------------------------------------------------------------------------
// PHASE 0 characterization for the VEGETATION radii re-homed in the second C1
// batch. Like the character thresholds, these shipped with zero test coverage.
// ---------------------------------------------------------------------------
#include "graphics/GrassRenderPipeline.h"
#include "graphics/FoliageRenderPipeline.h"

TEST(LodCharacterizationTest, GrassAndFoliageDefaultsAreUnchanged) {
    // UPDATED 2026-08-01 (twice — same day) for the grass retunes: first denser/skinnier/further
    // (192u / 70 blades), then the continuous per-blade density falloff that made 224u / 140
    // blades / an 80u fade affordable. This test did its job both times — it went red the moment
    // the defaults moved, which is exactly why C1 added it. The values below are the NEW pinned
    // baseline, not a relaxation of the check.
    // Rationale + the vertex budget behind them: docs/VegetationWindPlan.md and the budget
    // comment on GrassRenderPipeline::bladesForDistance.
    Phyxel::Graphics::GrassRenderPipeline::Params g;
    EXPECT_TRUE(g.enabled);
    EXPECT_FLOAT_EQ(g.radius, 224.0f) << "affordable only via the continuous density falloff";
    EXPECT_FLOAT_EQ(g.fadeRange, 80.0f) << "deliberately a third of the radius — a short fade reads as a mowing line";
    // UPDATED 2026-08-21: 30 -> 55 (user: "noticeably denser"), with the patch-field coverage
    // keeping ~83% on average. Budget recomputed per the standing rule: 55 blades * 24 verts
    // (4 segments) ≈ 23M verts/frame at R=224 — the historically-accepted 22.8M operating point.
    EXPECT_EQ(g.bladesPerVoxel, 55u);
    // UPDATED 2026-08-21: 0.50 -> 1.0 ("lively meadow", user-picked target): ~15 degree average
    // lean, gusts to ~43 degrees — leanSin ≈ 2*(base + gustAmp*gust)*windStrength at the
    // WindSystem defaults. At the old 0.50 the average lean was ~7 degrees ("way too slight").
    EXPECT_FLOAT_EQ(g.windStrength, 1.0f);
    // Pinned 2026-08-21 with the pointy-default + seam-free-meadow change: smooth style is the
    // default again (user call), and the meadow periods must DIVIDE 2048 or the field seams at
    // the hash-domain wrap (GrassMeadowSeamTest owns the continuity proof).
    EXPECT_EQ(g.bladeStyle, 0u);
    EXPECT_FLOAT_EQ(g.meadowScale, 64.0f);
    EXPECT_FLOAT_EQ(g.meadowDetailScale, 32.0f);
    // The old "whole clumps only" assertion is DELETED, not relaxed: grass.vert no longer groups
    // blades into tufts (`blade / BLADES_PER_CLUMP` is gone), so there is no partial clump to
    // render torn and no reason for the authored count to be a multiple of 7 — 30 is not.
    // `bladesForDistance` still quantises the DRAWN count to 7 for tidy draws, and
    // GrassDensityLodTest pins that separately.

    Phyxel::Graphics::FoliageRenderPipeline::Params f;
    EXPECT_TRUE(f.enabled);
    EXPECT_FLOAT_EQ(f.radius, 512.0f);
}

TEST(LodServiceTest, VegetationRadiiAreUnchangedAtReferenceConfigAndScaleElsewhere) {
    // Calls the PRODUCTION function the render path calls -- not a re-derivation.
    // (An earlier version recomputed `radius * viewScaleVsReference(...)` inline, so
    // mutating the real function to `return baseRadius` left all 32 tests green.)
    using RC = Phyxel::Graphics::RenderCoordinator;

    const float refScale = LodService::viewScaleVsReference(
        LodService::kReferenceViewportHeight, LodService::kReferenceFovYDegrees);
    EXPECT_FLOAT_EQ(RC::effectiveVegetationRadius(48.0f, refScale), 48.0f)
        << "grass radius must be untouched at 1600x900";
    EXPECT_FLOAT_EQ(RC::effectiveVegetationRadius(512.0f, refScale), 512.0f)
        << "foliage radius must be untouched at 1600x900";

    const float hiDpi = LodService::viewScaleVsReference(1800.0f, 45.0f);
    EXPECT_NEAR(RC::effectiveVegetationRadius(48.0f, hiDpi), 96.0f, 1e-2f)
        << "at 2x resolution grass must reach twice as far to cover the same pixels";
    EXPECT_NEAR(RC::effectiveVegetationRadius(512.0f, hiDpi), 1024.0f, 1e-1f);

    // The disabled path (s_screenSpaceLod=false -> scale 1.0) must be an exact no-op.
    EXPECT_FLOAT_EQ(RC::effectiveVegetationRadius(48.0f, 1.0f), 48.0f);
}

// ---------------------------------------------------------------------------
// FAR TERRAIN — the last of C1's four named subsystems. Calls the SHIPPED
// computeRingsFor(), not a re-derivation.
// ---------------------------------------------------------------------------
#include "graphics/FarTerrainManager.h"

TEST(LodCharacterizationTest, FarTerrainDefaultsAreUnchanged) {
    // UPDATED 2026-08-01: far terrain now ships ON with a 4096 horizon. The old assertion
    // ("ships OFF; flipping it is a separate decision") was correct at the time and this test
    // correctly went red when the decision was taken — see docs/ContinuousLodPlan.md for the
    // measurement (57 tiles / 71,630 tris / horizon filled) and for the four-config far-plane
    // trap that made an earlier attempt look like a far-terrain regression.
    Phyxel::Graphics::FarTerrainManager::Params p;
    EXPECT_TRUE(p.enabled) << "far terrain ships ON as of 2026-08-01";
    EXPECT_FLOAT_EQ(p.maxDistance, 4096.0f);
    EXPECT_EQ(p.ringSteps, std::vector<int>({2, 4, 8, 16}));
    EXPECT_EQ(p.maxResidentTiles, 768);
    EXPECT_FLOAT_EQ(p.viewScale, 1.0f) << "default must be an exact no-op";
}

TEST(LodServiceTest, FarTerrainRingsAreUnchangedAtReferenceConfig) {
    using FT = Phyxel::Graphics::FarTerrainManager;
    FT::Params p;                       // viewScale defaults to 1.0
    const auto rings = FT::computeRingsFor(p);
    ASSERT_FALSE(rings.empty());
    // Band edges double per ring (512, 1024, 2048) with the last ring pinned to maxDistance.
    EXPECT_FLOAT_EQ(rings.front().startR, 0.0f);
    EXPECT_FLOAT_EQ(rings.back().endR, 4096.0f);
    for (const auto& r : rings) EXPECT_LE(r.endR, 4096.0f);
    EXPECT_EQ(rings.size(), 4u) << "4 rings for a 4096 horizon";
    // Rings must tile the radius with no gap: each ring starts where the previous ended.
    for (size_t i = 1; i < rings.size(); ++i)
        EXPECT_FLOAT_EQ(rings[i].startR, rings[i - 1].endR) << "gap before ring " << i;
}

TEST(LodServiceTest, FarTerrainHorizonScalesWithTheView) {
    using FT = Phyxel::Graphics::FarTerrainManager;
    FT::Params base;
    FT::Params scaled;
    scaled.viewScale = LodService::viewScaleVsReference(1800.0f, 45.0f);  // 2x
    ASSERT_NEAR(scaled.viewScale, 2.0f, 1e-4f);

    const auto a = FT::computeRingsFor(base);
    const auto b = FT::computeRingsFor(scaled);
    ASSERT_FALSE(a.empty());
    ASSERT_FALSE(b.empty());
    EXPECT_NEAR(b.back().endR, a.back().endR * 2.0f, 1.0f)
        << "at 2x resolution the horizon must sit twice as far out to cover the same pixels";
    // Ring PROPORTIONS must be preserved -- only absolute distances move.
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i)
        EXPECT_EQ(a[i].step, b[i].step) << "ring " << i << " sampling stride must not change";
}

// ---------------------------------------------------------------------------
// C2.1 guard (docs/ContinuousLodPlan.md). A solution-auditor found this guard had
// ZERO regression coverage: deleting it would leave the whole suite green while the
// GPU-driven shadow path silently rendered wrong geometry. It is the single thing
// standing between "safe no-op" and "shadow corruption", so it gets pinned.
//
// vkCmdDrawIndexedIndirect's firstInstance addresses instances by STRIDE, so an arena
// span byte offset is only addressable when it is an exact multiple of that stride.
TEST(LodServiceTest, C21GuardRejectsStrideMisalignedArenaSpans) {
    using RC = Phyxel::Graphics::RenderCoordinator;
    constexpr size_t kInstanceStride = 24;   // sizeof(InstanceData), core/Types.h
    constexpr size_t kArenaAlignment = 256;  // ChunkArenaAllocator::kAlignment

    // The real, shipped combination: 256 % 24 == 16, so a span at the first aligned
    // offset is NOT addressable. This is the case that fires in the engine today.
    ASSERT_NE(kArenaAlignment % kInstanceStride, 0u)
        << "precondition: if the allocator ever aligns to a stride multiple this test is moot";
    EXPECT_FALSE(RC::spanIsStrideAddressable(kArenaAlignment, kInstanceStride))
        << "offset 256 is not a multiple of 24 — firstInstance would truncate";

    // Every multiple of the alignment that is not also a multiple of the stride must fail.
    for (size_t k = 1; k <= 8; ++k) {
        const size_t off = k * kArenaAlignment;
        EXPECT_EQ(RC::spanIsStrideAddressable(off, kInstanceStride), (off % kInstanceStride) == 0)
            << "offset " << off;
    }

    // lcm(256, 24) = 768 — the proposed allocator fix — must be addressable.
    EXPECT_TRUE(RC::spanIsStrideAddressable(768, kInstanceStride))
        << "768 is lcm(256,24) and must be accepted; it is the recommended allocator alignment";
    EXPECT_TRUE(RC::spanIsStrideAddressable(0, kInstanceStride)) << "offset 0 is always addressable";
    // Degenerate stride must not divide by zero.
    EXPECT_FALSE(RC::spanIsStrideAddressable(256, 0));
}

// ---------------------------------------------------------------------------
// STRUCTURE-LOD RESIDENCY GATE — the 2026-08-05 stale-proxy bug.
// A structure proxy's fade floor (minFade = 1 - readiness) must be able to
// reach 0, or the low-poly proxy renders FULLY SOLID on top of the real
// building forever. The tree tile gate (tileHandoffMinFade) earned three
// escapes for exactly this defect class; these tests pin the same escapes on
// the structure gate's pure probe. Written RED against the verbatim
// extraction of the shipped logic (single mid-Y plane, every column votes).
// ---------------------------------------------------------------------------

namespace {
using GateRC = Phyxel::Graphics::RenderCoordinator;

/// Chunk-residency stub: rendered geometry exists only within `radius` of `center` (XZ).
auto residentWithin(glm::vec3 center, float radius) {
    return [center, radius](const glm::ivec3& wp) {
        const glm::vec2 d(float(wp.x) - center.x, float(wp.z) - center.z);
        return glm::length(d) <= radius;
    };
}
}  // namespace

// A settlement WIDER than the residency radius: the camera stands at one end,
// chunks near the camera are resident+meshed, the far end is legitimately not
// loaded. Columns beyond the fade band must NOT vote — under the old rule they
// veto readiness forever and the proxy stays solid at point-blank range.
TEST(StructureLodGateTest, OutOfBandColumnsDoNotVote) {
    const glm::ivec3 mn(0, 40, 0), mx(500, 60, 40);       // 500u-wide settlement
    const glm::vec3 camera(10.0f, 50.0f, 20.0f);          // standing at the west end
    const float fadeGateEnd = 346.0f;                     // load+90 band edge (live default)
    // Everything within the 352u unload radius is resident and meshed; the east half is not.
    // (fadeGateEnd is capped at unload-6 in the renderer, so voting columns are always
    // inside the streamed field when streaming is caught up — the realistic ordering.)
    const auto gate = GateRC::structureGateProbe(
        mn, mx, camera, fadeGateEnd, residentWithin(glm::vec3(camera), 352.0f));
    EXPECT_TRUE(gate.ready)
        << "columns beyond fadeGateEnd vetoed readiness — the proxy would render solid "
           "on top of the resident, meshed half the player is standing in";
    EXPECT_GT(gate.votes, 0) << "in-band columns must still vote";
}

// A structure ENTIRELY beyond the fade band: nothing is close enough to vote,
// so distance fade must govern (votes == 0 => caller releases the proxy).
// Under the old rule every column voted, readiness pinned to 0, and minFade
// stayed 1 even where the distance fade alone should own the transition.
TEST(StructureLodGateTest, ZeroVotesReleasesTheProxyToDistanceFade) {
    const glm::ivec3 mn(1000, 40, 1000), mx(1040, 60, 1040);
    const glm::vec3 camera(0.0f, 50.0f, 0.0f);            // ~1.4 km away
    const auto gate = GateRC::structureGateProbe(
        mn, mx, camera, 346.0f, [](const glm::ivec3&) { return false; });
    EXPECT_EQ(gate.votes, 0)
        << "no probe column is inside the fade band, so none may vote";
}

// A TALL tower whose mid-height chunk is pure air (renders zero instances)
// while its base is resident and meshed. The probe must scan the AABB's full
// Y-span like the tree gate does — the old single mid-plane probe reads the
// air chunk, vetoes readiness, and pins the proxy solid over the real tower.
TEST(StructureLodGateTest, ProbesFullYSpanNotOneMidPlane) {
    const glm::ivec3 mn(0, 0, 0), mx(20, 300, 20);        // 300u tower AABB
    const glm::vec3 camera(30.0f, 10.0f, 30.0f);
    // Rendered geometry exists only below y=64 (the tower's meshed base band).
    const auto gate = GateRC::structureGateProbe(
        mn, mx, camera, 346.0f,
        [](const glm::ivec3& wp) { return wp.y < 64; });
    EXPECT_TRUE(gate.ready)
        << "mid-plane-only probing hit the air chunk at y=150 and vetoed readiness "
           "even though the tower's base is resident and rendered";
    EXPECT_GT(gate.votes, 0);
}
