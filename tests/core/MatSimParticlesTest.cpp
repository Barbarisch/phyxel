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
                    ps.addParticle({12.f + 0.9f * a, 12.f + 0.9f * b, 12.f + 0.9f * c, 0, 0, 0, s.full(), Mat::Water, 0, 0, 0, 0});
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

TEST(MatSimParticles, DISABLED_AveragedSpeedAtRest) {
    // Still tank (15 x 18 x 3 cells, 4 cells of water) at 1/3 m, defaults: the distribution of
    // instantaneous vs time-averaged speed (m/s) at 5, 10, 20 s. Sets the settle threshold.
    const int cpm = 3;
    std::string why;
    Config cfg;
    cfg.pressure = false;
    MatSimCore s;
    s.create({0, 0, 0}, {15, 18, 3}, cpm, [](int, int, int) { return Occ::Empty; }, &why, cfg);
    s.fillBox(Mat::Water, {0, 0, 0}, {15, 4, 3}, 1.0);
    if (std::getenv("MATSIM_UTUBE")) s = uTube(cpm);   // the 2|2 U-tube instead (sloshing settles by ~30 s)
    ParticleConfig pc;
    if (const char* e = std::getenv("MATSIM_TAU")) pc.settleTau = float(std::atof(e));
    MatSimParticles ps(pc);
    ps.lift(s, s.origin(), s.size());
    const int last = std::getenv("MATSIM_UTUBE") ? 2400 : 1200;
    for (int f = 1; f <= last; ++f) {
        ps.step(s, 1.0 / 60.0);
        if (f == 300 || f == 600 || f == 1200 || f == 2400) {
            std::vector<float> v, a;
            for (const Particle& p : ps.particles()) {
                v.push_back(std::sqrt(p.vx * p.vx + p.vy * p.vy + p.vz * p.vz) / cpm);
                a.push_back(std::sqrt(p.ax * p.ax + p.ay * p.ay + p.az * p.az) / cpm);
            }
            std::sort(v.begin(), v.end());
            std::sort(a.begin(), a.end());
            const size_t n = v.size();
            std::printf("[avg] tau %.2f t %2d s: instant median %.3f p90 %.3f max %.3f | averaged median %.3f p90 %.3f p99 %.3f max %.3f m/s\n",
                        pc.settleTau, f / 60, v[n / 2], v[n * 9 / 10], v.back(), a[n / 2], a[n * 9 / 10], a[n * 99 / 100], a.back());
        }
    }
}

// E2 (6f): settle. Particles slower than 0.3 m/s (averaged over 1 s) and supported deposit into
// cells; the CA (pressure on) moves cell water from then on. Predictions, written first: a still
// tank is all cells within 5 s, ledger exact every frame, surface flat within 1/2 cell; the U-tube
// ends level within max(1/2 cell, 3 cm) with every particle settled.
namespace {
struct SettleRun {
    double allSettledAt = -1, rightAt3s = -1;
    bool ledgerExact = true;
    size_t left = 0;
};
SettleRun runSettle(MatSimCore& s, MatSimParticles& ps, double seconds) {
    SettleRun r;
    const uint64_t total = s.ledger()[Mat::Water] + ps.units(Mat::Water);
    for (int f = 0; f < int(seconds * 60); ++f) {
        ps.step(s, 1.0 / 60.0);
        ps.settle(s);
        s.step(1);
        if (s.ledger()[Mat::Water] + ps.units(Mat::Water) != total) r.ledgerExact = false;
        if (r.allSettledAt < 0 && ps.particles().empty()) r.allSettledAt = (f + 1) / 60.0;
        if (f + 1 == 180 && s.size().x == 15 && s.size().y == 18 && s.size().z == 3 && s.cellsPerM() == 3)   // the 2|2 U-tube box
            r.rightAt3s = armFromCells(s, 9, 15) + armFromParticles(ps, 3, s.full(), 9, 15);
    }
    r.left = ps.particles().size();
    return r;
}
// Per-column water height (cells) over the box; returns max - min over columns that hold water.
double surfaceSpread(const MatSimCore& s, int x0, int x1, double* mean) {
    double mn = 1e9, mx = -1e9, sum = 0;
    int n = 0;
    for (int z = 0; z < s.size().z; ++z)
        for (int x = x0; x < x1; ++x) {
            uint64_t u = 0;
            for (int y = 0; y < s.size().y; ++y) {
                const Cell c = s.at({x, y, z});
                if (cellMaterial(c) == Mat::Water) u += cellFill(c);
            }
            const double h = double(u) / s.full();
            mn = std::min(mn, h); mx = std::max(mx, h); sum += h; ++n;
        }
    *mean = sum / n;
    return mx - mn;
}
} // namespace

