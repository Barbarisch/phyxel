// WaterCore 31 (M4): the water's push on a body = the pressure integral over its oriented box (docs/WaterCore.md 31.2-31.3).
// The function under test is the shader's own (shaders/water_body_force.glsl through core/water/WaterBodyForce.h), on analytic
// water: still water below a level, a cap of water over a block with air under it, a sloped surface.
#include <gtest/gtest.h>
#include <cmath>
#include <random>
#include <glm/gtc/quaternion.hpp>
#include "core/water/WaterBodyForce.h"

using namespace Phyxel::Core::Water;
using glm::vec2; using glm::vec3; using glm::vec4;

namespace {
constexpr float kG = 9.81f;
constexpr float kH = 1.0f / 9.0f;
const vec3 X(1, 0, 0), Y(0, 1, 0), Z(0, 0, 1);

// still water below `level`: p = g (level - y) at the point itself
std::function<vec2(vec3)> still(float level) {
    return [level](vec3 x) { return vec2(x.y < level ? kG * (level - x.y) : 0.0f, x.y); };
}
// the table law the debris used (solver_integrate.comp 6c): f = the vertical extent below the column's top surface
float tableF(float columnTop, float cy, float a) { return std::clamp((columnTop - (cy - a)) / (2.0f * a), 0.0f, 1.0f); }
}

TEST(WaterBodyForce, AnUnrotatedFloaterIsTheTableLaw) {
    // a 1/3 m block at drafts 0..1: a_y = g f exactly (the equilibrium draft stays 1 / b)
    const float a = 1.0f / 6.0f; const vec3 c(0.37f, 16.4f, -2.11f);
    for (int i = 0; i <= 10; ++i) {
        const float f = 0.1f * i, level = c.y - a + 2.0f * a * f;
        const vec4 r = BodyForce::accel(still(level), c, vec3(a), X, Y, Z, kH, kG);
        EXPECT_NEAR(r.y, kG * f, 1e-4f) << "draft " << f;
        EXPECT_NEAR(r.x, 0.0f, 1e-5f); EXPECT_NEAR(r.z, 0.0f, 1e-5f);
        EXPECT_NEAR(r.w, f, 1e-5f);
    }
}

TEST(WaterBodyForce, ATiltedBlockFeelsTheWaterItDisplaces) {
    // Archimedes at any orientation: a_y = g V_sub / V (V_sub by Monte Carlo), no net sideways push in still water
    std::mt19937 rng(31); std::uniform_real_distribution<float> u(-1.0f, 1.0f), u01(0.0f, 1.0f);
    const vec3 he(1.0f / 6.0f, 1.0f / 6.0f, 1.0f / 18.0f);   // a plank-ish block: the faces are not alike
    float worst = 0.0f;
    for (int t = 0; t < 60; ++t) {
        const glm::quat q = glm::normalize(glm::quat(u(rng), u(rng), u(rng), u(rng)));
        const vec3 a0 = q * X, a1 = q * Y, a2 = q * Z;
        const vec3 c(u(rng), 16.0f + u(rng), u(rng));
        const float ext = std::abs(a0.y) * he.x + std::abs(a1.y) * he.y + std::abs(a2.y) * he.z;
        const float level = c.y - ext + 2.0f * ext * u01(rng);
        int in = 0; const int N = 200000;
        for (int k = 0; k < N; ++k) {
            const vec3 x = c + a0 * (he.x * u(rng)) + a1 * (he.y * u(rng)) + a2 * (he.z * u(rng));
            in += x.y < level;
        }
        const float sub = float(in) / N;
        const vec4 r = BodyForce::accel(still(level), c, he, a0, a1, a2, kH, kG);
        worst = std::max(worst, std::abs(r.y / kG - sub));
        EXPECT_NEAR(r.y / kG, sub, 0.01f) << "trial " << t;
        EXPECT_LT(std::hypot(r.x, r.z), 0.002f * kG) << "trial " << t;
    }
    std::printf("[ worst |a_y/g - V_sub/V| over 60 orientations: %.4f ]\n", worst);
}

