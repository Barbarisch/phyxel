// WaterCoreGpuParityTest — docs/WaterCore.md §15.11: the GPU core is accepted on parity with the
// CPU reference, red first. Needs a Vulkan device: the fixture GTEST_SKIPs without one (never a pass).
// Slice 1 covers the FILL transport: still water, hydrostatics, mass, determinism, the dam-break front.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"
#include "core/water/WaterCore.h"
#include "core/water/WaterCoreGpu.h"
#include "core/water/WaterSurfaceMesh.h"
#include "core/water/WaterCoreManager.h"
#include <cmath>
#include <cstdlib>
#include <functional>

using namespace Phyxel::Core::Water;
using Phyxel::Testing::VulkanTestFixture;

namespace {
constexpr float kDt = 1.0f / 60.0f;
constexpr double kG = 9.81;

struct Tank {
    WaterGrid grid;
    std::function<Occ(const glm::ivec3&)> extra;
    Tank(int nx, int ny, int nz, float h = 1.0f) : grid(GridSpec{glm::ivec3(0), glm::ivec3(nx, ny, nz), h}) {}
    SolidQuery query() const {
        const GridSpec s = grid.spec(); auto ex = extra;
        return [s, ex](const glm::ivec3& cw) -> Occ {
            const glm::ivec3 c = cw - s.origin;
            if (c.x < 0 || c.y < 0 || c.z < 0 || c.x >= s.dims.x || c.y >= s.dims.y || c.z >= s.dims.z) return Occ::Solid;
            return ex ? ex(c) : Occ::Air; };
    }
    void bakeOcc() {   // the GPU reads occupancy from the grid, so write the tank's walls into it
        auto q = query();
        for (int z = 0; z < grid.nz(); ++z) for (int y = 0; y < grid.ny(); ++y) for (int x = 0; x < grid.nx(); ++x)
            grid.occ(x, y, z) = q(glm::ivec3(x, y, z));
    }
};

int frontX(const WaterGrid& g, int z, double minColumnMass) {
    int front = -1;
    for (int x = 0; x < g.nx(); ++x) if (g.columnMass(x, z) > minColumnMass) front = x;
    return front;
}
float maxSpeed(WaterGrid& g) {
    float m = 0.0f;
    for (float v : g.uData()) m = std::max(m, std::abs(v));
    for (float v : g.vData()) m = std::max(m, std::abs(v));
    for (float v : g.wData()) m = std::max(m, std::abs(v));
    return m;
}
} // namespace

class WaterCoreGpuParityTest : public VulkanTestFixture {
protected:
    WaterCoreGpu gpu;
    void SetUp() override {
        VulkanTestFixture::SetUp();
        if (!isVulkanAvailable()) return;
        std::string err;
        if (!gpu.init(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) GTEST_SKIP() << "WaterCoreGpu init: " << err;
    }
    void TearDown() override { gpu.shutdown(); VulkanTestFixture::TearDown(); }
    WaterCoreGpu::Volume* make(Tank& t) {
        t.bakeOcc();
        std::string err;
        WaterCoreGpu::Volume* v = gpu.createVolume(t.grid, &err);
        if (!v) ADD_FAILURE() << "createVolume: " << err;
        return v;
    }
};

// Still water stays still on the GPU exactly as on the CPU (rule R: a resting pool has no velocity).
TEST_F(WaterCoreGpuParityTest, GpuStillWaterStaysStill) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    Tank t(8, 6, 4);
    t.grid.fillBox({0, 0, 0}, {7, 2, 3}, 1.0f);
    auto* vol = make(t); ASSERT_NE(vol, nullptr);
    SolverParams prm;
    const auto st = gpu.step(*vol, prm, kDt, 120, 40);
    gpu.download(*vol, t.grid);
    EXPECT_LT(maxSpeed(t.grid), 1e-3f) << "residual motion in a resting pool (RBGS residual " << st.rbgsResidualMax << ")";
    EXPECT_NEAR(t.grid.totalMass(), 8.0 * 3.0 * 4.0, 1e-4);
    gpu.destroyVolume(vol);
}

// The same mass to 1e-6 relative under the same arbitrary velocity field: the checkerboard advect
// is exact like the CPU's sequential one.
TEST_F(WaterCoreGpuParityTest, GpuMassExactUnderArbitraryVelocity) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    Tank t(12, 10, 6);
    t.grid.fillBox({0, 0, 0}, {11, 4, 5}, 1.0f);
    for (int z = 0; z < 6; ++z) for (int y = 0; y < 10; ++y) for (int x = 0; x <= 12; ++x) t.grid.u(x, y, z) = std::sin(0.7f * x + 0.3f * y) * 2.0f;
    for (int z = 0; z < 6; ++z) for (int y = 0; y <= 10; ++y) for (int x = 0; x < 12; ++x) t.grid.v(x, y, z) = std::cos(0.5f * z + 0.9f * y) * 2.0f;
    const double m0 = t.grid.totalMass();
    auto* vol = make(t); ASSERT_NE(vol, nullptr);
    SolverParams prm;
    gpu.step(*vol, prm, kDt, 300, 20);
    gpu.download(*vol, t.grid);
    EXPECT_NEAR(t.grid.totalMass(), m0, 1e-6 * m0);
    gpu.destroyVolume(vol);
}

// Two runs on the same device are bit-identical (fixed pass order, no atomics).
TEST_F(WaterCoreGpuParityTest, GpuDeterministic) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    auto run = [&]() {
        Tank t(30, 6, 3);
        t.grid.fillBox({0, 0, 0}, {9, 2, 2}, 1.0f);
        auto* vol = make(t);
        SolverParams prm;
        gpu.step(*vol, prm, kDt, 60, 40);
        gpu.download(*vol, t.grid);
        gpu.destroyVolume(vol);
        return t.grid.fData();
    };
    const auto a = run(), b = run();
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) ASSERT_EQ(a[i], b[i]) << "cell " << i;
}

// Hydrostatic pressure gradient after settling (the CPU reads 1 % of rho g h): the RBGS solve
// must reach it too; the residual is reported.
TEST_F(WaterCoreGpuParityTest, GpuHydrostaticPressureParity) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    // the CPU test's exact form (WaterCoreTest.HydrostaticPressure): 10 m deep, 5 ticks, at cell y
    // the pressure is g * (10 - y - 0.5) within 1 % (rho = 1; the projection's p is a pressure)
    Tank t(4, 12, 4);
    t.grid.fillBox({0, 0, 0}, {3, 9, 3}, 1.0f);
    auto* vol = make(t); ASSERT_NE(vol, nullptr);
    SolverParams prm;
    const auto st = gpu.step(*vol, prm, kDt, 5, 40);
    std::vector<float> p; gpu.readPressure(*vol, p);
    for (int y = 0; y < 9; ++y) {
        const double depth = 10.0 - (y + 0.5);
        EXPECT_NEAR(p[t.grid.idx(1, y, 1)], kG * depth, 0.01 * kG * depth) << "y=" << y << " (RBGS residual " << st.rbgsResidualMax << ")";
    }
    gpu.destroyVolume(vol);
}

// The dam-break front on the GPU within one cell of the CPU reference at 2 s (coarse grid).
TEST_F(WaterCoreGpuParityTest, GpuDamBreakFrontParity) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    Tank c(60, 6, 3);
    c.grid.fillBox({0, 0, 0}, {9, 2, 2}, 1.0f);
    WaterSolver cpu(c.grid, c.query());
    for (int i = 0; i < 120; ++i) cpu.step(kDt);
    const int cpuFront = frontX(c.grid, 1, 0.5);
    Tank g(60, 6, 3);
    g.grid.fillBox({0, 0, 0}, {9, 2, 2}, 1.0f);
    auto* vol = make(g); ASSERT_NE(vol, nullptr);
    SolverParams prm;
    const auto st = gpu.step(*vol, prm, kDt, 120, 40);
    gpu.download(*vol, g.grid);
    const int gpuFront = frontX(g.grid, 1, 0.5);
    std::printf("  dam break at 2 s: CPU front %d, GPU front %d (RBGS residual %.2e, mass %.4f)\n", cpuFront, gpuFront, st.rbgsResidualMax, g.grid.totalMass());
    EXPECT_NEAR(gpuFront, cpuFront, 1) << "front differs by more than one cell";
    EXPECT_NEAR(g.grid.totalMass(), 90.0, 1e-3);
    gpu.destroyVolume(vol);
}

