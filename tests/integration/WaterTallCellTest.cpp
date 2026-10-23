// WaterTallCellTest - docs/WaterCore.md 27.22: TALL CELLS. Below a floor, each 1/9 m column of water is ONE cell (its f a volume in
// cells, hydrostatic, one side velocity for its height); the band above is the ordinary solver. The gates run the SAME water twice -
// all fine (the oracle: the same code, so a difference is the tall cells') and tall - and compare what the water does. Needs a Vulkan
// device: the fixture GTEST_SKIPs without one (never a pass).
//
// The rigs: walled boxes (no shore - long waves reflect: the hard case), the bed = the box floor, 1/9 m, 2-tall bricks, sparse with
// ground bricks, the partial wake OFF (every stored brick steps), 20 sweeps, the solver's default parameters.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"
#include "core/water/WaterCore.h"
#include "core/water/WaterCoreGpu.h"
#include "core/water/WaterCoreManager.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Phyxel::Core::Water;
using Phyxel::Testing::VulkanTestFixture;

namespace {
constexpr float kDt = 1.0f / 60.0f;
constexpr float kH = 1.0f / 9.0f;
constexpr int kSweeps = 20;

// a walled box at `origin` (cells), nx x ny x nz, filled to `depth` cells (fractional top cell allowed); `tilt` (cells) raises the
// surface linearly along x from -tilt at x = 0 to +tilt at the far end (the slosh's first mode, roughly)
WaterGrid pond(const glm::ivec3& origin, const glm::ivec3& dims, float depth, float tilt = 0.0f) {
    WaterGrid g(GridSpec{origin, dims, kH});
    for (int z = 0; z < dims.z; ++z) for (int y = 0; y < dims.y; ++y) for (int x = 0; x < dims.x; ++x) {
        g.occ(x, y, z) = Occ::Air;
        const float d = depth + tilt * (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(dims.x) - 1.0f);
        g.f(x, y, z) = std::clamp(d - static_cast<float>(y), 0.0f, 1.0f);
    }
    return g;
}

double columnDepth(const WaterGrid& g, int x, int z) { double s = 0.0; for (int y = 0; y < g.ny(); ++y) s += g.f(x, y, z); return s * kH; }
double gridMass(const WaterGrid& g) { double s = 0.0; for (float f : g.fData()) s += f; return s * kH * kH * kH; }

// the floor for a band of B metres over a rest level of `restCells` cells (WaterCoreManager::updateTall's rule, 2-tall bricks, origin y 0)
int tallLayer(float restCells, float bandM) {
    const int cyFloor = static_cast<int>(std::floor((restCells * kH - bandM) / kH));
    return ((cyFloor - 1) / 2) * 2;
}
std::vector<int> openHeights(const WaterGrid& g, int T) {   // every column open from the box floor: H = T + 1
    std::vector<int> hs(static_cast<size_t>(g.nx()) * g.nz(), T + 1);
    return hs;
}
} // namespace