TEST(WaterBodyForce, AWaterCapOverABlockPushesItDown) {
    // 31.1: a rising block's displaced water landed on top of it - a cap 0.3 m deep over its top face, air under and beside it.
    // The table law read the column's top (the cap's) as the waterline: f = 1, +g b - the 3 m launch. The pressure pushes DOWN.
    const float a = 1.0f / 6.0f; const vec3 c(0.0f, 18.0f, 0.0f);
    const float top = c.y + a, capTop = top + 0.3f;
    auto cap = [&](vec3 x) {
        const bool over = std::abs(x.x) <= a && std::abs(x.z) <= a && x.y >= top && x.y < capTop;
        return vec2(over ? kG * (capTop - x.y) : 0.0f, x.y);
    };
    const vec4 r = BodyForce::accel(cap, c, vec3(a), X, Y, Z, kH, kG);
    EXPECT_GT(tableF(capTop, c.y, a), 0.99f) << "the control: the table law counts the block as submerged";
    EXPECT_LT(r.y, 0.0f) << "the cap's weight presses the block down";
    EXPECT_NEAR(r.y, -kG * 0.3f / (2.0f * a), 1e-3f);   // p on the top face = g 0.3, over the block's height
    EXPECT_EQ(r.w, 0.0f) << "nothing under it is water: no drag or current from the cap";
}

TEST(WaterBodyForce, TheFallbackNeverReadsTheBodysOwnCap) {
    // 33 (A2g): a body holding a window whose sample was unknown falls back to the table law. Under its own pumped cap the column's
    // top is the cap's (f = 1, the launch); the fallback's waterline is the ring's surface round it - here the pond at 16.5 with the
    // block's centre at 16.6 (a 1/3 m block, bottom 16.433): the true f = 0.2. No water in the ring (NaN): f = 0.
    const float a = 1.0f / 6.0f, cy = 16.6f, ring = 16.5f, capTop = cy + a + 0.3f;
    EXPECT_GT(tableF(capTop, cy, a), 0.99f) << "the control: the raw table law under the cap counts the block as submerged";
    EXPECT_NEAR(tableF(BodyForce::wbfFallbackSurface(capTop, ring), cy, a), (ring - (cy - a)) / (2.0f * a), 1e-5f);
    EXPECT_NEAR(tableF(BodyForce::wbfFallbackSurface(16.45f, ring), cy, a), (16.45f - (cy - a)) / (2.0f * a), 1e-5f)
        << "a column lower than the ring keeps its own surface (a trough beside the body)";
    EXPECT_EQ(tableF(BodyForce::wbfFallbackSurface(capTop, std::numeric_limits<float>::quiet_NaN()), cy, a), 0.0f)
        << "no water round the body: no buoyancy from the table law";
}

TEST(WaterBodyForce, FullySubmergedIsArchimedesAtAnyDepth) {
    const float a = 1.0f / 6.0f;
    for (float depth : {0.5f, 2.0f, 10.0f}) {
        const vec3 c(1.0f, 16.0f - depth, 1.0f);
        const vec4 r = BodyForce::accel(still(16.0f), c, vec3(a), X, Y, Z, kH, kG);
        EXPECT_NEAR(r.y, kG, 1e-3f * depth) << "depth " << depth;
        EXPECT_NEAR(r.w, 1.0f, 1e-4f);
    }
}

TEST(WaterBodyForce, ASlopedSurfacePushesDownTheSlope) {
    // a surface rising along +x (y = 16 + 0.1 x): the pressure gradient pushes a submerged block toward -x at g 0.1
    auto slope = [](vec3 x) { const float lv = 16.0f + 0.1f * x.x; return vec2(x.y < lv ? kG * (lv - x.y) : 0.0f, x.y); };
    const vec4 r = BodyForce::accel(slope, vec3(0.0f, 15.0f, 0.0f), vec3(1.0f / 6.0f), X, Y, Z, kH, kG);
    EXPECT_NEAR(r.x, -0.1f * kG, 1e-3f);
    EXPECT_NEAR(r.y, kG, 1e-3f);
}

TEST(WaterBodyForce, AirPushesNothingAndUnknownWaterSaysSo) {
    auto air = [](vec3 x) { return vec2(0.0f, x.y); };
    const vec4 r = BodyForce::accel(air, vec3(0.0f, 20.0f, 0.0f), vec3(0.5f), X, Y, Z, kH, kG);
    EXPECT_EQ(r, vec4(0.0f));
    auto unknown = [](vec3 x) { return vec2(x.x > 0.4f ? NAN : 0.0f, x.y); };
    EXPECT_EQ(BodyForce::accel(unknown, vec3(0.0f), vec3(0.5f), X, Y, Z, kH, kG).w, -1.0f) << "the caller falls back";
}

TEST(WaterBodyForce, ASampledFieldUsesTheCellCentresHydrostatically) {
    // the field the debris reads is per CELL (the pressure windows): p at the cell centre, the step to the face hydrostatic -
    // still exact for still water, at a block not aligned to the cells
    const float level = 16.5f;
    auto cells = [&](vec3 x) {
        const float yc = (std::floor(x.y / kH) + 0.5f) * kH;
        return vec2(yc < level ? kG * (level - yc) : 0.0f, yc);
    };
    const float a = 1.0f / 6.0f;
    for (float cy : {16.43f, 16.401f, 16.47f}) {
        const vec4 r = BodyForce::accel(cells, vec3(102.013f, cy, 14.07f), vec3(a), X, Y, Z, kH, kG);
        const float f = (level - (cy - a)) / (2.0f * a);
        EXPECT_NEAR(r.y, kG * f, 0.02f * kG) << "centre " << cy;
    }
}

