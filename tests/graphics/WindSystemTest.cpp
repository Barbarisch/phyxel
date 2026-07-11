// Unit tests for the shared procedural wind field (docs/VegetationWindPlan.md Phase 1).
// The CPU side must be deterministic, continuous, and — critically for the visual contract —
// produce EXACTLY zero vegetation motion at wind speed 0.

#include <gtest/gtest.h>

#include "graphics/WindSystem.h"

#include <cmath>

using Phyxel::Graphics::WindSystem;

TEST(WindSystemTest, DeterministicFromSettingsAndTime) {
    WindSystem a, b;
    a.settings() = {40.0f, 0.7f, 0.3f};
    b.settings() = {40.0f, 0.7f, 0.3f};

    // Different tick histories, same final time → identical state (pure function of
    // settings + time; nothing accumulates across ticks).
    a.tick(1.0f);
    a.tick(123.456f);
    b.tick(123.456f);

    EXPECT_EQ(a.state().dir.x,     b.state().dir.x);
    EXPECT_EQ(a.state().dir.y,     b.state().dir.y);
    EXPECT_EQ(a.state().base,      b.state().base);
    EXPECT_EQ(a.state().gustAmp,   b.state().gustAmp);
    EXPECT_EQ(a.state().gustScale, b.state().gustScale);
    EXPECT_EQ(a.state().gustSpeed, b.state().gustSpeed);
}

TEST(WindSystemTest, ZeroSpeedMeansZeroMotion) {
    // The shaders compute every displacement/rotation term as a multiple of base or gustAmp,
    // so base == 0 && gustAmp == 0 guarantees perfectly still vegetation (validation contract:
    // "grass field + canopy at wind 0 must be perfectly still").
    WindSystem ws;
    ws.settings().speed = 0.0f;
    for (float t : {0.0f, 1.0f, 60.0f, 3600.0f}) {
        ws.tick(t);
        EXPECT_EQ(ws.state().base, 0.0f) << "at t=" << t;
        EXPECT_EQ(ws.state().gustAmp, 0.0f) << "at t=" << t;
    }
}

TEST(WindSystemTest, DirectionIsUnitAndWandersAroundMean) {
    WindSystem ws;
    ws.settings() = {90.0f, 0.5f, 1.0f};  // mean +Z, max gustiness = widest wander (±30°)
    for (float t = 0.0f; t < 300.0f; t += 7.3f) {
        ws.tick(t);
        const auto& d = ws.state().dir;
        EXPECT_NEAR(std::sqrt(d.x * d.x + d.y * d.y), 1.0f, 1e-5f);
        float deg = glm::degrees(std::atan2(d.y, d.x));
        EXPECT_GT(deg, 55.0f);
        EXPECT_LT(deg, 125.0f);
    }
}

TEST(WindSystemTest, ContinuousOverFrameSteps) {
    // No pops: consecutive frames (60 Hz) must produce near-identical state. Catches any
    // future change that swaps the smooth drift noise for something discontinuous.
    WindSystem ws;
    ws.settings() = {25.0f, 1.0f, 1.0f};
    ws.tick(50.0f);
    auto s0 = ws.state();
    ws.tick(50.0f + 1.0f / 60.0f);
    auto s1 = ws.state();

    EXPECT_NEAR(s0.dir.x, s1.dir.x, 0.01f);
    EXPECT_NEAR(s0.dir.y, s1.dir.y, 0.01f);
    EXPECT_NEAR(s0.base, s1.base, 0.01f);
    EXPECT_NEAR(s0.gustAmp, s1.gustAmp, 0.01f);
}

TEST(WindSystemTest, StrongerWindScalesEveryTerm) {
    WindSystem calm, storm;
    calm.settings()  = {25.0f, 0.2f, 0.5f};
    storm.settings() = {25.0f, 1.0f, 0.5f};
    calm.tick(10.0f);
    storm.tick(10.0f);

    EXPECT_GT(storm.state().base,      calm.state().base);
    EXPECT_GT(storm.state().gustAmp,   calm.state().gustAmp);
    EXPECT_GT(storm.state().gustSpeed, calm.state().gustSpeed);
}