class WaterTallCellTest : public VulkanTestFixture {
protected:
    WaterCoreGpu gpu;
    SolverParams prm;
    void SetUp() override {
        prm.restTicks = 1 << 30;   // the gates measure stepping water: it never sleeps here
        VulkanTestFixture::SetUp();
        if (!isVulkanAvailable()) return;
        std::string err;
        if (!gpu.init(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) GTEST_SKIP() << "WaterCoreGpu init: " << err;
    }
    void TearDown() override { gpu.shutdown(); VulkanTestFixture::TearDown(); }

    WaterCoreGpu::Volume* make(const WaterGrid& g) {
        std::string err;
        BrickOptions o; o.sparse = true; o.height = 2; o.ground = 1;
        WaterCoreGpu::Volume* v = gpu.createVolume(g.spec(), &err, false, 0, o);
        EXPECT_NE(v, nullptr) << err;
        if (v) gpu.upload(*v, g);
        return v;
    }
    void tall(WaterCoreGpu::Volume& v, const WaterGrid& g, int T) {
        std::string err;
        ASSERT_TRUE(gpu.setTall(v, T, openHeights(g, T), &err)) << err;
    }
};

// RegridIsExact (half 1) + StillWaterStaysStill: a 2 m pond at rest, collapsed (B = 0.5 m) and stepped 120 ticks, then expanded:
// velocity ~0, the surface flat, the books exact, and the expanded grid equal to the start cell for cell (a full column of 13 cells
// spreads back as exactly 1.0 each).
TEST_F(WaterTallCellTest, StillWaterStaysStillAndTheRegridIsExact) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const glm::ivec3 dims(72, 24, 72);   // 8 x 2.67 x 8 m
    WaterGrid g = pond(glm::ivec3(0), dims, 18.0f);   // 2 m
    WaterCoreGpu::Volume* v = make(g);
    ASSERT_NE(v, nullptr);
    const int T = tallLayer(18.0f, 0.5f);
    tall(*v, g, T);
    const double m0 = gridMass(g);
    GpuStepStats st;
    double dropped = 0.0;
    for (int k = 0; k < 120; ++k) { st = gpu.step(*v, prm, kDt, 1, kSweeps); dropped += st.residueDropped; }
    gpu.settle(*v);
    EXPECT_TRUE(v->tallActive);
    EXPECT_EQ(v->tallColumns, dims.x * dims.z);
    WaterGrid out(g.spec());
    gpu.download(*v, out);
    double dMin = 1e9, dMax = -1e9;
    for (int z = 0; z < dims.z; ++z) for (int x = 0; x < dims.x; ++x) { const double d = columnDepth(out, x, z); dMin = std::min(dMin, d); dMax = std::max(dMax, d); }
    std::printf("  still 2 m pond, tall (T %d, %d columns): max speed %.2e m/s, surface spread %.3f mm, mass %.9f -> %.9f m^3 (reduction %.9f + residue dropped %.9f)\n",
                T, v->tallColumns, st.maxSpeed, (dMax - dMin) * 1000.0, m0, gridMass(out), st.totalMass, dropped);
    EXPECT_LT(st.maxSpeed, 1e-3);
    EXPECT_LT((dMax - dMin) * 1000.0, 0.1);
    EXPECT_NEAR(st.totalMass + dropped, m0, 1e-6 * m0);
    // expand: the grid back to ordinary cells - equal to what it held just before (the host's spread of the same tall cells)
    WaterGrid before(g.spec());
    gpu.download(*v, before);
    gpu.clearTall(*v);
    gpu.step(*v, prm, kDt, 1, kSweeps);
    gpu.settle(*v);
    EXPECT_FALSE(v->tallActive);
    gpu.download(*v, out);
    long differ = 0; double worst = 0.0;
    for (size_t i = 0; i < out.fData().size(); ++i) { const double d = std::abs(out.fData()[i] - before.fData()[i]); worst = std::max(worst, d); if (d > 1e-4) ++differ; }
    std::printf("  expanded (and one call stepped): %ld cells differ from just before by > 1e-4 (worst %.2e), mass %.9f (before %.9f)\n", differ, worst, gridMass(out), gridMass(before));
    EXPECT_EQ(differ, 0);
    EXPECT_NEAR(gridMass(out), gridMass(before), 1e-6 * m0);
    gpu.destroyVolume(v);
}

