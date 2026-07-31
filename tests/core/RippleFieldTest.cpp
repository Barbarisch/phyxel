#include <gtest/gtest.h>

#include "core/RippleField.h"

#include <cmath>

// L1/L2 validation for the ripple/disturbance heightfield (small-scale water plan Phase 3).
// The field is purely visual — these pin the WAVE facts the renderer depends on: an impulse
// becomes an expanding symmetric ring, energy decays to genuine sleep, recentring keeps crests
// world-stationary, and out-of-window injection is a no-op.

namespace Phyxel {
namespace Core {
namespace {

TEST(RippleFieldTest, StartsAsleepAndTickIsFree) {
    RippleField f;
    EXPECT_TRUE(f.asleep());
    const unsigned long long v = f.version();
    for (int i = 0; i < 100; ++i) f.tick(1.0f / 60.0f);
    EXPECT_TRUE(f.asleep());
    EXPECT_EQ(f.version(), v) << "ticking an asleep field must do no work";
    EXPECT_FLOAT_EQ(f.heightAt(glm::vec2(0.0f)), 0.0f);
}

TEST(RippleFieldTest, ImpulseProducesASymmetricExpandingRing) {
    RippleField f;
    f.followTo(glm::vec2(0.0f), 0.0f);   // centre the window on the origin exactly
    f.addImpulse(glm::vec2(0.0f), 1.5f, 0.5f);
    ASSERT_FALSE(f.asleep());

    // Let the ring travel outward, then measure at a radius the initial cap never covered.
    for (int i = 0; i < 60; ++i) f.tick(1.0f / 60.0f);   // 1 s → crest ~3 units out
    const float probeR = 2.5f;
    float h[8];
    for (int k = 0; k < 8; ++k) {
        const float a = k * 3.14159265f / 4.0f;
        h[k] = f.heightAt(glm::vec2(probeR * std::cos(a), probeR * std::sin(a)));
    }
    // 4-fold symmetry is exact on a square grid; diagonals may differ slightly (grid anisotropy).
    EXPECT_NEAR(h[0], h[2], 1e-4f);
    EXPECT_NEAR(h[2], h[4], 1e-4f);
    EXPECT_NEAR(h[4], h[6], 1e-4f);
    EXPECT_NEAR(h[1], h[3], 1e-4f);
    EXPECT_NEAR(h[3], h[5], 1e-4f);
    // The ring genuinely reached the probe radius: SOME bearing shows real amplitude.
    float maxAbs = 0.0f;
    for (float v : h) maxAbs = std::max(maxAbs, std::abs(v));
    EXPECT_GT(maxAbs, 1e-3f) << "no wave reached r=2.5 after 1 s — the ring is not expanding";
    // And the centre is no longer the peak (the cap collapsed outward).
    EXPECT_LT(std::abs(f.heightAt(glm::vec2(0.0f))), 0.5f * 0.9f);
}

TEST(RippleFieldTest, EnergyDecaysAndFieldReachesGenuineSleep) {
    // NOTE: Σ|h| is NOT monotone for a wave equation — a one-signed cap disperses into positive
    // and negative lobes, so the absolute sum legitimately RISES for the first fraction of a
    // second (observed 4.4 → 9.0). The honest claims are long-range decay + genuine sleep.
    RippleField f;
    f.followTo(glm::vec2(0.0f), 0.0f);
    f.addImpulse(glm::vec2(0.0f), 1.5f, 0.5f);
    ASSERT_GT(f.totalAmplitude(), 0.0f);

    for (int i = 0; i < 30; ++i) f.tick(1.0f / 60.0f);    // t = 0.5 s
    const float early = f.totalAmplitude();
    for (int i = 0; i < 120; ++i) f.tick(1.0f / 60.0f);   // t = 2.5 s
    const float late = f.totalAmplitude();
    EXPECT_LT(late, early * 0.35f)
        << "amplitude barely decays (t0.5s " << early << " -> t2.5s " << late
        << ") — rings would linger for many seconds";

    int guard = 0;
    while (!f.asleep() && guard++ < 40) {
        for (int i = 0; i < 20; ++i) f.tick(1.0f / 60.0f);
    }
    EXPECT_TRUE(f.asleep()) << "field never slept (residual " << f.totalAmplitude() << ")";
    // Sleep is a genuine snap-to-zero: heights read exactly 0.
    EXPECT_FLOAT_EQ(f.heightAt(glm::vec2(0.7f, -0.3f)), 0.0f);
}

TEST(RippleFieldTest, RecentreKeepsCrestsWorldStationary) {
    RippleField f;
    f.followTo(glm::vec2(0.0f), 0.0f);
    f.addImpulse(glm::vec2(3.0f, -2.0f), 2.0f, 0.6f);
    for (int i = 0; i < 10; ++i) f.tick(1.0f / 60.0f);

    // Sample a fixed WORLD point, recentre the window by several cells, sample again.
    const glm::vec2 probe(3.5f, -1.5f);
    const float before = f.heightAt(probe);
    ASSERT_NE(before, 0.0f) << "probe point must be on the wave for the test to mean anything";
    ASSERT_TRUE(f.followTo(glm::vec2(9.0f, 4.0f), 0.5f));
    const float after = f.heightAt(probe);
    EXPECT_NEAR(after, before, 1e-4f) << "recentring moved the wave in world space";
}

TEST(RippleFieldTest, OutOfWindowImpulseIsANoOp) {
    RippleField f;   // window is 64 units, centred near the origin
    f.followTo(glm::vec2(0.0f), 0.0f);
    const unsigned long long v = f.version();
    f.addImpulse(glm::vec2(500.0f, 0.0f), 2.0f, 1.0f);
    EXPECT_TRUE(f.asleep());
    EXPECT_EQ(f.version(), v);
}

TEST(RippleFieldTest, ImpulseOutsideWindowAfterRecentreLands) {
    // The complement of the no-op test: after following the player 500 units away, an impulse
    // THERE must land (the window travelled).
    RippleField f;
    f.followTo(glm::vec2(500.0f, 0.0f), 0.5f);
    f.addImpulse(glm::vec2(500.0f, 0.0f), 2.0f, 1.0f);
    EXPECT_FALSE(f.asleep());
    for (int i = 0; i < 5; ++i) f.tick(1.0f / 60.0f);
    EXPECT_NE(f.heightAt(glm::vec2(500.5f, 0.5f)), 0.0f);
}

}  // namespace
}  // namespace Core
}  // namespace Phyxel