TEST(MatSimParticles, StillTankSettlesIntoCells) {
    const int cpm = 3;
    std::string why;
    MatSimCore s;
    ASSERT_TRUE(s.create({0, 0, 0}, {15, 18, 3}, cpm, [](int, int, int) { return Occ::Empty; }, &why)) << why;
    s.fillBox(Mat::Water, {0, 0, 0}, {15, 4, 3}, 1.0);
    ParticleConfig pc;
    pc.settleSpeed = 0.35f;
    MatSimParticles ps(pc);
    ps.lift(s, s.origin(), s.size());
    const SettleRun r = runSettle(s, ps, 8.0);
    double mean = 0;
    const double spread = surfaceSpread(s, 0, 15, &mean);
    std::printf("[MatSimParticles] still tank settle 1/3 m: all cells at %.2f s (%zu particles left), column height mean %.3f "
                "cells, spread %.3f cells, ledger %s\n", r.allSettledAt, r.left, mean, spread, r.ledgerExact ? "exact" : "BROKEN");
    EXPECT_TRUE(r.ledgerExact);
    EXPECT_GT(r.allSettledAt, 0.0);
    EXPECT_LE(r.allSettledAt, 5.0);
    EXPECT_NEAR(mean, 4.0, 1e-9);
    EXPECT_LE(spread, 0.5);
}

TEST(MatSimParticles, UTubeSettlesLevel) {
    const int cpm = 3;
    MatSimCore s = uTube(cpm);
    s.config().pressure = true;   // cell water keeps the P1c pressure rule
    ParticleConfig pc;
    pc.settleSpeed = 0.35f;
    MatSimParticles ps(pc);
    ps.lift(s, s.origin(), s.size());
    const SettleRun r = runSettle(s, ps, 40.0);
    const double hl = armFromCells(s, 0, 2 * cpm), hr = armFromCells(s, 3 * cpm, 5 * cpm);
    std::printf("[MatSimParticles] U-tube settle 1/3 m: right arm %.3f m at 3 s (cells + particles), all cells at %.2f s (%zu left), "
                "arms %.3f / %.3f m (level 2.000), ledger %s\n",
                r.rightAt3s, r.allSettledAt, r.left, hl, hr, r.ledgerExact ? "exact" : "BROKEN");
    EXPECT_GE(r.rightAt3s, 1.8) << "the particles, not the CA, must move the water (the CA alone took 29 s)";
    const double tol = std::max(0.5 / cpm, 0.03);
    EXPECT_TRUE(r.ledgerExact);
    EXPECT_GT(r.allSettledAt, 0.0);
    EXPECT_NEAR(hl, hr, tol);
    EXPECT_NEAR(hr, 2.0, tol);
}