// DIAG: the stone, fine vs tall, call by call - max speed, kinetic energy, the pressure residual and the centre column's surface
TEST_F(WaterTallCellTest, DISABLED_DiagStoneCallByCall) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const glm::ivec3 dims(72, 24, 72);
    for (int tallOn = 0; tallOn < 2; ++tallOn) {
        WaterGrid g = pond(glm::ivec3(0), dims, 18.0f);
        WaterCoreGpu::Volume* v = make(g);
        if (!v) return;
        if (tallOn) tall(*v, g, tallLayer(18.0f, 0.5f));
        gpu.step(*v, prm, kDt, 1, kSweeps); gpu.settle(*v);
        gpu.queueMomentum(*v, glm::vec3(4.0f, 1.9f, 4.0f), 1.0f / 3.0f, glm::vec3(0.0f, -0.148f, 0.0f));
        WaterGrid out(g.spec());
        for (int k = 0; k < 240; ++k) {
            const GpuStepStats st = gpu.step(*v, prm, kDt, 1, kSweeps); gpu.settle(*v);
            if (k % 20 == 0) {
                gpu.download(*v, out);
                std::printf("  %s k %2d: maxSpeed %.4f KE %.3e residual %.3e substeps %d | depth centre %.2f mm, 1 m %.2f mm, 3 m %.2f mm | u at (45, y, 36) y 0/6/12/14/17: %.4f %.4f %.4f %.4f %.4f\n",
                            tallOn ? "TALL" : "fine", k, st.maxSpeed, st.kineticEnergy, st.rbgsResidualMax, st.substepsLast,
                            (columnDepth(out, 36, 36) - 2.0) * 1000, (columnDepth(out, 45, 36) - 2.0) * 1000, (columnDepth(out, 63, 36) - 2.0) * 1000,
                            out.u(45, 0, 36), out.u(45, 6, 36), out.u(45, 12, 36), out.u(45, 14, 36), out.u(45, 17, 36));
            }
        }
        gpu.destroyVolume(v);
    }
}

// LongWaveMatchesAllFine: a long pulse along a 24 x 1 m channel, 1 m and 2 m deep - the first 3 m raised 3 cm; its speed between 12 m
// and 20 m (the time its front first lifts the surface 1 mm at each station) all fine vs tall (B = 0.5 m), within 5 %; its height at
// 20 m within 10 %. Theory: sqrt(g d) (4.4 m/s at 2 m). RED CONTROL: all fine with only the band's depth of water (a false bottom at
// the floor) - its speed must be outside the gate (the test sees depth). (A slosh in a box was the first rig: the solver damps it to
// a tenth in 10 s - too few crossings for a 5 % gate.)
TEST_F(WaterTallCellTest, LongWaveMatchesAllFine) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    struct Run { double speed = 0, peak20 = 0, ms = 0; };
    auto run = [&](float depthCells, bool tallOn, int bandCellsOnly) {
        const glm::ivec3 dims(216, static_cast<int>(depthCells) + 8, 9);
        const float d = bandCellsOnly > 0 ? static_cast<float>(bandCellsOnly) : depthCells;
        WaterGrid g = pond(glm::ivec3(0), dims, d);
        for (int z = 0; z < dims.z; ++z) for (int x = 0; x < 27; ++x) for (int y = 0; y < dims.y; ++y)   // the first 3 m: +0.27 cells (3 cm) - long against the depth
            g.f(x, y, z) = std::clamp(d + 0.27f - static_cast<float>(y), 0.0f, 1.0f);
        WaterCoreGpu::Volume* v = make(g);
        Run r;
        if (!v) return r;
        if (tallOn) tall(*v, g, tallLayer(depthCells, 0.5f));
        WaterGrid out(g.spec());
        double t12 = -1, t20 = -1, ms = 0; int calls = 0;
        for (int k = 1; k <= 1200 && t20 < 0; ++k) {   // 20 s: the false bottom's slower front reaches 20 m too
            const auto t0 = std::chrono::steady_clock::now();
            gpu.step(*v, prm, kDt, 1, kSweeps); gpu.settle(*v);
            ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); ++calls;
            if (k % 2) continue;
            gpu.download(*v, out);
            auto eta = [&](int x) { double s = 0; for (int z = 0; z < dims.z; ++z) s += columnDepth(out, x, z); return s / dims.z - d * kH; };
            const double e12 = eta(108), e20 = eta(180);
            if (t12 < 0 && e12 > 0.001) t12 = k * kDt;
            if (t20 < 0 && e20 > 0.001) t20 = k * kDt;
        }
        // the peak at 20 m: keep stepping a second after the front
        for (int k = 0; k < 60; ++k) {
            gpu.step(*v, prm, kDt, 1, kSweeps); gpu.settle(*v);
            if (k % 3) continue;
            gpu.download(*v, out);
            double s = 0; for (int z = 0; z < dims.z; ++z) s += columnDepth(out, 180, z);
            r.peak20 = std::max(r.peak20, s / dims.z - d * kH);
        }
        if (t12 > 0 && t20 > t12) r.speed = 8.0 / (t20 - t12);
        r.ms = ms / calls;
        gpu.destroyVolume(v);
        return r;
    };
    for (float depth : {9.0f, 18.0f}) {
        const Run fine = run(depth, false, 0), tl = run(depth, true, 0);
        const int bandCells = static_cast<int>(depth) - (tallLayer(depth, 0.5f) + 1);
        const Run shallow = run(depth, false, bandCells);
        std::printf("  long pulse %.0f m deep (theory %.2f m/s): speed fine %.2f m/s, tall %.2f (%+.1f %%), false bottom (%d cells) %.2f (%+.1f %%); height at 20 m fine %.2f mm, tall %.2f mm (%+.1f %%); ms/call fine %.3f, tall %.3f\n",
                    depth * kH, std::sqrt(9.81 * depth * kH), fine.speed, tl.speed, 100.0 * (tl.speed / fine.speed - 1.0), bandCells, shallow.speed, 100.0 * (shallow.speed / fine.speed - 1.0),
                    fine.peak20 * 1000.0, tl.peak20 * 1000.0, 100.0 * (tl.peak20 / std::max(1e-9, fine.peak20) - 1.0), fine.ms, tl.ms);
        ASSERT_GT(fine.speed, 0.0);
        EXPECT_NEAR(tl.speed / fine.speed, 1.0, 0.05);
        EXPECT_NEAR(tl.peak20 / fine.peak20, 1.0, 0.10);
        EXPECT_GT(std::abs(shallow.speed / fine.speed - 1.0), 0.05) << "the red control: a false bottom must change the speed";
    }
}