// WaterCore.md 26.5: the ALL-FACES-AT-ONCE fill transport on the GPU equals the CPU's - the same dam break, both with the new
// rule (EulerianTransport::s_allFacesAtOnce): fronts within one cell at 2 s, mass exact to float, and the small tank's fills
// elementwise within 5e-3 after 60 ticks.
TEST_F(WaterCoreGpuParityTest, GpuAllFacesTransportMatchesCpu) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    EulerianTransport::s_allFacesAtOnce = true;
    Tank c(60, 6, 3), g(60, 6, 3);
    for (Tank* t : {&c, &g}) t->grid.fillBox({0, 0, 0}, {9, 2, 2}, 1.0f);
    WaterSolver cpu(c.grid, c.query());
    for (int i = 0; i < 120; ++i) cpu.step(kDt);
    auto* vol = make(g); ASSERT_NE(vol, nullptr);
    SolverParams prm;
    gpu.step(*vol, prm, kDt, 120, 40);
    gpu.download(*vol, g.grid);
    const int cf = frontX(c.grid, 1, 0.5), gf = frontX(g.grid, 1, 0.5);
    std::printf("  all faces at once, dam break at 2 s: CPU front %d, GPU front %d; mass CPU %.6f GPU %.6f (90)\n", cf, gf, c.grid.totalMass(), g.grid.totalMass());
    EXPECT_NEAR(gf, cf, 1);
    EXPECT_NEAR(g.grid.totalMass(), 90.0, 1e-3);
    gpu.destroyVolume(vol);
    Tank c2(4, 4, 2), g2(4, 4, 2);
    for (Tank* t : {&c2, &g2}) { t->grid.fillBox({0, 0, 0}, {3, 0, 1}, 1.0f); for (int x = 0; x < 4; ++x) for (int z = 0; z < 2; ++z) t->grid.f(x, 1, z) = 0.3f + 0.05f * x; }
    WaterSolver cpu2(c2.grid, c2.query());
    for (int i = 0; i < 60; ++i) cpu2.step(kDt);
    auto* vol2 = make(g2); ASSERT_NE(vol2, nullptr);
    gpu.step(*vol2, prm, kDt, 60, 40);
    gpu.download(*vol2, g2.grid);
    double df = 0.0;
    for (size_t i = 0; i < c2.grid.fData().size(); ++i) df = std::max(df, static_cast<double>(std::abs(c2.grid.fData()[i] - g2.grid.fData()[i])));
    std::printf("  small tank, 60 ticks: max|df| %.2e\n", df);
    EXPECT_LT(df, 5e-3);
    gpu.destroyVolume(vol2);
    EulerianTransport::s_allFacesAtOnce = true;   // the default
}

// 26.5 red test: a RESTING pond (one full layer, one half layer - the bench pond's state) keeps its mass on the GPU with the
// all-faces transport. Found by the engine A/B (slosh_live_p1_new: 24.00 -> 24.30 m^3 in 24 s): a cell left a hair NEGATIVE
// by float rounding (-5e-23) sends nothing, so its outflow sum is 0 > its fill and the kernel's scale divided by zero; the
// NaN reached compaction, whose min(NaN, cap) moved one free-fall cap (0.0738 cells) down from nowhere, and the residue
// sweep zeroed the NaN. The CPU never divides there (it scales only faces whose donor sends). Before the fix: +0.074 at
// tick 1183, +0.29 by 20 s, the pond woke (KE 1e-5 -> 0.4).
TEST_F(WaterCoreGpuParityTest, GpuAllFacesRestingPondKeepsItsMass) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    EulerianTransport::s_allFacesAtOnce = true;
    Tank g(12, 9, 12);
    g.grid.fillBox({0, 0, 0}, {11, 0, 11}, 1.0f);
    for (int x = 0; x < 12; ++x) for (int z = 0; z < 12; ++z) g.grid.f(x, 1, z) = 0.5f;
    auto* vol = make(g); ASSERT_NE(vol, nullptr);
    SolverParams prm;
    double worst = 0.0; int nan = 0;
    for (int tick = 0; tick < 1800; ++tick) {   // 30 s, one tick per call (the call pattern that found it)
        gpu.step(*vol, prm, kDt, 1, 40);
        if (tick % 30 == 29) {
            gpu.download(*vol, g.grid);
            for (float v : g.grid.fData()) if (!std::isfinite(v)) ++nan;
            worst = std::max(worst, std::abs(g.grid.totalMass() - 216.0));
        }
    }
    std::printf("  resting pond, 30 s on the GPU, all faces at once: worst |mass - 216| %.2e cells, non-finite fills %d\n", worst, nan);
    EXPECT_LT(worst, 1e-3);
    EXPECT_EQ(nan, 0);
    gpu.destroyVolume(vol);
    EulerianTransport::s_allFacesAtOnce = true;   // the default
}

// 26.5 diagnostic: the still pool (GpuRestDecisionDeterministic (a)) - KE, the water above the pool and the quiet counter
// per tick, GPU vs CPU; the all-faces rule, without and with the still-water first guess (26.9)
TEST_F(WaterCoreGpuParityTest, DISABLED_DiagStillPool) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const bool guessWas = WaterCoreGpu::s_stillWaterGuess;
    for (bool guess : {false, true}) {
        WaterCoreGpu::s_stillWaterGuess = guess;
        Tank t(6, 4, 3), c(6, 4, 3);
        for (Tank* x : {&t, &c}) x->grid.fillBox({0, 0, 0}, {5, 1, 2}, 1.0f);
        auto* vol = make(t);
        WaterSolver cpu(c.grid, c.query());
        SolverParams prm;
        std::printf("  %s\n", guess ? "STILL-WATER GUESS" : "cold start");
        for (int tick = 0; tick < 60; ++tick) {
            const auto st = gpu.step(*vol, prm, kDt, 1, std::getenv("DIAG_SWEEPS") ? std::atoi(std::getenv("DIAG_SWEEPS")) : 40);
            const StepReport cr = cpu.step(kDt);
            if (tick % 6 == 0) {
                gpu.download(*vol, t.grid);
                double above = 0.0, aboveC = 0.0, fmax = 0.0, fmin = 2.0;
                for (int z = 0; z < 3; ++z) for (int x = 0; x < 6; ++x) {
                    for (int y = 2; y < 4; ++y) { above += t.grid.f(x, y, z); aboveC += c.grid.f(x, y, z); }
                    for (int y = 0; y < 2; ++y) { fmax = std::max(fmax, (double)t.grid.f(x, y, z)); fmin = std::min(fmin, (double)t.grid.f(x, y, z)); }
                }
                std::printf("    tick %2d GPU: KE %.2e quiet %d asleep %d, above %.2e, full cells %.9f..%.9f | CPU: KE %.2e, above %.2e\n",
                            tick, st.kineticEnergy, st.quietTicks, int(st.asleep), above, fmin, fmax, cr.kineticEnergy, aboveC);
            }
        }
        {
            std::vector<float> pr; gpu.readPressure(*vol, pr); gpu.download(*vol, t.grid);
            for (int y = 0; y < 4; ++y) {
                std::printf("    column (2,%d,1): p %.5f (hydrostatic %.5f) f %.6f | v below %.2e\n", y, pr[t.grid.idx(2, y, 1)], kG * (2.0f - (y + 0.5f)), t.grid.f(2, y, 1), t.grid.v(2, y, 1));
            }
            std::printf("    corner (0,1,0): p %.5f, edge (0,1,1): p %.5f, centre (2,1,1): p %.5f; v top faces corner %.2e edge %.2e centre %.2e\n",
                        pr[t.grid.idx(0, 1, 0)], pr[t.grid.idx(0, 1, 1)], pr[t.grid.idx(2, 1, 1)], t.grid.v(0, 2, 0), t.grid.v(0, 2, 1), t.grid.v(2, 2, 1));
        }
        gpu.destroyVolume(vol);
    }
    WaterCoreGpu::s_stillWaterGuess = guessWas;
}