// E2 (6f): wake. Water starts as CELLS; wake() lifts cell water that is about to move (head
// excess > 1/2 cell over an open neighbour, or falling), the particles move it, settle() puts it
// back. Predictions, written first: the dam break's front runs within 10 % of the all-particle
// 8.18 m/s at 1/3 m (control, wake off = the CA alone: ~4.5 m/s); the U-tube's far arm reaches
// >= 1.8 m by 3 s (control: the CA, ~0.2 m) and ends level; ledger exact every frame.
namespace {
ParticleConfig hybridConfig(bool wakeOn) {
    ParticleConfig pc;
    pc.settleSpeed = 0.35f;
    pc.wakeHeadCells = wakeOn ? 1.0f : 0.f;   // one cell of head excess; settle needs a surface level to 0.8 cell
    return pc;
}
// One hybrid frame: wake -> particles -> settle -> CA.
void hybridFrame(MatSimCore& s, MatSimParticles& ps) {
    ps.wake(s);
    ps.step(s, 1.0 / 60.0);
    ps.settle(s);
    s.step(1);
}
double hybridDamFront(bool wakeOn, bool* ledgerExact, size_t* peakParticles) {
    const int cpm = 3;
    std::string why;
    MatSimCore s;
    EXPECT_TRUE(s.create({0, 0, 0}, {16 * cpm, 8 * cpm, cpm}, cpm, [](int, int, int) { return Occ::Empty; }, &why)) << why;
    s.fillBox(Mat::Water, {0, 0, 0}, {3 * cpm, 3 * cpm, cpm}, 1.0);
    MatSimParticles ps(hybridConfig(wakeOn));
    const uint64_t total = s.ledger()[Mat::Water];
    const uint32_t F = s.full();
    const double thr = 0.01 * 8.0 * (1.0 / cpm) * double(F) * cpm * cpm * cpm;
    double ta = -1, tb = -1;
    *ledgerExact = true;
    *peakParticles = 0;
    for (int f = 0; f < 240 && tb < 0; ++f) {
        hybridFrame(s, ps);
        *peakParticles = std::max(*peakParticles, ps.particles().size());
        if (s.ledger()[Mat::Water] + ps.units(Mat::Water) != total) *ledgerExact = false;
        std::vector<uint64_t> col(size_t(16 * cpm), 0);
        for (const Particle& p : ps.particles()) {
            const int c = int(std::floor(p.x));
            if (c >= 0 && c < 16 * cpm) col[size_t(c)] += p.units;
        }
        for (int x = 0; x < 16 * cpm; ++x)
            for (int y = 0; y < 8 * cpm; ++y)
                for (int z = 0; z < cpm; ++z) {
                    const Cell c = s.at({x, y, z});
                    if (cellMaterial(c) == Mat::Water) col[size_t(x)] += cellFill(c);
                }
        int front = 0;
        for (int c = 0; c < 16 * cpm; ++c) if (double(col[size_t(c)]) > thr) front = c + 1;
        const double fx = double(front) / cpm, t = (f + 1) / 60.0;
        if (ta < 0 && fx >= 4.5) ta = t;
        if (tb < 0 && fx >= 12.0) tb = t;
    }
    return (ta > 0 && tb > ta) ? 7.5 / (tb - ta) : -1.0;
}
} // namespace

TEST(MatSimParticles, WakeDamBreakFromCells) {
    bool exact = false, exactOff = false;
    size_t peak = 0, peakOff = 0;
    const double v = hybridDamFront(true, &exact, &peak);
    const double vOff = hybridDamFront(false, &exactOff, &peakOff);
    std::printf("[MatSimParticles] dam break from cells 1/3 m: wake on %.2f m/s (peak %zu particles), wake off (CA) %.2f m/s; "
                "all-particle reference 8.18; ledger %s / %s\n", v, peak, vOff, exact ? "exact" : "BROKEN", exactOff ? "exact" : "BROKEN");
    EXPECT_TRUE(exact);
    EXPECT_TRUE(exactOff);
    EXPECT_NEAR(v, 8.18, 0.1 * 8.18);
    EXPECT_LT(vOff, 0.7 * 8.18) << "control: the CA alone must be clearly slower";
}