// ShortWaveMatchesAllFine + TheBooksAreExact: a stone-sized downward push at the centre of an 8 x 8 m, 2 m deep pond; the surface at
// 1 m and 3 m from the centre over 4 s - the largest |eta| within 10 % of all fine; the reduction's mass every call within 1e-6 of
// the start (and the all-fine run's own drift reported beside it).
TEST_F(WaterTallCellTest, ShortWaveMatchesAllFineAndTheBooksAreExact) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const glm::ivec3 dims(72, 24, 72);
    struct Run { double eta1 = 0, eta3 = 0, drift = 0, ms = 0; };
    auto run = [&](bool tallOn, float band = 0.5f) {
        WaterGrid g = pond(glm::ivec3(0), dims, 18.0f);
        WaterCoreGpu::Volume* v = make(g);
        Run r; if (!v) return r;
        if (tallOn) tall(*v, g, tallLayer(18.0f, band));
        gpu.step(*v, prm, kDt, 1, kSweeps); gpu.settle(*v);   // the collapse call
        const double m0 = gridMass(g);
        const glm::vec3 c(4.0f, 2.0f - 0.1f, 4.0f);
        gpu.queueMomentum(*v, c, 1.0f / 3.0f, glm::vec3(0.0f, -4.0f * 0.037f, 0.0f));   // ~ the ladder's stone (strength 4 over a 1/3 m sphere)
        WaterGrid out(g.spec());
        double ms = 0; int calls = 0;
        for (int k = 0; k < 240; ++k) {
            const auto t0 = std::chrono::steady_clock::now();
            const GpuStepStats st = gpu.step(*v, prm, kDt, 1, kSweeps); gpu.settle(*v);
            ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); ++calls;
            r.drift = std::max(r.drift, std::abs(st.totalMass - m0) / m0);
            if (k % 4 == 0) {
                gpu.download(*v, out);
                r.eta1 = std::max(r.eta1, std::abs(columnDepth(out, 45, 36) - 2.0));
                r.eta3 = std::max(r.eta3, std::abs(columnDepth(out, 63, 36) - 2.0));
            }
        }
        r.ms = ms / calls;
        gpu.destroyVolume(v);
        return r;
    };
    const Run fine = run(false);
    const int shipped = WaterCoreGpu::s_tallSweeps;
    for (int sw : {0, 5, 20}) for (float band : {0.44f, 0.67f, 0.89f}) {
        WaterCoreGpu::s_tallSweeps = sw;
        const Run r = run(true, band);
        std::printf("    band %.2f m (floor at %.2f m), tall sweeps %2d: |eta| 1 m %+.1f %%, 3 m %+.1f %%, ms/call %.3f (fine %.3f)\n", band,
                    (tallLayer(18.0f, band) + 1) * kH, sw, 100.0 * (r.eta1 / fine.eta1 - 1.0), 100.0 * (r.eta3 / fine.eta3 - 1.0), r.ms, fine.ms);
    }
    WaterCoreGpu::s_tallSweeps = shipped;
    const Run tl = run(true);
    std::printf("  stone in a 2 m pond: max |eta| at 1 m fine %.2f mm tall %.2f mm (%+.1f %%), at 3 m fine %.2f mm tall %.2f mm (%+.1f %%); mass drift fine %.2e tall %.2e; ms/call fine %.3f tall %.3f\n",
                fine.eta1 * 1000, tl.eta1 * 1000, 100.0 * (tl.eta1 / fine.eta1 - 1.0), fine.eta3 * 1000, tl.eta3 * 1000, 100.0 * (tl.eta3 / fine.eta3 - 1.0),
                fine.drift, tl.drift, fine.ms, tl.ms);
    ASSERT_GT(fine.eta1, 1e-4);
    EXPECT_NEAR(tl.eta1 / fine.eta1, 1.0, 0.10);
    EXPECT_NEAR(tl.eta3 / fine.eta3, 1.0, 0.10);
    EXPECT_LT(tl.drift, 1e-6);
}

