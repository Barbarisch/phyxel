// FarTreeResidencyMaskTest.cpp -- the far-tree residency hold is per CARD (G-178).
//
// Ravenmere, 2026-09-23 (rv_horizon_l4_after.png): pale impostor discs floated among the real
// voxel oaks 30-80 u from the camera. The hold that keeps a card solid until its real chunk
// exists was decided per TILE by four quadrant probes, and a probe in the annulus between the
// load radius (256 u) and the end of the fade band (346 u) can never become resident, so the
// whole tile's cards stayed solid - including the ones over fully resident forest next to the
// camera. The shader now looks each card's own chunk column up in a push-constant bit mask.
// These tests pin the mask: its grid, its bit layout (which the GLSL mirrors), and the rule.

#include <gtest/gtest.h>

#include "graphics/FarTreeResidencyMask.h"

using Phyxel::Graphics::FarTreeResidencyMask;

// The GLSL side of the lookup, transcribed: phxResidencyHold() in far_tree.vert.
static float glslHold(const FarTreeResidencyMask& m, float worldX, float worldZ, float fallback) {
    if (!m.enabled) return fallback;
    const int cx = int(std::floor(worldX / 32.0f)) - m.originChunk.x;
    const int cz = int(std::floor(worldZ / 32.0f)) - m.originChunk.y;
    const int dim = FarTreeResidencyMask::kDim;
    if (cx < 0 || cz < 0 || cx >= dim || cz >= dim) return fallback;
    const int b = cz * dim + cx;
    return ((m.bits[b >> 5] >> (b & 31)) & 1u) != 0u ? 0.0f : 1.0f;
}

TEST(FarTreeResidencyMask, TheGridIsCentredOnTheCameraColumnAndCoversTheFadeBand) {
    const auto m = FarTreeResidencyMask::fromColumns({0, 0}, {});
    EXPECT_EQ(m.originChunk, glm::ivec2(-11, -11));
    EXPECT_TRUE(m.inGrid(-11, -11));
    EXPECT_TRUE(m.inGrid(10, 10));
    EXPECT_FALSE(m.inGrid(11, 0)) << "22 columns: -11..10";
    EXPECT_FALSE(m.inGrid(0, -12));
    // +-11 chunks = +-352 u: the near fade band ends at load 256 + 90 = 346 u, inside the grid.
    EXPECT_GE(11 * 32, 346);
}

TEST(FarTreeResidencyMask, ARenderedColumnIsResidentAndOthersAreNot) {
    const auto m = FarTreeResidencyMask::fromColumns({0, 0}, {{0, -1}, {3, 2}});
    EXPECT_TRUE(m.resident(0, -1));
    EXPECT_TRUE(m.resident(3, 2));
    EXPECT_FALSE(m.resident(0, 0));
    EXPECT_FALSE(m.resident(-1, -1));
    EXPECT_EQ(m.residentCount(), 2);
    // a column outside the grid is dropped, not wrapped into a neighbour's bit
    const auto far = FarTreeResidencyMask::fromColumns({0, 0}, {{40, 0}, {-12, 0}});
    EXPECT_EQ(far.residentCount(), 0);
}

TEST(FarTreeResidencyMask, TheShaderRuleHoldsACardOverAnUnrenderedColumnAndFreesOneOverARenderedColumn) {
    // The farm's south edge: the region's chunk (0,0,-1) renders; the column past the wall
    // (0,0,-2) has not landed yet.
    const auto m = FarTreeResidencyMask::fromColumns({0, -1}, {{0, -1}});
    EXPECT_FLOAT_EQ(glslHold(m, 6.0f, -30.0f, 0.5f), 0.0f) << "over a rendered column: distance decides";
    EXPECT_FLOAT_EQ(glslHold(m, 6.0f, -40.0f, 0.5f), 1.0f) << "over an unrendered column: solid";
    // negative coordinates floor the right way: x = -0.5 is column -1
    const auto n = FarTreeResidencyMask::fromColumns({0, 0}, {{-1, 0}});
    EXPECT_FLOAT_EQ(glslHold(n, -0.5f, 0.5f, 0.5f), 0.0f);
    EXPECT_FLOAT_EQ(glslHold(n, 0.5f, 0.5f, 0.5f), 1.0f);
}

TEST(FarTreeResidencyMask, OutsideTheGridOrDisabledTheCallerValueApplies) {
    const auto m = FarTreeResidencyMask::fromColumns({0, 0}, {{0, 0}});
    EXPECT_FLOAT_EQ(glslHold(m, 2000.0f, 0.0f, 0.37f), 0.37f) << "beyond +-352 u the per-tile value stands";
    FarTreeResidencyMask off;   // enabled == false
    EXPECT_FLOAT_EQ(glslHold(off, 0.0f, 0.0f, 0.37f), 0.37f);
}

TEST(FarTreeResidencyMask, BuildingIsOrderIndependent) {
    const auto a = FarTreeResidencyMask::fromColumns({5, -3}, {{1, 1}, {7, -9}, {-6, 4}});
    const auto b = FarTreeResidencyMask::fromColumns({5, -3}, {{-6, 4}, {1, 1}, {7, -9}, {1, 1}});
    EXPECT_EQ(a.bits, b.bits) << "a set of bits: order and repeats do not matter";
}
