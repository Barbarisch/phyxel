// docs/WaterCore.md 22 - the ripple layer (RippleLayer): R-T1..R-T4, R-T6 (R-T5, the body source, lives with the
// manager's feed). Predictions are the design's (22.6), written before the build.
#include <gtest/gtest.h>
#include "core/water/RippleLayer.h"
#include <cmath>
#include <cstdio>

namespace Phyxel::Core::Water {
namespace {

constexpr float kD = RippleLayer::kPitch;
constexpr float kPi = 3.14159265358979f;

// R-T2 - the kernel's spectrum is the dispersion law: K(k) within 5 % of L(k) = k (1 + sigma k^2 / rho g) for every
// lattice wavelength from 4 cells (44 cm) to 24 cells (2.7 m - beyond the band, the grid's).
TEST(WaterRippleTest, KernelIsTheDispersionLaw) {
    RippleLayer layer({0, 0}, 8, 8);
    for (int cells : {4, 5, 6, 8, 12, 24}) {
        const float k = 2.0f * kPi / (cells * kD);
        const float got = layer.kernelResponse(k), want = RippleLayer::targetResponse(k);
        std::printf("  lambda %2d cells (%.2f m): kernel %.3f, law %.3f (%+.1f %%)\n", cells, cells * kD, got, want, 100.0 * (got / want - 1.0));
        if (cells <= 12) EXPECT_NEAR(got / want, 1.0f, 0.05f) << "lambda " << cells << " cells";
    }
}

// R-T2c - stable everywhere: the kernel's response is positive at every lattice wavenumber (any direction, up to
// Nyquist) and below the leapfrog bound 4 / (g dt^2) at the largest substep. A negative response grows
// exponentially (it blew the first fitted kernel up, 2026-10-10).
TEST(WaterRippleTest, KernelIsStableEverywhere) {
    RippleLayer layer({0, 0}, 8, 8);
    const auto& K = layer.kernel();
    const int R = RippleLayer::kRadius, W = 2 * R + 1;
    float lo = 1e30f, hi = -1e30f;
    for (int iz = 0; iz <= 64; ++iz) for (int ix = 0; ix <= 64; ++ix) {
        if (ix == 0 && iz == 0) continue;
        const float kx = kPi / kD * ix / 64.0f, kz = kPi / kD * iz / 64.0f;
        double acc = 0.0;
        for (int dz = -R; dz <= R; ++dz) for (int dx = -R; dx <= R; ++dx) acc += K[(dx + R) + W * (dz + R)] * std::cos((kx * dx + kz * dz) * kD);
        lo = std::min(lo, static_cast<float>(acc)); hi = std::max(hi, static_cast<float>(acc));
    }
    const float bound = 4.0f / (9.81f * RippleLayer::kMaxStep * RippleLayer::kMaxStep);
    std::printf("  response over the lattice: min %.3f, max %.3f (leapfrog bound %.1f)\n", lo, hi, bound);
    EXPECT_GT(lo, 0.0f);
    EXPECT_LT(hi, bound);
}

// R-T2b - the integrator keeps the law: a plane wave of 6 cells (0.67 m) and 4 cells (0.44 m), periodic in x
// on a long strip, oscillates at omega = sqrt(g L(k)) within 5 %.
TEST(WaterRippleTest, PlaneWaveRunsAtItsOwnSpeed) {
    for (int cells : {4, 6}) {
        const int nx = cells * 24, nz = 72;   // wide: the z walls are ~4 m away (a narrow strip added a z mode)
        RippleLayer layer({0, 0}, nx, nz);
        layer.setOpenBorder(false);
        const float k = 2.0f * kPi / (cells * kD), omega = std::sqrt(9.81f * RippleLayer::targetResponse(k));
        // a standing wave from rest: kick it with a cosine impulse, then time the zero crossings of a crest cell
        for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x)
            layer.addImpulse((x + 0.5f) * kD, (z + 0.5f) * kD, 1.0f * std::cos(k * (x + 0.5f) * kD));
        const float dt = 1.0f / 240.0f;
        float prev = 0.0f; int crossings = 0; float tFirst = -1.0f, tLast = -1.0f;
        const int px = cells * 12, pz = nz / 2;
        for (int s = 1; s <= 480; ++s) {   // 2 s: before anything from the walls reaches the centre
            layer.step(dt);
            const float v = layer.height(px, pz);
            static float r19 = 0.0f, r20 = 0.0f, kr20 = 0.0f;
            if (s == 19) r19 = v;
            if (s == 20) {   // diagnostic: the convolution the center actually sees
                const auto& K = layer.kernel(); const int R = RippleLayer::kRadius, W = 2 * R + 1; double acc = 0.0;
                for (int dz = -R; dz <= R; ++dz) for (int dx = -R; dx <= R; ++dx) acc += K[(dx + R) + W * (dz + R)] * layer.height(px + dx, pz + dz);
                r20 = v; kr20 = static_cast<float>(acc);
            }
            if (s == 21) std::printf("    second difference %.4e vs -g dt^2 (K*r) %.4e\n", v - 2.0f * r20 + r19, -9.81f * dt * dt * kr20);
            if (s > 1 && ((prev < 0.0f) != (v < 0.0f))) { ++crossings; if (tFirst < 0.0f) tFirst = s * dt; tLast = s * dt; std::printf("    crossing %d at %.4f s (r %.3e)\n", crossings, s * dt, v); }
            prev = v;
        }
        ASSERT_GE(crossings, 4);
        const float measured = kPi * (crossings - 1) / (tLast - tFirst);
        std::printf("  plane wave %d cells: omega measured %.3f rad/s, law %.3f (%+.1f %%)\n", cells, measured, omega, 100.0 * (measured / omega - 1.0));
        EXPECT_NEAR(measured / omega, 1.0f, 0.05f);
    }
}

// R-T1 - a point kick spreads into a TRAIN of rings, longest waves first: along a ray the crests further out are
// spaced wider than the crests behind them. Control: a non-dispersive field (the old RippleField's single speed)
// would keep one spacing.
TEST(WaterRippleTest, AKickSpreadsIntoARingTrain) {
    const int n = 160;
    RippleLayer layer({0, 0}, n, n);
    layer.setOpenBorder(true);
    layer.addImpulse(n / 2 * kD + 0.5f * kD, n / 2 * kD + 0.5f * kD, 50.0f);
    const float dt = 1.0f / 60.0f;
    for (int s = 0; s < 90; ++s) layer.step(dt);   // 1.5 s
    std::vector<int> crests;
    for (int x = n / 2 + 2; x < n - 3; ++x) {
        const float a = layer.height(x - 1, n / 2), b = layer.height(x, n / 2), c = layer.height(x + 1, n / 2);
        if (b > a && b >= c && b > 0.01f * layer.maxAbs()) crests.push_back(x - n / 2);   // ignore sub-1 % numerical tails
    }
    std::printf("  crests along +x at 1.5 s (cells from the kick):");
    for (int c : crests) std::printf(" %d", c);
    std::printf("\n");
    ASSERT_GE(crests.size(), 3u) << "a train, not one ring";
    const int inner = crests[1] - crests[0], outer = crests[crests.size() - 1] - crests[crests.size() - 2];
    EXPECT_GT(outer, inner) << "outer crests (long waves) must be spaced wider than inner ones (short waves)";
}

// R-T3 - a one-voxel wall (9 lattice cells, wider than the kernel's reach) reflects: nothing reaches the far side.
TEST(WaterRippleTest, AVoxelWallReflects) {
    const int nx = 120, nz = 60;
    RippleLayer layer({0, 0}, nx, nz);
    layer.setOpenBorder(false);
    for (int z = 0; z < nz; ++z) for (int x = 70; x < 79; ++x) layer.mask()[layer.idx(x, z)] = 0;
    layer.addImpulse(40.5f * kD, 30.5f * kD, 50.0f);
    double behind = 0.0;
    for (int s = 0; s < 240; ++s) {
        layer.step(1.0f / 60.0f);
        for (int z = 0; z < nz; ++z) for (int x = 79; x < nx; ++x) behind = std::max(behind, static_cast<double>(std::abs(layer.height(x, z))));
    }
    std::printf("  largest |r| behind the wall in 4 s: %.3e m (front max now %.3e)\n", behind, layer.maxAbs());
    EXPECT_EQ(behind, 0.0);
}

// R-T4 - sleep: one droplet's kick (20 ml at 3 m/s on one cell) dies to exact zero within 30 s; a still layer never wakes.
TEST(WaterRippleTest, ItSleeps) {
    RippleLayer still({0, 0}, 36, 36);
    for (int s = 0; s < 600; ++s) still.step(1.0f / 60.0f);
    EXPECT_TRUE(still.asleep());
    EXPECT_EQ(still.maxAbs(), 0.0f);
    RippleLayer layer({0, 0}, 36, 36);
    layer.setOpenBorder(false);   // a walled 4 x 4 m pond: the ring bounces until the film damps it
    const float I = 1000.0f * 20e-6f * 3.0f / (kD * kD);
    layer.addImpulse(18.5f * kD, 18.5f * kD, I);
    float peak = 0.0f; int sleptAt = -1;
    for (int s = 0; s < 1800 && sleptAt < 0; ++s) { layer.step(1.0f / 60.0f); peak = std::max(peak, layer.maxAbs()); if (layer.asleep()) sleptAt = s; }
    std::printf("  droplet kick: peak |r| %.2f mm, asleep after %.1f s (-1 = not within 30 s)\n", peak * 1000.0f, sleptAt < 0 ? -1.0f : sleptAt / 60.0f);
    EXPECT_GE(sleptAt, 0);
    EXPECT_EQ(layer.maxAbs(), 0.0f);
}

// R-T6 - chunk-blind: two layers whose origins differ by one chunk (32 m = 288 lattice cells), the same kick at the
// corresponding world points (one straddling the seam x = 96 m) -> bit-equal fields.
TEST(WaterRippleTest, ChunkSeamIsInvisible) {
    RippleLayer a({860, 0}, 40, 40), b({860 + 288, 0}, 40, 40);
    const float ax = 96.0f + 0.05f, bx = ax + 32.0f;   // a's kick sits at the x = 96 m chunk seam
    a.addImpulse(ax, 2.0f, 10.0f); b.addImpulse(bx, 2.0f, 10.0f);
    for (int s = 0; s < 60; ++s) { a.step(1.0f / 60.0f); b.step(1.0f / 60.0f); }
    EXPECT_GT(a.maxAbs(), 0.0f);
    EXPECT_EQ(a.heights(), b.heights());
}

}  // namespace
}  // namespace Phyxel::Core::Water
