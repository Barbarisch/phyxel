// MatSimGpuParityTest — docs/MaterialSimPlan.md §6a/§6c: the GPU port (MatSimGpu) is accepted only
// on BIT-EXACT parity with the CPU reference (MatSimCore), red first. Needs a Vulkan device: the
// fixture GTEST_SKIPs without one (never a pass). Every comparison covers the cells, both sleep
// mark sets, the tick, and per-tick eligible/evaluated block counts.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"
#include "core/matsim/MatSimCore.h"
#include "core/matsim/MatSimGpu.h"
#include "core/matsim/MatSimLab.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace Phyxel::Core::MatSim;
using Phyxel::Testing::VulkanTestFixture;
using Phyxel::DynamicSubcubeInstanceData;

namespace {

Occ openAir(int, int, int) { return Occ::Empty; }

// A world in MICRO coordinates: a dam wall, a ledge, a staircased diagonal wall and a U-tube
// connector, so every rule and the face-connectivity checks run.
Occ labWorld(int mx, int my, int mz) {
    if (mx >= 18 && mx < 21 && my < 27 && mz >= 0 && mz < 36) return Occ::Solid;          // dam wall, 1/3 m thick
    if (mx >= 27 && mx < 45 && my >= 18 && my < 21 && mz >= 9 && mz < 27) return Occ::Solid; // a ledge, 1 m up
    if (mz >= 45 && mz < 72 && (mx / 3) == (mz / 3) - 12 && my < 27) return Occ::Solid;    // staircase diagonal
    if (mx >= 54 && mx < 57 && my >= 9 && my < 36) return Occ::Solid;                     // U-tube divider (open below 1 m)
    return Occ::Empty;
}

std::string cellStr(Cell c) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "mat=%u fill=%u", cellMaterial(c), cellFill(c));
    return buf;
}

// "" if the stepping states are identical; else the first difference, in world terms.
std::string diff(const MatSimCore& a, const MatSimCore& b) {
    if (a.tick() != b.tick()) return "tick " + std::to_string(a.tick()) + " vs " + std::to_string(b.tick());
    if (!(a.origin() == b.origin()) || !(a.size() == b.size())) return "different boxes";
    const IVec3 o = a.origin(), s = a.size();
    for (int y = 0; y < s.y; ++y)
        for (int z = 0; z < s.z; ++z)
            for (int x = 0; x < s.x; ++x) {
                const IVec3 w{o.x + x, o.y + y, o.z + z};
                if (a.at(w) != b.at(w))
                    return "world cell (" + std::to_string(w.x) + "," + std::to_string(w.y) + "," + std::to_string(w.z) +
                           "): cpu " + cellStr(a.at(w)) + " gpu " + cellStr(b.at(w));
            }
    if (a.marksTick1() != b.marksTick1()) return "sleep marks (tick-1) differ";
    if (a.marksTick2() != b.marksTick2()) return "sleep marks (tick-2) differ";
    return "";
}

bool ledgerExact(const MatSimCore& s, std::string& why) {
    const Ledger got = s.ledger();
    for (int m = 0; m < 256; ++m)
        if (got[m] != s.expectedLedger()[m]) {
            why = "material " + std::to_string(m) + " off by " +
                  std::to_string(static_cast<long long>(got[m]) - static_cast<long long>(s.expectedLedger()[m]));
            return false;
        }
    return true;
}

