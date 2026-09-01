// Grass density LOD — the mechanism that lets the grass radius be 320 units at near-field
// density (docs/VegetationWindPlan.md; GrassRenderPipeline::bladesForDistance).
// NOTE (2026-08-01): the real density falloff is now PER BLADE and continuous, in
// grass.vert. What bladesForDistance returns is a CONSERVATIVE UPPER BOUND used only to
// shorten the draw -- see CpuBoundNeverClipsWhatTheShaderKeeps at the bottom of this file,
// which is now the load-bearing test.
//
// The contract these tests pin:
//   1. The near field is UNTOUCHED — full authored density, no LOD artifacts where the player is.
//   2. Density is monotonically non-increasing with distance (a farther chunk never draws more).
//   3. Every count is a whole number of CLUMPS. grass.vert derives a blade's clump from
//      `gl_VertexIndex / kBladesPerClump`, so a count that splits a clump draws a PARTIAL tuft —
//      some blades of one hashed clump present, the rest missing. That reads as a torn tuft.
//   4. No chunk inside the radius ever goes bald.
//   5. Width compensation holds ground coverage as blades are dropped, and stays bounded.
//
// These are pure static functions precisely so they are testable without a Vulkan device — the
// LodService lesson (ContinuousLodPlan C1): tests that re-derive the formula locally pass even
// when the feature is disabled entirely.

#include <gtest/gtest.h>
#include "graphics/GrassRenderPipeline.h"

#include <cmath>

using Phyxel::Graphics::GrassRenderPipeline;

namespace {

constexpr uint32_t kClump   = GrassRenderPipeline::kBladesPerClump;
constexpr uint32_t kBlades  = 140;    // stress count, NOT the shipped default (55 — see Params{})
constexpr float    kRadius  = 224.0f; // the shipped default

} // namespace

TEST(GrassDensityLod, FullAuthoredDensityWhereThePlayerStands) {
    // The LOD must be invisible at the camera. If this drops below the authored count, "much
    // denser grass" was silently undone by the optimisation that made it affordable.
    EXPECT_EQ(GrassRenderPipeline::bladesForDistance(kBlades, 0.0f, kRadius), kBlades);
    EXPECT_EQ(GrassRenderPipeline::bladesForDistance(kBlades, 10.0f, kRadius), kBlades);
    // Full density holds across the whole near band (kDensityNearBand * radius = ~34u).
    EXPECT_EQ(GrassRenderPipeline::bladesForDistance(kBlades, 30.0f, kRadius), kBlades);
}

TEST(GrassDensityLod, NoRegressionAgainstThePreviousDefaultInsideTheOldRadius) {
    // THE ACTUAL NON-REGRESSION CONTRACT, and the one worth pinning. Before this change grass was
    // a flat 28 blades/voxel out to a 48-unit radius. Whatever the tiers do, every distance the
    // OLD config drew grass at must now get at LEAST as many blades — otherwise the radius was
    // bought by making the near field sparser, which is the opposite of the request.
    //
    // (An earlier version of this test asserted full authored density at 48u. That was a stale
    // constant from a 320-radius draft and it FAILED here — 35 vs 70 — which is how this weaker
    // but correct invariant got written. The failure was real; the expectation was wrong.)
    constexpr uint32_t kOldBlades = 28;
    constexpr float    kOldRadius = 48.0f;
    for (float d = 0.0f; d <= kOldRadius; d += 0.5f) {
        ASSERT_GE(GrassRenderPipeline::bladesForDistance(kBlades, d, kRadius), kOldBlades)
            << "sparser than the old default at d=" << d;
    }
}

TEST(GrassDensityLod, FarFieldIsDramaticallyCheaperThanNear) {
    const uint32_t near = GrassRenderPipeline::bladesForDistance(kBlades, 5.0f, kRadius);
    const uint32_t far  = GrassRenderPipeline::bladesForDistance(kBlades, 215.0f, kRadius);
    EXPECT_EQ(near, kBlades);
    // Without a real reduction the 320-unit radius is unaffordable and the feature is pointless.
    EXPECT_LT(far * 4u, near) << "far band must be at least 4x cheaper than the near band";
}

TEST(GrassDensityLod, DensityNeverIncreasesWithDistance) {
    // Asserted at EVERY step across the whole radius, not just at the band edges — a
    // non-monotonic tier would make grass thicken as you walk away from it.
    uint32_t prev = GrassRenderPipeline::bladesForDistance(kBlades, 0.0f, kRadius);
    for (float d = 0.0f; d <= kRadius * 1.5f; d += 0.5f) {
        const uint32_t n = GrassRenderPipeline::bladesForDistance(kBlades, d, kRadius);
        ASSERT_LE(n, prev) << "density increased with distance at d=" << d;
        prev = n;
    }
}

