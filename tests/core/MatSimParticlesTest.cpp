// MatSimParticlesTest — docs/MaterialSimPlan.md §6f, experiment E1: MatSim's own particle fluid
// (Position Based Fluids). Predictions are written in §6f before the code: the 2|2 U-tube levels
// within max(1/2 cell, 3 cm) at 1/3 and 1/9 m, reaching 90 % in <= 3 s (physical reference 1.4 s),
// ledger exact; the control (no density iterations) leaves the far arm below 50 % of level.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/matsim/MatSimCore.h"
#include "core/matsim/MatSimParticles.h"

using namespace Phyxel::Core::MatSim;

namespace {

// The same 2|2 U-tube as MatSimCoreTest: box 5 x 6 x 1 m, a 1 m divider at x in [2, 3) m standing
// on a 1 m connector; the connector full, the left arm filled 4 m deep.
MatSimCore uTube(int cpm) {
    auto world = [](int mx, int my, int) { return (mx >= 18 && mx < 27 && my >= 9) ? Occ::Solid : Occ::Empty; };
    MatSimCore s;
    std::string why;
    Config cfg;
    cfg.pressure = false;   // the cells only hold water here; the particles move it
    EXPECT_TRUE(s.create({0, 0, 0}, {5 * cpm, 6 * cpm, cpm}, cpm, world, &why, cfg)) << why;
    s.fillBox(Mat::Water, {0, 0, 0}, {5 * cpm, cpm, cpm}, 1.0);
    s.fillBox(Mat::Water, {0, cpm, 0}, {2 * cpm, 4 * cpm, cpm}, 1.0);
    return s;
}

// Water height (m) above the connector in world-cell x in [x0, x1), from particles; 1 m deep box.
double armFromParticles(const MatSimParticles& ps, int cpm, uint32_t F, int x0, int x1) {
    uint64_t u = 0;
    for (const Particle& p : ps.particles())
        if (p.y >= cpm && p.x >= x0 && p.x < x1) u += p.units;
    return double(u) / F / double(cpm * cpm * cpm) / (double(x1 - x0) / cpm);
}
double armFromCells(const MatSimCore& s, int x0, int x1) {
    const int cpm = s.cellsPerM();
    uint64_t u = 0;
    for (int y = cpm; y < s.size().y; ++y)
        for (int z = 0; z < s.size().z; ++z)
            for (int x = x0; x < x1; ++x) {
                const Cell c = s.at({x, y, z});
                if (cellMaterial(c) == Mat::Water) u += cellFill(c);
            }
    return double(u) / s.full() / double(cpm * cpm * cpm) / (double(x1 - x0) / cpm);
}

struct UTubeRun {
    double t90 = -1, left = 0, right = 0, peakRight = 0, msPerFrame = 0;
    double firstPeakT = -1, secondPeakT = -1, meanRightLast4s = 0;
    size_t particles = 0;
    int maxSub = 0;
    bool ledgerExact = true;
    uint64_t lost = 0;
};

// Run the 2|2 U-tube for `seconds`, sampling the right arm (particles) every frame, then deposit.
UTubeRun runUTube(int cpm, ParticleConfig pc, double seconds) {
    UTubeRun r;
    MatSimCore s = uTube(cpm);
    const uint64_t total = s.ledger()[Mat::Water];
    MatSimParticles ps(pc);
    r.particles = ps.lift(s, s.origin(), s.size());
    const uint32_t F = s.full();
    const double dt = 1.0 / 60.0;
    const int frames = int(seconds * 60);
    std::vector<double> hr(static_cast<size_t>(frames));
    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; ++f) {
        ps.step(s, dt);
        r.maxSub = std::max(r.maxSub, ps.lastSubsteps());
        if (s.ledger()[Mat::Water] + ps.units(Mat::Water) != total) r.ledgerExact = false;
        hr[size_t(f)] = armFromParticles(ps, cpm, F, 3 * cpm, 5 * cpm);
        r.peakRight = std::max(r.peakRight, hr[size_t(f)]);
        if (r.t90 < 0 && hr[size_t(f)] >= 1.8) r.t90 = (f + 1) * dt;
    }
    r.msPerFrame = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
    // Slosh peaks: a sample above 2.15 m that is the maximum within +-0.5 s.
    for (int f = 30; f + 30 < frames; ++f) {
        if (hr[size_t(f)] < 2.15) continue;
        bool isMax = true;
        for (int k = f - 30; k <= f + 30 && isMax; ++k) isMax = hr[size_t(k)] <= hr[size_t(f)] && !(k < f && hr[size_t(k)] == hr[size_t(f)]);
        if (!isMax) continue;
        if (r.firstPeakT < 0) r.firstPeakT = (f + 1) * dt;
        else if (r.secondPeakT < 0) { r.secondPeakT = (f + 1) * dt; break; }
    }
    const int last = std::min(frames, 240);
    for (int f = frames - last; f < frames; ++f) r.meanRightLast4s += hr[size_t(f)] / last;
    r.lost = ps.deposit(s);
    if (s.ledger()[Mat::Water] != total) r.ledgerExact = false;
    r.left = armFromCells(s, 0, 2 * cpm);
    r.right = armFromCells(s, 3 * cpm, 5 * cpm);
    return r;
}

