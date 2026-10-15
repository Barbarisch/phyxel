// WaterCore.md 25.13 H0 - MEASURE FIRST: can the shore column solver (ShoreSolver, §18.5) carry a creek? A sloped
// channel 30 m x 2 m at 1/3 m columns, a spring at the top and an outfall at the bottom (this harness supplies both and
// keeps its own ledger - the solver has neither yet). WS-1 steady flow vs Manning normal depth, WS-2 a dam, WS-3 a
// redirect, WS-11 the cost. Printed (a measurement), with only the ledger asserted: the numbers decide H1.
#include <gtest/gtest.h>
#include "core/water/ShoreSolver.h"
#include "core/water/WaterCore.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace Phyxel::Core::Water {
namespace {

constexpr float kH = 1.0f / 3.0f;
constexpr float kSlope = 1.0f / 50.0f;
constexpr float kN = 0.035f;   // Manning n, a clean natural stony stream (Chow, Open-Channel Hydraulics, 1959: 0.030-0.040)

// a wide channel's normal depth: q = (1/n) d^(5/3) S^(1/2)  ->  d = (q n / sqrt S)^(3/5)
double normalDepth(double q, double n, double S) { return std::pow(q * n / std::sqrt(S), 0.6); }

struct Creek {
    ShoreSolver s;
    int nzWet;
    double in = 0.0, out = 0.0, m0 = 0.0;
    std::vector<int> spring;    // column indices fed
    std::vector<int> outfall;   // column indices drained
    Creek(int nx, int nz, ShoreParams p) : s(glm::vec2(0.0f), nx, nz, kH, p), nzWet(nz - 2) {}
    // one tick: the spring adds Q dt spread over its columns (no momentum - water welling up), the outfall removes all
    // water above the bed in its columns (a free overfall at the channel's end)
    void tick(float dt, double Q) {
        if (!spring.empty()) {
            const double dEta = Q * dt / (kH * kH * spring.size());
            for (int i : spring) { s.col(i % s.nx(), i / s.nx()).eta += dEta; }
            in += Q * dt;
        }
        s.step(dt);
        for (int i : outfall) {
            ShoreColumn& c = s.col(i % s.nx(), i / s.nx());
            const double d = c.eta - c.bed;
            if (d > 0.0) { out += d * kH * kH; c.eta = c.bed; c.u = 0.0f; c.w = 0.0f; }
        }
    }
    double ledgerError() const { return s.totalMass() - (m0 + in - out); }
};

// a straight channel along x: walls at z = 0 and z = nz - 1, bed falling at kSlope from 10 m, quantised to `step` (0 =
// smooth), the spring in the first column, the outfall in the last
Creek straight(int nx, float step, ShoreParams p = ShoreParams{}) {
    p.manningN = kN;
    Creek c(nx, 8, p);
    for (int z = 0; z < 8; ++z) for (int x = 0; x < nx; ++x) {
        ShoreColumn& col = c.s.col(x, z);
        float bed = 10.0f - kSlope * (x + 0.5f) * kH;
        if (step > 0.0f) bed = std::floor(bed / step) * step;
        col.bed = bed;
        col.eta = bed;
        if (z == 0 || z == 7) { col.wall = 1; col.bed = 30.0f; col.eta = 30.0f; }
    }
    for (int z = 1; z < 7; ++z) { c.spring.push_back(static_cast<int>(c.s.idx(0, z))); c.outfall.push_back(static_cast<int>(c.s.idx(nx - 1, z))); }
    c.m0 = c.s.totalMass();
    return c;
}

// outflow rate over the last `window` seconds of `seconds`, and the mean depth along the middle third
struct Steady { double outRate = 0.0, depthMid = 0.0, speedMid = 0.0, wall = 0.0, ms = 0.0, ledger = 0.0; long steps = 0; };
Steady runSteady(Creek& c, double Q, float seconds, float window = 20.0f, float dt = 1.0f / 30.0f) {
    Steady r;
    double outAtWindow = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    const int ticks = static_cast<int>(seconds / dt);
    for (int k = 0; k < ticks; ++k) {
        if (k == static_cast<int>((seconds - window) / dt)) outAtWindow = c.out;
        c.tick(dt, Q);
        ++r.steps;
    }
    r.wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    r.ms = 1000.0 * r.wall / r.steps;
    r.outRate = (c.out - outAtWindow) / window;
    double dSum = 0.0, uSum = 0.0; int n = 0;
    for (int x = c.s.nx() / 3; x < 2 * c.s.nx() / 3; ++x) for (int z = 1; z < 7; ++z) { dSum += c.s.depth(x, z); uSum += c.s.col(x, z).u; ++n; }
    r.depthMid = dSum / n; r.speedMid = uSum / n;
    r.ledger = c.ledgerError();
    return r;
}

}  // namespace