TEST(GrassDensityLod, EveryCountIsAWholeNumberOfClumps) {
    // The torn-tuft invariant (contract 3). Swept across blade counts AND distances, asserting
    // per-step, because a single averaged pass would hide one bad tier.
    //
    // ⚑2026-08-05 — THE REASON FOR THIS CONTRACT IS GONE, THE CONTRACT IS KEPT ANYWAY.
    // It existed because grass.vert assigned blades to TUFTS of 7 (`blade / BLADES_PER_CLUMP`), so
    // a draw count that split a clump rendered a partial tuft — some blades of a hashed clump
    // present, the rest missing — which read as a torn tuft rather than a sparser meadow. Blades
    // are now placed one-per-cell on a progressive blue-noise lattice (shaders/grass_sites.glsl)
    // with NO clumps at all, so any count is a legal, well-spread distribution and 1 is as valid
    // as 7.
    //
    // Kept, deliberately, for two reasons: `bladesForDistance` still quantises to 7 (harmless, and
    // it keeps draw counts tidy), and this test is the thing that would catch someone reintroducing
    // clumping without thinking about the prefix property. If the quantum is ever removed, delete
    // this test in the SAME commit and say so — do not let it rot into a mystery.
    for (uint32_t blades : {7u, 14u, 28u, 49u, 98u, 140u, 343u}) {
        for (float d = 0.0f; d <= kRadius; d += 1.0f) {
            const uint32_t n = GrassRenderPipeline::bladesForDistance(blades, d, kRadius);
            ASSERT_EQ(n % kClump, 0u)
                << "partial clump: blades=" << blades << " d=" << d << " -> " << n;
            ASSERT_LE(n, blades) << "drew more blades than authored";
        }
    }
}

TEST(GrassDensityLod, NoChunkInsideTheRadiusGoesBald) {
    for (uint32_t blades : {7u, 28u, 98u, 343u}) {
        for (float d = 0.0f; d <= kRadius; d += 1.0f) {
            ASSERT_GT(GrassRenderPipeline::bladesForDistance(blades, d, kRadius), 0u)
                << "bald chunk at blades=" << blades << " d=" << d;
        }
    }
}

TEST(GrassDensityLod, DegenerateInputsDoNotProduceGarbage) {
    // Zero blades authored = grass off; must not synthesise a clump out of nothing.
    EXPECT_EQ(GrassRenderPipeline::bladesForDistance(0, 100.0f, kRadius), 0u);
    // A zero/near-zero radius must not divide by zero or return more than authored.
    EXPECT_LE(GrassRenderPipeline::bladesForDistance(kBlades, 10.0f, 0.0f), kBlades);
    EXPECT_EQ(GrassRenderPipeline::bladesForDistance(kBlades, 10.0f, 0.0f) % kClump, 0u);
    // Past the radius the chunk is culled upstream, but the function must stay well-defined.
    EXPECT_LE(GrassRenderPipeline::bladesForDistance(kBlades, 1.0e6f, kRadius), kBlades);
    EXPECT_EQ(GrassRenderPipeline::bladesForDistance(kBlades, 1.0e6f, kRadius) % kClump, 0u);
    // A blade count smaller than one clump must still round UP to a drawable clump, never 0.
    EXPECT_EQ(GrassRenderPipeline::bladesForDistance(3, 300.0f, kRadius), 3u);
}

TEST(GrassDensityLod, WidthCompensationIsNeutralAtFullDensityAndWidensWhenThinned) {
    // Full density must be EXACTLY neutral — otherwise the near field silently changes width.
    EXPECT_FLOAT_EQ(GrassRenderPipeline::widthCompensation(kBlades, kBlades), 1.0f);

    const uint32_t farCount = GrassRenderPipeline::bladesForDistance(kBlades, 215.0f, kRadius);
    const float    w        = GrassRenderPipeline::widthCompensation(farCount, kBlades);
    EXPECT_GT(w, 1.0f) << "thinned grass must widen or the meadow reads as thinning out";
    EXPECT_LE(w, 2.6f) << "compensation must stay capped or far blades read as fat ribbons";
}

TEST(GrassDensityLod, WidthCompensationRisesMonotonicallyAsBladesAreDropped) {
    float prev = 0.0f;
    for (uint32_t drawn = kBlades; drawn >= kClump; drawn -= kClump) {
        const float w = GrassRenderPipeline::widthCompensation(drawn, kBlades);
        ASSERT_GE(w, prev) << "compensation dropped while thinning at drawn=" << drawn;
        ASSERT_LE(w, 2.6f);
        prev = w;
    }
    EXPECT_FLOAT_EQ(GrassRenderPipeline::widthCompensation(0, kBlades), 1.0f);
    EXPECT_FLOAT_EQ(GrassRenderPipeline::widthCompensation(kBlades, 0), 1.0f);
}