void print(const char* tag, int cpm, const UTubeRun& r) {
    std::printf("[MatSimParticles] %s 1/%d m: %zu particles, t90 %.2f s, peak right %.3f m, peaks at %.2f / %.2f s "
                "(period %.2f s, theory 4.49), mean right last 4 s %.3f m, deposited left %.3f right %.3f m (level 2.000), "
                "max substeps %d, %.1f ms/frame, ledger %s, lost %llu\n",
                tag, cpm, r.particles, r.t90, r.peakRight, r.firstPeakT, r.secondPeakT,
                r.secondPeakT > 0 ? r.secondPeakT - r.firstPeakT : -1.0, r.meanRightLast4s, r.left, r.right, r.maxSub,
                r.msPerFrame, r.ledgerExact ? "exact" : "BROKEN", (unsigned long long)r.lost);
}

} // namespace

// The U-tube's natural period: T = 2 pi sqrt(L_eff / 2g), L_eff = path lengths scaled by
// A_arm / A_section = 2 + 2 + 3 * (2/1) = 10 m -> 4.49 s. A real U-tube sloshes; "at rest within
// 10 s" (the first prediction in 6f) was wrong physics and is recorded as failed, not tested.
TEST(MatSimParticles, UTubeLevelsAtOneThirdMetre) {
    const int cpm = 3;
    const UTubeRun r = runUTube(cpm, {}, 20.0);
    print("U-tube", cpm, r);
    const double tol = std::max(0.5 / cpm, 0.03);
    EXPECT_TRUE(r.ledgerExact);
    EXPECT_EQ(r.lost, 0u);
    EXPECT_GT(r.t90, 0.0) << "the far arm never reached 90 %";
    EXPECT_LE(r.t90, 3.0);
    ASSERT_GT(r.secondPeakT, 0.0) << "no slosh: two peaks above 2.15 m expected";
    EXPECT_NEAR(r.secondPeakT - r.firstPeakT, 4.49, 0.2 * 4.49) << "slosh period vs U-tube theory";
    EXPECT_NEAR(r.meanRightLast4s, 2.0, tol);
    EXPECT_NEAR(r.left, r.right, tol);
    EXPECT_NEAR(r.right, 2.0, tol);
}

TEST(MatSimParticles, ControlWithoutDensityConstraintDoesNotLevel) {
    // No density iterations = no pressure: no particle ever rises into the far arm (measured on the
    // particles; the deposit's overflow search would spread a collapsed pile into both arms).
    // Both incompressibility solves off: the velocity (divergence) solve alone also carries
    // pressure (measured: with only it on, the far arm reached 1.22 m).
    ParticleConfig pc;
    pc.iterations = 0;
    pc.divergenceIterations = 0;
    const UTubeRun r = runUTube(3, pc, 4.0);
    print("control (0 iterations)", 3, r);
    EXPECT_TRUE(r.ledgerExact);
    EXPECT_LT(r.peakRight, 1.0);
}