// WS-1: steady flow. Prediction (25.12): outflow = inflow within 2 %, mid-reach depth within 15 % of Manning normal depth
// (smooth bed). The stepped beds are what voxel terrain gives - a staircase of weirs - reported, not predicted.
TEST(WaterCreekH0, WS1_SteadyFlow) {
    const double width = 6 * kH;   // 2 m
    for (double Q : {0.02, 0.1}) for (float step : {0.0f, 1.0f / 9.0f, 1.0f / 3.0f}) {
        Creek c = straight(90, step);
        const Steady r = runSteady(c, Q, 180.0f);
        const double dn = normalDepth(Q / width, kN, kSlope), un = Q / (width * dn);
        std::printf("  Q %.2f m^3/s, bed %s: out/in %.3f; mid depth %.4f m (Manning %.4f, x%.2f); mid speed %.3f m/s (Manning %.3f); ledger %.2e m^3\n",
                    Q, step == 0.0f ? "smooth  " : (step < 0.2f ? "1/9 step" : "1/3 step"), r.outRate / Q, r.depthMid, dn, r.depthMid / dn, r.speedMid, un, r.ledger);
        EXPECT_NEAR(r.ledger, 0.0, 1e-6 * (c.in + 1.0)) << "the harness ledger must close";
    }
}

// Front speed on a dry bed (the dam's spill seemed slow): a spring into the dry 30 m channel, the time until water first
// leaves the outfall, against the kinematic-wave front speed 5/3 u_n (Lighthill & Whitham 1955) - the front of a
// rising flow on a slope travels at the celerity of the normal-depth flow it carries.
TEST(WaterCreekH0, FrontSpeedOnADryBed) {
    for (double Q : {0.02, 0.1}) for (float dry : {0.001f, 0.0001f}) {
        ShoreParams p; p.dryDepth = dry;
        Creek c = straight(90, 0.0f, p);
        const float dt = 1.0f / 30.0f;
        float tFirst = -1.0f;
        for (int k = 0; k < 300 * 30 && tFirst < 0.0f; ++k) { c.tick(dt, Q); if (c.out > 1e-5) tFirst = (k + 1) * dt; }
        const double un = Q / (6 * kH * normalDepth(Q / (6 * kH), kN, kSlope));
        std::printf("  Q %.2f, dryDepth %.4f m: water reaches the end of 30 m after %.1f s (kinematic front 5/3 u_n: %.1f s)\n",
                    Q, dry, tFirst, 30.0 / (5.0 / 3.0 * un));
    }
}

// WS-2: a dam. Steady creek (Q 0.1, smooth bed), then the bed of one column line at x = 60 is raised 0.6 m (its water moved
// upstream first - a dam displaces, it does not destroy). Prediction: upstream storage grows at Q until the pool reaches
// the crest (outflow ~0), then spills and the outflow recovers to Q.
TEST(WaterCreekH0, WS2_Dam) {
    const double Q = 0.1;
    Creek c = straight(90, 0.0f);
    runSteady(c, Q, 120.0f);
    const float crest = c.s.col(60, 1).bed + 0.6f;
    for (int z = 1; z < 7; ++z) {
        ShoreColumn& d = c.s.col(60, z); ShoreColumn& up = c.s.col(59, z);
        up.eta += d.eta - d.bed; d.bed = crest; d.eta = crest; d.u = d.w = 0.0f;
    }
    auto upstreamVol = [&] { double v = 0.0; for (int x = 0; x < 60; ++x) for (int z = 1; z < 7; ++z) v += c.s.depth(x, z) * kH * kH; return v; };
    std::printf("  dam crest %.2f m; t, upstream storage (m^3), pool level at x=59 (m), outflow rate (m^3/s):\n", crest);
    const float dt = 1.0f / 30.0f;
    double prevOut = c.out, v0 = upstreamVol();
    for (int sec = 1; sec <= 240; ++sec) {
        for (int k = 0; k < 30; ++k) c.tick(dt, Q);
        if (sec % 20 == 0 || sec <= 3) {
            std::printf("    %3d s: storage %+.3f (Q t = %.3f), level %.3f, outflow %.4f\n", sec, upstreamVol() - v0, Q * sec, c.s.col(59, 3).eta, (c.out - prevOut));
        }
        prevOut = c.out;
    }
    std::printf("  ledger %.2e m^3\n", c.ledgerError());
    EXPECT_NEAR(c.ledgerError(), 0.0, 1e-6 * (c.in + 1.0));
}

