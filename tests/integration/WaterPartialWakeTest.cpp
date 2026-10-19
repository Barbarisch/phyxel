// WaterCore 26.31 (c) THE PARTIAL WAKE - gate, red first. Needs a Vulkan device: the fixture GTEST_SKIPs without one.
//
// A disturbance woke the whole volume and every pass stepped every stored brick. With the partial wake a brick column (8 x 8
// cells) that passes the volume's own sleep test, more than kPartialWakeMargin columns from any moving one, FREEZES: it is not
// stepped and is a wall to the awake columns (closed faces, no pressure read from it). The look must not change: the same
// pond and stone run with the partial wake ON and OFF (the control: the whole body, as before) must agree -
//   predictions written before the first run (docs/WaterCore.md 26.31): mass equal (relative 1e-6), every wet column's surface
//   within 1 mm of the OFF run at every tick, more than half of the stored columns frozen over the first 0.5 s after the stone.
//   The first run MISSED the first two (214 mm, 1.8e-6) - and so did the whole body against itself with the stone 1e-5 stronger
//   (273 mm, 2.8e-6): a splash amplifies any change, and the whole body loses ~1.3e-3 m^3 in 5 s to the residue sweep, which a
//   frozen column does not run. The bound is now that FLOOR (below), on the books (mass + residue dropped).
// Red: the same run with the interface left OPEN (frozen columns skipped but read as water by their awake neighbours,
// WaterCoreGpu::s_partialWakeOpenInterfaceForTest) must break the bound - the wall is what keeps the books and the surface.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"
#include "core/water/WaterCore.h"
#include "core/water/WaterCoreGpu.h"
#include "core/water/WaterSurfaceMesh.h"
#include "core/water/WaterCoreManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <vector>

using namespace Phyxel::Core::Water;
using Phyxel::Testing::VulkanTestFixture;

namespace {

struct WakeRun {
    std::vector<std::vector<float>> tops;   // per tick, per column: the top run's top (NaN = dry)
    std::vector<double> mass;               // per tick (m^3)
    std::vector<int> awake;                 // per tick: awake columns
    std::vector<double> books;              // per tick: mass + residue dropped (m^3) - what no transport may change
    double frozenShareFirst = 0.0;          // mean frozen / stored columns over the first 30 ticks after the stone
    int wakeColumnsMax = 0;
    int stepSlotsMin = 1 << 30, bricksAtMin = 0;   // 26.31 step 2: the fewest slots a call stepped (the partition at work)
    bool ok = false;
};

}  // namespace

class WaterPartialWakeTest : public VulkanTestFixture {
public:
    // a 20 x 10 m pond at 1/9 m, 1 m deep, walled by its box; settled for 2 s, then a stone at the west end (x 2 m, z 5 m);
    // 5 s recorded tick by tick
    WakeRun run(bool partial, bool openInterface, float strength = 4.0f, bool droplets = true) {
        WakeRun out;
        auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
        WaterCoreManager mgr(state);
        std::string err;
        if (!mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) { ADD_FAILURE() << err; return out; }
        mgr.setDroplets(droplets);
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(19, 1, 9), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        if (!id) { ADD_FAILURE() << err; return out; }
        if (partial && !mgr.setPartialWake(id, true)) { ADD_FAILURE() << "partial wake refused (not a sparse GPU volume?)"; return out; }
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(19, 0, 9), 1.0f, nullptr);
        WaterCoreGpu::s_partialWakeOpenInterfaceForTest = openInterface;
        AvRecord rec;
        const float dt = 1.0f / 60.0f;
        for (int k = 0; k < 120; ++k) mgr.step(id, 1, dt, &rec);
        mgr.addImpulse(glm::vec3(2.0f, 0.9f, 5.0f), 1.0f / 3.0f, strength, glm::vec3(0.0f, -1.0f, 0.0f));
        double frozenSum = 0.0;
        for (int k = 0; k < 300; ++k) {
            mgr.step(id, 1, dt, &rec);
            const AvRecord* r = mgr.find(id);
            if (k < 30 && r && r->awakeColumns + r->frozenColumns > 0) frozenSum += static_cast<double>(r->frozenColumns) / (r->awakeColumns + r->frozenColumns);
            if (r) out.wakeColumnsMax = std::max(out.wakeColumnsMax, r->awakeColumns);
            if (r && r->stepSlots > 0 && r->stepSlots < out.stepSlotsMin) { out.stepSlotsMin = r->stepSlots; out.bricksAtMin = r->gpuBricks; }
            out.awake.push_back(r ? r->awakeColumns : -1);
            const WaterSurfaceField& f = *mgr.surfaceFields()[0];
            std::vector<float> t(f.cols.size(), std::nanf(""));
            for (size_t i = 0; i < f.cols.size(); ++i) {
                const int runs = std::min(static_cast<int>(f.cols[i].runs + 0.5f), kSurfaceMaxRuns);
                if (runs > 0) t[i] = f.cols[i].top[runs - 1];
            }
            out.tops.push_back(std::move(t));
            out.mass.push_back(rec.mass);
            out.books.push_back(rec.mass + (r ? r->residueDropped : 0.0));   // the water in the grid + what the residue sweep dropped
        }
        WaterCoreGpu::s_partialWakeOpenInterfaceForTest = false;
        out.frozenShareFirst = frozenSum / 30.0;
        out.ok = true;
        return out;
    }
    struct Diff { float maxSurf = 0.0f, maxFar = 0.0f; double maxMassRel = 0.0; int worstTick = -1, worstCol = -1; };
    // far = columns more than 3 m from the stone (the linear waves; the splash is chaotic)
    static Diff compare(const WakeRun& a, const WakeRun& b) {
        Diff d;
        const int nx = 180;   // the field's columns along x (20 m at 1/9 m)
        for (size_t k = 0; k < a.tops.size() && k < b.tops.size(); ++k) {
            for (size_t i = 0; i < a.tops[k].size(); ++i) {
                const float x = a.tops[k][i], y = b.tops[k][i];
                if (std::isnan(x) || std::isnan(y)) continue;
                const float e = std::abs(x - y);
                if (e > d.maxSurf) { d.maxSurf = e; d.worstTick = static_cast<int>(k); d.worstCol = static_cast<int>(i); }
                const float cx = (static_cast<int>(i) % nx + 0.5f) / 9.0f, cz = (static_cast<int>(i) / nx + 0.5f) / 9.0f;
                if ((cx - 2.0f) * (cx - 2.0f) + (cz - 5.0f) * (cz - 5.0f) > 9.0f) d.maxFar = std::max(d.maxFar, e);
            }
            d.maxMassRel = std::max(d.maxMassRel, std::abs(a.books[k] - b.books[k]) / std::max(1e-9, b.books[k]));
        }
        return d;
    }
};