MatSimCore labScene(int cpm, IVec3 originM, Config cfg = {}) {
    MatSimCore s;
    std::string why;
    // 9 x 4 x 9 m, placed at originM metres
    EXPECT_TRUE(s.create({originM.x * cpm, originM.y * cpm, originM.z * cpm}, {9 * cpm, 4 * cpm, 9 * cpm}, cpm,
                         [originM](int mx, int my, int mz) { return labWorld(mx - 9 * originM.x, my - 9 * originM.y, mz - 9 * originM.z); },
                         &why, cfg)) << why;
    const IVec3 o = s.origin();
    s.fillBox(Mat::Water, o, {2 * cpm, 3 * cpm, 4 * cpm}, 1.0);                                    // behind the dam
    s.fillBox(Mat::Sand, {o.x + 3 * cpm, o.y + 2 * cpm + 1, o.z + 1 * cpm}, {2 * cpm, cpm, 2 * cpm}, 1.0); // sand on the ledge
    s.fillBox(Mat::Water, {o.x + 4 * cpm, o.y + 2 * cpm + 1, o.z + 2 * cpm}, {cpm, cpm, cpm}, 0.6);   // water on it
    s.fillBox(Mat::Gravel, {o.x + 1 * cpm, o.y + 2 * cpm, o.z + 6 * cpm}, {2 * cpm, 2 * cpm, 1 * cpm}, 1.0);
    s.fillBox(Mat::Water, {o.x + 7 * cpm, o.y, o.z}, {cpm, 4 * cpm, 9 * cpm}, 0.75);               // one U-tube arm
    return s;
}

} // namespace

