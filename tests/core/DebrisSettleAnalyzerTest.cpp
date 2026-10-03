// DebrisSettleAnalyzerTest — the measuring instrument must be right before it is trusted to
// prove the "bubbling crater" bug (docs/DebrisSettlingPlan.md §3). Synthetic trajectories with
// known answers: an honest fall/land injects nothing; a push-out pop injects exactly its
// potential energy and registers as a rebound; slot reuse is a new body; force-frozen bodies
// are told apart from clean sleeps.

#include <gtest/gtest.h>

#include "core/DebrisSettleAnalyzer.h"

using Phyxel::Core::DebrisSettleAnalyzer;
using Phyxel::Core::SettleBodySample;
using Phyxel::Core::SettleSolverCounters;

namespace {
constexpr float kDt = 1.0f / 60.0f;
constexpr float kG  = 9.81f;
constexpr uint32_t kActive = 1u, kSleeping = 2u;

uint32_t flagsFor(uint32_t age, bool asleep = false) {
    return kActive | (asleep ? kSleeping : 0u) | ((age & 0xFFu) << 16);
}

SettleBodySample body(glm::vec3 pos, glm::vec3 prev, uint32_t age, bool asleep = false) {
    SettleBodySample s;
    s.position = pos; s.prevPosition = prev; s.flags = flagsFor(age, asleep);
    s.mass = 2.0f; s.radius = 0.5f;
    return s;
}

// Implicit-Euler free fall onto a floor at y=floorY, perfectly inelastic landing — what an
// ideal solver produces. Total energy must never rise.
std::vector<std::vector<SettleBodySample>> fallAndLand(float y0, float floorY, int ticks) {
    std::vector<std::vector<SettleBodySample>> out;
    float y = y0, v = 0.0f;
    for (int t = 0; t < ticks; ++t) {
        float prev = y;
        v -= kG * kDt;
        y += v * kDt;
        if (y < floorY) { y = floorY; v = 0.0f; }
        out.push_back({body({0, y, 0}, {0, prev, 0}, uint32_t(t + 1))});
    }
    return out;
}
} // namespace

TEST(DebrisSettleAnalyzerTest, HonestFallAndLandInjectsNothing) {
    DebrisSettleAnalyzer a;
    for (auto& tick : fallAndLand(5.0f, 1.0f, 240)) a.addTick(tick, {});
    auto s = a.summary();
    EXPECT_NEAR(s["energy"]["injected_total_J"].get<double>(), 0.0, 1e-6);
    EXPECT_EQ(s["rebounds"]["total"].get<uint64_t>(), 0u);
    // Energy went DOWN by the fall height (landing absorbs it).
    EXPECT_LT(s["energy"]["final_J"].get<double>(), s["energy"]["initial_J"].get<double>() - 2.0 * kG * 3.9);
}

TEST(DebrisSettleAnalyzerTest, PushOutPopIsInjectedEnergyAndARebound) {
    // A body resting at y=1 is shoved up 1 cm in one tick (what the hard-contact pass does),
    // then falls back. Velocity is DERIVED from displacement, so the 1 cm becomes 0.6 m/s.
    DebrisSettleAnalyzer a;
    const float y = 1.0f;
    for (uint32_t t = 1; t <= 10; ++t) a.addTick({body({0, y, 0}, {0, y, 0}, t)}, {});
    a.addTick({body({0, y + 0.01f, 0}, {0, y, 0}, 11)}, {});
    auto s = a.summary();
    EXPECT_EQ(s["rebounds"]["total"].get<uint64_t>(), 1u);
    // ΔE = m·g·0.01 + ½·m·(0.6)^2 = 0.1962 + 0.36
    const double v = 0.01 / kDt;
    EXPECT_NEAR(s["energy"]["injected_total_J"].get<double>(), 2.0 * kG * 0.01 + 0.5 * 2.0 * v * v, 1e-3);
}

TEST(DebrisSettleAnalyzerTest, EnergyTransferBetweenBodiesIsNotInjection) {
    // Body A loses exactly what body B gains: closed system, ΣE constant → no injection.
    DebrisSettleAnalyzer a;
    a.addTick({body({0, 1, 0}, {0, 1, 0}, 1), body({2, 1, 0}, {2, 1, 0}, 1)}, {});
    a.addTick({body({0, 0.99f, 0}, {0, 0.99f, 0}, 2), body({2, 1.01f, 0}, {2, 1.01f, 0}, 2)}, {});
    EXPECT_NEAR(a.summary()["energy"]["injected_total_J"].get<double>(), 0.0, 1e-4);
}