// Measured on the first run (2026-10-18), and the reason the bound is the FLOOR, not 1 mm: the whole-body run is bit-repeatable
// (itself: 0), but a stone 1e-5 stronger moves its splash by 273 mm and the far field by 1.6 mm, the mass by 2.8e-6 (films the
// residue sweep drops, droplets) - a splash amplifies ANY change. So each case compares ON vs OFF against OFF vs OFF-perturbed
// (the same run, the push 1e-5 stronger): ON may differ no more than 2x that floor (+ a small absolute allowance), and the red
// control (the interface left open) must exceed the bound on the surface or the mass.
struct Verdict { WaterPartialWakeTest::Diff on, floor, open; };
static void report(const char* name, const Verdict& v) {
    std::printf("  %s: ON vs OFF max %.3f mm (tick %d col %d), far %.3f mm, mass %.2e | floor (OFF vs OFF push +1e-5) max %.3f, far %.3f mm, mass %.2e | OPEN max %.3f, far %.3f mm, mass %.2e\n",
                name, 1000.0f * v.on.maxSurf, v.on.worstTick, v.on.worstCol, 1000.0f * v.on.maxFar, v.on.maxMassRel, 1000.0f * v.floor.maxSurf, 1000.0f * v.floor.maxFar, v.floor.maxMassRel,
                1000.0f * v.open.maxSurf, 1000.0f * v.open.maxFar, v.open.maxMassRel);
}

TEST_F(WaterPartialWakeTest, AStoneWakesAPatchAndTheSurfaceMatchesTheWholeBody) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    for (const float strength : {0.4f, 4.0f}) {   // a gentle push (small, linear waves) and the bench's stone (a splash)
        const WakeRun off = run(false, false, strength), offP = run(false, false, strength * (1.0f + 1e-5f));
        const WakeRun on = run(true, false, strength), open = run(true, true, strength);
        ASSERT_TRUE(off.ok && offP.ok && on.ok && open.ok);
        Verdict v{compare(on, off), compare(offP, off), compare(open, off)};
        const std::string name = strength < 1.0f ? "PUSH 0.4" : "STONE 4";
        report(name.c_str(), v);
        std::printf("    frozen share over the first 0.5 s %.1f %%, most awake columns %d; fewest slots stepped %d (of %d bricks)\n", 100.0 * on.frozenShareFirst, on.wakeColumnsMax, on.stepSlotsMin, on.bricksAtMin);
        EXPECT_LT(on.stepSlotsMin, on.bricksAtMin) << name << ": step 2 - the step covered fewer slots than bricks stored (frozen bricks get no threads)";
        const float surfBound = 2.0f * v.floor.maxSurf + 1e-3f, farBound = 2.0f * v.floor.maxFar + 1e-3f;
        const double massBound = 2.0 * v.floor.maxMassRel + 1e-7;
        EXPECT_GT(on.frozenShareFirst, 0.5) << name << ": the push wakes a patch - most of the pond stays frozen at first";
        EXPECT_LE(v.on.maxSurf, surfBound) << name << ": the surface within the floor";
        EXPECT_LE(v.on.maxFar, farBound) << name << ": the far field within the floor";
        EXPECT_LE(v.on.maxMassRel, massBound) << name << ": the books (mass + residue dropped) within the floor";
        EXPECT_TRUE(v.open.maxSurf > surfBound || v.open.maxFar > farBound || v.open.maxMassRel > massBound)
            << name << ": control - without the wall the bound must break (the test must see the interface)";
    }
}