// TallCellsAcrossChunkSeam (the chunk-independence shape): the same tall pond at x 0 and translated by exactly one chunk (288 cells =
// 36 bricks: the same brick alignment) - every cell bit-identical after 60 ticks with a push.
TEST_F(WaterTallCellTest, TallCellsAcrossChunkSeam) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const glm::ivec3 dims(72, 24, 36);
    auto run = [&](const glm::ivec3& origin) {
        WaterGrid g = pond(origin, dims, 18.0f);
        WaterCoreGpu::Volume* v = make(g);
        WaterGrid out(g.spec());
        if (!v) return out;
        tall(*v, g, tallLayer(18.0f, 0.5f));
        gpu.step(*v, prm, kDt, 1, kSweeps);
        gpu.queueMomentum(*v, glm::vec3(origin) * kH + glm::vec3(4.0f, 1.9f, 2.0f), 1.0f / 3.0f, glm::vec3(0.1f, -0.15f, 0.0f));   // world metres
        for (int k = 0; k < 60; ++k) gpu.step(*v, prm, kDt, 1, kSweeps);
        gpu.settle(*v);
        gpu.download(*v, out);
        gpu.destroyVolume(v);
        return out;
    };
    WaterGrid a = run(glm::ivec3(0, 0, 0)), b = run(glm::ivec3(288 - 32, 0, 0));   // x 256-327: across the seam at 288, the same brick alignment
    long differ = 0;
    for (size_t i = 0; i < a.fData().size(); ++i) if (!(a.fData()[i] == b.fData()[i])) ++differ;
    for (size_t i = 0; i < a.uData().size(); ++i) if (!(a.uData()[i] == b.uData()[i])) ++differ;
    std::printf("  seam: %ld sites differ between the pond at x 0 and the pond across the chunk seam\n", differ);
    EXPECT_EQ(differ, 0);
}

