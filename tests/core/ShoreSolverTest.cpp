#include <gtest/gtest.h>

#include "core/water/SeaSwell.h"
#include "core/water/ShoreSolver.h"

#include <cmath>
#include <cstdio>

// WaterCore Phase G (docs/WaterCore.md 18.4): the shoreline band's column solver. The rows the
// 3-D fill-fraction core failed (a swell carried into a free channel) and the physics a shore needs
// (run-up against Hunt's formula, mass-exact wet/dry, a wall reflects).

namespace Phyxel::Core::Water {
namespace {

constexpr float kDt = 1.0f / 60.0f;

struct Airy {
    float a, L, d;
    float k() const { return 6.28318530718f / L; }
    float omega() const { return k() * std::sqrt(9.81f / k()); }
    float period() const { return 6.28318530718f / omega(); }
    float cPhase() const { return omega() / k(); }
    float eta(float x, float t) const { return a * std::sin(k() * x - omega() * t); }
    /// depth-averaged horizontal velocity of the progressive wave (linear theory): (eta / d) c_shallow-ish;
    /// exact Airy: u_avg = a omega / (k d) sin(theta) ... = (eta / d) * (omega / k)
    float uAvg(float x, float t) const { return eta(x, t) / d * cPhase(); }
};

ShoreSolver channel(int nx, float h, float still, float bedY = 0.0f) {
    ShoreSolver s(glm::vec2(0.0f), nx, 1, h);
    for (int x = 0; x < nx; ++x) { s.col(x, 0).bed = bedY; s.col(x, 0).eta = still; }
    return s;
}

TEST(ShoreSolverTest, StillWaterOnASlopeStaysStill) {
    // a 1:10 beach under still water at 3.0: nothing moves, mass is exact, after 600 ticks
    ShoreSolver s(glm::vec2(0.0f), 60, 4, 1.0f / 3.0f);
    for (int z = 0; z < 4; ++z) for (int x = 0; x < 60; ++x) { s.col(x, z).bed = x / 30.0f; s.col(x, z).eta = std::max(3.0f, x / 30.0f); }
    const double m0 = s.totalMass();
    ShoreStepReport r;
    for (int k = 0; k < 600; ++k) r = s.step(kDt);
    EXPECT_LT(r.maxSpeed, 1e-6f);
    EXPECT_NEAR(s.totalMass(), m0, 1e-9 * m0);
    for (int x = 0; x < 60; ++x) EXPECT_NEAR(s.col(x, 1).eta, std::max(3.0f, x / 30.0f), 1e-6f);
}

TEST(ShoreSolverTest, CarriesTheSwell) {
    // the row the 3-D core failed (18.4): a 60 m channel, 3 m deep, 1/3 m columns, the first 12 m
    // prescribed as the ocean carrying a 0.3 m, 14 m Airy wave; the free channel must carry it:
    // amplitude >= 0.8 a at 24 m and >= 0.6 a at 36 m, arrival within c-travel time + 1 s, the
    // ledger closes.
    const float h = 1.0f / 3.0f;
    const int nx = 180;
    ShoreSolver s = channel(nx, h, 3.0f);
    const Airy w{0.3f, 14.0f, 3.0f};
    const double m0 = s.totalMass();
    double exchanged = 0.0;
    float hi24 = -1e9f, lo24 = 1e9f, hi36 = -1e9f, lo36 = 1e9f; int firstArrival = -1;
    const int ticks = static_cast<int>(12.0f / kDt);
    for (int k = 1; k <= ticks; ++k) {
        const float tt = k * kDt;
        for (int x = 0; x < 36; ++x) { const float wx = (x + 0.5f) * h; s.prescribe(x, 0, 3.0f + w.eta(wx, tt), glm::vec2(w.uAvg(wx, tt), 0.0f)); }
        const ShoreStepReport r = s.step(kDt);
        exchanged += r.exchanged;
        const float e24 = static_cast<float>(s.col(72, 0).eta), e36 = static_cast<float>(s.col(108, 0).eta);
        if (tt > 4.0f) { hi24 = std::max(hi24, e24); lo24 = std::min(lo24, e24); }
        if (tt > 7.0f) { hi36 = std::max(hi36, e36); lo36 = std::min(lo36, e36); }
        if (firstArrival < 0 && std::abs(e24 - 3.0f) > 0.05f) firstArrival = k;
    }
    const float a24 = 0.5f * (hi24 - lo24), a36 = 0.5f * (hi36 - lo36);
    std::printf("  shore swell: amplitude at 24 m %.3f, at 36 m %.3f (driven %.3f), arrival %.2f s (c %.2f m/s), exchanged %.3f m^3\n", a24, a36, w.a, firstArrival * kDt, w.cPhase(), exchanged);
    EXPECT_GE(a24, 0.8f * w.a);
    EXPECT_GE(a36, 0.6f * w.a);
    EXPECT_GT(firstArrival, 0);
    EXPECT_LT(firstArrival * kDt, 12.0f / w.cPhase() + 1.0f);
    EXPECT_NEAR(s.totalMass() - m0, exchanged, 1e-5 * m0) << "the flux ledger closes (float32 surfaces)";
    RecordProperty("amp_24m", a24); RecordProperty("amp_36m", a36);
}

TEST(ShoreSolverTest, MassExactWithWetDry) {
    // a 2 m pile of water released onto a dry 1:5 slope with a wall at the end: never negative, never
    // created, the sum of depths is the initial mass to 1e-9 after 20 s, and the dry slope above the
    // final waterline stays dry
    const float h = 1.0f / 3.0f;
    ShoreSolver s(glm::vec2(0.0f), 90, 3, h);
    for (int z = 0; z < 3; ++z) for (int x = 0; x < 90; ++x) {
        s.col(x, z).bed = x < 30 ? 0.0f : (x - 30) * h / 5.0f;
        s.col(x, z).eta = x < 30 ? 2.0f : s.col(x, z).bed;
        if (x == 89) s.col(x, z).wall = 1;
    }
    const double m0 = s.totalMass();
    double clamped = 0.0; float vmax = 0.0f;
    for (int k = 0; k < 1200; ++k) { const ShoreStepReport r = s.step(kDt); clamped += r.clamped; vmax = std::max(vmax, r.maxSpeed); }
    std::printf("  wet/dry: mass %.9f -> %.9f m^3, clamped %.3g, vmax %.2f\n", m0, s.totalMass(), clamped, vmax);
    EXPECT_NEAR(s.totalMass(), m0, 1e-9 * m0) << "exact: double surfaces, conservative fluxes";
    EXPECT_EQ(clamped, 0.0) << "no depth was ever clamped from negative";
    for (int z = 0; z < 3; ++z) for (int x = 0; x < 90; ++x) EXPECT_GE(s.col(x, z).eta, s.col(x, z).bed - 1e-6f);
    // the water spread: the pile is lower, the slope holds some
    EXPECT_LT(s.col(5, 1).eta, 2.0f);
    EXPECT_GT(s.col(40, 1).eta - s.col(40, 1).bed, 0.0f);
}

TEST(ShoreSolverTest, WallReflects) {
    // a wave meeting a vertical wall doubles in height at the wall (standing wave): the max surface
    // at the wall column is >= 1.6 x the incident amplitude above still level
    const float h = 1.0f / 3.0f;
    ShoreSolver s = channel(120, h, 3.0f);
    s.col(119, 0).wall = 1; s.col(119, 0).eta = 0.0f;
    const Airy w{0.2f, 10.0f, 3.0f};
    float peak = -1e9f;
    for (int k = 1; k <= 900; ++k) {
        const float tt = k * kDt;
        for (int x = 0; x < 30; ++x) { const float wx = (x + 0.5f) * h; s.prescribe(x, 0, 3.0f + w.eta(wx, tt), glm::vec2(w.uAvg(wx, tt), 0.0f)); }
        s.step(kDt);
        if (tt > 8.0f) peak = std::max(peak, static_cast<float>(s.col(118, 0).eta - 3.0));
    }
    std::printf("  wall: peak at the wall %.3f (incident amplitude %.3f)\n", peak, w.a);
    EXPECT_GE(peak, 1.6f * w.a);
}

TEST(ShoreSolverTest, RunUpOnTheBeachVsHunt) {
    // S12 ground: Hunt's formula R = H xi, xi = tan(beta) / sqrt(H / L0), for a 1:20 beach, H = 0.6 m
    // (2 a), L0 = g T^2 / 2 pi (deep-water length of the 3.0 s period) -> reported against +-20 %.
    // Measured as the highest bed reached by water deeper than 1 cm, above the still level.
    const float h = 1.0f / 3.0f;
    const int nx = 360;   // 120 m: 20 m flat at 3 m depth, then a 1:20 beach from x = 20 m: the bed crosses the still line (0) at 80 m and reaches +2 m at 120 m
    ShoreSolver s(glm::vec2(0.0f), nx, 1, h);
    for (int x = 0; x < nx; ++x) { const float wx = (x + 0.5f) * h; s.col(x, 0).bed = wx < 20.0f ? -3.0f : -3.0f + (wx - 20.0f) / 20.0f; s.col(x, 0).eta = std::max(0.0f, s.col(x, 0).bed); }
    const Airy w{0.3f, 14.0f, 3.0f};
    const float H = 2.0f * w.a, T = w.period(), L0 = 9.81f * T * T / 6.28318530718f;
    const float xi = (1.0f / 20.0f) / std::sqrt(H / L0);
    const float hunt = H * xi;
    float runUp = -1e9f;
    for (int k = 1; k <= 2400; ++k) {
        const float tt = k * kDt;
        for (int x = 0; x < 36; ++x) { const float wx = (x + 0.5f) * h; s.prescribe(x, 0, w.eta(wx, tt), glm::vec2(w.uAvg(wx, tt), 0.0f)); }
        const ShoreStepReport rr = s.step(kDt);
        if (k % 600 == 0) {
            int front = -1; for (int x = 0; x < nx; ++x) if (s.col(x, 0).eta - s.col(x, 0).bed > 0.01f) front = x;
            std::printf("    t %5.1f  eta@20m %+.3f @50m %+.3f @70m %+.3f @78m %+.3f  front bed %+.3f  vmax %.2f sub %d\n", tt, s.col(60, 0).eta, s.col(150, 0).eta, s.col(210, 0).eta, s.col(234, 0).eta, s.col(front, 0).bed, rr.maxSpeed, rr.substeps);
        }
        if (tt > 25.0f) for (int x = 0; x < nx; ++x) if (s.col(x, 0).eta - s.col(x, 0).bed > 0.01f) runUp = std::max(runUp, s.col(x, 0).bed);
    }
    std::printf("  run-up: measured %.3f m, Hunt R = H xi = %.3f m (xi %.2f, H %.2f, L0 %.1f)\n", runUp, hunt, xi, H, L0);
    RecordProperty("runup_m", runUp); RecordProperty("hunt_m", hunt);
    EXPECT_GT(runUp, 0.0f) << "water must climb the beach above the still line";
    EXPECT_NEAR(runUp, hunt, 0.2f * hunt + 0.02f) << "Hunt +-20 % (plus a 2 cm floor for the 1 cm wet threshold)";
}

}  // namespace
}  // namespace Phyxel::Core::Water