TEST(MatSimParticles, WakeUTubeFromCells) {
    for (bool wakeOn : {true, false}) {
        const int cpm = 3;
        MatSimCore s = uTube(cpm);
        s.config().pressure = true;
        MatSimParticles ps(hybridConfig(wakeOn));
        const uint64_t total = s.ledger()[Mat::Water];
        bool exact = true;
        double at3 = -1;
        size_t peak = 0;
        size_t trigC = 0, trigF = 0, trigP = 0, lifted = 0, settled = 0;
        for (int f = 0; f < 40 * 60; ++f) {
            lifted += ps.wake(s);
            trigC += ps.lastWakeStats().contact; trigF += ps.lastWakeStats().falling; trigP += ps.lastWakeStats().pressure;
            ps.step(s, 1.0 / 60.0);
            settled += ps.settle(s);
            s.step(1);
            if (wakeOn && (f + 1) % 60 == 0 && f < 20 * 60) {
                std::printf("[wake] t %2d s: %zu particles; this second lifted %zu cells, settled %zu; trigger cells contact %zu falling %zu pressure %zu\n",
                            (f + 1) / 60, ps.particles().size(), lifted, settled, trigC, trigF, trigP);
                trigC = trigF = trigP = lifted = settled = 0;
            }
            peak = std::max(peak, ps.particles().size());
            if (s.ledger()[Mat::Water] + ps.units(Mat::Water) != total) exact = false;
            if (f + 1 == 180) at3 = armFromCells(s, 9, 15) + armFromParticles(ps, cpm, s.full(), 9, 15);
        }
        size_t slow = 0, slowUnsup = 0;
        ps.settleBlockers(s, slow, slowUnsup);   // slowUnsup = groups still busy
        std::printf("[MatSimParticles]   at 40 s: %zu particles, %zu slow enough to settle, %zu groups busy\n",
                    ps.particles().size(), slow, slowUnsup);
        const double hl = armFromCells(s, 0, 6) + armFromParticles(ps, cpm, s.full(), 0, 6);
        const double hr = armFromCells(s, 9, 15) + armFromParticles(ps, cpm, s.full(), 9, 15);
        std::printf("[MatSimParticles] U-tube from cells 1/3 m, wake %s: right %.3f m at 3 s, at 40 s arms %.3f / %.3f m, "
                    "%zu particles left (peak %zu), ledger %s\n", wakeOn ? "on" : "off", at3, hl, hr, ps.particles().size(), peak,
                    exact ? "exact" : "BROKEN");
        EXPECT_TRUE(exact);
        if (wakeOn) {
            const double tol = std::max(0.5 / cpm, 0.03);
            EXPECT_GE(at3, 1.8);
            EXPECT_EQ(ps.particles().size(), 0u) << "the body must settle back into cells";
            EXPECT_NEAR(hl, hr, tol);
            EXPECT_NEAR(hr, 2.0, tol);
        } else {
            EXPECT_LT(at3, 1.0) << "control: the CA alone";
        }
    }
}

// 6i video (owner, 2026-11-07): spray settled on the ledge as a thin film. The next splash touched it, and the contact wake
// lifted it whole, one particle per cell at the cell centre: a grid of evenly spaced specks that snapped into view.
// Two knobs were tried. Both are REJECTED as defaults: the live-rig replica at 1/3 m did not settle in 36 s, or blew up
// (MaterialSimPlan 6i). These tests pin the default behaviour and what each knob does.
TEST(MatSimParticles, ThinFilmTouchedByADropLiftsUnlessTheContactKnobIsSet) {
    const int cpm = 3;
    for (float minFill : {0.0f, 0.5f}) {
        std::string why;
        MatSimCore s;
        ASSERT_TRUE(s.create({0, 0, 0}, {6 * cpm, 2 * cpm, 6 * cpm}, cpm, [](int, int, int) { return Occ::Empty; }, &why)) << why;
        const uint32_t F = s.full();
        s.fillBox(Mat::Water, {0, 0, 0}, {6 * cpm, 1, 6 * cpm}, 0.125);   // a film an eighth of a cell deep
        ParticleConfig pc = hybridConfig(true);
        pc.absorbGroupMax = 0;   // a lone drop would be absorbed; this test is about the lift
        pc.wakeContactMinFill = minFill;
        MatSimParticles ps(pc);
        ps.mutableParticles().push_back({9.5f, 1.5f, 9.5f, 0.f, -3.f, 0.f, F, Mat::Water});   // a drop just above it
        const uint64_t total = s.ledger()[Mat::Water] + ps.units(Mat::Water);
        const size_t lifted = ps.wake(s);
        std::printf("[MatSimParticles] film 1/8 cell over %d cells, a drop touching it, contact min fill %.2f: lifted %zu cells\n",
                    36 * cpm * cpm, minFill, lifted);
        EXPECT_EQ(lifted, minFill > 0.f ? 0u : size_t(36 * cpm * cpm)) << minFill;
        EXPECT_EQ(s.ledger()[Mat::Water] + ps.units(Mat::Water), total);
    }
}