// Diagnostics (run with --gtest_also_run_disabled_tests): 1 m (predicted crude: the connector is
// one particle tall) and 1/9 m (9,477 particles; slow in Debug).
TEST(MatSimParticles, DISABLED_UTubeOtherSizes) {
    const char* env = std::getenv("MATSIM_DIAG_CPM");
    for (int cpm : {1, 9}) {
        if (env && std::atoi(env) != cpm) continue;
        const UTubeRun r = runUTube(cpm, {}, 10.0);
        print("U-tube", cpm, r);
    }
}

// Diagnostic: the slosh. Right-arm height (particles) every 0.25 s for 40 s at 1/3 m; the
// U-tube's natural period is T = 2 pi sqrt(L_eff / 2g), L_eff = sum of path lengths scaled by
// A_arm / A_section = 2 + 2 + 3 * (2/1) = 10 m -> T = 4.49 s.
TEST(MatSimParticles, DISABLED_SloshTimeSeries) {
    const char* env = std::getenv("MATSIM_DIAG_CPM");
    const int cpm = env ? std::atoi(env) : 3;
    const char* visc = std::getenv("MATSIM_VISC");
    ParticleConfig pc;
    if (visc) pc.viscosity = float(std::atof(visc));
    if (const char* e = std::getenv("MATSIM_DIV")) pc.divergenceIterations = std::atoi(e);
    MatSimCore s = uTube(cpm);
    MatSimParticles ps(pc);
    ps.lift(s, s.origin(), s.size());
    const uint32_t F = s.full();
    std::string line;
    for (int f = 1; f <= 40 * 60; ++f) {
        ps.step(s, 1.0 / 60.0);
        if (f % 15 == 0) {
            char buf[32];
            std::snprintf(buf, sizeof buf, " %.2f", armFromParticles(ps, cpm, F, 3 * cpm, 5 * cpm));
            line += buf;
        }
        if (f % 240 == 0) {
            std::printf("[slosh] cpm %d visc %.3f t %5.1f-%5.1f s:%s | vmax %.2f m/s\n", cpm, pc.viscosity, (f - 240) / 60.0,
                        f / 60.0, line.c_str(), ps.maxSpeed() / cpm);
            line.clear();
        }
    }
}