// WS-3: a redirect. Two parallel channels A (z 1-3) and B (z 5-7) separated by a wall line at z 4; the spring feeds A's top.
// Steady, then "dig": open the wall at x 10-12 (bed lowered to A's bed) and block A at x 14 (bed raised 1 m). Prediction:
// B's outflow rises to Q, A's falls to 0.
TEST(WaterCreekH0, WS3_Redirect) {
    const double Q = 0.1;
    ShoreParams p; p.manningN = kN;
    Creek c(90, 9, p);
    for (int z = 0; z < 9; ++z) for (int x = 0; x < 90; ++x) {
        ShoreColumn& col = c.s.col(x, z);
        col.bed = 10.0f - kSlope * (x + 0.5f) * kH; col.eta = col.bed;
        if (z == 0 || z == 4 || z == 8) { col.wall = 1; col.bed = 30.0f; col.eta = 30.0f; }
    }
    for (int z = 1; z < 4; ++z) c.spring.push_back(static_cast<int>(c.s.idx(0, z)));
    std::vector<int> outA, outB;
    for (int z = 1; z < 4; ++z) outA.push_back(static_cast<int>(c.s.idx(89, z)));
    for (int z = 5; z < 8; ++z) outB.push_back(static_cast<int>(c.s.idx(89, z)));
    c.outfall = outA; c.outfall.insert(c.outfall.end(), outB.begin(), outB.end());
    c.m0 = c.s.totalMass();
    auto drained = [&](const std::vector<int>& cols) { double v = 0.0; for (int i : cols) { const ShoreColumn& k = c.s.col(i % 90, i / 90); v += std::max(0.0, k.eta - k.bed); } return v; };
    (void)drained;
    // track each branch's outflow separately by draining manually
    double outAsum = 0.0, outBsum = 0.0;
    auto tick = [&](float dt) {
        const double dEta = Q * dt / (kH * kH * c.spring.size());
        for (int i : c.spring) c.s.col(i % 90, i / 90).eta += dEta;
        c.in += Q * dt;
        c.s.step(dt);
        for (int pass = 0; pass < 2; ++pass) for (int i : (pass == 0 ? outA : outB)) {
            ShoreColumn& k = c.s.col(i % 90, i / 90);
            const double d = k.eta - k.bed;
            if (d > 0.0) { (pass == 0 ? outAsum : outBsum) += d * kH * kH; c.out += d * kH * kH; k.eta = k.bed; k.u = k.w = 0.0f; }
        }
    };
    const float dt = 1.0f / 30.0f;
    for (int k = 0; k < 120 * 30; ++k) tick(dt);
    std::printf("  before: A outflow %.4f, B outflow %.4f m^3/s (last s)\n", 0.0, 0.0);
    double a0 = outAsum, b0 = outBsum;
    for (int k = 0; k < 30; ++k) tick(dt);
    std::printf("  steady: A %.4f, B %.4f m^3/s\n", outAsum - a0, outBsum - b0);
    for (int x = 10; x <= 12; ++x) { ShoreColumn& w = c.s.col(x, 4); w.wall = 0; w.bed = c.s.col(x, 3).bed; w.eta = w.bed; }
    for (int z = 1; z < 4; ++z) { ShoreColumn& d = c.s.col(14, z); ShoreColumn& up = c.s.col(13, z); up.eta += d.eta - d.bed; d.bed += 1.0f; d.eta = d.bed; d.u = d.w = 0.0f; }
    for (int sec = 1; sec <= 180; ++sec) {
        a0 = outAsum; b0 = outBsum;
        for (int k = 0; k < 30; ++k) tick(dt);
        if (sec % 30 == 0 || sec <= 2) std::printf("    %3d s after the edit: A %.4f, B %.4f m^3/s\n", sec, outAsum - a0, outBsum - b0);
    }
    std::printf("  ledger %.2e m^3\n", c.ledgerError());
    EXPECT_NEAR(c.ledgerError(), 0.0, 1e-6 * (c.in + 1.0));
}