TEST(MatSimParticles, LiftHeightCellCentreByDefaultCentroidUnderTheKnob) {
    const int cpm = 3;
    for (bool centroid : {false, true}) {
        std::string why;
        MatSimCore s;
        ASSERT_TRUE(s.create({0, 0, 0}, {6 * cpm, 2 * cpm, 6 * cpm}, cpm, [](int, int, int) { return Occ::Empty; }, &why)) << why;
        const uint32_t F = s.full();
        s.fillBox(Mat::Water, {0, 0, 0}, {6 * cpm, 1, 6 * cpm}, 1.0);   // a full layer...
        s.fillBox(Mat::Water, {0, 1, 0}, {6 * cpm, 1, 6 * cpm}, 0.5);   // ...under a half-full one
        ParticleConfig pc = hybridConfig(true);
        pc.absorbGroupMax = 0;   // a lone drop would be absorbed; this test is about the lift
        pc.liftAtCentroid = centroid;
        MatSimParticles ps(pc);
        ps.mutableParticles().push_back({9.5f, 2.5f, 9.5f, 0.f, -3.f, 0.f, F, Mat::Water});
        const uint64_t total = s.ledger()[Mat::Water] + ps.units(Mat::Water);
        const size_t lifted = ps.wake(s);
        float yHalf = -1.f, yFull = -1.f;
        for (const Particle& q : ps.particles()) {
            if (q.units == F / 2 && yHalf < 0) yHalf = q.y;
            if (q.units == F && q.y < 1.f && yFull < 0) yFull = q.y;
        }
        EXPECT_EQ(lifted, size_t(2 * 36 * cpm * cpm)) << "a body deep enough to move wakes whole";
        EXPECT_FLOAT_EQ(yFull, 0.5f);
        EXPECT_FLOAT_EQ(yHalf, centroid ? 1.25f : 1.5f);
        EXPECT_EQ(s.ledger()[Mat::Water] + ps.units(Mat::Water), total);
    }
}