// 26.6 P2 cost model (DIAGNOSTIC): the GPU core's time per call (one tick, realtime's shape) against the cells it steps, on
// dense boxes at 1/9 m - a resting pond half full (substeps 1) and the same box sloshing (a tilted start). The fixed cost
// per call (dispatch count x barriers) and the per-cell slope decide the brick size and whether sparse stepping fits 2 ms.
TEST_F(WaterCoreGpuParityTest, DISABLED_DiagCostPerCell) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const float h = 1.0f / 9.0f;
    for (glm::ivec3 d : {glm::ivec3(16, 16, 16), glm::ivec3(32, 16, 32), glm::ivec3(64, 16, 64), glm::ivec3(128, 16, 64), glm::ivec3(128, 16, 128), glm::ivec3(256, 16, 128), glm::ivec3(256, 16, 256)}) {
        for (bool slosh : {false, true}) {
            Tank t(d.x, d.y, d.z, h);
            for (int z = 0; z < d.z; ++z) for (int x = 0; x < d.x; ++x) {
                const float top = d.y * 0.5f + (slosh ? 3.0f * (x - d.x * 0.5f) / d.x : 0.0f);
                for (int y = 0; y < d.y; ++y) t.grid.f(x, y, z) = std::clamp(top - static_cast<float>(y), 0.0f, 1.0f);
            }
            std::string err;
            auto* vol = gpu.createVolume(t.grid, &err);
            if (!vol) { std::printf("  %dx%dx%d: refused (%s)\n", d.x, d.y, d.z, err.c_str()); continue; }
            SolverParams prm;
            for (int k = 0; k < 10; ++k) gpu.step(*vol, prm, kDt, 1, 40);   // warm
            std::vector<double> ms; int sub = 0;
            for (int k = 0; k < 30; ++k) { const auto st = gpu.step(*vol, prm, kDt, 1, 40); ms.push_back(st.gpuMs); sub = st.substepsLast; }
            std::sort(ms.begin(), ms.end());
            const double cells = double(d.x) * d.y * d.z;
            std::printf("  %3dx%2dx%3d = %8.0f cells, %s: median %.3f ms (p90 %.3f), substeps %d -> %.1f ns per cell-substep\n",
                        d.x, d.y, d.z, cells, slosh ? "slosh " : "resting", ms[15], ms[27], sub, ms[15] * 1e6 / (cells * std::max(sub, 1)));
            gpu.destroyVolume(vol);
        }
    }
}

// 26.6 diagnostic: the WaterBench_Small pond through the MANAGER, one tick per call (the engine's realtime shape): the
// live GPU pond lost ~2.2e-5 m^3/s at rest where the CPU held exactly (2026-10-13). Stone floor at voxel y <= 14, walls at
// x 99 / 104 and z 11 / 16, water at y 15 (full) and 16 (half) over x 100-103, z 12-15; the volume box 99..104 x 14..19 x 11..16.
TEST_F(WaterCoreGpuParityTest, DISABLED_DiagBenchPondThroughTheManager) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    auto state = [](const glm::ivec3& m) -> int {   // micro (1/9) coordinates
        const glm::ivec3 v(static_cast<int>(std::floor(m.x / 9.0)), static_cast<int>(std::floor(m.y / 9.0)), static_cast<int>(std::floor(m.z / 9.0)));
        if (v.y <= 14) return 1;
        if (v.x <= 99 || v.x >= 104 || v.z <= 11 || v.z >= 16) return v.y <= 17 ? 1 : 0;
        return 0;
    };
    // the engine's shape: realtime update(frame dt) with an occupancy revision that may change every frame (each change
    // re-reads occupancy and re-uploads a GPU volume)
    for (const char* backend : {"cpu", "gpu"}) for (bool revEveryFrame : {false, true}) for (float frame : {1.0f / 60.0f, 1.0f / 165.0f}) {
        uint64_t rev = 0;
        WaterCoreManager mgr(state, [&rev, revEveryFrame]() { return revEveryFrame ? ++rev : rev; });
        std::string err;
        if (std::string(backend) == "gpu") ASSERT_TRUE(mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
        const int id = mgr.create(glm::ivec3(99, 14, 11), glm::ivec3(104, 19, 16), 1.0f / 3.0f, "eulerian", &err, backend, 0);
        ASSERT_NE(id, 0) << err;
        mgr.placeBox(glm::ivec3(100, 15, 12), glm::ivec3(103, 15, 15), 1.0f, nullptr);
        mgr.placeBox(glm::ivec3(100, 16, 12), glm::ivec3(103, 16, 15), 0.5f, nullptr);
        mgr.setRealtime(true);
        AvRecord rec;
        double m0 = -1.0, last = 0.0, ke = 0.0;
        const int frames = static_cast<int>(10.0f / frame);
        for (int k = 0; k < frames; ++k) {
            mgr.update(frame);
            if (k == 0 || k == frames - 1) { const auto l = mgr.list(); rec = l.front(); if (m0 < 0.0) m0 = rec.mass; last = rec.mass; ke = rec.kineticEnergy; }
        }
        std::printf("  %s, revision %s, frame %.4f s: 10 s mass %.6f -> %.6f (%+.3e m^3), ke %.1e\n", backend, revEveryFrame ? "EVERY frame" : "fixed", frame, m0, last, last - m0, ke);
    }
}

// The pressure solve's convergence is MEASURED (docs/WaterCore.md §15.11 risk): for each relaxation
// factor and sweep count, the worst hydrostatic error on the 10 m column after 5 ticks and the
// RBGS residual. The row the solver ships with must reach the CPU test's 1 % at a sweep count
// inside the §10 dispatch budget; the table is printed so the choice is on the record.
TEST_F(WaterCoreGpuParityTest, GpuRbgsConvergenceScan) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    std::printf("  omega  sweeps  worst hydrostatic err  residual\n");
    double bestErr = 1e9; float bestOmega = 0; int bestSweeps = 0;
    for (float omega : {1.0f, 1.5f, 1.7f, 1.85f, 1.95f}) for (int sweeps : {40, 100, 200}) {
        Tank t(4, 12, 4);
        t.grid.fillBox({0, 0, 0}, {3, 9, 3}, 1.0f);
        auto* vol = make(t); ASSERT_NE(vol, nullptr);
        gpu.setOmega(omega);
        SolverParams prm;
        const auto st = gpu.step(*vol, prm, kDt, 5, sweeps);
        std::vector<float> p; gpu.readPressure(*vol, p);
        double worst = 0.0;
        for (int y = 0; y < 9; ++y) { const double depth = 10.0 - (y + 0.5); worst = std::max(worst, std::abs(p[t.grid.idx(1, y, 1)] - kG * depth) / (kG * depth)); }
        std::printf("  %.2f   %4d    %.4f                 %.3e\n", omega, sweeps, worst, st.rbgsResidualMax);
        if (worst < bestErr) { bestErr = worst; bestOmega = omega; bestSweeps = sweeps; }
        gpu.destroyVolume(vol);
    }
    gpu.setOmega(1.85f);
    std::printf("  best: omega %.2f, %d sweeps, err %.4f\n", bestOmega, bestSweeps, bestErr);
    EXPECT_LT(bestErr, 0.01) << "no (omega, sweeps) in the scan reaches the CPU test's 1 % on a 10 m column";
}

// §15.11 red test: the rest decision is deterministic - two runs of the same settling pool fall
// asleep on the same tick (fixed-tree reductions, no float atomics).
TEST_F(WaterCoreGpuParityTest, GpuRestDecisionDeterministic) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    // (a) a still pool must sleep on exactly the same tick in two runs (restTicks = 30 quiet ticks)
    auto stillRun = [&]() {
        Tank t(6, 4, 3);
        t.grid.fillBox({0, 0, 0}, {5, 1, 2}, 1.0f);
        auto* vol = make(t);
        SolverParams prm;
        int sleepTick = -1;
        for (int tick = 0; tick < 300 && sleepTick < 0; ++tick) { const auto st = gpu.step(*vol, prm, kDt, 1, 40); if (st.asleep) sleepTick = tick; }
        gpu.destroyVolume(vol);
        return sleepTick;
    };
    const int sa = stillRun(), sb = stillRun();
    std::printf("  still pool asleep at tick %d / %d\n", sa, sb);
    EXPECT_EQ(sa, sb);
    EXPECT_GE(sa, 0) << "a still pool must sleep";
    // (b) a settling pool's whole quiet-counter sequence is identical between two runs, tick by tick
    auto settleRun = [&]() {
        Tank t(4, 4, 2);
        t.grid.fillBox({0, 0, 0}, {3, 0, 1}, 1.0f);
        for (int x = 0; x < 4; ++x) for (int z = 0; z < 2; ++z) t.grid.f(x, 1, z) = 0.3f + 0.05f * x;
        auto* vol = make(t);
        SolverParams prm;
        std::vector<int> quiet; std::vector<float> ke;
        for (int tick = 0; tick < 300; ++tick) { const auto st = gpu.step(*vol, prm, kDt, 1, 40); quiet.push_back(st.quietTicks); ke.push_back(static_cast<float>(st.kineticEnergy)); }
        gpu.destroyVolume(vol);
        return std::make_pair(quiet, ke);
    };
    const auto a = settleRun(), b = settleRun();
    EXPECT_EQ(a.first, b.first) << "the quiet-counter sequences differ";
    EXPECT_EQ(a.second, b.second) << "the kinetic-energy sequences differ (a reduction order leak)";
}