// WS-11 (cost): CPU ms per tick at 30 Hz for creeks of 30, 100 and 300 m (2 m wide, 1/3 m columns), steady flow -
// the CPU reference's cost per column; the GPU's is H1's question.
TEST(WaterCreekH0, WS11_Cost) {
    for (int len : {30, 100, 300}) {
        Creek c = straight(len * 3, 0.0f);
        runSteady(c, 0.1, 60.0f);   // fill to steady
        const Steady r = runSteady(c, 0.1, 10.0f, 5.0f);
        const long cols = static_cast<long>(len) * 3 * 6;
        std::printf("  %3d m creek: %ld wet columns, %.3f ms per 1/30 s tick (CPU, one thread), %.1f ns per column-tick\n",
                    len, cols, r.ms, 1e6 * r.ms / cols);
    }
}

// H0 part 2 - the 3-D core (WaterSolver, CPU) on the same 30 m creek, for comparison: 1/3 m cells, the bed a staircase of
// solid cells (the core sees voxels as cells - 0.6 m of fall over 30 m is two 1/3 m steps), the spring as sources over the
// top cells, a bottomless sink in the last column. Q 0.02 and 0.1 m^3/s: does it flow, how deep, at what cost.
TEST(WaterCreekH0, Core3D_SameCreek) {
    for (double Q : {0.02, 0.1}) for (float fr : {0.0f, SolverParams{}.pondFriction}) {
        const int nx = 90, ny = 6, nz = 8;
        WaterGrid g(GridSpec{glm::ivec3(0), glm::ivec3(nx, ny, nz), kH});
        auto bedCells = [](int x) { return static_cast<int>(std::floor((1.0f - kSlope * (x + 0.5f) * kH) / kH)) + 1; };   // 3 .. 1 solid layers
        SolidQuery q = [&](const glm::ivec3& c) -> Occ {
            if (c.x < 0 || c.y < 0 || c.z < 0 || c.x >= nx || c.y >= ny || c.z >= nz) return Occ::Solid;
            if (c.z == 0 || c.z == nz - 1) return Occ::Solid;
            return c.y < bedCells(c.x) ? Occ::Solid : Occ::Air;
        };
        SolverParams prm; prm.pondFriction = fr;
        WaterSolver s(g, q, prm);
        for (int z = 1; z < nz - 1; ++z) s.addSource({0, bedCells(0), z}, static_cast<float>(Q / 6.0));
        const size_t firstSink = s.sources().size();
        for (int z = 1; z < nz - 1; ++z) s.addSource({nx - 1, bedCells(nx - 1), z}, -1000.0f);
        const float dt = 1.0f / 30.0f;
        const auto t0 = std::chrono::steady_clock::now();
        double outPrev = 0.0, outRate = 0.0; int steps = 0;
        for (int k = 0; k < 180 * 30; ++k) {
            s.step(dt); ++steps;
            if (k == 160 * 30) { outPrev = 0.0; for (size_t i = firstSink; i < s.sources().size(); ++i) outPrev -= s.sources()[i].placedTotal; }
        }
        const double ms = 1000.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / steps;
        double out = 0.0; for (size_t i = firstSink; i < s.sources().size(); ++i) out -= s.sources()[i].placedTotal;
        outRate = (out - outPrev) / 20.0;
        double dSum = 0.0; int n = 0;
        for (int x = nx / 3; x < 2 * nx / 3; ++x) for (int z = 1; z < nz - 1; ++z) { double col = 0.0; for (int y = 0; y < ny; ++y) col += g.f(x, y, z); dSum += col * kH; ++n; }
        const double dn = normalDepth(Q / (6 * kH), kN, kSlope);
        std::printf("  3-D core, Q %.2f, friction %s: out/in %.3f (last 20 s); mid depth %.4f m (Manning %.4f); %.3f ms per tick (%d cells)\n",
                    Q, fr > 0.0f ? "on " : "off", outRate / Q, dSum / n, dn, ms, nx * ny * nz);
    }
}

}  // namespace Phyxel::Core::Water