// 6i speck fix: a woken thin film is packed into particles of about one cell's water each (at the
// water's horizontal centroid, at the cell centre's height), not one sliver particle per cell.
TEST(MatSimParticles, WokenThinFilmIsPackedIntoCellSizedParticles) {
    const int cpm = 3;
    for (float pack : {0.0f, 0.5f}) {
        std::string why;
        MatSimCore s;
        ASSERT_TRUE(s.create({0, 0, 0}, {6 * cpm, 2 * cpm, 6 * cpm}, cpm, [](int, int, int) { return Occ::Empty; }, &why)) << why;
        const uint32_t F = s.full();
        s.fillBox(Mat::Water, {0, 0, 0}, {6 * cpm, 1, 6 * cpm}, 0.125);   // a film an eighth of a cell deep
        ParticleConfig pc = hybridConfig(true);
        pc.absorbGroupMax = 0;   // a lone drop would be absorbed; this test is about the lift
        pc.packThinBelow = pack;
        MatSimParticles ps(pc);
        ps.mutableParticles().push_back({9.5f, 1.5f, 9.5f, 0.f, -3.f, 0.f, F, Mat::Water});   // a drop just above it
        const uint64_t total = s.ledger()[Mat::Water] + ps.units(Mat::Water);
        const size_t lifted = ps.wake(s);
        const size_t made = ps.particles().size() - 1;
        uint32_t minU = ~0u, maxU = 0;
        bool heightOk = true, insideOk = true;
        for (size_t i = 1; i < ps.particles().size(); ++i) {
            const Particle& q = ps.particles()[i];
            minU = std::min(minU, q.units); maxU = std::max(maxU, q.units);
            heightOk = heightOk && std::fabs(q.y - 0.5f) < 1e-4f;
            insideOk = insideOk && q.x > 0.f && q.x < 6.f * cpm && q.z > 0.f && q.z < 6.f * cpm;
        }
        std::printf("[MatSimParticles] film 1/8 cell over %d cells woken, pack below %.2f: %zu cells -> %zu particles, units %.3f..%.3f cell\n",
                    36 * cpm * cpm, pack, lifted, made, double(minU) / F, double(maxU) / F);
        EXPECT_EQ(lifted, size_t(36 * cpm * cpm));
        EXPECT_EQ(s.ledger()[Mat::Water] + ps.units(Mat::Water), total);
        EXPECT_TRUE(heightOk);
        EXPECT_TRUE(insideOk);
        if (pack > 0.f) {
            // Clusters are bounded to 2 cells from their seed (no particle inside a solid, PackedParticlesNeverStartInsideSolid),
            // so a few leftover pieces stay under a cell: measured 44 particles, smallest a quarter cell.
            EXPECT_LE(made, size_t(36 * cpm * cpm / 8 * 12 / 10)) << "about one particle per cell of water";
            EXPECT_GE(minU, F / 4) << "no sliver particles (under a quarter cell)";
            EXPECT_LE(maxU, 2 * F);
        } else {
            EXPECT_EQ(made, size_t(36 * cpm * cpm));   // the control: one sliver per cell
        }
    }
}

// A lone thin cell has nothing to pack with: it lifts as itself, mass exact.
TEST(MatSimParticles, LoneThinCellLiftsAsItself) {
    const int cpm = 3;
    std::string why;
    MatSimCore s;
    ASSERT_TRUE(s.create({0, 0, 0}, {6 * cpm, 2 * cpm, 6 * cpm}, cpm, [](int, int, int) { return Occ::Empty; }, &why)) << why;
    const uint32_t F = s.full();
    s.fillBox(Mat::Water, {9, 0, 9}, {1, 1, 1}, 0.125);
    ParticleConfig pc = hybridConfig(true);
    pc.absorbGroupMax = 0;   // a lone drop would be absorbed; this test is about the lift
    MatSimParticles ps(pc);
    ps.mutableParticles().push_back({9.5f, 1.5f, 9.5f, 0.f, -3.f, 0.f, F, Mat::Water});
    const uint64_t total = s.ledger()[Mat::Water] + ps.units(Mat::Water);
    EXPECT_EQ(ps.wake(s), 1u);
    ASSERT_EQ(ps.particles().size(), 2u);
    EXPECT_EQ(ps.particles()[1].units, F / 8);
    EXPECT_EQ(s.ledger()[Mat::Water] + ps.units(Mat::Water), total);
}

// 6i: a particle at rest on a film does not lift it (it settles into it itself); a moving one does.
// Without the gate, two packed films on the live-rig ledge lifted each other in turn forever.
TEST(MatSimParticles, RestingParticleDoesNotWakeAFilmItTouches) {
    const int cpm = 3;
    for (float minSpeed : {0.35f, 0.0f}) {
        std::string why;
        MatSimCore s;
        ASSERT_TRUE(s.create({0, 0, 0}, {6 * cpm, 2 * cpm, 6 * cpm}, cpm, [](int, int, int) { return Occ::Empty; }, &why)) << why;
        const uint32_t F = s.full();
        s.fillBox(Mat::Water, {0, 0, 0}, {6 * cpm, 1, 6 * cpm}, 0.125);
        ParticleConfig pc = hybridConfig(true);
        pc.absorbGroupMax = 0;   // a lone drop would be absorbed; this test is about the lift
        pc.wakeContactMinSpeed = minSpeed;
        MatSimParticles ps(pc);
        ps.mutableParticles().push_back({9.5f, 1.5f, 9.5f, 0.f, 0.3f, 0.f, F, Mat::Water});   // at rest (0.1 m/s)
        EXPECT_EQ(ps.wake(s), minSpeed > 0.f ? 0u : size_t(36 * cpm * cpm)) << minSpeed;
    }
}

