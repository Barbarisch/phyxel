// WaterRippleGpuParityTest - docs/WaterCore.md 25.34 H1b-T3 / T4: the ripple step on the GPU (RippleGpu, wc_ripple.comp)
// against the CPU reference (RippleLayer::step / advect). Needs a Vulkan device: the fixture GTEST_SKIPs without one
// (never a pass). Both layers run the SAME sources through the same bookkeeping; the GPU runs the frame protocol
// (collect -> sources -> record -> submit) and is collected once more at the end, so both have stepped every frame.
// Parity, not bit-equality: the device sums in another order. Tolerance: max |r_gpu - r_cpu| <= 1 % of max |r_cpu|.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"
#include "core/water/RippleLayer.h"
#include "core/water/RippleGpu.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>

using namespace Phyxel::Core::Water;
using Phyxel::Testing::VulkanTestFixture;

namespace {
constexpr float kD = RippleLayer::kPitch;
using SetupFn = std::function<void(RippleLayer&)>;
using SourcesFn = std::function<void(RippleLayer&, int frame)>;
struct Diff { float maxDiff = 0.0f, maxAbs = 0.0f; double brokenCpu = 0.0, brokenGpu = 0.0; };
}  // namespace

class WaterRippleGpuParityTest : public VulkanTestFixture {
protected:
    RippleGpu gpu;
    void SetUp() override {
        VulkanTestFixture::SetUp();
        if (!isVulkanAvailable()) return;
        std::string err;
        if (!gpu.init(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) GTEST_SKIP() << "RippleGpu init: " << err;
    }
    void TearDown() override { gpu.shutdown(); VulkanTestFixture::TearDown(); }

    Diff run(int nx, int nz, const SetupFn& setup, const SourcesFn& sources, int frames, float dt, const std::vector<glm::vec2>* vel = nullptr) {
        RippleLayer cpu({0, 0}, nx, nz), g({0, 0}, nx, nz);
        setup(cpu); setup(g);
        Diff d;
        for (int f = 0; f < frames; ++f) {
            sources(cpu, f);
            if (vel) cpu.advect(*vel, dt);
            cpu.step(dt);
            d.brokenCpu += cpu.takeBroken();
            gpu.collect({&g});
            d.brokenGpu += g.takeBroken();
            sources(g, f);
            gpu.record(g, dt, vel);
            gpu.submit();
        }
        gpu.drain({&g});   // every step in flight in
        d.brokenGpu += g.takeBroken();
        for (size_t i = 0; i < cpu.heights().size(); ++i) {
            d.maxAbs = std::max(d.maxAbs, std::abs(cpu.heights()[i]));
            d.maxDiff = std::max(d.maxDiff, std::abs(cpu.heights()[i] - g.heights()[i]));
        }
        return d;
    }
};

// (1) a kick's ring in a still 6 x 6 m pond, 2 s
TEST_F(WaterRippleGpuParityTest, RingInAStillPond) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const Diff d = run(54, 54, [](RippleLayer& L) { L.setOpenBorder(false); },
                       [](RippleLayer& L, int f) { if (f == 0) L.addImpulse(27.5f * kD, 27.5f * kD, 30.0f); }, 120, 1.0f / 60.0f);
    std::printf("  ring: max |gpu - cpu| %.3e m of max |cpu| %.3e m (%.3f %%)\n", d.maxDiff, d.maxAbs, 100.0 * d.maxDiff / d.maxAbs);
    ASSERT_GT(d.maxAbs, 1e-4f);
    EXPECT_LE(d.maxDiff, 0.01f * d.maxAbs);
}

// (2) a wall between two bodies: labels + the zero mean per body (the right body stays at rest)
TEST_F(WaterRippleGpuParityTest, AWallBetweenTwoBodies) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const Diff d = run(54, 30, [](RippleLayer& L) { L.setOpenBorder(false); for (int z = 0; z < 30; ++z) for (int x = 26; x < 29; ++x) L.mask()[L.idx(x, z)] = 0; },
                       [](RippleLayer& L, int f) { if (f == 0 || f == 20) L.addImpulse(10.5f * kD, 15.5f * kD, 40.0f); }, 90, 1.0f / 60.0f);
    std::printf("  wall: max |gpu - cpu| %.3e m of max |cpu| %.3e m (%.3f %%)\n", d.maxDiff, d.maxAbs, 100.0 * d.maxDiff / d.maxAbs);
    ASSERT_GT(d.maxAbs, 1e-4f);
    EXPECT_LE(d.maxDiff, 0.01f * d.maxAbs);
}

