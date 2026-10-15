// docs/WaterCore.md 22 - the ripple layer (RippleLayer): R-T1..R-T4, R-T6 (R-T5, the body source, lives with the
// manager's feed). Predictions are the design's (22.6), written before the build.
#include <gtest/gtest.h>
#include <chrono>
#include "core/water/RippleLayer.h"
#include <cmath>
#include <cstdio>
#include <memory>

namespace Phyxel::Core::Water {
namespace {

constexpr float kD = RippleLayer::kPitch;
constexpr float kPi = 3.14159265358979f;

// 22.12.1 F1: the steepest facet the layer holds (the shader's central difference, water cells only), so each test's
// split - limit on or off - is measured, not assumed.
float peakSlope(const RippleLayer& L) {
    float mx = 0.0f;
    for (int z = 1; z + 1 < L.nz(); ++z) for (int x = 1; x + 1 < L.nx(); ++x) {
        const float sx = (L.height(x + 1, z) - L.height(x - 1, z)) * 0.5f / kD, sz = (L.height(x, z + 1) - L.height(x, z - 1)) * 0.5f / kD;
        mx = std::max(mx, std::sqrt(sx * sx + sz * sz));
    }
    return mx;
}

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
        layer.setBreakSlope(0.0f);   // 22.12.1 F1: measures linear propagation - the breaking limit off
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
        std::printf("  plane wave %d cells: peak slope %.3f; ", cells, peakSlope(layer));
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
    layer.setBreakSlope(0.0f);   // 22.12.1 F1: measures linear propagation - the breaking limit off
    layer.addImpulse(n / 2 * kD + 0.5f * kD, n / 2 * kD + 0.5f * kD, 50.0f);
    const float dt = 1.0f / 60.0f;
    float peak = 0.0f;
    for (int s = 0; s < 90; ++s) { layer.step(dt); peak = std::max(peak, peakSlope(layer)); }   // 1.5 s
    std::printf("  peak slope %.3f (limit 0.44)\n", peak);
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
    layer.setBreakSlope(0.0f);   // 22.12.1 F1: measures linear reflection - the breaking limit off
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
    float peak = 0.0f, peakS = 0.0f; int sleptAt = -1;
    for (int s = 0; s < 1800 && sleptAt < 0; ++s) { layer.step(1.0f / 60.0f); peak = std::max(peak, layer.maxAbs()); peakS = std::max(peakS, peakSlope(layer)); if (layer.asleep()) sleptAt = s; }
    std::printf("  droplet kick (limit on): peak slope %.3f; ", peakS);
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

// R-T7 (owner's screenshot 2026-10-10: the whole Small pond a black/white checkerboard, the layer at 0.8-1.4 m with
// nothing driving it). The live layer: a 54 x 54 lattice (the 6-voxel box) whose water is the inner 36 x 36 (walls
// masked), stepped once per frame with the frame's dt. RED before the fixed step: even 1/60 s steps decayed (0.9 mm
// after 60 s, walls or not); jittered 4-25 ms frames grew without bound (1.7e6 m open, 2.9e6 m walled) - the
// leapfrog keeps the previous height, so a step of a different length rescales the implied velocity.
// Prediction: with every frame-time pattern the field 55-60 s after five kicks is below its first-second peak.
TEST(WaterRippleTest, UnevenFramesStayBounded) {
    auto run = [](bool walls, bool jitter, float seconds) -> void {
        RippleLayer layer({0, 0}, 54, 54);
        layer.setBreakSlope(0.0f);   // 22.12.1 F1: catches the stepping instability - a slope limit would hide it
        if (walls) for (int z = 0; z < 54; ++z) for (int x = 0; x < 54; ++x)
            layer.mask()[layer.idx(x, z)] = (x >= 9 && x < 45 && z >= 9 && z < 45) ? 1 : 0;
        for (int k = 0; k < 5; ++k) layer.addImpulse((15.5f + 6 * k) * kD, (20.5f + 3 * k) * kD, 200.0f);
        uint32_t h = 12345u;
        float t = 0.0f, peakLate = 0.0f, peakEarly = 0.0f;
        std::printf("  walls %d, dt %s:", walls ? 1 : 0, jitter ? "jittered 4-25 ms" : "1/60");
        int printed = 0;
        while (t < seconds) {
            float dt = 1.0f / 60.0f;
            if (jitter) { h = h * 1664525u + 1013904223u; dt = 0.004f + 0.021f * static_cast<float>(h >> 8) / 16777216.0f; }
            layer.step(dt); t += dt;
            if (t > printed * 5.0f) { std::printf(" t%.0f %.1fmm", t, layer.maxAbs() * 1000.0f); ++printed; }
            if (t < 1.0f) peakEarly = std::max(peakEarly, layer.maxAbs());
            if (t > seconds - 5.0f) peakLate = std::max(peakLate, layer.maxAbs());
        }
        std::printf(" | first second peak %.1f mm, last 5 s peak %.1f mm\n", peakEarly * 1000.0f, peakLate * 1000.0f);
        EXPECT_LT(peakLate, peakEarly) << "the field grew with nothing driving it (walls " << walls << ", jitter " << jitter << ")";
    };
    run(false, false, 60.0f); run(true, false, 60.0f); run(false, true, 60.0f); run(true, true, 60.0f);
}

namespace {
// the steepest lattice edge relative to its limit (axis s d, diagonal s d sqrt 2), water-water edges of one body only
float worstEdgeExcess(const RippleLayer& L, float s) {
    float worst = -1e9f;
    const int dirs[4][3] = {{1, 0, 0}, {0, 1, 0}, {1, 1, 1}, {1, -1, 1}};
    auto& m = const_cast<RippleLayer&>(L).mask();
    for (int z = 0; z < L.nz(); ++z) for (int x = 0; x < L.nx(); ++x) for (const auto& d : dirs) {
        const int xx = x + d[0], zz = z + d[1];
        if (!L.inBounds(xx, zz) || !m[L.idx(x, z)] || !m[L.idx(xx, zz)]) continue;
        worst = std::max(worst, std::abs(L.height(x, z) - L.height(xx, zz)) - s * kD * (d[2] ? 1.41421356f : 1.0f));
    }
    return worst;
}
double bodySum(const RippleLayer& L, int x0, int x1) {
    double sum = 0.0;
    for (int z = 0; z < L.nz(); ++z) for (int x = x0; x < x1; ++x) sum += L.height(x, z);
    return sum;
}
}  // namespace

// RC-T2 (WaterCore.md 25.26) - a ring on a current is carried by it: the same kick on still water (control) and on a uniform
// 0.5 m/s current along +x; after 2 s the ring's energy centroid sits u t = 1.0 m further downstream, within 10 %, and the
// ring keeps its energy within 15 % (advection moves it, it does not eat it).
TEST(WaterRippleTest, ARingRidesTheCurrent) {
    auto run = [](float u) {
        RippleLayer L({0, 0}, 160, 60);
        L.setOpenBorder(false);
        L.setBreakSlope(0.0f);
        L.addImpulse(50.5f * kD, 30.5f * kD, 50.0f);
        std::vector<glm::vec2> vel(static_cast<size_t>(160) * 60, glm::vec2(u, 0.0f));
        for (int k = 0; k < 120; ++k) { L.advect(vel, 1.0f / 60.0f); L.step(1.0f / 60.0f); }
        double e = 0.0, ex = 0.0;
        for (int z = 0; z < 60; ++z) for (int x = 0; x < 160; ++x) { const double h = L.height(x, z); e += h * h; ex += h * h * (x + 0.5) * kD; }
        return std::make_pair(ex / e, e);
    };
    const auto still = run(0.0f), moving = run(0.5f);
    std::printf("  centroid still %.3f m, on 0.5 m/s %.3f m: moved %.3f m (u t = 1.000); energy ratio %.3f\n", still.first, moving.first, moving.first - still.first, moving.second / still.second);
    EXPECT_NEAR(moving.first - still.first, 1.0, 0.1);
    EXPECT_NEAR(moving.second / still.second, 1.0, 0.15);
}

// B-T1 (22.12) - the breaking limit: a 0.6 m spike and a 0.3 m step (a stone entry's leftovers, 22.10) in a walled
// 6 x 6 m pond. Prediction: within 0.1 s (12 steps) no edge exceeds the limit by more than 1e-4 m, and the water's
// sum of r is unchanged (pairwise moves). Control: limit off - the edges stay steep.
TEST(WaterRippleTest, BreakingLimitsTheSlope) {
    auto make = [](float limit) {
        auto L = std::make_unique<RippleLayer>(glm::ivec2(0, 0), 54, 54);
        L->setOpenBorder(false); L->setBreakSlope(limit);
        std::vector<float> h(54 * 54, 0.0f);
        h[L->idx(20, 27)] = 0.6f;
        for (int z = 10; z < 44; ++z) for (int x = 34; x < 44; ++x) h[L->idx(x, z)] = 0.3f;
        double mean = 0.0; for (float v : h) mean += v; mean /= h.size();
        for (float& v : h) v -= static_cast<float>(mean);   // zero mean, as the layer holds it
        L->pin(h);
        return L;
    };
    auto on = make(RippleLayer::kBreakSlope), off = make(0.0f);
    const double sum0 = bodySum(*on, 0, 54);
    for (int s = 0; s < 12; ++s) { on->step(RippleLayer::kStep); off->step(RippleLayer::kStep); }
    const float exOn = worstEdgeExcess(*on, RippleLayer::kBreakSlope), exOff = worstEdgeExcess(*off, RippleLayer::kBreakSlope);
    const double sum1 = bodySum(*on, 0, 54);
    const double broken = on->takeBroken();
    std::printf("  worst edge over the limit after 0.1 s: on %.2e m, off %.3f m; sum %.6e -> %.6e; broken %.4f m^3\n", exOn, exOff, sum0, sum1, broken);
    EXPECT_LE(exOn, 1e-4f);
    EXPECT_GT(exOff, 0.01f) << "control: without the limit the field stays steep (measured 0.061 m past the limit - slope ~1.0; first guess 0.1 m was off)";
    EXPECT_NEAR(sum1, sum0, 1e-3) << "pairwise moves keep the water's sum (float rounding over 2916 cells)";
    EXPECT_GT(broken, 0.0);
}

// B-T3 (22.12) - no edge across a wall: two bodies split by a one-voxel wall; a steep field in the left one only.
// Prediction: the right body is exactly zero after the limit acts.
TEST(WaterRippleTest, BreakingStaysInItsBody) {
    RippleLayer L({0, 0}, 60, 30);
    L.setOpenBorder(false);
    for (int z = 0; z < 30; ++z) for (int x = 25; x < 34; ++x) L.mask()[L.idx(x, z)] = 0;
    std::vector<float> h(60 * 30, 0.0f);
    for (int z = 5; z < 25; ++z) for (int x = 15; x < 25; ++x) h[L.idx(x, z)] = 0.4f;   // a 0.4 m block against the wall
    L.pin(h);
    for (int s = 0; s < 3; ++s) L.step(RippleLayer::kStep);
    double right = 0.0;
    for (int z = 0; z < 30; ++z) for (int x = 34; x < 60; ++x) right = std::max(right, static_cast<double>(std::abs(L.height(x, z))));
    std::printf("  right body after 3 steps: max |r| %.3e; broken %.4f m^3\n", right, L.takeBroken());
    EXPECT_EQ(right, 0.0);
}

// B-T4 (22.12.1 F3) - the limit is chunk-blind: a steep spike at the x = 96 m seam and the same spike one chunk on
// (32 m = 288 lattice cells) -> bit-equal fields while the limit acts.
TEST(WaterRippleTest, BreakingIsChunkBlind) {
    RippleLayer a({860, 0}, 40, 40), b({860 + 288, 0}, 40, 40);
    std::vector<float> h(40 * 40, 0.0f);
    h[a.idx(4, 18)] = 0.5f; h[a.idx(3, 18)] = -0.3f;   // straddles lattice x = 864 = the 96 m seam
    a.pin(h); b.pin(h);
    for (int s = 0; s < 30; ++s) { a.step(RippleLayer::kStep); b.step(RippleLayer::kStep); }
    EXPECT_GT(a.takeBroken(), 0.0) << "the limit acted";
    EXPECT_EQ(a.heights(), b.heights());
}

// H1b-M0 COST (diagnostic): one ripple window per 60 Hz frame - flow forcing impulses on every water cell, advection on a
// sheared current, the step (2 substeps at 120 Hz). Masks: the bench creek (384 x 54, a 2 m band), a creek meandering across a
// full window (384 x 384, a 2 m band on a sine), and a full window of water (a pond-sized region).
TEST(WaterRippleTest, DISABLED_CostRippleWindow) {
    struct Case { const char* name; int nx, nz; int mode; };
    for (const Case cs : {Case{"bench creek 384 x 54", 384, 54, 0}, Case{"meander 384 x 384", 384, 384, 1}, Case{"full 384 x 384", 384, 384, 2}}) {
        RippleLayer L({0, 0}, cs.nx, cs.nz, 9.81f); L.setOpenBorder(true);
        std::vector<glm::vec2> vel(static_cast<size_t>(cs.nx) * cs.nz, glm::vec2(0.0f));
        long wet = 0;
        for (int z = 0; z < cs.nz; ++z) for (int x = 0; x < cs.nx; ++x) {
            const float centre = cs.mode == 0 ? cs.nz * 0.5f : cs.nz * 0.5f + 0.35f * cs.nz * std::sin(x * 6.2831853f / 300.0f);
            const bool w = cs.mode == 2 || std::abs(z + 0.5f - centre) < 9.0f;
            L.mask()[L.idx(x, z)] = w ? 1 : 0; wet += w;
            const float e = (z + 0.5f - centre) / 9.0f;
            vel[L.idx(x, z)] = glm::vec2(w ? 0.45f * std::max(0.0f, 1.0f - e * e) : 0.0f, 0.0f);
        }
        double ms = 0.0; int timed = 0;
        for (int k = 0; k < 60 * 12; ++k) {
            const auto c0 = std::chrono::steady_clock::now();
            for (int z = 0; z < cs.nz; ++z) for (int x = 0; x < cs.nx; ++x) {
                if (!L.mask()[L.idx(x, z)]) continue;
                uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(z) * 19349663u ^ static_cast<uint32_t>(k) * 83492791u;
                h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                L.addImpulse((x + 0.5f) * kD, (z + 0.5f) * kD, 0.2f * ((h & 0xFFFFu) / 32767.5f - 1.0f));
            }
            L.advect(vel, 1.0f / 60.0f); L.step(1.0f / 60.0f);
            if (k >= 120) { ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - c0).count(); ++timed; }
        }
        ms /= timed;
        std::printf("  %-22s %7d cells, %6ld water (%4.1f %%): %.3f ms per 60 Hz frame, %.1f ns per water cell\n",
                    cs.name, cs.nx * cs.nz, wet, 100.0 * wet / (cs.nx * cs.nz), ms, 1e6 * ms / std::max(1L, wet));
    }
}

// 25.34: addImpulseField (two separable 1 2 1 / 4 passes) equals one addImpulse per cell (the 3 x 3 binomial tent) away from
// the layer's edges - a random field of impulses on the interior, both layers stepped once: max difference ~ float rounding.
TEST(WaterRippleTest, ImpulseFieldEqualsPerCellImpulses) {
    RippleLayer a({0, 0}, 40, 30), b({0, 0}, 40, 30);
    a.setOpenBorder(false); b.setOpenBorder(false);
    std::vector<float> raw(40 * 30, 0.0f);
    for (int z = 2; z < 28; ++z) for (int x = 2; x < 38; ++x) {
        uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(z) * 19349663u;
        h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
        const float v = (h & 0xFFFFu) / 32767.5f - 1.0f;
        raw[a.idx(x, z)] = v;
        b.addImpulse((x + 0.5f) * kD, (z + 0.5f) * kD, v);
    }
    a.addImpulseField(raw);
    a.step(RippleLayer::kStep); b.step(RippleLayer::kStep);
    float worst = 0.0f, peak = 0.0f;
    for (int z = 0; z < 30; ++z) for (int x = 0; x < 40; ++x) { worst = std::max(worst, std::abs(a.height(x, z) - b.height(x, z))); peak = std::max(peak, std::abs(b.height(x, z))); }
    std::printf("  field vs per-cell impulses: max difference %.3e m of peak %.3e m\n", worst, peak);
    ASSERT_GT(peak, 0.0f);
    EXPECT_LE(worst, 1e-5f * peak);
}

}  // namespace Phyxel::Core::Water