TEST(MatSimParticles, DISABLED_WhoIsFast) {
    // After 20 s at 1/3 m: the speed distribution and the five fastest particles (position, speed).
    const int cpm = 3;
    MatSimCore s = uTube(cpm);
    if (std::getenv("MATSIM_STILL")) {   // a still tank instead: 5 x 6 x 1 m box, water 1.5 m deep, no divider
        std::string why;
        Config cfg;
        cfg.pressure = false;
        s = MatSimCore();
        const int depth = std::getenv("MATSIM_DEPTH") ? std::atoi(std::getenv("MATSIM_DEPTH")) : 1;   // metres
        s.create({0, 0, 0}, {5 * cpm, 6 * cpm, depth * cpm}, cpm, [](int, int, int) { return Occ::Empty; }, &why, cfg);
        s.fillBox(Mat::Water, {0, 0, 0}, {5 * cpm, std::getenv("MATSIM_FULLTANK") ? 6 * cpm : 3 * cpm / 2, depth * cpm}, 1.0);
    }
    ParticleConfig pc;
    if (const char* e = std::getenv("MATSIM_CFL")) pc.cflCells = float(std::atof(e));
    if (const char* e = std::getenv("MATSIM_G")) pc.gravity = float(std::atof(e));
    if (const char* e = std::getenv("MATSIM_OMEGA")) pc.omega = float(std::atof(e));
    if (const char* e = std::getenv("MATSIM_TENSILE")) pc.tensileK = float(std::atof(e));
    if (const char* e = std::getenv("MATSIM_ITER")) pc.iterations = std::atoi(e);
    if (const char* e = std::getenv("MATSIM_EPS")) pc.relaxation = float(std::atof(e));
    if (const char* e = std::getenv("MATSIM_VISC")) pc.viscosity = float(std::atof(e));
    MatSimParticles ps(pc);
    ps.lift(s, s.origin(), s.size());
    double peak = 0;
    std::vector<Particle> at19;
    for (int f = 0; f < 20 * 60; ++f) {
        ps.step(s, 1.0 / 60.0);
        peak = std::max(peak, armFromParticles(ps, cpm, s.full(), 3 * cpm, 5 * cpm));
        if (f == 19 * 60 - 1) at19 = ps.particles();
    }
    {
        std::vector<float> d;
        for (size_t i = 0; i < at19.size(); ++i) {
            const Particle &a0 = at19[i], &a1 = ps.particles()[i];
            d.push_back(std::sqrt((a1.x - a0.x) * (a1.x - a0.x) + (a1.y - a0.y) * (a1.y - a0.y) + (a1.z - a0.z) * (a1.z - a0.z)));
        }
        std::sort(d.begin(), d.end());
        std::printf("[fast] net displacement 19->20 s (cells): median %.3f p90 %.3f max %.3f\n", d[d.size() / 2],
                    d[d.size() * 9 / 10], d.back());
    }
    std::printf("[fast] iter %d eps %g visc %g: right %.3f m at 20 s, peak %.3f m\n", pc.iterations, pc.relaxation,
                pc.viscosity, armFromParticles(ps, cpm, s.full(), 3 * cpm, 5 * cpm), peak);
    std::vector<std::pair<float, size_t>> v;
    for (size_t i = 0; i < ps.particles().size(); ++i) {
        const Particle& p = ps.particles()[i];
        v.push_back({std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz) / cpm, i});
    }
    std::sort(v.rbegin(), v.rend());
    {
        std::vector<float> inner;
        const IVec3 n = s.size();
        for (const auto& [sp, i] : v) {
            const Particle& p = ps.particles()[i];
            if (p.x > 2 && p.x < n.x - 2 && p.z > 2 && p.z < n.z - 2 && p.y > 2) inner.push_back(sp);
        }
        std::sort(inner.begin(), inner.end());
        if (!inner.empty())
            std::printf("[fast] %zu particles >= 2 cells from side walls and floor: median %.4f m/s, max %.3f\n", inner.size(),
                        inner[inner.size() / 2], inner.back());
    }
    std::printf("[fast] speed m/s: max %.2f p99 %.3f p90 %.3f median %.4f\n", v[0].first, v[v.size() / 100].first,
                v[v.size() / 10].first, v[v.size() / 2].first);
    for (int k = 0; k < 8; ++k) {
        const Particle& p = ps.particles()[v[k].second];
        std::printf("[fast] %.2f m/s at cell (%.2f, %.2f, %.2f) v (%.2f, %.2f, %.2f)\n", v[k].first, p.x, p.y, p.z, p.vx / cpm,
                    p.vy / cpm, p.vz / cpm);
    }
}

TEST(MatSimParticles, DISABLED_FullBoxZeroGGrowth) {
    // A box completely full of particles on the exact lattice, gravity 0: nothing should move.
    const int cpm = 3;
    std::string why;
    Config cfg;
    cfg.pressure = false;
    MatSimCore s;
    s.create({0, 0, 0}, {15, 18, 3}, cpm, [](int, int, int) { return Occ::Empty; }, &why, cfg);
    s.fillBox(Mat::Water, {0, 0, 0}, {15, 18, 3}, 1.0);
    ParticleConfig pc;
    pc.gravity = 0.f;
    if (const char* e = std::getenv("MATSIM_TENSILE")) pc.tensileK = float(std::atof(e));
    MatSimParticles ps(pc);
    ps.lift(s, s.origin(), s.size());
    for (int f = 1; f <= 40; ++f) {
        ps.step(s, 1.0 / 60.0);
        if (f <= 10 || f % 5 == 0) std::printf("[growth] frame %2d max speed %.3e cells/s\n", f, ps.maxSpeed());
    }
}