// (3) the breaking limit: B-T1's 0.6 m spike and 0.3 m step, 12 steps of kStep. RED: a limiter whose gather drops the
// backward edges (a porting fault) must leave parity - the test can see a wrong limiter. (The first red tried - every pass
// forced, the gate removed - moved the field by 1e-6 m: the gate only spares edges within 0.1 mm of the limit, below this
// parity's 1 %; it is kept for the CPU's untouched-bit-for-bit rule and for the cost.)
TEST_F(WaterRippleGpuParityTest, BreakingLimitAndItsGate) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    auto setup = [](RippleLayer& L) {
        L.setOpenBorder(false); L.setBreakSlope(RippleLayer::kBreakSlope);
        std::vector<float> h(54 * 54, 0.0f);
        h[L.idx(20, 27)] = 0.6f;
        for (int z = 10; z < 44; ++z) for (int x = 34; x < 44; ++x) h[L.idx(x, z)] = 0.3f;
        double mean = 0.0; for (float v : h) mean += v; mean /= h.size();
        for (float& v : h) v -= static_cast<float>(mean);
        L.pin(h);
    };
    const auto none = [](RippleLayer&, int) {};
    const Diff d = run(54, 54, setup, none, 12, RippleLayer::kStep);
    gpu.setLimiterFaultForTest(true);
    const Diff red = run(54, 54, setup, none, 12, RippleLayer::kStep);
    gpu.setLimiterFaultForTest(false);
    std::printf("  limit: max |gpu - cpu| %.3e of %.3e (%.3f %%), broken cpu %.5f gpu %.5f m^3; backward edges dropped (the red): %.3e (%.3f %%), broken %.5f\n",
                d.maxDiff, d.maxAbs, 100.0 * d.maxDiff / d.maxAbs, d.brokenCpu, d.brokenGpu, red.maxDiff, 100.0 * red.maxDiff / red.maxAbs, red.brokenGpu);
    EXPECT_LE(d.maxDiff, 0.01f * d.maxAbs);
    EXPECT_NEAR(d.brokenGpu, d.brokenCpu, 0.01 * d.brokenCpu);
    EXPECT_GT(d.brokenCpu, 0.0);
    EXPECT_TRUE(red.maxDiff > 0.01f * red.maxAbs || std::abs(red.brokenGpu - red.brokenCpu) > 0.01 * red.brokenCpu)
        << "the red: a limiter applying every pass must leave parity";
}

// (4) a ring on a 0.5 m/s current (advection)
TEST_F(WaterRippleGpuParityTest, RingOnACurrent) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    std::vector<glm::vec2> vel(160 * 60, glm::vec2(0.5f, 0.0f));
    const Diff d = run(160, 60, [](RippleLayer& L) { L.setOpenBorder(false); L.setBreakSlope(0.0f); },
                       [](RippleLayer& L, int f) { if (f == 0) L.addImpulse(50.5f * kD, 30.5f * kD, 50.0f); }, 120, 1.0f / 60.0f, &vel);
    std::printf("  current: max |gpu - cpu| %.3e m of max |cpu| %.3e m (%.3f %%)\n", d.maxDiff, d.maxAbs, 100.0 * d.maxDiff / d.maxAbs);
    ASSERT_GT(d.maxAbs, 1e-4f);
    EXPECT_LE(d.maxDiff, 0.01f * d.maxAbs);
}

// (5) a body crossing the surface: kinematic cells (a 4 x 4 block pushed down for 20 frames, at its sharp edge)
TEST_F(WaterRippleGpuParityTest, ABodyCrossingTheSurface) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const Diff d = run(54, 54, [](RippleLayer& L) { L.setOpenBorder(false); },
                       [](RippleLayer& L, int f) {
                           if (f >= 20) return;
                           for (int z = 25; z < 29; ++z) for (int x = 25; x < 29; ++x) L.setKinematic(x, z, (x == 25 || x == 28 || z == 25 || z == 28) ? -0.3f : 0.2f);
                       }, 60, 1.0f / 60.0f);
    std::printf("  body: max |gpu - cpu| %.3e m of max |cpu| %.3e m (%.3f %%)\n", d.maxDiff, d.maxAbs, 100.0 * d.maxDiff / d.maxAbs);
    ASSERT_GT(d.maxAbs, 1e-4f);
    EXPECT_LE(d.maxDiff, 0.01f * d.maxAbs);
}