class MatSimGpuParity : public VulkanTestFixture {
protected:
    MatSimGpu gpu;
    void SetUp() override {
        VulkanTestFixture::SetUp();
        if (!isVulkanAvailable()) GTEST_SKIP() << "no Vulkan device";
        std::string err;
        if (!gpu.init(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) GTEST_SKIP() << "MatSimGpu init: " << err;
    }
    void TearDown() override {
        gpu.shutdown();
        VulkanTestFixture::TearDown();
    }

    // Step a CPU core and the GPU copy side by side. Every tick for `everyTickUntil` ticks, then
    // in chunks of `chunk`; at each comparison the states and the stats must agree.
    // Returns "" or the first difference.
    std::string runParity(MatSimCore& cpu, int ticks, int everyTickUntil, int chunk) {
        MatSimCore viaGpu = cpu;
        std::string err;
        if (!gpu.upload(viaGpu, &err)) return "upload: " + err;
        int t = 0;
        while (t < ticks) {
            const int k = (t < everyTickUntil) ? 1 : std::min(chunk, ticks - t);
            const StepStats sc = cpu.step(k);
            const StepStats sg = gpu.step(k);
            if (!gpu.download(viaGpu, &err)) return "download: " + err;
            t += k;
            const std::string d = diff(cpu, viaGpu);
            if (!d.empty()) return "after tick " + std::to_string(cpu.tick()) + ": " + d;
            if (sc.eligibleBlocks != sg.eligibleBlocks || sc.evaluatedBlocks != sg.evaluatedBlocks ||
                sc.lastTickEligible != sg.lastTickEligible)
                return "stats differ after tick " + std::to_string(cpu.tick()) + ": eligible " +
                       std::to_string(sc.eligibleBlocks) + "/" + std::to_string(sg.eligibleBlocks) + ", evaluated " +
                       std::to_string(sc.evaluatedBlocks) + "/" + std::to_string(sg.evaluatedBlocks);
            if (!ledgerExact(viaGpu, err)) return "gpu ledger at tick " + std::to_string(cpu.tick()) + ": " + err;
        }
        return "";
    }
};

// The acceptance test (§6a): the lab scene at all three cell sizes, from an odd negative origin,
// identical to the CPU after every tick for the first 64 and at every chunk to 1,500.
TEST_F(MatSimGpuParity, GpuEqualsCpuAfterNTicks) {
    for (int cpm : {1, 3, 9}) {
        MatSimCore cpu = labScene(cpm, {-3, -1, 5});
        const int ticks = cpm == 9 ? 900 : 1500;
        const std::string d = runParity(cpu, ticks, 64, 97);
        EXPECT_EQ(d, "") << "cells_per_m " << cpm;
        std::printf("[MatSimGpuParity] cpm %d: %d ticks identical (cells, marks, stats, ledger)\n", cpm, ticks);
    }
}

// Control: the comparator must see a one-tick shift (and a different seed).
TEST_F(MatSimGpuParity, ControlShiftedRunDiffers) {
    MatSimCore cpu = labScene(3, {0, 0, 0});
    MatSimCore viaGpu = cpu;
    std::string err;
    ASSERT_TRUE(gpu.upload(viaGpu, &err)) << err;
    cpu.step(41);
    gpu.step(40);
    ASSERT_TRUE(gpu.download(viaGpu, &err)) << err;
    EXPECT_NE(diff(cpu, viaGpu), "") << "a one-tick shift was not seen";
    viaGpu.step(1);   // catch the copy up on the CPU: now it must match again
    EXPECT_EQ(diff(cpu, viaGpu), "");

    Config other;
    other.seed = 2;
    MatSimCore a = labScene(3, {0, 0, 0}), b = labScene(3, {0, 0, 0}, other);
    ASSERT_TRUE(gpu.upload(b, &err)) << err;
    a.step(200);
    gpu.step(200);
    ASSERT_TRUE(gpu.download(b, &err)) << err;
    EXPECT_NE(diff(a, b), "") << "a different seed gave the same state";
}

// Sleep on the GPU: dense and sleepy GPU runs equal the CPU, and a still basin evaluates nothing.
TEST_F(MatSimGpuParity, SleepIsExactAndSkipsOnGpu) {
    Config dense;
    dense.sleep = false;
    MatSimCore cpuDense = labScene(3, {1, 0, -2}, dense);
    EXPECT_EQ(runParity(cpuDense, 600, 16, 128), "") << "sleep off";

    MatSimCore basin;
    std::string why;
    ASSERT_TRUE(basin.create({0, 0, 0}, {48, 6, 48}, 3, openAir, &why)) << why;   // 16 x 2 x 16 m, 6 x 1 x 6 bricks
    basin.fillBox(Mat::Water, {0, 0, 0}, {48, 2, 48}, 1.0);
    basin.fillBox(Mat::Water, {0, 2, 0}, {4, 3, 4}, 1.0);   // a mound in one corner
    MatSimCore g = basin;
    ASSERT_TRUE(gpu.upload(g, &why)) << why;
    // 9,000 ticks = 150 s: levelling is diffusive (time ~ width^2, §6a), and the 4 x 4 m CPU basin
    // took 3.8 s, so 16 x 16 m needs tens of seconds. (600 ticks was too short: the first run never slept.)
    int asleepAt = -1;
    uint64_t prev = 1;
    for (int t = 1; t <= 9000 && asleepAt < 0; ++t) {
        const uint64_t e = gpu.step(1).lastTickEligible;
        if (e == 0 && prev == 0) asleepAt = t;
        prev = e;
    }
    ASSERT_NE(asleepAt, -1) << "the GPU basin never slept";
    const StepStats after = gpu.step(100);
    EXPECT_EQ(after.evaluatedBlocks, 0u) << "an asleep basin still evaluated blocks on the GPU";
    basin.step(asleepAt + 100);
    ASSERT_TRUE(gpu.download(g, &why)) << why;
    EXPECT_EQ(diff(basin, g), "");
    std::printf("[MatSimGpuParity] 16 x 2 x 16 m basin asleep on the GPU at tick %d (%.2f s)\n", asleepAt, asleepAt / 60.0);
}

// Edits between GPU runs: the host edits (fill, resolidify, cell-size switches) and re-uploads;
// the result equals a pure CPU run doing the same edits, and the ledger stays exact.
TEST_F(MatSimGpuParity, EditsBetweenRunsStayExact) {
    bool wall = false;
    auto world = [&wall](int mx, int my, int mz) {
        if (wall && mx >= 36 && mx < 45 && my < 18) return Occ::Solid;   // a 1 m wall that appears mid-run
        return labWorld(mx, my, mz);
    };
    MatSimCore cpu, g;
    std::string why;
    ASSERT_TRUE(cpu.create({0, 0, 0}, {27, 12, 27}, 3, world, &why)) << why;
    cpu.fillBox(Mat::Water, {0, 0, 0}, {6, 9, 12}, 1.0);
    cpu.fillBox(Mat::Sand, {12, 6, 12}, {6, 3, 6}, 1.0);
    g = cpu;
    const int sizes[] = {3, 9, 3, 1, 3};
    for (int phase = 0; phase < 5; ++phase) {
        ASSERT_TRUE(gpu.upload(g, &why)) << why;
        cpu.step(150);
        gpu.step(150);
        ASSERT_TRUE(gpu.download(g, &why)) << why;
        ASSERT_EQ(diff(cpu, g), "") << "phase " << phase;
        ASSERT_TRUE(ledgerExact(g, why)) << why;
        // Edits on the host, the same on both.
        wall = (phase % 2) == 0;
        cpu.resolidify(world);
        g.resolidify(world);
        const IVec3 o = cpu.origin();
        cpu.fillBox(Mat::Water, {o.x + 1, o.y + cpu.size().y - 2, o.z + 1}, {2, 1, 2}, 0.5);
        g.fillBox(Mat::Water, {o.x + 1, o.y + g.size().y - 2, o.z + 1}, {2, 1, 2}, 0.5);
        const bool okC = cpu.setCellSize(sizes[phase], world, &why);
        const bool okG = g.setCellSize(sizes[phase], world, &why);
        ASSERT_EQ(okC, okG);
        ASSERT_EQ(diff(cpu, g), "") << "after the edits of phase " << phase;
        ASSERT_TRUE(ledgerExact(g, why)) << why;
    }
}

// Stress: random scenes (water, sand, gravel; random pillars and shelves) at every cell size,
// odd and negative origins, boxes not a multiple of the brick or block size, n > one chunk.
TEST_F(MatSimGpuParity, StressRandomScenes) {
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        auto world = [seed](int mx, int my, int mz) {
            const int cx = mx / 9 - (mx < 0 && mx % 9 ? 1 : 0), cz = mz / 9 - (mz < 0 && mz % 9 ? 1 : 0);
            const uint32_t h = cellHash(cx, 0, cz, 0, seed);
            if (h % 7 == 0 && my < int(9 + h % 18)) return Occ::Solid;
            if (h % 11 == 1 && my == 13) return Occ::Solid;
            return Occ::Empty;
        };
        const int cpm = (seed % 3 == 0) ? 1 : (seed % 3 == 1) ? 3 : 9;
        Config cfg;
        cfg.seed = seed * 7919u;
        MatSimCore cpu;
        std::string why;
        const IVec3 o{-3 * int(seed) - 1, -int(seed), 2 - int(seed)};
        const IVec3 sz{2 * cpm * 3 + int(seed), cpm * 3 + 1, 2 * cpm * 3 + 3};   // odd extents
        ASSERT_TRUE(cpu.create(o, sz, cpm, world, &why, cfg)) << why;
        const uint8_t mats[] = {Mat::Water, Mat::Sand, Mat::Gravel, Mat::Snow};
        for (int k = 0; k < 12; ++k) {
            const uint32_t h = cellHash(k, 1, 2, 3, seed);
            const IVec3 lo{o.x + int(h % uint32_t(sz.x)), o.y + int((h >> 4) % uint32_t(sz.y)), o.z + int((h >> 8) % uint32_t(sz.z))};
            const IVec3 bs{1 + int((h >> 12) % 5), 1 + int((h >> 16) % 4), 1 + int((h >> 20) % 5)};
            const double f = 0.1 + 0.9 * double((h >> 26) % 5) / 4.0;
            cpu.fillBox(mats[(h >> 24) % 4], lo, bs, f);
        }
        const std::string d = runParity(cpu, 700, 8, 300);
        ASSERT_EQ(d, "") << "seed " << seed << " cpm " << cpm;
    }
}