TEST(MatSimParticles, DISABLED_LayerHeights) {
    // Still tank (5 x 6 x 1 m, 1.5 m of water) at 1/3 m, tensile off: mean y of each layer of 45
    // particles after 10 s, and the lattice it started on (0.5, 1.5, 2.5, 3.5).
    const int cpm = 3;
    std::string why;
    Config cfg;
    cfg.pressure = false;
    MatSimCore s;
    s.create({0, 0, 0}, {15, 18, 3}, cpm, [](int, int, int) { return Occ::Empty; }, &why, cfg);
    s.fillBox(Mat::Water, {0, 0, 0}, {15, 4, 3}, 1.0);
    ParticleConfig pc;
    pc.tensileK = 0.f;
    if (const char* e = std::getenv("MATSIM_ITER")) pc.iterations = std::atoi(e);
    MatSimParticles ps(pc);
    ps.lift(s, s.origin(), s.size());
    for (int f = 0; f < 600; ++f) ps.step(s, 1.0 / 60.0);
    std::vector<float> ys;
    for (const Particle& p : ps.particles()) ys.push_back(p.y);
    std::sort(ys.begin(), ys.end());
    for (size_t l = 0; l < 4; ++l) {
        double m = 0, lo = 1e9, hi = -1e9;
        for (size_t k = l * 45; k < (l + 1) * 45; ++k) { m += ys[k]; lo = std::min<double>(lo, ys[k]); hi = std::max<double>(hi, ys[k]); }
        std::printf("[layers] iter %d layer %zu: mean y %.3f cells (min %.3f max %.3f), lattice %.1f\n", pc.iterations, l, m / 45, lo, hi, l + 0.5);
    }
}

TEST(MatSimParticles, DISABLED_JitterVsDepthAndGravity) {
    // Still tank 15 x 18 x 3 cells at 1/3 m, water H cells deep; median and p90 speed (m/s) after
    // 10 s. Depth-dependence points at pressure propagation; gravity-scaling at the per-substep kick.
    for (float g : {9.81f, 2.45f})
        for (int H : {2, 4, 8}) {
            const int cpm = 3;
            std::string why;
            Config cfg;
            cfg.pressure = false;
            MatSimCore s;
            s.create({0, 0, 0}, {15, 18, 3}, cpm, [](int, int, int) { return Occ::Empty; }, &why, cfg);
            s.fillBox(Mat::Water, {0, 0, 0}, {15, H, 3}, 1.0);
            ParticleConfig pc;
            pc.gravity = g;
            pc.viscosity = std::getenv("MATSIM_VISC") ? float(std::atof(std::getenv("MATSIM_VISC"))) : 0.01f;
            if (const char* e = std::getenv("MATSIM_MINC")) pc.minC = float(std::atof(e));
            if (const char* e = std::getenv("MATSIM_DIV")) pc.divergenceIterations = std::atoi(e);
            if (const char* e = std::getenv("MATSIM_TENSILE")) pc.tensileK = float(std::atof(e));
            MatSimParticles ps(pc);
            ps.lift(s, s.origin(), s.size());
            for (int f = 0; f < 600; ++f) ps.step(s, 1.0 / 60.0);
            std::vector<float> v;
            for (const Particle& p : ps.particles()) v.push_back(std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz) / cpm);
            std::sort(v.begin(), v.end());
            double top = 0;
            for (const Particle& p : ps.particles()) top = std::max<double>(top, p.y);
            std::printf("[jitter] div %d minC %.2f top %.2f (filled to %d) g %.2f H %d cells: median %.3f p90 %.3f m/s (substeps %d)\n", pc.divergenceIterations, pc.minC, top, H, g, H, v[v.size() / 2],
                        v[v.size() * 9 / 10], ps.lastSubsteps());
        }
}

TEST(MatSimParticles, DISABLED_ProjectionConverges) {
    // A 6x6x6 cube of particles at 0.9-cell spacing (compressed) in the middle of an empty box,
    // gravity 0: one frame (one substep) with N iterations. A correct projection lowers max C.
    std::string why;
    Config cfg;
    cfg.pressure = false;
    MatSimCore s;
    s.create({0, 0, 0}, {30, 30, 30}, 3, [](int, int, int) { return Occ::Empty; }, &why, cfg);
    for (int it : {0, 1, 2, 4, 8, 16}) {
        ParticleConfig pc;
        pc.gravity = 0.f;
        pc.viscosity = 0.f;
        pc.iterations = it;
        MatSimParticles ps(pc);
        for (int a = 0; a < 6; ++a)
            for (int b = 0; b < 6; ++b)
                for (int c = 0; c < 6; ++c)
                    ps.addParticle({12.f + 0.9f * a, 12.f + 0.9f * b, 12.f + 0.9f * c, 0, 0, 0, s.full(), Mat::Water});
        float m0, x0, m1, x1;
        ps.densityError(s, m0, x0);
        ps.step(s, 1.0 / 60.0);
        ps.densityError(s, m1, x1);
        std::printf("[proj] iterations %2d: max C %.4f -> %.4f, mean |C| %.4f -> %.4f, max speed after %.2f cells/s\n", it, x0,
                    x1, m0, m1, ps.maxSpeed());
    }
}