// DIAG: the stone in a 24 x 24 m, 2 m pond - does the tall layer stay stable as the pond widens (its 2-D solve converges slower), and
// what does a call cost, fine vs tall (B 0.5 m), at the shipped tall sweep count and at 40
TEST_F(WaterTallCellTest, DISABLED_DiagWidePond) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const glm::ivec3 dims(216, 24, 216);
    for (int mode = 0; mode < 3; ++mode) {
        WaterGrid g = pond(glm::ivec3(0), dims, 18.0f);
        WaterCoreGpu::Volume* v = make(g);
        if (!v) return;
        const int shipped = WaterCoreGpu::s_tallSweeps;
        if (mode == 2) WaterCoreGpu::s_tallSweeps = 40;
        if (mode > 0) tall(*v, g, tallLayer(18.0f, 0.5f));
        gpu.step(*v, prm, kDt, 1, kSweeps); gpu.settle(*v);
        gpu.queueMomentum(*v, glm::vec3(12.0f, 1.9f, 12.0f), 1.0f / 3.0f, glm::vec3(0.0f, -0.148f, 0.0f));
        WaterGrid out(g.spec());
        double ms = 0, eta1 = 0, eta3 = 0, keMax = 0; int calls = 0;
        for (int k = 0; k < 300; ++k) {
            const auto t0 = std::chrono::steady_clock::now();
            const GpuStepStats st = gpu.step(*v, prm, kDt, 1, kSweeps); gpu.settle(*v);
            if (k >= 10) { ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); ++calls; }
            keMax = std::max(keMax, st.kineticEnergy);
            if (k % 10 == 0) {
                gpu.download(*v, out);
                eta1 = std::max(eta1, std::abs(columnDepth(out, 117, 108) - 2.0)); eta3 = std::max(eta3, std::abs(columnDepth(out, 135, 108) - 2.0));
                if (k % 60 == 0) std::printf("    %s k %3d: KE %.3e maxSpeed %.3f residual %.2e substeps %d\n", mode == 0 ? "fine" : (mode == 1 ? "tall20" : "tall40"), k, st.kineticEnergy, st.maxSpeed, st.rbgsResidualMax, st.substepsLast);
            }
        }
        std::printf("  24 x 24 m 2 m pond, %s: max |eta| 1 m %.2f mm, 3 m %.2f mm, KE max %.3e, ms/call %.3f\n", mode == 0 ? "fine" : (mode == 1 ? "tall, 20 tall sweeps" : "tall, 40 tall sweeps"), eta1 * 1000, eta3 * 1000, keMax, ms / calls);
        WaterCoreGpu::s_tallSweeps = shipped;
        gpu.destroyVolume(v);
    }
}

// DIAG (28): what a dispatch and a barrier cost on this GPU by themselves - 1000 dispatches of an empty kernel, with and without a
// barrier after each, at 1 / 64 / 1024 workgroups
TEST_F(WaterTallCellTest, DISABLED_DiagLaunchCost) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    WaterGrid g = pond(glm::ivec3(0), glm::ivec3(72, 24, 72), 3.0f);
    WaterCoreGpu::Volume* v = make(g);
    if (!v) return;
    for (int groups : {1, 64, 1024})
        for (int bar = 1; bar >= 0; --bar) {
            double best = 1e9;
            for (int r = 0; r < 3; ++r) best = std::min(best, gpu.diagLaunchCost(*v, 1000, groups, bar != 0));
            std::printf("  1000 dispatches x %4d workgroups, %s: %.3f ms (%.2f us each)\n", groups, bar ? "a barrier after each" : "no barriers", best, best);
        }
    gpu.destroyVolume(v);
}

// DIAG (27.22d): the live engine lost the water under the tall layer (one brick layer, every column) - the tests did not. The live
// differences one at a time: the partial wake ON, the async calls, an upload between calls (a write)
TEST_F(WaterTallCellTest, DISABLED_DiagLiveDifferences) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const glm::ivec3 dims(72, 24, 72);
    for (int variant = 0; variant < 4; ++variant) {
        WaterGrid g = pond(glm::ivec3(0, 144, 0), dims, 18.0f);
        WaterCoreGpu::Volume* v = make(g);
        if (!v) return;
        if (variant == 1 || variant == 3) gpu.setPartialWake(*v, true);
        if (variant == 2 || variant == 3) gpu.setAsync(*v, true);
        const double m0 = gridMass(g);
        tall(*v, g, tallLayer(18.0f, 0.5f));
        double worst = 0.0; GpuStepStats st;
        for (int k = 0; k < 120; ++k) {
            if (k == 10) gpu.queueMomentum(*v, glm::vec3(4.0f, 16.0f + 1.9f, 4.0f), 1.0f / 3.0f, glm::vec3(0.0f, -0.148f, 0.0f));
            st = gpu.step(*v, prm, kDt, 1, kSweeps);
            gpu.settle(*v);
            st = v->pend.st;
            if (k > 1 && st.totalMass > 0) worst = std::max(worst, std::abs(st.totalMass - m0));
        }
        WaterGrid out(g.spec()); gpu.download(*v, out);
        std::printf("  variant %d (partial wake %d, async %d): tall active %d, mass %.4f -> grid %.4f, worst reduction error %.4f m^3\n",
                    variant, int(variant == 1 || variant == 3), int(variant >= 2), int(v->tallActive), m0, gridMass(out), worst);
        gpu.destroyVolume(v);
    }
}

