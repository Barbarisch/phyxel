// MatSimParticlesGpuTest — docs/MaterialSimPlan.md §6h: the GPU port of MatSimParticles::step.
// Not bit-exact (float), so it is accepted on the physics the CPU reference measured (§6f/§6g),
// on the same rigs. Predictions, written before the first run:
//   - tracking: after 1 frame of the 1/3 m U-tube every particle is within 1e-3 cells of the CPU's;
//     after 30 frames within 0.1 cells (the fluid is chaotic, so it is a short-horizon check);
//   - the U-tube at 1/3 and 1/9 m: 90 % in <= 3 s, period within 20 % of 4.49 s, level within
//     max(1/2 cell, 3 cm), ledger exact (CPU: 1.1-1.2 s, 4.23-4.35 s);
//   - the dam break at 1/3 and 1/9 m: 70-105 % of Ritter and within 5 % of the CPU's 8.18 m/s;
//   - E2 hybrid at 1/9 m (never run before: the CPU took ~1.4 s a frame): a U-tube starting as
//     cells reaches >= 1.8 m in the far arm by 3 s and ends as cells, level, ledger exact. The
//     settle thresholds were fitted at 1/3 m, so this one may fail; that is data.
// Negative control: MATSIM_GPU_STUB=1 records no substeps (particles freeze); these tests must fail.
// Needs a Vulkan device: skipped without one (never a pass).
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"
#include "core/matsim/MatSimCore.h"
#include "core/matsim/MatSimParticles.h"
#include "core/matsim/MatSimParticlesGpu.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Phyxel::Core::MatSim;
using Phyxel::Testing::VulkanTestFixture;

namespace {

// The 2|2 U-tube of MatSimParticlesTest: box 5 x 6 x 1 m, a 1 m divider at x in [2, 3) m on a 1 m
// connector; the connector full, the left arm 4 m deep.
MatSimCore uTube(int cpm, bool pressure) {
    auto world = [](int mx, int my, int) { return (mx >= 18 && mx < 27 && my >= 9) ? Occ::Solid : Occ::Empty; };
    MatSimCore s;
    std::string why;
    Config cfg;
    cfg.pressure = pressure;
    EXPECT_TRUE(s.create({0, 0, 0}, {5 * cpm, 6 * cpm, cpm}, cpm, world, &why, cfg)) << why;
    s.fillBox(Mat::Water, {0, 0, 0}, {5 * cpm, cpm, cpm}, 1.0);
    s.fillBox(Mat::Water, {0, cpm, 0}, {2 * cpm, 4 * cpm, cpm}, 1.0);
    return s;
}
double armParticles(const MatSimParticles& ps, int cpm, uint32_t F, int x0, int x1) {
    uint64_t u = 0;
    for (const Particle& p : ps.particles())
        if (p.y >= cpm && p.x >= x0 && p.x < x1) u += p.units;
    return double(u) / F / double(cpm * cpm * cpm) / (double(x1 - x0) / cpm);
}
double armCells(const MatSimCore& s, int x0, int x1) {
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
    double t90 = -1, firstPeakT = -1, secondPeakT = -1, meanRightLast4s = 0, left = 0, right = 0;
    double msPerFrame = 0, gpuMsPerFrame = 0;
    bool ledgerExact = true;
    uint64_t lost = 0;
    size_t particles = 0;
};
UTubeRun runUTube(int cpm, MatSimParticlesGpu* gpu, double seconds) {
    UTubeRun r;
    MatSimCore s = uTube(cpm, false);
    const uint64_t total = s.ledger()[Mat::Water];
    MatSimParticles ps;
    ps.setGpu(gpu);
    r.particles = ps.lift(s, s.origin(), s.size());
    const uint32_t F = s.full();
    const int frames = int(seconds * 60);
    std::vector<double> hr(static_cast<size_t>(frames));
    const auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < frames; ++f) {
        ps.step(s, 1.0 / 60.0);
        if (gpu) r.gpuMsPerFrame += gpu->lastGpuMs() / frames;
        if (s.ledger()[Mat::Water] + ps.units(Mat::Water) != total) r.ledgerExact = false;
        hr[size_t(f)] = armParticles(ps, cpm, F, 3 * cpm, 5 * cpm);
        if (r.t90 < 0 && hr[size_t(f)] >= 1.8) r.t90 = (f + 1) / 60.0;
    }
    r.msPerFrame = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / frames;
    for (int f = 30; f + 30 < frames; ++f) {   // slosh peaks, as MatSimParticlesTest
        if (hr[size_t(f)] < 2.15) continue;
        bool isMax = true;
        for (int k = f - 30; k <= f + 30 && isMax; ++k) isMax = hr[size_t(k)] <= hr[size_t(f)] && !(k < f && hr[size_t(k)] == hr[size_t(f)]);
        if (!isMax) continue;
        if (r.firstPeakT < 0) r.firstPeakT = (f + 1) / 60.0;
        else if (r.secondPeakT < 0) { r.secondPeakT = (f + 1) / 60.0; break; }
    }
    const int last = std::min(frames, 240);
    for (int f = frames - last; f < frames; ++f) r.meanRightLast4s += hr[size_t(f)] / last;
    r.lost = ps.deposit(s);
    if (s.ledger()[Mat::Water] != total) r.ledgerExact = false;
    r.left = armCells(s, 0, 2 * cpm);
    r.right = armCells(s, 3 * cpm, 5 * cpm);
    return r;
}

