// WaterCoreGpuParityTest — docs/WaterCore.md §15.11: the GPU core is accepted on parity with the
// CPU reference, red first. Needs a Vulkan device: the fixture GTEST_SKIPs without one (never a pass).
// Slice 1 covers the FILL transport: still water, hydrostatics, mass, determinism, the dam-break front.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"
#include "core/water/WaterCore.h"
#include "core/water/WaterCoreGpu.h"
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
    const float* p = static_cast<const float*>(vol->p.mapped);
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
        const float* p = static_cast<const float*>(vol->p.mapped);
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