// §15.11: a GPU volume the device cannot hold is refused at create with the byte count in the
// message - never a failure inside a dispatch. Since 26.6 a box this size meets the brick-map cap first (4 M bricks):
// the same refusal, the same promise (reason + bytes).
TEST_F(WaterCoreGpuParityTest, GpuAllocationRefusalCarriesTheByteCount) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    GridSpec spec; spec.dims = glm::ivec3(4096, 4096, 512); spec.h = 1.0f;   // 8.6e9 cells: 34 GB per buffer
    std::string err;
    WaterCoreGpu::Volume* vol = gpu.createVolume(spec, &err);
    if (vol) { gpu.destroyVolume(vol); GTEST_SKIP() << "this device allocated 34 GB buffers; the refusal path needs a larger request"; }
    EXPECT_NE(err.find("bytes"), std::string::npos) << err;
    std::printf("  refusal: %s\n", err.c_str());
}

// Sources on the GPU: the CPU's SubmergedPumpDelivers rig (18 m^3 box, 0.5 m^3/s two cells under
// the surface, 8 s) must deliver its rate and keep the ledger exact.
TEST_F(WaterCoreGpuParityTest, GpuSubmergedPumpDelivers) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    Tank t(6, 8, 1);
    t.grid.fillBox({0, 0, 0}, {5, 2, 0}, 1.0f);
    auto* vol = make(t); ASSERT_NE(vol, nullptr);
    std::string err;
    GpuSource src; src.cell = static_cast<int32_t>(t.grid.idx(3, 1, 0)); src.rate = 0.5f;
    ASSERT_TRUE(gpu.setSources(*vol, {src}, &err)) << err;
    SolverParams prm;
    for (int i = 0; i < 8; ++i) gpu.step(*vol, prm, kDt, 60, 40);
    std::vector<GpuSource> back; gpu.readSources(*vol, back);
    gpu.download(*vol, t.grid);
    ASSERT_EQ(back.size(), 1u);
    std::printf("  pump on the GPU: placed %.3f m^3 of 4.0, pending %.4f, grid mass %.4f\n", back[0].placedTotal, back[0].pending, t.grid.totalMass());
    EXPECT_NEAR(back[0].placedTotal, 4.0, 0.1);
    EXPECT_NEAR(t.grid.totalMass(), 18.0 + back[0].placedTotal, 1e-4);
    EXPECT_LT(back[0].pending, 0.5f);
    gpu.destroyVolume(vol);
}

static WaterCoreGpu::Volume* makeFlip(WaterCoreGpu& gpu, Tank& t, float blend = 0.95f) {
    t.bakeOcc();
    std::string err;
    WaterCoreGpu::Volume* v = gpu.createVolume(t.grid.spec(), &err, true);
    if (!v) { ADD_FAILURE() << "createVolume(particles): " << err; return nullptr; }
    gpu.upload(*v, t.grid);
    FlipTransport seedT(blend); seedT.seed(t.grid);     // the CPU seeding rule, exact masses
    if (!gpu.setParticles(*v, seedT.particles(), &err)) { ADD_FAILURE() << "setParticles: " << err; gpu.destroyVolume(v); return nullptr; }
    gpu.setFlipBlend(blend);
    return v;
}

TEST_F(WaterCoreGpuParityTest, GpuFlipMassExactAndDeterministic) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    auto run = [&]() {
        Tank t(16, 8, 4);
        t.grid.fillBox({0, 0, 0}, {5, 4, 3}, 1.0f);
        auto* vol = makeFlip(gpu, t);
        SolverParams prm;
        gpu.step(*vol, prm, kDt, 120, 40);
        std::vector<FlipParticle> ps; gpu.readParticles(*vol, ps);
        gpu.download(*vol, t.grid);
        double m = 0.0; for (const auto& p : ps) m += p.mass;
        std::vector<float> f = t.grid.fData();
        gpu.destroyVolume(vol);
        return std::make_tuple(ps.size(), m, f);
    };
    const auto a = run(), b = run();
    EXPECT_EQ(std::get<0>(a), 6 * 5 * 4 * 8u) << "particle count";
    EXPECT_NEAR(std::get<1>(a), 120.0, 1e-4) << "particle mass is the ledger";
    EXPECT_EQ(std::get<0>(a), std::get<0>(b));
    EXPECT_EQ(std::get<1>(a), std::get<1>(b));
    const auto& fa = std::get<2>(a); const auto& fb = std::get<2>(b);
    ASSERT_EQ(fa.size(), fb.size());
    for (size_t i = 0; i < fa.size(); ++i) ASSERT_EQ(fa[i], fb[i]) << "cell " << i << " differs between two runs";
}

// Phase B2 on the GPU against the CPU FLIP reference on the 20 m channel at 1/3 m. The BULK flow is
// the parity gate: the dam-break front at 1 s within 3 cells (CPU 42 / GPU 45 measured) with mass
// exact. The wall run-up is reported, not gated: it is the most solver-sensitive quantity on every
// ledger so far (fills: 1.11 CPU / 1.22 GPU at 40 sweeps / 1.67 at 100), and the two particle
// realisations (scatter + PCG on the CPU, gather + SOR on the GPU) read 1.94 h0 and a ceiling-
// clipped 3.0 h0 at the same wall; the open row is in WaterCore.md 15.15. The GPU particles must
// still beat the fill transport's 1.22 h0, which is what B2 exists for.
TEST_F(WaterCoreGpuParityTest, GpuFlipBulkParityAndRunup) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const float h = 1.0f / 3.0f; const int per = 3;
    Tank c(20 * per, 9 * per, per, h);
    c.grid.fillBox({0, 0, 0}, {10 * per - 1, 3 * per - 1, per - 1}, 1.0f);
    WaterSolver cs(c.grid, c.query());
    cs.setTransport(std::make_unique<FlipTransport>(0.95f)); cs.transport().seed(cs.grid());
    double cpuPeak = 0.0; int cpuFront1s = -1;
    for (int k = 0; k < 4 * 60; ++k) {
        cs.step(kDt);
        if (k == 59) cpuFront1s = frontX(c.grid, 1, 0.5 * h * h);
        for (int z = 0; z < per; ++z) for (int x = 18 * per; x < 20 * per; ++x) { const float sy = c.grid.wetTipWorldY(x, z); if (!std::isnan(sy)) cpuPeak = std::max(cpuPeak, static_cast<double>(sy)); }
    }
    Tank t(20 * per, 9 * per, per, h);
    t.grid.fillBox({0, 0, 0}, {10 * per - 1, 3 * per - 1, per - 1}, 1.0f);
    auto* vol = makeFlip(gpu, t); ASSERT_NE(vol, nullptr);
    SolverParams prm;
    double gpuPeak = 0.0; int gpuFront1s = -1;
    for (int k = 0; k < 4 * 60; k += 6) {
        gpu.step(*vol, prm, kDt, 6, 90);
        gpu.download(*vol, t.grid);
        if (k + 6 == 60) gpuFront1s = frontX(t.grid, 1, 0.5 * h * h);
        for (int z = 0; z < per; ++z) for (int x = 18 * per; x < 20 * per; ++x) { const float sy = t.grid.wetTipWorldY(x, z); if (!std::isnan(sy)) gpuPeak = std::max(gpuPeak, static_cast<double>(sy)); }
    }
    std::vector<FlipParticle> ps; gpu.readParticles(*vol, ps);
    double m = 0.0; for (const auto& q : ps) m += q.mass;
    std::printf("  FLIP on the channel: front at 1 s CPU %d / GPU %d cells; run-up CPU %.2f h0 / GPU %.2f h0 (literature 2.1-2.3; the tank top is 3.0); GPU mass %.4f\n", cpuFront1s, gpuFront1s, cpuPeak / 3.0, gpuPeak / 3.0, m);
    EXPECT_NEAR(gpuFront1s, cpuFront1s, 3) << "bulk front parity at 1 s";
    EXPECT_NEAR(m, 30.0, 1e-4) << "particle mass is the ledger";
    EXPECT_GT(gpuPeak / 3.0, 1.22) << "the GPU particles must beat the fill transport's run-up";
    gpu.destroyVolume(vol);
}