TEST(DebrisSettleAnalyzerTest, SlotReuseIsANewBodyNotAnEnergyJump) {
    DebrisSettleAnalyzer a;
    a.addTick({body({0, 1, 0}, {0, 1, 0}, 200)}, {});
    // Same slot, spawn age reset, spawned 50 m higher: must NOT count as +E injection.
    a.addTick({body({0, 51, 0}, {0, 51, 0}, 1)}, {});
    auto s = a.summary();
    EXPECT_NEAR(s["energy"]["injected_total_J"].get<double>(), 0.0, 1e-9);
    EXPECT_EQ(s["bodies"].get<uint32_t>(), 2u);
}

TEST(DebrisSettleAnalyzerTest, CleanVersusForcedSleep) {
    DebrisSettleAnalyzer a;
    // Body 0: 40 still ticks then sleeps → clean. Body 1: jitters at 0.1 m/s (above strict,
    // below lax) then is frozen → forced.
    const float jit = 0.1f * kDt;
    for (uint32_t t = 1; t <= 40; ++t) {
        float y1 = 1.0f + ((t & 1) ? jit : 0.0f);
        float p1 = 1.0f + ((t & 1) ? 0.0f : jit);
        a.addTick({body({0, 1, 0}, {0, 1, 0}, t), body({2, y1, 0}, {2, p1, 0}, t)}, {});
    }
    a.addTick({body({0, 1, 0}, {0, 1, 0}, 41, true), body({2, 1, 0}, {2, 1, 0}, 41, true)}, {});
    auto s = a.summary();
    EXPECT_EQ(s["sleep"]["clean"].get<uint32_t>(), 1u);
    EXPECT_EQ(s["sleep"]["forced"].get<uint32_t>(), 1u);
    EXPECT_EQ(s["time_all_asleep_s"].get<double>(), 40 * kDt);
}

TEST(DebrisSettleAnalyzerTest, ColorSkipCountsOnlyAwakeBodiesWithConstraints) {
    DebrisSettleAnalyzer a;
    SettleBodySample s0 = body({0, 1, 0}, {0, 1, 0}, 1); s0.color = 12; s0.constraintCount = 3;
    SettleBodySample s1 = body({2, 1, 0}, {2, 1, 0}, 1); s1.color = 0xFFFFFFFFu; s1.constraintCount = 0;
    SettleBodySample s2 = body({4, 1, 0}, {4, 1, 0}, 1); s2.color = 3;  s2.constraintCount = 2;
    SettleSolverCounters c; c.maxColors = 12;
    a.addTick({s0, s1, s2}, c);
    EXPECT_EQ(a.ticks().back().colorSkipped, 1u);
    EXPECT_EQ(a.summary()["checks"]["color_skipped"]["pass"].get<bool>(), false);
}

TEST(DebrisSettleAnalyzerTest, TunnellingBelowFloorIsCounted) {
    DebrisSettleAnalyzer::Config cfg;
    cfg.floorY = 17.0f;
    DebrisSettleAnalyzer a(cfg);
    a.addTick({body({0, 17.5f, 0}, {0, 17.5f, 0}, 1), body({2, 15.5f, 0}, {2, 15.6f, 0}, 1)}, {});
    auto s = a.summary();
    EXPECT_EQ(s["tunnelled"]["max_bodies_below_floor"].get<uint32_t>(), 1u);
    EXPECT_FLOAT_EQ(s["tunnelled"]["lowest_y"].get<float>(), 15.5f);
    EXPECT_FALSE(s["checks"]["tunnelled_through_floor"]["pass"].get<bool>());
}

TEST(DebrisSettleAnalyzerTest, PerfectPileVerdictSettles) {
    // Falls, lands, sits still ≥ strictTicks, then sleeps cleanly before allAsleepBy.
    DebrisSettleAnalyzer a;
    auto ticks = fallAndLand(2.0f, 1.0f, 120);
    for (auto& t : ticks) a.addTick(t, {});
    a.addTick({body({0, 1, 0}, {0, 1, 0}, 121, true)}, {});
    for (uint32_t t = 122; t < 300; ++t) a.addTick({body({0, 1, 0}, {0, 1, 0}, t, true)}, {});
    auto s = a.summary();
    EXPECT_EQ(s["verdict"].get<std::string>(), "SETTLES") << s.dump(2);
}