// Degenerate boxes: one cell, one column, a slab one cell thick; n = 0 steps nothing.
TEST_F(MatSimGpuParity, DegenerateBoxes) {
    const IVec3 sizes[] = {{1, 1, 1}, {1, 7, 1}, {13, 1, 5}, {2, 2, 2}, {17, 3, 1}};
    for (const IVec3& sz : sizes) {
        MatSimCore cpu;
        std::string why;
        ASSERT_TRUE(cpu.create({-1, -1, -1}, sz, 3, openAir, &why)) << why;
        cpu.fillBox(Mat::Water, {-1, -1 + sz.y - 1, -1}, {sz.x, 1, sz.z}, 0.8);
        cpu.fillBox(Mat::Sand, {-1, -1, -1}, {1, sz.y, 1}, 0.5);
        EXPECT_EQ(runParity(cpu, 120, 120, 1), "") << "box " << sz.x << "x" << sz.y << "x" << sz.z;
    }
    MatSimCore cpu = labScene(3, {0, 0, 0});
    MatSimCore g = cpu;
    std::string why;
    ASSERT_TRUE(gpu.upload(g, &why)) << why;
    const StepStats none = gpu.step(0);
    EXPECT_EQ(none.ticks, 0);
    ASSERT_TRUE(gpu.download(g, &why)) << why;
    EXPECT_EQ(diff(cpu, g), "");
}