// Element-wise closeness of the fill backends on the small tilted pool: the two solvers (PCG vs
// 40 SOR sweeps) agree on every fill to 5e-3 and on every PROJECTION face (a pressure-domain cell, f >= 0.5, on either
// side) to 5e-3 m/s after 60 ticks. Faces between film cells and air are not gated (26.5, 2026-10-13): they are set by
// settleThinFilmTopFaces, which branches on "any water above" (f > 0) - float dust from the all-faces transport flips it
// (0.16 m/s apart on the film's top faces while the fills agree to 6e-4); they are printed.
TEST_F(WaterCoreGpuParityTest, GpuFillsMatchCpuElementwise) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    Tank c(4, 4, 2), g(4, 4, 2);
    for (Tank* t : {&c, &g}) { t->grid.fillBox({0, 0, 0}, {3, 0, 1}, 1.0f); for (int x = 0; x < 4; ++x) for (int z = 0; z < 2; ++z) t->grid.f(x, 1, z) = 0.3f + 0.05f * x; }
    WaterSolver cpu(c.grid, c.query());
    for (int i = 0; i < 60; ++i) cpu.step(kDt);
    auto* vol = make(g); ASSERT_NE(vol, nullptr);
    SolverParams prm;
    gpu.step(*vol, prm, kDt, 60, 40);
    gpu.download(*vol, g.grid);
    double df = 0.0, dvDomain = 0.0, dvFilm = 0.0;
    for (size_t i = 0; i < c.grid.fData().size(); ++i) df = std::max(df, static_cast<double>(std::abs(c.grid.fData()[i] - g.grid.fData()[i])));
    const WaterGrid& G = c.grid;
    auto dom = [&](int x, int y, int z) { return G.inBounds(x, y, z) && G.f(x, y, z) >= 0.5f; };
    auto acc = [&](bool domain, float a, float b) { double& d = domain ? dvDomain : dvFilm; d = std::max(d, static_cast<double>(std::abs(a - b))); };
    for (int z = 0; z < G.nz(); ++z) for (int y = 0; y < G.ny(); ++y) for (int x = 0; x <= G.nx(); ++x) acc(dom(x - 1, y, z) || dom(x, y, z), c.grid.u(x, y, z), g.grid.u(x, y, z));
    for (int z = 0; z < G.nz(); ++z) for (int y = 0; y <= G.ny(); ++y) for (int x = 0; x < G.nx(); ++x) acc(dom(x, y - 1, z) || dom(x, y, z), c.grid.v(x, y, z), g.grid.v(x, y, z));
    for (int z = 0; z <= G.nz(); ++z) for (int y = 0; y < G.ny(); ++y) for (int x = 0; x < G.nx(); ++x) acc(dom(x, y, z - 1) || dom(x, y, z), c.grid.w(x, y, z), g.grid.w(x, y, z));
    std::printf("  fills after 60 ticks: max|df| %.2e; max|dvel| on projection faces %.2e m/s, on film/air faces %.2e m/s (not gated)\n", df, dvDomain, dvFilm);
    EXPECT_LT(df, 5e-3);
    EXPECT_LT(dvDomain, 5e-3);
    gpu.destroyVolume(vol);
}

TEST_F(WaterCoreGpuParityTest, GpuSurfaceFieldMatchesCpu) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    Tank g(12, 9, 6, 1.0f / 3.0f);
    g.extra = [](const glm::ivec3& c) { return (c.x == 5 && c.y < 6) ? Occ::Solid : Occ::Air; };   // a wall mid-tank
    g.grid.fillBox({0, 0, 0}, {4, 3, 5}, 1.0f);   // a pool west of the wall
    g.grid.fillBox({0, 4, 0}, {4, 4, 5}, 0.3f);
    g.grid.fillBox({7, 6, 0}, {11, 7, 5}, 1.0f);  // water held up east of the wall (it will fall)
    auto* vol = make(g); ASSERT_NE(vol, nullptr);
    SolverParams prm;
    gpu.step(*vol, prm, kDt, 20, 40);
    gpu.download(*vol, g.grid);
    WaterSurfaceField cpu; extractSurfaceField(g.grid, cpu);
    WaterSurfaceField dev; dev.origin = g.grid.spec().origin; dev.nx = g.grid.nx(); dev.nz = g.grid.nz(); dev.h = g.grid.h();
    dev.cols.resize(cpu.cols.size());
    ASSERT_TRUE(gpu.readSurface(*vol, reinterpret_cast<float*>(dev.cols.data())));
    int runsTotal = 0, multi = 0; float maxDiff = 0.0f;
    for (size_t i = 0; i < cpu.cols.size(); ++i) {
        const SurfaceColumn& a = cpu.cols[i]; const SurfaceColumn& b = dev.cols[i];
        EXPECT_EQ(static_cast<int>(a.runs), static_cast<int>(b.runs)) << "column " << i;
        EXPECT_NEAR(a.solidTopY, b.solidTopY, 1e-6f) << "column " << i;
        for (int r = 0; r < static_cast<int>(a.runs) && r < kSurfaceMaxRuns; ++r) {
            maxDiff = std::max(maxDiff, std::max(std::abs(a.top[r] - b.top[r]), std::abs(a.bottom[r] - b.bottom[r])));
        }
        maxDiff = std::max(maxDiff, std::max(std::abs(a.u - b.u), std::abs(a.w - b.w)));   // E2: the surface velocity too
        runsTotal += static_cast<int>(a.runs); if (a.runs > 1.5f) ++multi;
    }
    std::printf("  surface field: %d runs over %zu columns (%d multi-run), max |CPU - GPU| %.2e\n", runsTotal, cpu.cols.size(), multi, maxDiff);
    EXPECT_LT(maxDiff, 1e-5f);
    EXPECT_GT(runsTotal, 0);
    gpu.destroyVolume(vol);
}