// 6i: a drop landing on resting cell water joins it (absorbGroupMax) instead of waking the whole body;
// a real inflow (a group bigger than the limit) still wakes it.
TEST(MatSimParticles, DropJoinsAPoolButAnInflowWakesIt) {
    const int cpm = 3;
    for (int absorb : {8, 0}) {
        for (int side : {1, 3}) {   // a 1-particle drop, a 3 x 3 x 3 inflow
            std::string why;
            MatSimCore s;
            ASSERT_TRUE(s.create({0, 0, 0}, {6 * cpm, 3 * cpm, 6 * cpm}, cpm, [](int, int, int) { return Occ::Empty; }, &why)) << why;
            const uint32_t F = s.full();
            s.fillBox(Mat::Water, {0, 0, 0}, {6 * cpm, 2, 6 * cpm}, 1.0);   // a pool two cells deep, at rest
            ParticleConfig pc = hybridConfig(true);
            pc.absorbGroupMax = absorb;
            MatSimParticles ps(pc);
            for (int a = 0; a < side; ++a)
                for (int b = 0; b < side; ++b)
                    for (int c = 0; c < side; ++c)
                        ps.mutableParticles().push_back({8.5f + a, 2.5f + b, 8.5f + c, 0.f, -6.f, 0.f, F, Mat::Water});   // 2 m/s down
            const uint64_t total = s.ledger()[Mat::Water] + ps.units(Mat::Water);
            const size_t lifted = ps.wake(s);
            const bool expectWake = absorb == 0 || side * side * side > absorb;
            EXPECT_EQ(lifted, expectWake ? size_t(2 * 36 * cpm * cpm) : 0u) << "absorb " << absorb << " side " << side;
            if (!expectWake) {
                ps.settle(s);
                EXPECT_EQ(ps.particles().size(), 0u) << "the drop joined the pool";
                EXPECT_EQ(ps.lastSettleStats().absorbed, 1u);
            }
            EXPECT_EQ(s.ledger()[Mat::Water] + ps.units(Mat::Water), total);
        }
    }
}

