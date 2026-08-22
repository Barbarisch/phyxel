// Grass height must be CONTINUOUS across voxel and chunk borders — the user-visible defect this
// pins: "I can see voxel/chunk boundaries from grass height alone."
//
// THE CONDITION. Blade height is bladeHeight * meadowHeightMul * (per-blade jitter). The meadow
// field is meant to be a smooth function of WORLD POSITION, so two blades whose roots sit a few
// millimetres apart — even either side of a voxel, chunk, or hash-wrap border — must get nearly
// the same height multiplier. Measured on the production mirror
// (GrassRenderPipeline::meadowHeightMulAt), not a re-derived formula.
//
// RED-BEFORE-GREEN. Against the corner-sampled meadow (grass.vert samples the field at the VOXEL
// corner, so every blade in a voxel shares one value) these tests FAIL: adjacent voxels step the
// height by up to ~0.07x per border (piecewise-constant plateaus), and the 2048-unit hash-domain
// wrap — which passes through the WORLD ORIGIN (mod pulls voxel -1 to 2047) — jumps by O(0.5x).
// They go green when the field is sampled at the blade ROOT through a lattice-periodic noise.
//
// The tolerance is a slope bound: over a gap of d units the field may move at most kMaxSlope*d.
// kMaxSlope 0.5/unit is ~7x the analytic worst case at the shipped meadow periods, so the test
// constrains continuity, not tuning.

#include <gtest/gtest.h>

#include "graphics/GrassRenderPipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

using Phyxel::Graphics::GrassRenderPipeline;

namespace {

constexpr float kGap      = 0.002f;   // world units between the two sample roots
constexpr float kMaxSlope = 0.5f;     // height-mul change allowed per world unit
constexpr float kTol      = kMaxSlope * kGap + 1e-4f;

// Deterministic LCG so failures reproduce exactly.
uint32_t lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }
int span(uint32_t& s, int lo, int hi) { return lo + static_cast<int>(lcg(s) % static_cast<uint32_t>(hi - lo)); }

struct WorstPair {
    float delta = 0.0f;
    int   cx = 0, cz = 0;
    bool  xBorder = true;
};

/// Height-mul delta between two roots kGap apart straddling the border between
/// (cx,cz) and its +x or +z neighbour.
float borderDelta(int cx, int cz, bool xBorder, const GrassRenderPipeline::Params& p) {
    const float lo = 1.0f - kGap * 0.5f, hi = kGap * 0.5f;
    const glm::vec2 nearSide = xBorder ? glm::vec2{lo, 0.5f} : glm::vec2{0.5f, lo};
    const glm::vec2 farSide  = xBorder ? glm::vec2{hi, 0.5f} : glm::vec2{0.5f, hi};
    const int nx = cx + (xBorder ? 1 : 0);
    const int nz = cz + (xBorder ? 0 : 1);
    const float a = GrassRenderPipeline::meadowHeightMulAt(cx, cz, nearSide, p);
    const float b = GrassRenderPipeline::meadowHeightMulAt(nx, nz, farSide, p);
    return std::fabs(a - b);
}

} // namespace

TEST(GrassMeadowSeamTest, HeightIsContinuousAcrossVoxelAndChunkBorders) {
    const GrassRenderPipeline::Params p{};
    uint32_t seed = 0xC0FFEEu;
    WorstPair worst;

    // Random borders across a ±3000-voxel world span (crosses many chunk borders too)...
    for (int i = 0; i < 4000; ++i) {
        const int cx = span(seed, -3000, 3000);
        const int cz = span(seed, -3000, 3000);
        const bool xB = (lcg(seed) & 1u) != 0u;
        const float d = borderDelta(cx, cz, xB, p);
        if (d > worst.delta) worst = {d, cx, cz, xB};
    }
    // ...plus explicit CHUNK borders (multiples of 32), the case the user actually sees.
    for (int i = 0; i < 500; ++i) {
        const int cx = span(seed, -90, 90) * 32 - 1;   // voxel just left of a chunk border
        const int cz = span(seed, -3000, 3000);
        const float d = borderDelta(cx, cz, true, p);
        if (d > worst.delta) worst = {d, cx, cz, true};
    }

    EXPECT_LE(worst.delta, kTol)
        << "grass height steps by " << worst.delta << "x across the border at voxel ("
        << worst.cx << "," << worst.cz << ") " << (worst.xBorder ? "+x" : "+z")
        << " — roots only " << kGap << " u apart. The voxel/chunk grid is visible in grass height.";
}

TEST(GrassMeadowSeamTest, HeightIsContinuousAcrossTheHashDomainWrap) {
    const GrassRenderPipeline::Params p{};
    WorstPair worst;

    // The hash domain wraps every 2048 units AND at the world origin (floor-mod pulls voxel -1 to
    // 2047). Walk both wrap lines: borders (-1|0) and (2047|2048), across many z.
    for (const int cx : {-1, 2047, -2049, 4095}) {
        for (int cz = -256; cz <= 256; cz += 7) {
            const float d = borderDelta(cx, cz, true, p);
            if (d > worst.delta) worst = {d, cx, cz, true};
        }
        for (int cz = -256; cz <= 256; cz += 7) {   // same lines as z-borders
            const float d = borderDelta(cz, cx, false, p);
            if (d > worst.delta) worst = {d, cz, cx, false};
        }
    }

    EXPECT_LE(worst.delta, kTol)
        << "grass height jumps by " << worst.delta << "x across the hash-domain wrap at voxel ("
        << worst.cx << "," << worst.cz << ") — a visible seam line through the meadow"
        << " (the wrap passes through the world origin).";
}