// Scale: the plan's 16 x 8 x 16 m lab box at 1/9 m (1.49 M cells) through more than one chunk.
TEST_F(MatSimGpuParity, LabBoxAtNinthMetre) {
    MatSimCore cpu;
    std::string why;
    ASSERT_TRUE(cpu.create({0, 0, 0}, {144, 72, 144}, 9, labWorld, &why)) << why;
    cpu.fillBox(Mat::Water, {0, 0, 0}, {18, 27, 36}, 1.0);
    cpu.fillBox(Mat::Sand, {30, 21, 10}, {12, 9, 12}, 1.0);
    cpu.fillBox(Mat::Water, {57, 0, 0}, {20, 30, 144}, 1.0);
    EXPECT_EQ(runParity(cpu, 300, 2, 298), "");
}

// The lab layer (MatSimLab, what /api/matsim serves): realtime stepping with the raw-cell view on.
// A GPU-backed lab and a CPU-backed lab fed the same calls must show the same faces every frame -
// the view must follow the device's state, not a stale host copy (audit 2026-11-03, defect 1).
TEST_F(MatSimGpuParity, LabRealtimeViewTracksDevice) {
    using Json = MatSimLab::Json;
    MatSimLab g(labWorld), c(labWorld);
    std::string err;
    ASSERT_TRUE(g.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
    Json box = {{"min", {{"x", 0}, {"y", 0}, {"z", 0}}}, {"size", {{"x", 9}, {"y", 4}, {"z", 9}}}, {"cells_per_m", 3}};
    Json out;
    box["backend"] = "gpu";
    g.handle("create", box, out);
    ASSERT_FALSE(out.contains("error")) << out.dump();
    ASSERT_EQ(out["backend"], "gpu");
    box["backend"] = "cpu";
    c.handle("create", box, out);
    ASSERT_FALSE(out.contains("error")) << out.dump();
    const Json fill = {{"material", "Water"}, {"min", {{"x", 0}, {"y", 0}, {"z", 0}}}, {"size", {{"x", 2}, {"y", 3}, {"z", 4}}}, {"fraction", 1.0}};
    for (MatSimLab* l : {&g, &c}) {
        l->handle("fill_box", fill, out);
        ASSERT_FALSE(out.contains("error")) << out.dump();
        l->handle("view", {{"on", true}}, out);
        l->handle("realtime", {{"on", true}}, out);
    }
    auto sameFaces = [](const MatSimLab& a, const MatSimLab& b, std::string& why) {
        if (a.faces().size() != b.faces().size()) { why = std::to_string(a.faces().size()) + " vs " + std::to_string(b.faces().size()) + " faces"; return false; }
        for (size_t i = 0; i < a.faces().size(); ++i) {
            const auto& p = a.faces()[i];
            const auto& q = b.faces()[i];
            if (p.worldPosition != q.worldPosition || p.scale != q.scale || p.faceID != q.faceID) { why = "face " + std::to_string(i) + " differs"; return false; }
        }
        return true;
    };
    std::vector<DynamicSubcubeInstanceData> first = g.faces();
    for (int frame = 1; frame <= 120; ++frame) {
        g.update(1.0 / 60.0);
        c.update(1.0 / 60.0);
        std::string why;
        ASSERT_TRUE(sameFaces(g, c, why)) << "frame " << frame << ": gpu lab view vs cpu lab view: " << why;
    }
    bool moved = first.size() != g.faces().size();
    for (size_t i = 0; !moved && i < first.size(); ++i) moved = first[i].worldPosition != g.faces()[i].worldPosition || first[i].scale != g.faces()[i].scale;
    EXPECT_TRUE(moved) << "the view never changed in 120 frames of a dam break";
    Json sg, sc;
    g.handle("stats", Json::object(), sg);
    c.handle("stats", Json::object(), sc);
    EXPECT_EQ(sg["tick"], sc["tick"]);
    EXPECT_EQ(sg["ledger"], sc["ledger"]);
    EXPECT_EQ(sg["backend"], "gpu");
}

// Not a gate: GPU ms per tick (timestamps) for an awake and an asleep 16 x 8 x 16 m box at each
// cell size, for the §6a performance record. Asleep must cost less than awake.
TEST_F(MatSimGpuParity, TimingAwakeVsAsleep) {
    for (int cpm : {1, 3, 9}) {
        MatSimCore awake;
        std::string why;
        ASSERT_TRUE(awake.create({0, 0, 0}, {16 * cpm, 8 * cpm, 16 * cpm}, cpm, labWorld, &why)) << why;
        awake.fillBox(Mat::Water, {0, 0, 0}, {2 * cpm, 3 * cpm, 4 * cpm}, 1.0);
        awake.fillBox(Mat::Water, {7 * cpm, 0, 0}, {3 * cpm, 4 * cpm, 16 * cpm}, 1.0);
        ASSERT_TRUE(gpu.upload(awake, &why)) << why;
        gpu.step(4);   // warm-up
        const auto t0 = std::chrono::steady_clock::now();
        const StepStats sa = gpu.step(60);
        const double wallA = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 60.0;
        const double msAwake = gpu.lastGpuMsPerTick();
        // The upper bound: the same box with sleep off (every block evaluated every tick).
        Config dense;
        dense.sleep = false;
        MatSimCore all;
        ASSERT_TRUE(all.create({0, 0, 0}, {16 * cpm, 8 * cpm, 16 * cpm}, cpm, labWorld, &why, dense)) << why;
        all.fillBox(Mat::Water, {0, 0, 0}, {2 * cpm, 3 * cpm, 4 * cpm}, 1.0);
        all.fillBox(Mat::Water, {7 * cpm, 0, 0}, {3 * cpm, 4 * cpm, 16 * cpm}, 1.0);
        ASSERT_TRUE(gpu.upload(all, &why)) << why;
        gpu.step(4);
        const StepStats sd = gpu.step(60);
        std::printf("[MatSimGpuTiming] cpm %d dense (sleep off): %.3f ms/tick GPU, %llu blocks evaluated/tick\n", cpm,
                    gpu.lastGpuMsPerTick(), static_cast<unsigned long long>(sd.evaluatedBlocks / 60));

        MatSimCore still;
        ASSERT_TRUE(still.create({0, 0, 0}, {16 * cpm, 8 * cpm, 16 * cpm}, cpm, labWorld, &why)) << why;
        still.fillBox(Mat::Water, {0, 0, 0}, {16 * cpm, 1, 16 * cpm}, 1.0);   // a full floor layer: level from the start
        ASSERT_TRUE(gpu.upload(still, &why)) << why;
        gpu.step(600);
        ASSERT_EQ(gpu.step(2).eligibleBlocks, 0u) << "the still layer is not asleep at cpm " << cpm;
        const StepStats ss = gpu.step(60);
        const double msAsleep = gpu.lastGpuMsPerTick();
        std::printf("[MatSimGpuTiming] cpm %d (%d cells): awake %.3f ms/tick GPU (%.3f wall, %llu blocks evaluated/tick), "
                    "asleep %.3f ms/tick GPU (%llu evaluated)\n",
                    cpm, 16 * cpm * 8 * cpm * 16 * cpm, msAwake, wallA,
                    static_cast<unsigned long long>(sa.evaluatedBlocks / 60), msAsleep,
                    static_cast<unsigned long long>(ss.evaluatedBlocks / 60));
        if (msAwake >= 0 && msAsleep >= 0 && cpm > 1) EXPECT_LT(msAsleep, msAwake);
    }
}