// 6i: a packed particle starts inside a cell its water came from, never inside a solid. Unbounded clusters followed a
// film from a ledge top down its wall and put the centroid inside the ledge (live-rig replica, y 53.5).
TEST(MatSimParticles, PackedParticlesNeverStartInsideSolid) {
    const int cpm = 3;
    std::string why;
    MatSimCore s;
    // A 6 x 6 x 6-cell solid block in a 24 x 12 x 24-cell box (micro coords: 1 cell = 3 micro at 1/3 m).
    auto occ = [](int mx, int my, int mz) {
        const int x = mx / 3, y = my / 3, z = mz / 3;
        return (x >= 6 && x < 12 && y < 6 && z >= 6 && z < 12) ? Occ::Solid : Occ::Empty;
    };
    ASSERT_TRUE(s.create({0, 0, 0}, {24, 12, 24}, cpm, occ, &why)) << why;
    const uint32_t F = s.full();
    const double thin = 1.0 / 64.0;
    s.fillBox(Mat::Water, {6, 6, 6}, {6, 1, 6}, thin);       // on the block's top
    s.fillBox(Mat::Water, {12, 0, 6}, {1, 7, 6}, thin);      // down its +x wall
    s.fillBox(Mat::Water, {13, 0, 6}, {6, 1, 6}, thin);      // out over the floor
    std::vector<uint8_t> wasWater(24 * 12 * 24, 0);
    for (int y = 0; y < 12; ++y) for (int z = 0; z < 24; ++z) for (int x = 0; x < 24; ++x)
        wasWater[size_t(x) + 24 * (size_t(z) + 24 * size_t(y))] = cellFill(s.at({x, y, z})) > 0;
    ParticleConfig pc = hybridConfig(true);
    pc.absorbGroupMax = 0;   // a lone drop would be absorbed; this test is about the lift
    pc.packThinBelow = 0.5f;   // the knob under test (off by default)
    MatSimParticles ps(pc);
    ps.mutableParticles().push_back({9.5f, 7.5f, 9.5f, 0.f, -6.f, 0.f, F, Mat::Water});
    ps.mutableParticles().push_back({9.5f, 8.5f, 9.5f, 0.f, -6.f, 0.f, F, Mat::Water});
    for (int k = 0; k < 8; ++k) ps.mutableParticles().push_back({8.5f + k % 3, 9.5f + k / 3, 8.5f, 0.f, -6.f, 0.f, F, Mat::Water});   // a 10-particle inflow
    const uint64_t total = s.ledger()[Mat::Water] + ps.units(Mat::Water);
    const size_t before = ps.particles().size();
    const size_t lifted = ps.wake(s);
    EXPECT_GT(lifted, 0u);
    size_t bad = 0;
    for (size_t i = before; i < ps.particles().size(); ++i) {
        const Particle& q = ps.particles()[i];
        const int x = int(std::floor(q.x)), y = int(std::floor(q.y)), z = int(std::floor(q.z));
        const bool ok = x >= 0 && y >= 0 && z >= 0 && x < 24 && y < 12 && z < 24 && wasWater[size_t(x) + 24 * (size_t(z) + 24 * size_t(y))];
        if (!ok) { ++bad; std::printf("[MatSimParticles] packed particle at (%.2f %.2f %.2f) is not in a cell its water came from\n", q.x, q.y, q.z); }
    }
    std::printf("[MatSimParticles] film 1/64 cell over a block and down its wall: %zu cells -> %zu particles, %zu misplaced\n",
                lifted, ps.particles().size() - before, bad);
    EXPECT_EQ(bad, 0u);
    EXPECT_EQ(s.ledger()[Mat::Water] + ps.units(Mat::Water), total);
}

// 6i: only cells at least wakePressureMinFill full trigger the pressure wake: a part-filled cell is a free surface.
// Rig: a 2 x 8 x 2-cell column released on an empty floor, 3 CA ticks: its base spreads as a thin film whose cells
// still read the column's head. Control (knob 0): thin cells trigger too. Full cells at the base trigger in both.
TEST(MatSimParticles, ThinCellsDoNotTriggerThePressureWake) {
    const int cpm = 3;
    size_t trig[2] = {0, 0}, thinCells = 0;
    for (int k = 0; k < 2; ++k) {
        std::string why;
        MatSimCore s;
        ASSERT_TRUE(s.create({0, 0, 0}, {18, 12, 18}, cpm, [](int, int, int) { return Occ::Empty; }, &why)) << why;
        const uint32_t F = s.full();
        s.fillBox(Mat::Water, {8, 0, 8}, {2, 8, 2}, 1.0);
        for (int t = 0; t < 3; ++t) s.step(1);
        if (k == 0)
            for (int z = 0; z < 18; ++z) for (int x = 0; x < 18; ++x) {
                const uint32_t f = cellFill(s.at({x, 0, z}));
                thinCells += f > 0 && f < F / 2;
            }
        ParticleConfig pc = hybridConfig(true);
        pc.wakePressureMinFill = k == 0 ? 0.5f : 0.0f;
        pc.absorbGroupMax = 0;
        MatSimParticles ps(pc);
        const uint64_t total = s.ledger()[Mat::Water];
        ps.wake(s);
        trig[k] = ps.lastWakeStats().pressure;
        EXPECT_EQ(s.ledger()[Mat::Water] + ps.units(Mat::Water), total);
    }
    std::printf("[MatSimParticles] column released 3 ticks: %zu thin floor cells; pressure triggers %zu (min fill 0.5) vs %zu (any cell)\n",
                thinCells, trig[0], trig[1]);
    ASSERT_GT(thinCells, 0u) << "the rig must have a thin film";
    EXPECT_LT(trig[0], trig[1]) << "thin cells no longer trigger";
}