double damBreakFront(int cpm, MatSimParticlesGpu* gpu, double* msPerFrame) {
    std::string why;
    Config cfg;
    cfg.pressure = false;
    MatSimCore s;
    EXPECT_TRUE(s.create({0, 0, 0}, {16 * cpm, 8 * cpm, cpm}, cpm, [](int, int, int) { return Occ::Empty; }, &why, cfg)) << why;
    s.fillBox(Mat::Water, {0, 0, 0}, {3 * cpm, 3 * cpm, cpm}, 1.0);
    MatSimParticles ps;
    ps.setGpu(gpu);
    ps.lift(s, s.origin(), s.size());
    const uint32_t F = s.full();
    const double thr = 0.01 * 8.0 * 1.0 * (1.0 / cpm) * double(F) * cpm * cpm * cpm;
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
    *msPerFrame = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / std::max(frames, 1);
    return (ta > 0 && tb > ta) ? 7.5 / (tb - ta) : -1.0;
}

} // namespace

class MatSimParticlesGpuTest : public VulkanTestFixture {
protected:
    MatSimParticlesGpu gpu;
    void SetUp() override {
        VulkanTestFixture::SetUp();
        if (!isVulkanAvailable()) GTEST_SKIP() << "no Vulkan device";
        std::string err;
        ASSERT_TRUE(gpu.init(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
    }
    void TearDown() override {
        gpu.shutdown();
        VulkanTestFixture::TearDown();
    }
};

TEST_F(MatSimParticlesGpuTest, TracksTheCpuOverTheFirstFrames) {
    const int cpm = 3;
    MatSimCore sc = uTube(cpm, false), sg = uTube(cpm, false);
    MatSimParticles pc, pg;
    pc.lift(sc, sc.origin(), sc.size());
    pg.lift(sg, sg.origin(), sg.size());
    pg.setGpu(&gpu);
    double d1 = 0, d30 = 0, moved1 = 0;
    for (int f = 1; f <= 30; ++f) {
        pc.step(sc, 1.0 / 60.0);
        pg.step(sg, 1.0 / 60.0);
        ASSERT_EQ(pc.lastSubsteps(), pg.lastSubsteps()) << "frame " << f;
        double d = 0;
        for (size_t i = 0; i < pc.particles().size(); ++i) {
            const Particle &a = pc.particles()[i], &b = pg.particles()[i];
            d = std::max(d, double(std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z)));
            if (f == 1) moved1 = std::max(moved1, double(std::fabs(a.y - (std::floor(a.y) + 0.5f))));
        }
        if (f == 1) d1 = d;
        if (f == 30) d30 = d;
    }
    std::printf("[MatSimParticlesGpu] tracking 1/3 m U-tube: max |gpu - cpu| (L1, cells) %.2e after 1 frame "
                "(the CPU moved up to %.3f cells), %.2e after 30 frames\n", d1, moved1, d30);
    EXPECT_GT(moved1, 1e-3) << "the CPU reference must move in frame 1, or tracking proves nothing";
    EXPECT_LT(d1, 1e-3);
    EXPECT_LT(d30, 0.1);
}

TEST_F(MatSimParticlesGpuTest, UTubeLevelsAtBothSizes) {
    for (int cpm : {3, 9}) {
        const UTubeRun r = runUTube(cpm, &gpu, 20.0);
        const double period = r.secondPeakT > 0 ? r.secondPeakT - r.firstPeakT : -1.0;
        std::printf("[MatSimParticlesGpu] U-tube 1/%d m: %zu particles, t90 %.2f s, peaks %.2f / %.2f s (period %.2f, theory 4.49), "
                    "mean right last 4 s %.3f m, deposited %.3f / %.3f m, %.1f ms/frame wall, %.2f ms/frame GPU, ledger %s\n",
                    cpm, r.particles, r.t90, r.firstPeakT, r.secondPeakT, period, r.meanRightLast4s, r.left, r.right,
                    r.msPerFrame, r.gpuMsPerFrame, r.ledgerExact ? "exact" : "BROKEN");
        const double tol = std::max(0.5 / cpm, 0.03);
        EXPECT_TRUE(r.ledgerExact) << cpm;
        EXPECT_EQ(r.lost, 0u) << cpm;
        EXPECT_GT(r.t90, 0.0) << cpm;
        EXPECT_LE(r.t90, 3.0) << cpm;
        EXPECT_NEAR(period, 4.49, 0.2 * 4.49) << cpm;
        EXPECT_NEAR(r.meanRightLast4s, 2.0, tol) << cpm;
        EXPECT_NEAR(r.left, r.right, tol) << cpm;
    }
}

TEST_F(MatSimParticlesGpuTest, DamBreakFrontAtBothSizes) {
    const double ritter = 2.0 * std::sqrt(9.81 * 3.0);
    for (int cpm : {3, 9}) {
        double ms = 0;
        const double v = damBreakFront(cpm, &gpu, &ms);
        std::printf("[MatSimParticlesGpu] dam break 1/%d m: front %.2f m/s (Ritter %.2f, %.0f %%; CPU 8.18), %.1f ms/frame wall\n",
                    cpm, v, ritter, 100.0 * v / ritter, ms);
        EXPECT_GE(v, 0.70 * ritter) << cpm;
        EXPECT_LE(v, 1.05 * ritter) << cpm;
        EXPECT_NEAR(v, 8.18, 0.05 * 8.18) << cpm;
    }
}

// E2 at 1/9 m, for the first time: wake -> particles (GPU) -> settle -> CA, as WakeUTubeFromCells.
TEST_F(MatSimParticlesGpuTest, HybridUTubeAtOneNinthMetre) {
    for (int cpm : {3, 9}) {
        MatSimCore s = uTube(cpm, true);
        ParticleConfig pc;
        pc.settleSpeed = 0.35f;
        pc.wakeHeadCells = 1.0f;
        MatSimParticles ps(pc);
        ps.setGpu(&gpu);
        const uint64_t total = s.ledger()[Mat::Water];
        bool exact = true;
        double at3 = -1, allCellsAt = -1, firstWake = -1;
        size_t peak = 0, rewakes = 0;
        double wakeMs = 0, stepMs = 0, settleMs = 0, caMs = 0;
        const int frames = 40 * 60;
        for (int f = 0; f < frames; ++f) {
            auto t0 = std::chrono::steady_clock::now();
            const size_t lifted = ps.wake(s);
            auto t1 = std::chrono::steady_clock::now();
            ps.step(s, 1.0 / 60.0);
            auto t2 = std::chrono::steady_clock::now();
            ps.settle(s);
            auto t3 = std::chrono::steady_clock::now();
            s.step(1);
            auto t4 = std::chrono::steady_clock::now();
            wakeMs += std::chrono::duration<double, std::milli>(t1 - t0).count() / frames;
            stepMs += std::chrono::duration<double, std::milli>(t2 - t1).count() / frames;
            settleMs += std::chrono::duration<double, std::milli>(t3 - t2).count() / frames;
            caMs += std::chrono::duration<double, std::milli>(t4 - t3).count() / frames;
            if (lifted && firstWake < 0) firstWake = (f + 1) / 60.0;
            if (lifted && allCellsAt > 0) ++rewakes;
            peak = std::max(peak, ps.particles().size());
            if (s.ledger()[Mat::Water] + ps.units(Mat::Water) != total) exact = false;
            if (f + 1 == 180) at3 = armCells(s, 3 * cpm, 5 * cpm) + armParticles(ps, cpm, s.full(), 3 * cpm, 5 * cpm);
            if (allCellsAt < 0 && firstWake > 0 && ps.particles().empty()) allCellsAt = (f + 1) / 60.0;
            if ((f + 1) % 300 == 0 && !ps.particles().empty()) {   // why it has not settled: every 5 s
                std::vector<float> sp;
                for (const Particle& q : ps.particles()) sp.push_back(std::sqrt(q.ax * q.ax + q.ay * q.ay + q.az * q.az) / float(cpm));
                std::sort(sp.begin(), sp.end());
                const auto& st = ps.lastSettleStats();
                std::printf("[settle] 1/%d m t %2d s: %zu particles in %zu groups (busy: speed %zu, level %zu); largest %zu members, "
                            "%zu slow (%.0f %%, need %.0f %%), surface spread %.2f cells (limit %.2f); averaged speed m/s "
                            "p50 %.3f p80 %.3f p90 %.3f (limit %.2f)\n",
                            cpm, (f + 1) / 60, ps.particles().size(), st.groups, st.busySpeed, st.busyLevel, st.bigMembers,
                            st.bigSlow, 100.0 * st.bigSlow / std::max<size_t>(st.bigMembers, 1), 100.0 * pc.settleFraction,
                            st.bigSpread, pc.settleLevelCells, sp[sp.size() / 2], sp[sp.size() * 8 / 10], sp[sp.size() * 9 / 10],
                            pc.settleSpeed);
            }
        }
        if (!ps.particles().empty()) {   // settle's column surfaces (base + cell water + particle volume), per x, z-min..max
            const uint32_t F = s.full();
            std::vector<double> colU(size_t(s.size().x * s.size().z), 0.0);
            for (const Particle& q : ps.particles())
                colU[size_t(int(std::floor(q.x)) + s.size().x * int(std::floor(q.z)))] += double(q.units) / F;
            std::printf("[surface] 1/%d m at 40 s, x: column surface zmin..zmax, mean over z; 1 m patch average (cells; '-' capped); "
                        "top particle y; particles above y 28\n", cpm);
            const int NX = s.size().x, NZ = s.size().z;
            std::vector<double> surf(size_t(NX * NZ), -1.0);
            for (int x = 0; x < NX; ++x)
                for (int z = 0; z < NZ; ++z) {
                    int base = 0;
                    while (base < s.size().y && cellMaterial(s.at({x, base, z})) == kSolid) ++base;
                    double v = base + colU[size_t(x + NX * z)];
                    for (int y = base; y < s.size().y && cellMaterial(s.at({x, y, z})) != kSolid; ++y)
                        if (cellMaterial(s.at({x, y, z})) == Mat::Water) v += double(cellFill(s.at({x, y, z}))) / F;
                    bool capped = false;
                    for (int y = int(std::floor(v)) + 1; y < s.size().y && !capped; ++y) capped = cellMaterial(s.at({x, y, z})) == kSolid;
                    if (!capped) surf[size_t(x + NX * z)] = v;
                }
            for (int x = 0; x < NX; ++x) {
                double mn = 1e9, mx = -1e9, mean = 0, pavg = 0;
                int cnt = 0, pc2 = 0;
                for (int z = 0; z < NZ; ++z) {
                    const double v = surf[size_t(x + NX * z)];
                    if (v < 0) continue;
                    mn = std::min(mn, v); mx = std::max(mx, v); mean += v; ++cnt;
                }
                for (int xx = std::max(0, x - 4); xx <= std::min(NX - 1, x + 4); ++xx)
                    for (int z = 0; z < NZ; ++z)
                        if (surf[size_t(xx + NX * z)] >= 0) { pavg += surf[size_t(xx + NX * z)]; ++pc2; }
                float top = -1;
                int above = 0;
                for (const Particle& q : ps.particles())
                    if (int(std::floor(q.x)) == x) { top = std::max(top, q.y); if (q.y >= 28) ++above; }
                if (!cnt) { std::printf("  x %2d: - capped; top particle %.2f, %d above 28\n", x, top, above); continue; }
                std::printf("  x %2d: %.2f..%.2f mean %.2f; patch %.2f; top particle %.2f, %d above 28\n", x, mn, mx, mean / cnt,
                            pc2 ? pavg / pc2 : -1.0, top, above);
            }
        }
        const double hl = armCells(s, 0, 2 * cpm) + armParticles(ps, cpm, s.full(), 0, 2 * cpm);
        const double hr = armCells(s, 3 * cpm, 5 * cpm) + armParticles(ps, cpm, s.full(), 3 * cpm, 5 * cpm);
        std::printf("[MatSimParticlesGpu] hybrid U-tube 1/%d m: first wake %.2f s, right %.3f m at 3 s, all cells at %.2f s, "
                    "%zu re-wake frames after, at 40 s arms %.3f / %.3f m, %zu particles left (peak %zu), ledger %s; "
                    "ms/frame wake %.2f step %.2f settle %.2f CA %.2f\n",
                    cpm, firstWake, at3, allCellsAt, rewakes, hl, hr, ps.particles().size(), peak, exact ? "exact" : "BROKEN",
                    wakeMs, stepMs, settleMs, caMs);
        const double tol = std::max(0.5 / cpm, 0.03);
        EXPECT_TRUE(exact) << cpm;
        EXPECT_GE(at3, 1.8) << cpm;
        EXPECT_EQ(ps.particles().size(), 0u) << cpm << ": the body must settle back into cells";
        EXPECT_NEAR(hl, hr, tol) << cpm;
        EXPECT_NEAR(hr, 2.0, tol) << cpm;
    }
}

// Diagnostic: does the slosh decay? All particles, 60 s; every 4 s the mean kinetic energy per
// particle (J/kg) and the arm-surface waviness (std over x of the top particle's y, per arm, in m).
TEST_F(MatSimParticlesGpuTest, DISABLED_SloshDecay) {
    for (int cpm : {3, 9}) {
        for (float visc : {0.1f, 0.3f}) {
            MatSimCore s = uTube(cpm, false);
            ParticleConfig pc;
            pc.viscosity = visc;
            MatSimParticles ps(pc);
            ps.setGpu(&gpu);
            ps.lift(s, s.origin(), s.size());
            for (int f = 0; f < 60 * 60; ++f) {
                ps.step(s, 1.0 / 60.0);
                if ((f + 1) % 240) continue;
                double ke = 0;
                std::vector<float> top(size_t(5 * cpm), -1.f);
                for (const Particle& q : ps.particles()) {
                    ke += 0.5 * double(q.vx * q.vx + q.vy * q.vy + q.vz * q.vz) / double(cpm * cpm);
                    const int x = int(std::floor(q.x));
                    if (x >= 0 && x < 5 * cpm) top[size_t(x)] = std::max(top[size_t(x)], q.y);
                }
                auto wav = [&](int x0, int x1) {   // std of the top over interior columns (1 cell from walls)
                    double m = 0, m2 = 0;
                    int n = 0;
                    for (int x = x0 + 1; x < x1 - 1; ++x) { m += top[size_t(x)]; m2 += double(top[size_t(x)]) * top[size_t(x)]; ++n; }
                    m /= n;
                    return std::sqrt(std::max(0.0, m2 / n - m * m)) / cpm;
                };
                std::printf("[decay] 1/%d m visc %.1f t %2d s: KE %.4f J/kg, waviness left %.3f right %.3f m, arms (particles) %.3f / %.3f m\n",
                            cpm, visc, (f + 1) / 60, ke / double(ps.particles().size()), wav(0, 2 * cpm), wav(3 * cpm, 5 * cpm),
                            armParticles(ps, cpm, s.full(), 0, 2 * cpm), armParticles(ps, cpm, s.full(), 3 * cpm, 5 * cpm));
            }
        }
    }
}

// Diagnostic: is the 1/9 m energy gain the solve's convergence (iterations, substep size)? 60 s
// all-particle U-tube; after 20 s: max KE, max waviness, max "breathing" (both arms high at once:
// min(left, right) - 2 m), and the bottom layer's mean density error.
TEST_F(MatSimParticlesGpuTest, DISABLED_EnergyVsSolve) {
    struct V { int cpm, iters, div; float cfl; };
    for (V v : {V{3, 4, 2, 0.4f}, V{9, 4, 2, 0.4f}, V{9, 8, 2, 0.4f}, V{9, 16, 2, 0.4f}, V{9, 4, 2, 0.2f}, V{9, 4, 0, 0.4f},
                V{9, 12, 2, 0.4f}}) {
        MatSimCore s = uTube(v.cpm, false);
        ParticleConfig pc;
        pc.iterations = v.iters;
        pc.divergenceIterations = v.div;
        pc.cflCells = v.cfl;
        pc.maxSubsteps = 64;
        MatSimParticles ps(pc);
        ps.setGpu(&gpu);
        ps.lift(s, s.origin(), s.size());
        double keMax = 0, wavMax = 0, breathMax = -1, keLast = 0, ms = 0;
        int subMax = 0;
        for (int f = 0; f < 60 * 60; ++f) {
            ps.step(s, 1.0 / 60.0);
            ms += gpu.lastWallMs() / 3600;
            subMax = std::max(subMax, ps.lastSubsteps());
            if (f < 20 * 60 || (f + 1) % 15) continue;
            double ke = 0;
            std::vector<float> top(size_t(5 * v.cpm), -1.f);
            for (const Particle& q : ps.particles()) {
                ke += 0.5 * double(q.vx * q.vx + q.vy * q.vy + q.vz * q.vz) / double(v.cpm * v.cpm);
                const int x = int(std::floor(q.x));
                if (x >= 0 && x < 5 * v.cpm) top[size_t(x)] = std::max(top[size_t(x)], q.y);
            }
            ke /= double(ps.particles().size());
            double m = 0, m2 = 0;
            int n = 0;
            for (int x = 1; x < 5 * v.cpm - 1; ++x) {
                if (x >= 2 * v.cpm - 1 && x <= 3 * v.cpm) continue;
                m += top[size_t(x)]; m2 += double(top[size_t(x)]) * top[size_t(x)]; ++n;
            }
            m /= n;
            const double wav = std::sqrt(std::max(0.0, m2 / n - m * m)) / v.cpm;
            const double breath = std::min(armParticles(ps, v.cpm, s.full(), 0, 2 * v.cpm),
                                           armParticles(ps, v.cpm, s.full(), 3 * v.cpm, 5 * v.cpm)) - 2.0;
            keMax = std::max(keMax, ke); wavMax = std::max(wavMax, wav); breathMax = std::max(breathMax, breath);
            keLast = ke;
        }
        // Bottom-layer density: particles with y < 1 cell above the floor, mean C.
        float meanC = 0, maxC = 0;
        ps.densityError(s, meanC, maxC);
        std::printf("[solve] 1/%d m iters %2d div %d cfl %.1f: after 20 s KE max %.3f last %.3f J/kg, waviness max %.3f m, "
                    "breathing max %+.3f m; density |C| mean %.4f max %.4f; substeps max %d, %.1f ms/frame\n",
                    v.cpm, v.iters, v.div, v.cfl, keMax, keLast, wavMax, breathMax, meanC, maxC, subMax, ms);
    }
}

// Cost (printed, no assertion): CPU vs GPU ms per frame on the 1/9 m U-tube, 2 s.
TEST_F(MatSimParticlesGpuTest, CostAtOneNinthMetre) {
    const UTubeRun c = runUTube(9, nullptr, 2.0);
    const UTubeRun g = runUTube(9, &gpu, 2.0);
    std::printf("[MatSimParticlesGpu] cost 1/9 m U-tube (%zu particles), first 2 s: CPU %.1f ms/frame, GPU %.1f ms/frame wall "
                "(%.2f ms device)\n", c.particles, c.msPerFrame, g.msPerFrame, g.gpuMsPerFrame);
}