// Dam break (the live rig's, 1 m deep): a 16 x 8 x 1 m channel, a 3 m wide x 3 m deep reservoir
// at x in [0, 3) m released at t = 0. Front = the furthest 1-cell x slice holding > 1 % of an
// 8 m x 1 m x 1-cell slice of water; speed timed from x 4.5 m to 12 m (the rig's 12.5 -> 20 m).
// Ritter (frictionless) 2 sqrt(g h0) = 10.85 m/s is an upper bound; prediction (6f): 70-105 % of
// Ritter, and 1/3 and 1/9 m within 10 % of each other (speed independent of cell size).
namespace {
double damBreakFront(int cpm, double* msPerFrame) {
    std::string why;
    Config cfg;
    cfg.pressure = false;
    MatSimCore s;
    EXPECT_TRUE(s.create({0, 0, 0}, {16 * cpm, 8 * cpm, cpm}, cpm, [](int, int, int) { return Occ::Empty; }, &why, cfg)) << why;
    s.fillBox(Mat::Water, {0, 0, 0}, {3 * cpm, 3 * cpm, cpm}, 1.0);
    MatSimParticles ps;
    ps.lift(s, s.origin(), s.size());
    const uint32_t F = s.full();
    const double thr = 0.01 * 8.0 * 1.0 * (1.0 / cpm) * double(F) * cpm * cpm * cpm;   // units in 1 % of the slice
    double ta = -1, tb = -1;
    const auto t0 = std::chrono::steady_clock::now();
    int frames = 0;
    for (; frames < 180 && tb < 0; ++frames) {
        ps.step(s, 1.0 / 60.0);
        std::vector<uint64_t> col(size_t(16 * cpm), 0);
        for (const Particle& p : ps.particles()) {
            const int c = int(std::floor(p.x));
            if (c >= 0 && c < 16 * cpm) col[size_t(c)] += p.units;
        }
        int front = 0;
        for (int c = 0; c < 16 * cpm; ++c) if (double(col[size_t(c)]) > thr) front = c + 1;
        const double fx = double(front) / cpm, t = (frames + 1) / 60.0;
        if (ta < 0 && fx >= 4.5) ta = t;
        if (tb < 0 && fx >= 12.0) tb = t;
    }
    *msPerFrame = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
    return (ta > 0 && tb > ta) ? 7.5 / (tb - ta) : -1.0;
}
} // namespace

TEST(MatSimParticles, DamBreakFrontAtOneThirdMetre) {
    double ms = 0;
    const double v = damBreakFront(3, &ms);
    const double ritter = 2.0 * std::sqrt(9.81 * 3.0);
    std::printf("[MatSimParticles] dam break 1/3 m: front %.2f m/s (Ritter %.2f, %.0f %%), %.1f ms/frame\n", v, ritter,
                100.0 * v / ritter, ms);
    EXPECT_GE(v, 0.70 * ritter);
    EXPECT_LE(v, 1.05 * ritter);
}

TEST(MatSimParticles, DISABLED_DamBreakOtherSizes) {
    const double ritter = 2.0 * std::sqrt(9.81 * 3.0);
    for (int cpm : {1, 9}) {
        double ms = 0;
        const double v = damBreakFront(cpm, &ms);
        std::printf("[MatSimParticles] dam break 1/%d m: front %.2f m/s (Ritter %.2f, %.0f %%), %.1f ms/frame\n", cpm, v, ritter,
                    100.0 * v / ritter, ms);
    }
}