// Phase E1 (docs/WaterCore.md 19): the impulse path through the MANAGER on both backends. The red
// before E1: the manager wrote the kick into the CPU grid only; a GPU volume never saw it (no upload,
// the next download overwrote it, a sleeping volume was skipped) - its surface stayed flat.
TEST_F(WaterCoreGpuParityTest, GpuImpulseMatchesCpu) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    // a 4 x 4 m pond, 2 m deep at 1/3 m cells: voxels x, z in [0, 4), y in [0, 3); solid outside and below
    auto state = [](const glm::ivec3& m) -> int {
        if (m.y < 0 || m.x < 0 || m.z < 0 || m.x >= 36 || m.z >= 36) return 1;
        return 0;
    };
    int sweeps = 0;
    auto run = [&](const std::string& backend, std::vector<float>& tops, double& m0, double& m1) {
        WaterCoreManager mgr(state);
        std::string err;
        if (backend == "gpu") ASSERT_TRUE(mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(3, 2, 3), 1.0f / 3.0f, "eulerian", &err, backend, sweeps);
        ASSERT_NE(id, 0) << err;
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(3, 1, 3), 1.0f, nullptr);
        AvRecord rec;
        ASSERT_TRUE(mgr.step(id, 1, 1.0f / 60.0f, &rec));
        m0 = rec.mass;
        const auto k = mgr.addRadialImpulse(glm::vec3(7.0f, 2.0f, 2.0f), 6.0f, 3.0f, 0.3f);
        EXPECT_EQ(k.volumes, 1);
        for (int chunk = 0; chunk < 3; ++chunk) {
            ASSERT_TRUE(mgr.step(id, 10, 1.0f / 60.0f, &rec));
            float dev = 0.0f; double nearM = 0, farM = 0; int nn = 0, nf = 0;
            const auto& f0 = mgr.surfaceFields()[0];
            for (int z = 0; z < f0.nz; ++z) for (int x = 0; x < f0.nx; ++x) {
                const auto& c = f0.at(x, z); if (c.runs < 0.5f) continue;
                dev = std::max(dev, std::abs(c.top[0] - 2.0f));
                if (x == f0.nx - 1) { nearM += c.top[0]; ++nn; } if (x == 0) { farM += c.top[0]; ++nf; }
            }
            std::printf("  %s after %d ticks: near %+.3f far %+.3f, max deviation %.4f m, residual %.3g, sweeps %d\n", backend.c_str(), 10 * (chunk + 1), nn ? nearM / nn - 2.0 : 0.0, nf ? farM / nf - 2.0 : 0.0, dev, rec.rbgsResidual, rec.gpuSweeps);
        }
        m1 = rec.mass;
        const auto& fields = mgr.surfaceFields();
        ASSERT_EQ(fields.size(), 1u);
        tops.clear();
        for (const auto& c : fields[0].cols) tops.push_back(c.runs > 0.5f ? c.top[0] : -1.0f);
    };
    std::vector<float> cpu, gpu2; double cm0 = 0, cm1 = 0, gm0 = 0, gm1 = 0;
    run("cpu", cpu, cm0, cm1);
    run("gpu", gpu2, gm0, gm1);
    ASSERT_EQ(cpu.size(), gpu2.size());
    float devCpu = 0.0f, devGpu = 0.0f, maxDiff = 0.0f;
    for (size_t i = 0; i < cpu.size(); ++i) {
        if (cpu[i] > 0.0f) devCpu = std::max(devCpu, std::abs(cpu[i] - 2.0f));
        if (gpu2[i] > 0.0f) devGpu = std::max(devGpu, std::abs(gpu2[i] - 2.0f));
        if (cpu[i] > 0.0f && gpu2[i] > 0.0f) maxDiff = std::max(maxDiff, std::abs(cpu[i] - gpu2[i]));
    }
    std::printf("  impulse parity: max surface deviation CPU %.4f m, GPU %.4f m, max |CPU - GPU| %.4f m; mass CPU %.6f -> %.6f, GPU %.6f -> %.6f\n",
                devCpu, devGpu, maxDiff, cm0, cm1, gm0, gm1);
    EXPECT_GT(devCpu, 0.05f) << "the kick moved the CPU pond";
    EXPECT_GT(devGpu, 0.05f) << "the kick moved the GPU pond (the E1 bug left it flat)";
    // NOT parity (measured 2026-10-09, docs/WaterCore.md 19.2): under a strong kick the GPU responds
    // ~35 % weaker than the CPU even with a converged projection (80 sweeps, residual 4e-4) - an open
    // Phase C defect. This floor only stops it getting worse while it is diagnosed.
    EXPECT_GE(devGpu, 0.5f * devCpu) << "the GPU response must stay within 2x of the CPU's (open defect: they differ)";
    EXPECT_NEAR(gm1, gm0, 1e-4 * gm0);
}

// Phase E1 diagnostic: the same kicked pond grid stepped by the CPU solver and by the GPU kernels
// (no manager in between), compared after 1, 5 and 20 ticks: near-side mean surface and the largest
// face-velocity difference. Splits "the GPU differs on the kick" from "the GPU differs after it".
TEST_F(WaterCoreGpuParityTest, GpuKickedPondParity) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const float h = 1.0f / 3.0f;
    for (int ticks : {1, 5, 20}) {
        Tank c(12, 9, 12, h), g(12, 9, 12, h);
        c.grid.fillBox({0, 0, 0}, {11, 5, 11}, 1.0f);
        g.grid.fillBox({0, 0, 0}, {11, 5, 11}, 1.0f);
        WaterSolver cpu(c.grid, c.query());
        cpu.addRadialImpulse(glm::vec3(7.0f, 2.0f, 2.0f), 6.0f, 3.0f, 0.3f);
        { WaterSolver kicker(g.grid, g.query()); kicker.addRadialImpulse(glm::vec3(7.0f, 2.0f, 2.0f), 6.0f, 3.0f, 0.3f); }
        float uDiff0 = 0.0f;
        for (size_t i = 0; i < c.grid.uData().size(); ++i) uDiff0 = std::max(uDiff0, std::abs(c.grid.uData()[i] - g.grid.uData()[i]));
        auto* vol = make(g); ASSERT_NE(vol, nullptr);
        for (int k = 0; k < ticks; ++k) cpu.step(kDt);
        SolverParams prm;
        const auto st = gpu.step(*vol, prm, kDt, ticks, 80);
        gpu.download(*vol, g.grid);
        auto nearMean = [](WaterGrid& gr) { double s = 0; for (int z = 0; z < 12; ++z) s += gr.surfaceWorldY(11, z); return s / 12.0 - 2.0; };
        float uDiff = 0.0f, vDiff = 0.0f, fDiff = 0.0f;
        for (size_t i = 0; i < c.grid.uData().size(); ++i) uDiff = std::max(uDiff, std::abs(c.grid.uData()[i] - g.grid.uData()[i]));
        for (size_t i = 0; i < c.grid.vData().size(); ++i) vDiff = std::max(vDiff, std::abs(c.grid.vData()[i] - g.grid.vData()[i]));
        for (size_t i = 0; i < c.grid.fData().size(); ++i) fDiff = std::max(fDiff, std::abs(c.grid.fData()[i] - g.grid.fData()[i]));
        std::printf("  kicked pond after %2d ticks: near CPU %+.4f GPU %+.4f m; max |du| %.4f |dv| %.4f |df| %.4f (kick identical: %.1e); GPU residual %.2e\n",
                    ticks, nearMean(c.grid), nearMean(g.grid), uDiff, vDiff, fDiff, uDiff0, st.rbgsResidualMax);
        gpu.destroyVolume(vol);
    }
}


// ---- docs/WaterCore.md 20 (M1): moving solids on the GPU. The fields that do not read the water (s, the
// wake, fresh bodies) come from WaterSolver::updateSolidFields on the host; the rates (q, the ledger marks)
// are computed on the device from its own fill. Same predictions as WaterSolidTest (20.8).
namespace {
constexpr float kH3 = 1.0f / 3.0f;
void fillPond(WaterGrid& g) { g.fillBox({0, 0, 0}, {11, 3, 11}, 1.0f); g.fillBox({0, 4, 0}, {11, 4, 11}, 0.5f); }
float colTop(const std::vector<SurfaceColumn>& cols, int nx, int x, int z) {
    const SurfaceColumn& c = cols[static_cast<size_t>(x) + static_cast<size_t>(nx) * z];
    const int runs = static_cast<int>(c.runs + 0.5f);
    return runs > 0 ? c.top[std::min(runs, kSurfaceMaxRuns) - 1] : std::numeric_limits<float>::quiet_NaN();
}
double depthAway(const WaterGrid& g, int lo, int hi) {   // mean water depth (sum f h) outside [lo, hi] in x and z
    double sum = 0.0; int n = 0;
    for (int z = 0; z < g.nz(); ++z) for (int x = 0; x < g.nx(); ++x) {
        if (x >= lo && x <= hi && z >= lo && z <= hi) continue;
        double d = 0.0; for (int y = 0; y < g.ny(); ++y) d += g.f(x, y, z);
        sum += d * g.h(); ++n;
    }
    return sum / n;
}
double trappedAir(const WaterGrid& g) {   // WaterSolidTest's T10 measure: (1 - f - s) V under a full cell
    double air = 0.0;
    for (int z = 0; z < g.nz(); ++z) for (int x = 0; x < g.nx(); ++x) {
        bool fullAbove = false;
        for (int y = g.ny() - 1; y >= 0; --y) {
            const float fs = g.f(x, y, z) + g.s(x, y, z);
            if (fullAbove && fs < 0.999f) air += (1.0 - fs) * g.cellVolume();
            if (fs >= 0.999f) fullAbove = true;
        }
    }
    return air;
}
} // namespace