// TheBooksSurviveAWrite (27.22d - the live engine lost one brick layer under the floor at each write): a tall pond, the host downloads it
// (the tall cells spread back), writes nothing, uploads it, steps - the water exactly as before. RED CONTROL: the old order (upload clears
// the tall state at its end) must lose the water under the floor.
TEST_F(WaterTallCellTest, TheBooksSurviveAWrite) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const glm::ivec3 dims(72, 24, 72);
    for (int red = 1; red >= 0; --red) {
        WaterCoreGpu::s_uploadKeepsTallForTest = red != 0;
        WaterGrid g = pond(glm::ivec3(0, 144, 0), dims, 18.0f);
        WaterCoreGpu::Volume* v = make(g);
        ASSERT_NE(v, nullptr);
        const double m0 = gridMass(g);
        tall(*v, g, tallLayer(18.0f, 0.5f));
        for (int k = 0; k < 10; ++k) gpu.step(*v, prm, kDt, 1, kSweeps);
        gpu.settle(*v);
        ASSERT_TRUE(v->tallActive);
        WaterGrid host(g.spec());
        gpu.download(*v, host);   // the manager's syncFromGpu before a write
        gpu.upload(*v, host);     // ... and the upload after it
        for (int k = 0; k < 5; ++k) gpu.step(*v, prm, kDt, 1, kSweeps);
        gpu.settle(*v);
        WaterGrid out(g.spec()); gpu.download(*v, out);
        std::printf("  %s a write to a tall pond: mass %.6f -> host %.6f -> after the upload %.6f m^3\n", red ? "RED (the old order):" : "fixed:", m0, gridMass(host), gridMass(out));
        if (red) EXPECT_LT(gridMass(out), m0 - 1.0) << "the red control must lose the water under the floor";
        else EXPECT_NEAR(gridMass(out), m0, 1e-4 * m0);
        gpu.destroyVolume(v);
    }
    WaterCoreGpu::s_uploadKeepsTallForTest = false;
}

// DIAG (27.22f): the partial wake through the manager with tall cells on vs off - frozen columns over time, the tall state
TEST_F(WaterTallCellTest, DISABLED_DiagPartialWakeWithTall) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    for (float band : {0.0f, 0.5f}) {
        auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
        WaterCoreManager mgr(state);
        std::string err;
        ASSERT_TRUE(mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(19, 1, 9), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        ASSERT_TRUE(id);
        mgr.setTallBand(id, band);
        mgr.setPartialWake(id, true);
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(19, 0, 9), 1.0f, nullptr);
        AvRecord rec;
        for (int k = 0; k < 420; ++k) {
            if (k == 120) mgr.addImpulse(glm::vec3(2.0f, 0.9f, 5.0f), 1.0f / 3.0f, 4.0f, glm::vec3(0.0f, -1.0f, 0.0f));
            mgr.step(id, 1, 1.0f / 60.0f, &rec);
            const AvRecord* r = mgr.find(id);
            if (r && (k % 20 == 0 || (k > 118 && k < 130)))
                std::printf("  band %.1f k %3d: tall %d (%s) awake %d frozen %d KE %.3e mass %.4f\n", band, k, int(r->tallActive), r->tallReason.c_str(), r->awakeColumns, r->frozenColumns, rec.kineticEnergy, rec.mass);
        }
    }
}