// H1b-T4 COST (diagnostic): the three 25.31 windows with flow-forcing impulses on every water cell and a sheared current.
// CPU wall per frame of the GPU path (collect + record + submit; the wait is reported apart - here nothing renders in
// between, so it is the device time itself) and the device time; the CPU step for comparison.
TEST_F(WaterRippleGpuParityTest, DISABLED_CostRippleWindowGpu) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    struct Case { const char* name; int nx, nz, mode; };
    for (const Case cs : {Case{"bench creek 384 x 54", 384, 54, 0}, Case{"meander 384 x 384", 384, 384, 1}, Case{"full 384 x 384", 384, 384, 2}}) {
        RippleLayer cpu({0, 0}, cs.nx, cs.nz), g({0, 0}, cs.nx, cs.nz);
        std::vector<glm::vec2> vel(static_cast<size_t>(cs.nx) * cs.nz, glm::vec2(0.0f));
        for (RippleLayer* L : {&cpu, &g}) {
            L->setOpenBorder(true);
            for (int z = 0; z < cs.nz; ++z) for (int x = 0; x < cs.nx; ++x) {
                const float centre = cs.mode == 0 ? cs.nz * 0.5f : cs.nz * 0.5f + 0.35f * cs.nz * std::sin(x * 6.2831853f / 300.0f);
                const bool w = cs.mode == 2 || std::abs(z + 0.5f - centre) < 9.0f;
                L->mask()[L->idx(x, z)] = w ? 1 : 0;
                const float e = (z + 0.5f - centre) / 9.0f;
                vel[L->idx(x, z)] = glm::vec2(w ? 0.45f * std::max(0.0f, 1.0f - e * e) : 0.0f, 0.0f);
            }
        }
        auto force = [&](RippleLayer& L, int k) {
            for (int z = 0; z < cs.nz; ++z) for (int x = 0; x < cs.nx; ++x) {
                if (!L.mask()[L.idx(x, z)]) continue;
                uint32_t h = static_cast<uint32_t>(x) * 73856093u ^ static_cast<uint32_t>(z) * 19349663u ^ static_cast<uint32_t>(k) * 83492791u;
                h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                L.addImpulse((x + 0.5f) * kD, (z + 0.5f) * kD, 0.2f * ((h & 0xFFFFu) / 32767.5f - 1.0f));
            }
        };
        double cpuMs = 0.0, wallMs = 0.0, waitMs = 0.0, gpuMs = 0.0, recMs = 0.0; int timed = 0;
        for (int k = 0; k < 60 * 6; ++k) {
            force(cpu, k);
            auto c0 = std::chrono::steady_clock::now();
            cpu.advect(vel, 1.0f / 60.0f); cpu.step(1.0f / 60.0f);
            auto c1 = std::chrono::steady_clock::now();
            gpu.collect({&g});
            force(g, k);
            auto c2 = std::chrono::steady_clock::now();
            gpu.record(g, 1.0f / 60.0f, &vel);
            gpu.submit();
            auto c3 = std::chrono::steady_clock::now();
            if (k >= 60) {
                cpuMs += std::chrono::duration<double, std::milli>(c1 - c0).count();
                wallMs += gpu.stats().collectMs + std::chrono::duration<double, std::milli>(c3 - c2).count();
                waitMs += gpu.stats().waitMs; gpuMs += gpu.stats().gpuMs; recMs += gpu.stats().recordMs; ++timed;
            }
        }
        gpu.drain({&g});
        float md = 0.0f, ma = 0.0f;
        for (size_t i = 0; i < cpu.heights().size(); ++i) { ma = std::max(ma, std::abs(cpu.heights()[i])); md = std::max(md, std::abs(cpu.heights()[i] - g.heights()[i])); }
        std::printf("  %-22s CPU step %.3f ms | GPU path: CPU wall %.3f ms (record %.3f, wait %.3f), device %.3f ms | parity %.2f %%\n",
                    cs.name, cpuMs / timed, (wallMs - waitMs) / timed, recMs / timed, waitMs / timed, gpuMs / timed, 100.0 * md / std::max(ma, 1e-9f));
    }
}