// The device's rates (q and the ledger marks) equal the CPU's, cell for cell, from the same fill.
TEST_F(WaterCoreGpuParityTest, GpuSolidRatesMatchCpu) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    Tank c(12, 9, 12, kH3), g(12, 9, 12, kH3);
    fillPond(c.grid); fillPond(g.grid);
    // a big body half under (overflowing cells, open faces), a small one fully inside water (a body-filled
    // cell -> ledger), and a fresh one
    MovingSolid a; a.centre = {2.0f, 1.45f, 2.0f}; a.halfExtents = glm::vec3(0.5f);
    MovingSolid b; b.centre = {3.17f, 0.5f, 1.0f}; b.halfExtents = glm::vec3(kH3 / 2.0f);
    MovingSolid fr; fr.centre = {0.8f, 0.6f, 3.2f}; fr.halfExtents = glm::vec3(0.2f); fr.fresh = true;
    WaterSolver cpu(c.grid, c.query());
    cpu.setMovingSolids({a, b, fr}, kDt);
    auto* vol = make(g); ASSERT_NE(vol, nullptr);
    WaterSolver aux(g.grid, g.query());
    aux.updateSolidFields({a, b, fr}, kDt);
    gpu.setSolids(*vol, g.grid.sData(), aux.solidWake(), aux.solidFresh(), kDt);
    SolverParams prm;
    gpu.step(*vol, prm, kDt, 1, 40);   // the rates are computed at the start of the step, before any substep
    std::vector<float> q, ledger, sg;
    gpu.readCells(*vol, "solid_q", q); gpu.readCells(*vol, "solid_ledger", ledger); gpu.readCells(*vol, "solid_s", sg);
    double dq = 0.0, qmax = 0.0; long ledgerMismatch = 0, rateCells = 0;
    for (size_t i = 0; i < q.size(); ++i) {
        dq = std::max(dq, static_cast<double>(std::abs(q[i] - c.grid.qData()[i])));
        qmax = std::max(qmax, static_cast<double>(c.grid.qData()[i]));
        if ((ledger[i] > 0.5f) != (cpu.solidFresh()[i] != 0)) ++ledgerMismatch;
        if (c.grid.qData()[i] > 0.0f) ++rateCells;
        EXPECT_EQ(sg[i], c.grid.sData()[i]);
    }
    std::printf("  rates: %ld rate cells, max q %.4f m^3/s, max |dq| %.2e, ledger mismatches %ld\n", rateCells, qmax, dq, ledgerMismatch);
    EXPECT_GT(rateCells, 0);
    EXPECT_LT(dq, 1e-5 * std::max(qmax, 1.0));
    EXPECT_EQ(ledgerMismatch, 0);
    gpu.destroyVolume(vol);
}

struct GpuDrop { float entryMin = 1e9f, ring1Max = -1e9f, ring2Early = -1e9f, absMax = 0.0f; double m0 = 0, m1 = 0, air = 0; bool finite = true; };

// T4 / T10 on the device: one 1/3 m stone at 6 m/s from just above the rested pond to its floor, then held.
GpuDrop gpuDrop(WaterCoreGpu& gpu, WaterCoreGpu::Volume*& vol, Tank& g, bool solids, int holdTicks) {
    GpuDrop r;
    SolverParams prm;
    gpu.step(*vol, prm, kDt, 60, 40);   // rest
    gpu.download(*vol, g.grid); r.m0 = g.grid.totalMass();
    WaterSolver aux(g.grid, g.query());
    const float cx = 6.5f * kH3, he = kH3 / 2.0f;
    MovingSolid b; b.centre = {cx, 1.5f + he + 0.02f, cx}; b.halfExtents = glm::vec3(he); b.velocity = {0.0f, -6.0f, 0.0f};
    std::vector<SurfaceColumn> cols(static_cast<size_t>(12 * 12));
    for (int k = 0; k < 60 + holdTicks; ++k) {
        b.centre.y = std::max(he, b.centre.y - 6.0f * kDt);
        if (solids) { aux.updateSolidFields({b}, kDt); gpu.setSolids(*vol, g.grid.sData(), aux.solidWake(), aux.solidFresh(), kDt); }
        gpu.step(*vol, prm, kDt, 1, 40);
        if (k < 60) {
            gpu.readSurface(*vol, reinterpret_cast<float*>(cols.data()));
            const float e = colTop(cols, 12, 6, 6), r1 = colTop(cols, 12, 7, 6), r2 = colTop(cols, 12, 8, 6);
            if (!std::isnan(e)) r.entryMin = std::min(r.entryMin, e - 1.5f);
            if (!std::isnan(r1)) r.ring1Max = std::max(r.ring1Max, r1 - 1.5f);
            if (!std::isnan(r2) && k < 30) r.ring2Early = std::max(r.ring2Early, r2 - 1.5f);
            for (int z = 0; z < 12; ++z) for (int x = 0; x < 12; ++x) { const float y = colTop(cols, 12, x, z); if (!std::isnan(y)) r.absMax = std::max(r.absMax, std::abs(y - 1.5f)); }
        }
    }
    gpu.download(*vol, g.grid);
    for (float v : g.grid.fData()) if (!std::isfinite(v)) r.finite = false;
    r.m1 = g.grid.totalMass(); r.air = trappedAir(g.grid);
    return r;
}

TEST_F(WaterCoreGpuParityTest, GpuStoneMakesACraterAndTheCavityCloses) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    Tank g(12, 9, 12, kH3), ctl(12, 9, 12, kH3);
    fillPond(g.grid); fillPond(ctl.grid);
    auto* vol = make(g); ASSERT_NE(vol, nullptr);
    auto* vctl = make(ctl); ASSERT_NE(vctl, nullptr);
    const GpuDrop on = gpuDrop(gpu, vol, g, true, 300), off = gpuDrop(gpu, vctl, ctl, false, 0);
    const double stone = std::pow(kH3, 3.0);
    std::printf("  GPU solids ON : entry %+.3f m, 0.33 m %+.3f m, 0.67 m (0.5 s) %+.3f m, mass %.7f -> %.7f, trapped air 6 s after %.5f m^3, finite %d\n",
                on.entryMin, on.ring1Max, on.ring2Early, on.m0, on.m1, on.air, on.finite ? 1 : 0);
    std::printf("  GPU solids OFF: largest |dev| %.4f m\n", off.absMax);
    EXPECT_TRUE(on.finite);
    EXPECT_LE(on.entryMin, -0.08f);
    EXPECT_GE(on.ring1Max, 0.03f);
    EXPECT_GE(on.ring2Early, 0.015f);
    EXPECT_NEAR(on.m1, on.m0, 1e-4);   // float32 fills summed on the device (the GPU pump test's order)
    EXPECT_LE(on.air, 0.1 * stone) << "the cavity's air stayed trapped on the GPU";
    EXPECT_LT(off.absMax, 0.01f) << "control: no body, no splash";
    gpu.destroyVolume(vol); gpu.destroyVolume(vctl);
}