// 26.33 (WaterCore.md): with the partial wake the surface kernel and its copy to the host cover the AWAKE columns only; the host
// keeps every frozen column's last surface. The water is the same either way (only what the host reads changes) and the run is
// bit-repeatable, so the host's field must be BYTE-equal to the every-column read at every tick of the stone run - but for the
// surface velocity of a frozen column beside an awake one (within the tiler's 1e-3 m/s; measured ~1e-6). Red: the
// partial read resetting the columns it did not cover (the whole read's rule) must differ - the frozen columns are what it keeps.
TEST_F(WaterPartialWakeTest, TheSurfaceOverTheAwakeColumnsKeepsTheFrozenOnes) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    auto record = [&](bool partialSurface, bool resetAll, long& partialReads) {
        std::vector<std::vector<SurfaceColumn>> fields;
        auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
        WaterCoreManager mgr(state);
        std::string err;
        if (!mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) { ADD_FAILURE() << err; return fields; }
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(19, 1, 9), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        if (!id || !mgr.setPartialWake(id, true)) { ADD_FAILURE() << err; return fields; }
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(19, 0, 9), 1.0f, nullptr);
        WaterCoreGpu::s_partialSurface = partialSurface;
        WaterCoreGpu::s_partialSurfaceResetAllForTest = resetAll;
        const long reads0 = WaterCoreGpu::s_partialSurfaceReads;
        AvRecord rec;
        const float dt = 1.0f / 60.0f;
        for (int k = 0; k < 120; ++k) { mgr.step(id, 1, dt, &rec); (void)mgr.surfaceFields(); }
        mgr.addImpulse(glm::vec3(2.0f, 0.9f, 5.0f), 1.0f / 3.0f, 4.0f, glm::vec3(0.0f, -1.0f, 0.0f));
        for (int k = 0; k < 180; ++k) { mgr.step(id, 1, dt, &rec); fields.push_back(mgr.surfaceFields()[0]->cols); }
        partialReads = WaterCoreGpu::s_partialSurfaceReads - reads0;
        WaterCoreGpu::s_partialSurface = true; WaterCoreGpu::s_partialSurfaceResetAllForTest = false;
        return fields;
    };
    // every field but the surface velocity BYTE-equal; the velocity within the tiler's flow tolerance (26.30 step 3, 1e-3 m/s): a
    // frozen column's u, w average the face it shares with an awake neighbour, which the awake side still moves (measured ~1e-6 m/s)
    struct Cmp { int ticks = 0; long cols = 0; float maxFlow = 0.0f; };
    auto compareFields = [](const std::vector<std::vector<SurfaceColumn>>& a, const std::vector<std::vector<SurfaceColumn>>& b) {
        Cmp r;
        for (size_t k = 0; k < a.size() && k < b.size(); ++k) {
            long c = a[k].size() != b[k].size() ? 1 : 0;
            for (size_t i = 0; i < a[k].size() && i < b[k].size(); ++i) {
                SurfaceColumn x = a[k][i], y = b[k][i];
                r.maxFlow = std::max(r.maxFlow, std::max(std::abs(x.u - y.u), std::abs(x.w - y.w)));
                x.u = y.u = 0.0f; x.w = y.w = 0.0f;
                c += std::memcmp(&x, &y, sizeof(SurfaceColumn)) != 0;
            }
            r.cols += c; r.ticks += c > 0;
        }
        return r;
    };
    long readsAll = 0, readsPart = 0, readsRed = 0;
    const auto all = record(false, false, readsAll), part = record(true, false, readsPart), red = record(true, true, readsRed);
    ASSERT_EQ(all.size(), 180u); ASSERT_EQ(part.size(), 180u); ASSERT_EQ(red.size(), 180u);
    const Cmp cp = compareFields(part, all), cr = compareFields(red, all);
    std::printf("  partial surface reads %ld of 180 (every column: %ld); ticks differing %d (columns %ld), largest flow difference %.2e m/s; "
                "RED (reset all) ticks %d (columns %ld)\n", readsPart, readsAll, cp.ticks, cp.cols, cp.maxFlow, cr.ticks, cr.cols);
    EXPECT_EQ(readsAll, 0) << "the control read every column";
    EXPECT_GT(readsPart, 90) << "most reads covered the awake columns only (the stone wakes a patch)";
    EXPECT_EQ(cp.ticks, 0) << "the kept field (heights, runs, ground, body, foam) is byte-equal to the every-column read at every tick";
    EXPECT_LE(cp.maxFlow, 1e-3f) << "the kept surface velocity within the tiler's flow tolerance";
    EXPECT_GT(cr.ticks, 0) << "control: without keeping the frozen columns the field must differ";
}
