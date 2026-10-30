#include <gtest/gtest.h>

#include "core/water/WaterRestMotion.h"
#include "core/water/WaterCoreManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

// WaterCore A12 (docs/WaterCore.md 45): the rest motion - water at rest looks alive. These hold the C++ mirror of the shader's
// restSlope (water_surface.frag); the screen is held to the mirror live (debug 10 / 11).

namespace Phyxel::Core::Water {
namespace {

using RM = WaterRestMotion;

RM::Params moonshot() { RM::Params p; p.ampM = 1.0e-3f; return p; }   // 1 mm, the default breeze

// The defaults are a pinned contract (45.1 item 4): both A12 halves ship OFF until the owner has seen them.
TEST(WaterRestMotionTest, DefaultsAreOff) {
    EXPECT_EQ(RM::kAmpMmDefault, 0.0f) << "the rest motion waits for the owner's clip";
    EXPECT_EQ(RM::Params{}.ampM, 0.0f);
    EXPECT_EQ(WaterCoreManager::RippleHandover::kHeightMmDefault, 0.0f) << "the hand-over waits for L5 and the owner";
    EXPECT_EQ(WaterCoreManager::RippleHandover{}.rate(), 0.0f);
    EXPECT_EQ(RM::height(RM::Params{}, 3.3f, 7.1f, 12.0f), 0.0f);
}

// Faceted on the WORLD 1/9 m lattice: every point of a square has the square's slope; neighbouring squares differ.
// Control: the height itself (not faceted) varies inside a square - so the test sees a pattern, not a constant.
TEST(WaterRestMotionTest, TheSlopeIsOneFacetPerNinthOfAMetre) {
    const RM::Params p = moonshot();
    const float t = 20.0f;
    int squares = 0, differ = 0, heightVaries = 0, glassy = 0;
    for (int iz = 0; iz < 40; ++iz) for (int ix = 0; ix < 40; ++ix) {
        const glm::vec2 s = RM::slope(p, ix, iz, t);
        // inside a glassy patch (the envelope 0 around the square) the rest motion is exactly 0 - neighbours agree by design;
        // the facet test counts the rippled squares (the first run counted all: 490 of 1,600 differed, the rest were glassy)
        if (RM::envelope(p, (ix - 1.5f) * RM::kPitch, (iz + 0.5f) * RM::kPitch, t) <= 0.0f || RM::envelope(p, (ix + 2.5f) * RM::kPitch, (iz + 0.5f) * RM::kPitch, t) <= 0.0f) {
            ++glassy;
            continue;
        }
        for (float fx : {0.01f, 0.5f, 0.99f}) for (float fz : {0.02f, 0.6f, 0.97f}) {
            const glm::ivec2 q = RM::square((ix + fx) * RM::kPitch, (iz + fz) * RM::kPitch);
            ASSERT_EQ(q, glm::ivec2(ix, iz));
            ASSERT_EQ(RM::slope(p, q.x, q.y, t), s) << "one slope per square";
        }
        const glm::vec2 n = RM::slope(p, ix + 1, iz, t);
        if (n != s) ++differ;
        if (RM::height(p, (ix + 0.1f) * RM::kPitch, (iz + 0.5f) * RM::kPitch, t) != RM::height(p, (ix + 0.9f) * RM::kPitch, (iz + 0.5f) * RM::kPitch, t)) ++heightVaries;
        ++squares;
    }
    std::printf("  %d rippled squares (%d glassy skipped): %d differ from their +x neighbour; the height varies inside %d\n", squares, glassy, differ, heightVaries);
    ASSERT_GT(squares, 100) << "the window must hold a rippled patch";
    EXPECT_GT(differ, squares * 9 / 10);
    EXPECT_GT(heightVaries, squares / 2) << "control: the field itself is not constant inside a square";
}

// Off and calm are still: amplitude 0 and wind 0 give exactly zero everywhere (the shader's off path is the pre-A12 normal).
TEST(WaterRestMotionTest, OffAndCalmAreStill) {
    RM::Params off = moonshot(); off.ampM = 0.0f;
    RM::Params calm = moonshot(); calm.windMps = 0.0f;
    for (int i = 0; i < 200; ++i) {
        const float x = 0.37f * i, z = -0.53f * i, t = 0.25f * i;
        EXPECT_EQ(RM::height(off, x, z, t), 0.0f);
        EXPECT_EQ(RM::height(calm, x, z, t), 0.0f);
    }
}

// Chunk-blind (45.1 item 2): a pond's squares evaluated as one pass equal the same squares evaluated as two ponds split at the
// chunk seam x = 32 m (square 288) - and anywhere in the world (negative coordinates included), only the square index matters.
TEST(WaterRestMotionTest, SameSquareSameSlopeWhicheverPondDrawsIt) {
    const RM::Params p = moonshot();
    const float t = 41.5f;
    std::vector<glm::vec2> whole, west, east;
    for (int iz = 100; iz < 110; ++iz) for (int ix = 270; ix < 306; ++ix) whole.push_back(RM::slope(p, ix, iz, t));
    for (int iz = 100; iz < 110; ++iz) {
        for (int ix = 270; ix < 288; ++ix) west.push_back(RM::slope(p, ix, iz, t));
        for (int ix = 288; ix < 306; ++ix) east.push_back(RM::slope(p, ix, iz, t));
    }
    size_t w = 0, e = 0, k = 0, mism = 0;
    for (int iz = 100; iz < 110; ++iz) for (int ix = 270; ix < 306; ++ix, ++k)
        if (whole[k] != (ix < 288 ? west[w++] : east[e++])) ++mism;
    EXPECT_EQ(mism, 0u);
    // across the seam the field is continuous: the slope step at x = 288 is no larger than the steps either side of it
    float seam = 0.0f, inside = 0.0f;
    for (int iz = 0; iz < 200; ++iz) {
        seam = std::max(seam, glm::length(RM::slope(p, 288, iz, t) - RM::slope(p, 287, iz, t)));
        inside = std::max(inside, glm::length(RM::slope(p, 280, iz, t) - RM::slope(p, 279, iz, t)));
    }
    std::printf("  largest facet step at the seam %.4f, inside a chunk %.4f\n", seam, inside);
    EXPECT_LT(seam, 2.0f * inside + 1e-6f);
    // negative world coordinates: the hash wraps, the field is defined (finite, bounded)
    for (int i = -500; i < 0; i += 7) { const glm::vec2 s = RM::slope(p, i, -i * 3, t); EXPECT_TRUE(std::isfinite(s.x) && std::isfinite(s.y)); }
}

// The breeze's patches come and go: at any time some of a 30 x 30 m pond is glassy and some is rippled, and a patch changes within
// a few kEvolveS. Bounded: no facet steeper than the amplitude allows (sum of share x 2 pi / lambda x amp).
TEST(WaterRestMotionTest, PatchesComeAndGoAndTheSlopeIsBounded) {
    const RM::Params p = moonshot();
    float bound = 0.0f;
    for (int k = 0; k < RM::kComponents; ++k) bound += RM::kShare[k] * 6.2831853f / RM::kLambda[k];
    bound *= p.ampM;
    float steepest = 0.0f, minLit = 1.0f, maxLit = 0.0f, changed = 0.0f;
    for (float t : {10.0f, 30.0f, 50.0f}) {
        int lit = 0, n = 0, flips = 0;
        for (int iz = 0; iz < 30; ++iz) for (int ix = 0; ix < 30; ++ix) {
            const float e = RM::envelope(p, ix + 0.5f, iz + 0.5f, t), e2 = RM::envelope(p, ix + 0.5f, iz + 0.5f, t + 3.0f * RM::kEvolveS);
            if (e > 0.5f) ++lit;
            if ((e > 0.5f) != (e2 > 0.5f)) ++flips;
            ++n;
            steepest = std::max(steepest, glm::length(RM::slope(p, ix * 9, iz * 9, t)));
        }
        minLit = std::min(minLit, lit / float(n)); maxLit = std::max(maxLit, lit / float(n)); changed = std::max(changed, flips / float(n));
    }
    std::printf("  rippled share %.2f..%.2f; changed within 3 x %.0f s: %.2f; steepest facet %.4f (bound %.4f)\n", minLit, maxLit, RM::kEvolveS, changed, steepest, bound);
    EXPECT_GT(minLit, 0.05f) << "some of the pond is always rippled";
    EXPECT_LT(maxLit, 0.95f) << "some of it is always glassy";
    EXPECT_GT(changed, 0.2f) << "the patches come and go";
    EXPECT_LE(steepest, bound * 1.15f) << "the waves' slope + the envelope's own gradient (~1 / patch m x amp: under 10 % at 6 m)";
}

}  // namespace
}  // namespace Phyxel::Core::Water