// 26.9 diagnostic: a body RESTING on the bottom of a pond whose top layer is at the threshold (fillPond: 0.5) - the live
// bench's stones after a drop. The live slosh lost 1.5-2.2e-4 m^3 with the still-water guess on and 5e-6 with it off (droplets
// off made no difference). Lowered at 0.5 m/s to the floor, then 25 s at the engine's frame step; mass every 2.5 s, guess on
// vs off. DIAG_HALF = the cube's half extent (m, default 0.17 - the bench's 1/3 m stone).
TEST_F(WaterCoreGpuParityTest, DISABLED_DiagRestingBodyKeepsTheWater) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const bool guessWas = WaterCoreGpu::s_stillWaterGuess;
    const float half = std::getenv("DIAG_HALF") ? static_cast<float>(std::atof(std::getenv("DIAG_HALF"))) : 0.17f;
    const float dt = 1.0f / 165.0f;
    for (bool guess : {false, true}) {
        WaterCoreGpu::s_stillWaterGuess = guess;
        Tank g(12, 9, 12, kH3);
        fillPond(g.grid);
        auto* vol = make(g); ASSERT_NE(vol, nullptr);
        SolverParams prm;
        const double m0 = g.grid.totalMass();
        WaterSolver aux(g.grid, g.query());
        MovingSolid b; b.centre = {2.0f, 2.6f, 2.0f}; b.halfExtents = glm::vec3(half);
        std::printf("  %s (cube half %.2f m)\n", guess ? "GUESS" : "cold ", half);
        const int n = static_cast<int>(27.5f / dt);
        double dropped = 0.0, births = 0.0;
        WaterCoreGpu::s_stageCapture = true; WaterCoreGpu::s_stageCalls = 0;
        for (double& d : WaterCoreGpu::s_stageDm) d = 0.0;
        for (int k = 0; k < n; ++k) {
            b.centre.y = std::max(half + 0.001f, 2.6f - 0.5f * (k + 1) * dt);
            aux.updateSolidFields({b}, dt);
            gpu.setSolids(*vol, g.grid.sData(), aux.solidWake(), aux.solidFresh(), dt);
            const GpuStepStats st = gpu.step(*vol, prm, dt, 1, std::getenv("DIAG_SWEEPS") ? std::atoi(std::getenv("DIAG_SWEEPS")) : 27);
            dropped += st.residueDropped;
            { std::vector<float> br; if (gpu.readBirths(*vol, br)) for (size_t c = 0; c + 7 < br.size(); c += 8) births += static_cast<double>(br[c]) * kH3 * kH3 * kH3; }   // cell fractions -> m^3
            if (k % static_cast<int>(2.5f / dt) == 0 || k == n - 1) {
                WaterGrid w(g.grid.spec()); gpu.download(*vol, w);
                {   // TMP: where the water sits - negative fills, water inside the body, over-full cells
                    double neg = 0.0, over = 0.0, inBody = 0.0; int nNeg = 0, nOver = 0;
                    for (size_t c = 0; c < w.cellCount(); ++c) {
                        const float fv = w.fData()[c], sv = g.grid.sData()[c];
                        if (fv < 0.0f) { neg += fv; ++nNeg; }
                        if (fv + sv > 1.0f + 1e-6f) { over += fv + sv - 1.0f; ++nOver; }
                        if (sv > 0.0f) inBody += fv;
                    }
                    std::printf("      negative f: %d cells, %.3e; over-full (f + s > 1): %d cells, %.3e; water in body cells %.4f cells\n", nNeg, neg, nOver, over, inBody);
                }
                std::printf("    t %5.1f s: body y %.2f, mass %+.3e m^3 (residue dropped, counted %.3e; droplet births %.3e), ke %.1e, asleep %d, residual %.2e\n", (k + 1) * dt, b.centre.y, w.totalMass() - m0, dropped, births, st.kineticEnergy, int(st.asleep), st.rbgsResidualMax);
                {   // 26.10: exact stage sums (cells -> m^3)
                    static const char* nm[WaterCoreGpu::kStages] = {"between calls", "sources", "gather", "carry", "ledger", "compaction", "rest", "residue", "births"};
                    std::printf("      stages (%ld calls):", WaterCoreGpu::s_stageCalls);
                    for (int q = 0; q < WaterCoreGpu::kStages; ++q) std::printf(" %s %+.2e", nm[q], WaterCoreGpu::s_stageDm[q] * kH3 * kH3 * kH3);
                    std::printf("\n");
                }
            }
        }
        gpu.destroyVolume(vol);
        WaterCoreGpu::s_stageCapture = false;
    }
    WaterCoreGpu::s_stillWaterGuess = guessWas;
}

// 26.9 diagnostic: the resting-body loss TICK BY TICK once the body has settled (guess on): the worst ticks' changed cells,
// with each cell's body fraction s - where does the water go?
TEST_F(WaterCoreGpuParityTest, DISABLED_DiagRestingBodyWhereItGoes) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const float dt = 1.0f / 165.0f, half = 0.17f;
    Tank g(12, 9, 12, kH3);
    fillPond(g.grid);
    auto* vol = make(g); ASSERT_NE(vol, nullptr);
    SolverParams prm;
    WaterSolver aux(g.grid, g.query());
    MovingSolid b; b.centre = {2.0f, 2.6f, 2.0f}; b.halfExtents = glm::vec3(half);
    WaterGrid prev(g.grid.spec()), now(g.grid.spec());
    double lossSum = 0.0, gainSum = 0.0;
    struct Ev { double dm; int tick; std::vector<std::pair<size_t, std::pair<float, float>>> cells; };
    std::vector<Ev> worst;
    const int settle = static_cast<int>(10.0f / dt), n = settle + static_cast<int>(8.0f / dt);
    for (int k = 0; k < n; ++k) {
        b.centre.y = std::max(half + 0.001f, 2.6f - 0.5f * (k + 1) * dt);
        aux.updateSolidFields({b}, dt);
        gpu.setSolids(*vol, g.grid.sData(), aux.solidWake(), aux.solidFresh(), dt);
        if (k == settle) gpu.download(*vol, prev);
        gpu.step(*vol, prm, dt, 1, 27);
        if (k < settle) continue;
        gpu.download(*vol, now);
        const double dm = now.totalMass() - prev.totalMass();
        if (dm < 0) lossSum += dm; else gainSum += dm;
        if (dm < -2e-8) {
            Ev e; e.dm = dm; e.tick = k;
            for (size_t i = 0; i < now.cellCount(); ++i) if (now.fData()[i] != prev.fData()[i]) e.cells.push_back({i, {prev.fData()[i], now.fData()[i]}});
            worst.push_back(e);
            std::sort(worst.begin(), worst.end(), [](const Ev& a, const Ev& c) { return a.dm < c.dm; });
            if (worst.size() > 2) worst.resize(2);
        }
        prev = now;
    }
    std::printf("  settled 8 s: losses %.3e, gains %.3e m^3\n", lossSum, gainSum);
    for (const Ev& e : worst) {
        // the cells with the largest |change|, and the net change by body fraction band
        double netBody = 0.0, netFree = 0.0;
        for (auto& c : e.cells) { const double d = c.second.second - c.second.first; (g.grid.sData()[c.first] > 0.0f ? netBody : netFree) += d; }
        std::printf("  tick %d: dm %.3e m^3 (%.3e cells), %zu cells changed; net in body cells %.3e, elsewhere %.3e cells\n", e.tick, e.dm, e.dm / (kH3 * kH3 * kH3), e.cells.size(), netBody, netFree);
        std::vector<std::pair<size_t, std::pair<float, float>>> cs = e.cells;
        std::sort(cs.begin(), cs.end(), [](const auto& a, const auto& c) { return std::abs(a.second.second - a.second.first) > std::abs(c.second.second - c.second.first); });
        for (size_t q = 0; q < std::min<size_t>(cs.size(), 14); ++q) {
            const size_t i = cs[q].first; const int x = int(i % 12), y = int((i / 12) % 9), z = int(i / 108);
            std::printf("     (%d,%d,%d) s %.3f: %.7f -> %.7f (%+.2e)\n", x, y, z, g.grid.sData()[i], cs[q].second.first, cs[q].second.second, cs[q].second.second - cs[q].second.first);
        }
    }
    gpu.destroyVolume(vol);
}

// T2 on the device: a 1 m cube lowered at 0.2 m/s until fully under raises the level by 1/16 m.
TEST_F(WaterCoreGpuParityTest, GpuLoweredCubeRaisesTheLevel) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    Tank g(12, 9, 12, kH3);
    fillPond(g.grid);
    auto* vol = make(g); ASSERT_NE(vol, nullptr);
    SolverParams prm;
    const double m0 = g.grid.totalMass();
    WaterSolver aux(g.grid, g.query());
    MovingSolid b; b.centre = {2.0f, 2.1f, 2.0f}; b.halfExtents = glm::vec3(0.5f);
    for (int k = 0; k < 405 + 300; ++k) {
        if (k < 405) b.centre.y = 2.1f - 0.2f * (k + 1) * kDt;
        aux.updateSolidFields({b}, kDt);
        gpu.setSolids(*vol, g.grid.sData(), aux.solidWake(), aux.solidFresh(), kDt);
        gpu.step(*vol, prm, kDt, 1, 40);
    }
    gpu.download(*vol, g.grid);
    bool finite = true; float overlap = 0.0f;
    for (size_t i = 0; i < g.grid.cellCount(); ++i) { if (!std::isfinite(g.grid.fData()[i])) finite = false; overlap = std::max(overlap, g.grid.fData()[i] + g.grid.sData()[i]); }
    const double level = depthAway(g.grid, 4, 7);
    std::printf("  GPU lowered cube: level away %.4f m (predicted 1.5625), max f+s %.4f, mass %.7f -> %.7f, finite %d\n", level, overlap, m0, g.grid.totalMass(), finite ? 1 : 0);
    EXPECT_TRUE(finite);
    EXPECT_NEAR(level, 1.5625, 0.005);
    EXPECT_LE(overlap, 1.0f + 1e-3f);
    EXPECT_NEAR(g.grid.totalMass(), m0, 1e-4);
    gpu.destroyVolume(vol);
}
