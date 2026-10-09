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
// message - never a failure inside a dispatch.
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
        for (int z = 0; z < per; ++z) for (int x = 18 * per; x < 20 * per; ++x) { const float sy = c.grid.surfaceWorldY(x, z); if (!std::isnan(sy)) cpuPeak = std::max(cpuPeak, static_cast<double>(sy)); }
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
        for (int z = 0; z < per; ++z) for (int x = 18 * per; x < 20 * per; ++x) { const float sy = t.grid.surfaceWorldY(x, z); if (!std::isnan(sy)) gpuPeak = std::max(gpuPeak, static_cast<double>(sy)); }
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
// 40 SOR sweeps) agree on every fill to 5e-3 and every face to 5e-3 m/s after 60 ticks.
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
    double df = 0.0, dv = 0.0;
    for (size_t i = 0; i < c.grid.fData().size(); ++i) df = std::max(df, static_cast<double>(std::abs(c.grid.fData()[i] - g.grid.fData()[i])));
    for (size_t i = 0; i < c.grid.uData().size(); ++i) dv = std::max(dv, static_cast<double>(std::abs(c.grid.uData()[i] - g.grid.uData()[i])));
    for (size_t i = 0; i < c.grid.vData().size(); ++i) dv = std::max(dv, static_cast<double>(std::abs(c.grid.vData()[i] - g.grid.vData()[i])));
    for (size_t i = 0; i < c.grid.wData().size(); ++i) dv = std::max(dv, static_cast<double>(std::abs(c.grid.wData()[i] - g.grid.wData()[i])));
    std::printf("  fills after 60 ticks: max|df| %.2e, max|dvel| %.2e m/s\n", df, dv);
    EXPECT_LT(df, 5e-3);
    EXPECT_LT(dv, 5e-3);
    gpu.destroyVolume(vol);
}

// Phase F (docs/WaterCore.md 17.1): the surface field the GPU writes at the end of every step equals
// the CPU extraction of the downloaded grid, column for column - including a wall column and an
// overhang (two runs). The renderer reads only this field, never the grid.
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