TEST(WaterBodyForce, APumpSpikeIsCappedAtTheRingsLevel) {
    // 31.7: a body the volume rasterizes is a pump - the pressure that sheds the water it overlaps implied a level metres above the
    // pond (+6 m here). Capped at the highest real surface round the body, the push is the still pond's buoyancy; uncapped, the block
    // is shot out of the water. And the cap removes the water standing on top of a body, not just its lift.
    const float level = 16.5f, a = 1.0f / 6.0f; const vec3 c(0.0f, 16.43f, 0.0f);
    auto spiked = [&](vec3 x) { return vec2(x.y < level ? kG * (level + 6.0f - x.y) : 0.0f, x.y); };
    const vec4 raw = BodyForce::accel(spiked, c, vec3(a), X, Y, Z, kH, kG);
    const vec4 capped = BodyForce::accel(spiked, c, vec3(a), X, Y, Z, kH, kG, level);
    const float f = (level - (c.y - a)) / (2.0f * a);
    EXPECT_GT(raw.y, 2.0f * kG) << "the control: the spike alone throws the block out";
    EXPECT_NEAR(capped.y, kG * f, 1e-3f);
    const vec3 c2(0.0f, 18.0f, 0.0f);   // a block 1.5 m above the pond with its own water on top of it
    const float top = c2.y + a, capTop = top + 0.3f;
    auto cap = [&](vec3 x) {
        const bool over = std::abs(x.x) <= a && std::abs(x.z) <= a && x.y >= top && x.y < capTop;
        return vec2(over ? kG * (capTop - x.y) : 0.0f, x.y);
    };
    EXPECT_LT(BodyForce::accel(cap, c2, vec3(a), X, Y, Z, kH, kG).y, 0.0f) << "the control: uncapped, the cap presses";
    EXPECT_EQ(BodyForce::accel(cap, c2, vec3(a), X, Y, Z, kH, kG, level).y, 0.0f) << "a cap above the ring's level pushes nothing";
}

TEST(WaterBodyForce, ABodysOwnRasterIsLookedPast) {
    // 31.7: the volume rasterizes a moving body where it is PREDICTED to be - on entry ahead of it, so the cells under its bottom face
    // read "inside a body". The force steps outward (up to three cells) to the water: a 1 m block fell to the floor reading nothing.
    const float level = 16.5f, a = 0.5f; const vec3 c(0.0f, 16.3f, 0.0f);
    const float rasterLo = c.y - a - 0.15f;   // the raster ran 0.15 m ahead (below) of the body
    auto ahead = [&](vec3 x) {
        if (std::abs(x.x) <= a && std::abs(x.z) <= a && x.y >= rasterLo && x.y <= c.y + a) return vec2(-2.0e29f, x.y);
        return vec2(x.y < level ? kG * (level - x.y) : 0.0f, x.y);
    };
    const float f = (level - (c.y - a)) / (2.0f * a);
    EXPECT_NEAR(BodyForce::accel(ahead, c, vec3(a), X, Y, Z, kH, kG, level).y, kG * f, 0.01f * kG);
}

TEST(WaterBodyForce, ABlockOnThePondFloorIsLiftedByTheWaterUnderIt) {
    // 31.7: a wood block lying flat on the floor of a 1.5 m pond. With no water to read under its bottom face (solid ground) a perfect
    // seal held it down - the water above pressing, nothing under. The water's level seeps in under it: lifted at g (fully submerged).
    const float level = 16.5f, a = 0.5f; const vec3 c(0.0f, 15.495f, 0.0f);   // the floor's top at 15; resting 5 mm into it (a contact's penetration)
    auto pond = [&](vec3 x) {
        if (x.y < 15.0f) return vec2(-3.0e29f, x.y);   // the floor (the window's solid marker)
        return vec2(x.y < level ? kG * (level - x.y) : 0.0f, x.y);
    };
    EXPECT_NEAR(BodyForce::accel(pond, c, vec3(a), X, Y, Z, kH, kG, level).y, kG, 0.01f * kG);
    EXPECT_LT(BodyForce::accel(pond, c, vec3(a), X, Y, Z, kH, kG).y, 0.0f) << "the control: no level to seep in, the seal presses it down";
}