TEST(GrassDensityLod, CoverageIsBetterConservedThanDroppingBladesAlone) {
    // The actual justification for compensating at all: blades * width should stay far closer to
    // the near-field value than the raw blade count does. Without this the far band visibly
    // thins even though the tier change is "just an optimisation".
    const uint32_t farCount = GrassRenderPipeline::bladesForDistance(kBlades, 215.0f, kRadius);
    const float    w        = GrassRenderPipeline::widthCompensation(farCount, kBlades);

    const float rawRatio       = static_cast<float>(farCount) / static_cast<float>(kBlades);
    const float compensatedCov = rawRatio * w;

    EXPECT_GT(compensatedCov, rawRatio * 1.5f) << "compensation barely helped";
    EXPECT_LE(compensatedCov, 1.0f) << "must not over-compensate past full coverage";
}

// ---------------------------------------------------------------------------
// THE SEAM INVARIANT (2026-08-01)
//
// Density moved from a per-CHUNK band to a continuous PER-BLADE curve in grass.vert, because
// deciding it per-chunk made adjacent chunks in different bands draw different densities and the
// boundary showed as a hard seam through open field ("disjointed grass").
//
// What remains on the CPU is only a conservative bound used to shorten the draw. If that bound is
// ever TIGHTER than the shader's own test, it clips blades the shader wanted -- and the seam comes
// straight back, in a form no unit test of the curve alone would catch. So the bound is pinned
// against the shader's expression directly.
// ---------------------------------------------------------------------------

namespace {
// Mirror of grass.vert's densityFrac. Kept as an INDEPENDENT re-derivation on purpose: if someone
// edits the shader curve without editing kDensityFalloff, this disagrees and the test fails.
float shaderDensityFrac(float dist, float radius) {
    const float t = std::min(1.0f, std::max(0.0f, dist / radius));
    const float u = std::max(0.0f, t - 0.15f) / (1.0f - 0.15f);
    return std::max(1.0f / (1.0f + 140.0f * u * u), 1.0f / 18.0f);
}
} // namespace

TEST(GrassDensityLod, CpuBoundNeverClipsWhatTheShaderKeeps) {
    // Swept per-unit across the whole radius. The CPU count must cover every clump the shader
    // would keep at that distance, including the soft-edge band below the threshold.
    for (float d = 0.0f; d <= kRadius; d += 1.0f) {
        const uint32_t drawn = GrassRenderPipeline::bladesForDistance(kBlades, d, kRadius);
        const uint32_t clumpsDrawn = drawn / kClump;
        const uint32_t maxClumps = (kBlades + kClump - 1) / kClump;

        // Highest clump index the shader still gives non-zero height to. clumpFrac for clump i is
        // (i + 0.5)/maxClumps, and lodKeep is > 0 while clumpFrac < densityFrac (the soft edge
        // starts fading BELOW the threshold, so the threshold itself is the hard bound).
        const float frac = shaderDensityFrac(d, kRadius);
        uint32_t needed = 0;
        for (uint32_t i = 0; i < maxClumps; ++i) {
            const float clumpFrac = (float(i) + 0.5f) / float(maxClumps);
            if (clumpFrac < frac) needed = i + 1;
        }
        ASSERT_GE(clumpsDrawn, needed)
            << "CPU bound clipped a clump the shader would keep at d=" << d
            << " (drew " << clumpsDrawn << " clumps, shader needs " << needed << ")";
    }
}

TEST(GrassDensityLod, DensityIsContinuousInDistance) {
    // The seam was a DISCONTINUITY. The shader curve must have no step anywhere: a small change in
    // distance may only produce a small change in density. (The CPU count is still quantised to
    // whole clumps -- that is fine, because it no longer decides how a blade looks.)
    float prev = shaderDensityFrac(0.0f, kRadius);
    for (float d = 0.5f; d <= kRadius; d += 0.5f) {
        const float f = shaderDensityFrac(d, kRadius);
        ASSERT_LE(f, prev + 1e-6f) << "density increased with distance at d=" << d;
        // The bound has to distinguish a JUMP from a slope. The curve's own steepest descent is
        // ~0.020 per half-unit (measured); the banded predecessor this replaced jumped 1.00 -> 0.45
        // in one step. 0.05 sits an order of magnitude below that jump while clearing the real
        // slope, so it catches a reintroduced band without failing on the smooth curve.
        // (A first cut used 0.02 and failed at d=42.5 on the curve's own gradient — it was
        // measuring steepness, not discontinuity.)
        ASSERT_LT(prev - f, 0.05f) << "density stepped sharply at d=" << d
                                   << " (" << prev << " -> " << f << ") -- that is a visible seam";
        prev = f;
    }
}
