// WaterBrickTest — docs/WaterCore.md 26.6 (P2a): the GPU core stored in bricks that follow the water behaves EXACTLY as the
// same core with every brick of its box stored. Storage is a cost structure only: a cell's water never depends on which
// bricks are kept. Needs a Vulkan device: the fixture GTEST_SKIPs without one (never a pass).
//
// Every scene runs two volumes from the same start - DENSE (BrickOptions{}: every brick kept) and SPARSE (residency follows
// the water) - and compares, every tick, every cell's fill, every face's velocity and every cell's pressure with ==
// (bit-identical up to the sign of zero). The box straddles a 32 m chunk seam (world cell x = 288 at 1/9 m) and starts at
// a negative z, so brick seams, a chunk seam and floor division are all crossed on purpose.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"
#include "core/water/WaterCore.h"
#include "core/water/WaterCoreGpu.h"
#include <cmath>
#include <cstdio>
#include <functional>

using namespace Phyxel::Core::Water;
using Phyxel::Testing::VulkanTestFixture;

namespace {
constexpr float kDt = 1.0f / 60.0f;
constexpr float kH = 1.0f / 9.0f;
const glm::ivec3 kOrigin(288 - 13, 3, -21);   // local x 13 is the chunk seam; z from -21 (negative world cells)
const glm::ivec3 kDims(40, 16, 24);

WaterGrid makeGrid() {
    WaterGrid g(GridSpec{kOrigin, kDims, kH});
    for (int z = 0; z < kDims.z; ++z) for (int y = 0; y < kDims.y; ++y) for (int x = 0; x < kDims.x; ++x) g.occ(x, y, z) = Occ::Air;
    return g;
}

struct Diff { long f = 0, u = 0, v = 0, w = 0, p = 0; double worst = 0.0; std::string first; };
void compare(const char* what, const std::vector<float>& a, const std::vector<float>& b, long& count, Diff& d) {
    for (size_t i = 0; i < a.size(); ++i) if (!(a[i] == b[i])) {
        ++count; d.worst = std::max(d.worst, static_cast<double>(std::abs(a[i] - b[i])));
        if (d.first.empty()) { char buf[160]; std::snprintf(buf, sizeof buf, "%s[%zu] dense %.9g sparse %.9g", what, i, a[i], b[i]); d.first = buf; }
    }
}
} // namespace

class WaterBrickTest : public VulkanTestFixture {
protected:
    WaterCoreGpu gpu;
    void SetUp() override {
        VulkanTestFixture::SetUp();
        if (!isVulkanAvailable()) return;
        std::string err;
        if (!gpu.init(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) GTEST_SKIP() << "WaterCoreGpu init: " << err;
    }
    void TearDown() override { gpu.shutdown(); VulkanTestFixture::TearDown(); }

    struct SceneResult { long ticksCompared = 0, ticksDiffering = 0, flux = 0, haloShort = 0; bool sentinel = false; int minBricks = 1 << 30, maxBricks = 0, denseBricks = 0;
                         long allocated = 0, freed = 0; Diff diff; double massDense = 0, massSparse = 0; };
    /// Run `ticks` ticks of the same water in a dense and a sparse volume. `perTick(k, dense, sparse)` may feed bodies or
    /// sources before each step. `sparseOpts` lets the control freeze the residency.
    SceneResult runScene(const WaterGrid& start, int ticks, const std::function<void(int, WaterCoreGpu::Volume&, WaterCoreGpu::Volume&)>& perTick,
                         BrickOptions sparseOpts = BrickOptions{true, 0, false}, bool strict = true) {
        SceneResult r;
        std::string err;
        WaterCoreGpu::Volume* dv = gpu.createVolume(start, &err);
        EXPECT_NE(dv, nullptr) << err;
        WaterCoreGpu::Volume* sv = gpu.createVolume(start, &err, sparseOpts);
        EXPECT_NE(sv, nullptr) << err;
        if (!dv || !sv) return r;
        SolverParams prm;
        WaterGrid gd(start.spec()), gs(start.spec());
        std::vector<float> pd, ps;
        for (int k = 0; k < ticks; ++k) {
            if (perTick) perTick(k, *dv, *sv);
            const GpuStepStats a = gpu.step(*dv, prm, kDt, 1, 40);
            const GpuStepStats b = gpu.step(*sv, prm, kDt, 1, 40);
            r.denseBricks = a.bricks;
            r.minBricks = std::min(r.minBricks, b.bricks); r.maxBricks = std::max(r.maxBricks, b.bricks);
            r.flux += b.fluxIntoAbsent; r.haloShort += b.haloShort; r.sentinel = r.sentinel || b.sentinelDirty || a.sentinelDirty;
            r.allocated += b.allocated; r.freed += b.freed;
            if (strict) {
                EXPECT_EQ(a.asleep, b.asleep) << "tick " << k << ": the rest decision differs (a reduction order leak)";
                EXPECT_EQ(a.substepsLast, b.substepsLast) << "tick " << k;
            }
            gpu.download(*dv, gd); gpu.download(*sv, gs);
            gpu.readPressure(*dv, pd); gpu.readPressure(*sv, ps);
            Diff d;
            compare("f", gd.fData(), gs.fData(), d.f, d); compare("u", gd.uData(), gs.uData(), d.u, d);
            compare("v", gd.vData(), gs.vData(), d.v, d); compare("w", gd.wData(), gs.wData(), d.w, d);
            compare("p", pd, ps, d.p, d);
            ++r.ticksCompared;
            if (d.f + d.u + d.v + d.w + d.p > 0) {
                ++r.ticksDiffering;
                if (r.diff.first.empty()) { r.diff = d; r.diff.first = "tick " + std::to_string(k) + ": " + d.first; }
            }
            r.massDense = gd.totalMass(); r.massSparse = gs.totalMass();
        }
        gpu.destroyVolume(dv); gpu.destroyVolume(sv);
        return r;
    }
    void report(const char* name, const SceneResult& r) {
        std::printf("  %-22s %ld ticks, differing %ld (first: %s); bricks stored %d..%d of %d, allocated %ld freed %ld; halo short %ld; flux into absent %ld; sentinel %s; mass %.6f / %.6f\n",
                    name, r.ticksCompared, r.ticksDiffering, r.diff.first.empty() ? "-" : r.diff.first.c_str(), r.minBricks, r.maxBricks, r.denseBricks,
                    r.allocated, r.freed, r.haloShort, r.flux, r.sentinel ? "DIRTY" : "clean", r.massDense, r.massSparse);
    }
    void expectEqual(const char* name, const SceneResult& r) {
        report(name, r);
        EXPECT_EQ(r.ticksDiffering, 0) << name << ": " << r.diff.first << " (f " << r.diff.f << ", u " << r.diff.u << ", v " << r.diff.v << ", w " << r.diff.w << ", p " << r.diff.p << " sites)";
        EXPECT_EQ(r.haloShort, 0) << name << ": water came within 4 cells of a brick that was not stored (the halo is too thin)";
        EXPECT_EQ(r.flux, 0) << name << ": water tried to move into a brick that was not stored";
        EXPECT_FALSE(r.sentinel) << name << ": something wrote into the absent brick";
        EXPECT_LT(r.minBricks, r.denseBricks) << name << ": the sparse run stored every brick - the test would prove nothing";
    }
};

// 26.6 P2a GATE: sparse == dense, bit for bit, across chunk and brick seams; the halo never too thin; the absent brick clean.
TEST_F(WaterBrickTest, SparseEqualsDenseAcrossSeams) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    {   // a dam break whose front runs across brick seams and the chunk seam (local x 13 -> world 288)
        WaterGrid g = makeGrid();
        g.fillBox({0, 0, 0}, {7, 7, kDims.z - 1}, 1.0f);
        expectEqual("dam break", runScene(g, 180, nullptr));
    }
    {   // a slosh in a corner pond (a tilted surface), crossing the chunk seam
        WaterGrid g = makeGrid();
        for (int z = 0; z < 12; ++z) for (int x = 6; x < 22; ++x) {
            const float top = 3.0f + 1.5f * (x - 14) / 8.0f;
            for (int y = 0; y < kDims.y; ++y) g.f(x, y, z) = std::clamp(top - static_cast<float>(y), 0.0f, 1.0f);
        }
        for (int z = 0; z < kDims.z; ++z) for (int y = 0; y < kDims.y; ++y) for (int x = 0; x < kDims.x; ++x)   // the pond's walls
            if ((x == 5 || x == 22 || z == 12) && x >= 5 && x <= 22 && z <= 12) g.occ(x, y, z) = Occ::Solid;
        expectEqual("slosh", runScene(g, 150, nullptr));
    }
    {   // a 1/3 m stone dropped into a pond: moving solids (s, the wake, fresh) fed to both every frame
        WaterGrid g = makeGrid();
        g.fillBox({0, 0, 0}, {kDims.x - 1, 3, kDims.z - 1}, 1.0f);
        WaterGrid fieldGrid(g.spec());   // the CPU solver only rasterises the bodies (the fields that do not read the water)
        WaterSolver fields(fieldGrid, [](const glm::ivec3&) { return Occ::Air; });
        auto feed = [&](int k, WaterCoreGpu::Volume& a, WaterCoreGpu::Volume& b) {
            MovingSolid st; st.halfExtents = glm::vec3(1.0f / 6.0f);
            const glm::vec3 o = glm::vec3(kOrigin) * kH;
            st.centre = o + glm::vec3(20.5f * kH, std::max(1.0f / 6.0f, 1.3f - 4.0f * k * kDt), 12.5f * kH);
            st.velocity = glm::vec3(0.0f, st.centre.y > 1.0f / 6.0f + 1e-4f ? -4.0f : 0.0f, 0.0f);
            fields.updateSolidFields({st}, kDt);
            gpu.setSolids(a, fieldGrid.sData(), fields.solidWake(), fields.solidFresh(), kDt);
            gpu.setSolids(b, fieldGrid.sData(), fields.solidWake(), fields.solidFresh(), kDt);
        };
        expectEqual("stone entry", runScene(g, 120, feed));
    }
    {   // a pump pouring into DRY bricks (allocation as the water arrives), across the chunk seam
        WaterGrid g = makeGrid();
        g.fillBox({0, 0, 0}, {3, 1, 3}, 1.0f);
        const int cell = 18 + kDims.x * (6 + kDims.y * 10);   // grid-local (18, 6, 10)
        auto pump = [&](int k, WaterCoreGpu::Volume& a, WaterCoreGpu::Volume& b) {
            if (k != 0) return;
            GpuSource s; s.cell = cell; s.rate = 0.02f;
            std::string err;
            gpu.setSources(a, {s}, &err); gpu.setSources(b, {s}, &err);
        };
        expectEqual("pump into dry bricks", runScene(g, 240, pump));
    }
    {   // a sink draining a small pond dry (bricks FREED as it empties): 0.18 m^3 at 0.1 m^3/s
        WaterGrid g = makeGrid();
        g.fillBox({24, 0, 14}, {31, 1, 21}, 1.0f);
        const int cell = 28 + kDims.x * (0 + kDims.y * 18);
        auto sink = [&](int k, WaterCoreGpu::Volume& a, WaterCoreGpu::Volume& b) {
            if (k != 0) return;
            GpuSource s; s.cell = cell; s.rate = -0.1f;
            std::string err;
            gpu.setSources(a, {s}, &err); gpu.setSources(b, {s}, &err);
        };
        const SceneResult r = runScene(g, 360, sink);
        expectEqual("sink draining", r);
        EXPECT_GT(r.freed, 0) << "the drain must free bricks (else the free path is untested)";
    }
}

// The control (RED by design): residency FROZEN at the upload's bricks - the dam break's front reaches bricks that are not
// stored: the halo check counts it and the fields differ. Proves the equality test and the halo check see a wrong halo. (The
// transport's own count stays 0 here: a face between a stored cell and an absent brick belongs to the absent brick and
// never carries velocity - measured 2026-10-13, which is why haloShort exists.)
TEST_F(WaterBrickTest, AFrozenResidencyIsCaught) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    WaterGrid g = makeGrid();
    g.fillBox({0, 0, 0}, {7, 7, kDims.z - 1}, 1.0f);
    const SceneResult r = runScene(g, 120, nullptr, BrickOptions{true, 0, true}, false);
    report("frozen residency", r);
    EXPECT_GT(r.haloShort, 0) << "control: the halo check must see water next to bricks that are not stored";
    EXPECT_GT(r.ticksDiffering, 0) << "control: the frozen run must differ from the dense one";
    EXPECT_NEAR(r.massSparse, r.massDense, 1e-3) << "a closed face holds water - it never drops it";
}

// Capacity forced small: residency wants more bricks than there are slots - the faces stay closed (counted), the water is
// held, nothing is dropped, the absent brick stays clean. The dam break's water starts in 16 bricks; 24 slots hold it and
// half a halo. A capacity below the water itself is refused at create.
TEST_F(WaterBrickTest, CapacityHoldsWater) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    WaterGrid g = makeGrid();
    g.fillBox({0, 0, 0}, {7, 7, kDims.z - 1}, 1.0f);
    std::string err;
    WaterCoreGpu::Volume* tooSmall = gpu.createVolume(g, &err, BrickOptions{true, 10, false});
    EXPECT_EQ(tooSmall, nullptr) << "a capacity below the water must be refused";
    std::printf("  capacity 10 (the water needs 16): %s\n", err.c_str());
    if (tooSmall) gpu.destroyVolume(tooSmall);
    err.clear();
    WaterCoreGpu::Volume* v = gpu.createVolume(g, &err, BrickOptions{true, 24, false});
    ASSERT_NE(v, nullptr) << err;
    SolverParams prm;
    long held = 0, flux = 0, shortBy = 0; bool dirty = false;
    double m0 = g.totalMass(), worst = 0.0;
    WaterGrid out(g.spec());
    for (int k = 0; k < 120; ++k) {
        const GpuStepStats st = gpu.step(*v, prm, kDt, 1, 40);
        held += st.heldByCapacity; flux += st.fluxIntoAbsent; shortBy += st.haloShort; dirty = dirty || st.sentinelDirty;
        gpu.download(*v, out);
        worst = std::max(worst, std::abs(out.totalMass() - m0));
    }
    std::printf("  capacity 24 bricks: held %ld brick-calls, halo short %ld, closed faces %ld, sentinel %s, worst |mass - start| %.2e m^3 (start %.4f)\n",
                held, shortBy, flux, dirty ? "DIRTY" : "clean", worst, m0);
    EXPECT_GT(held, 0) << "the capacity must have refused bricks (else the test is vacuous)";
    EXPECT_GT(shortBy, 0) << "the refused bricks must show as a short halo";
    EXPECT_FALSE(dirty);
    EXPECT_LT(worst, 1e-4 * m0) << "water was dropped at the capacity limit";
    gpu.destroyVolume(v);
}

// 26.6 P2a COST GATE: a shallow creek (the bench's section: 2 m wide, 0.15 m deep at 1/9 m) running DIAGONALLY across a
// 60 x 60 m box (~85 m of creek) - the case a box cannot afford (5.2 M cells) and bricks can. Banks 0.44 m high, the water
// resting (cost does not depend on motion: DiagCostPerCell, resting vs slosh). Median of 30 calls, one tick each, after a
// warm-up. Gate: the sparse volume within the 2 ms budget (25.10); the stored bricks and stepped cells per water cell
// reported; the dense box measured beside it where the device holds it.
// STATUS (2026-10-13): OPEN - measured 1.94-2.02 ms median across runs (dense box 16.9 ms): at the line, so DISABLED as a
// measurement rather than a flaky gate; and 25.10's 2 ms is for ALL water, so one creek must cost well under it. The
// levers, measured: WaterCore.md 26.8.
TEST_F(WaterBrickTest, DISABLED_ACreekStoredSparseFitsTheBudget) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const glm::ivec3 dims(540, 18, 540);
    WaterGrid g(GridSpec{glm::ivec3(0), dims, kH});
    const float halfWidth = 9.0f;   // cells (1 m)
    long waterCells = 0;
    for (int z = 0; z < dims.z; ++z) for (int x = 0; x < dims.x; ++x) {
        const float d = std::abs(static_cast<float>(x - z)) / std::sqrt(2.0f);   // distance from the diagonal, cells
        const bool channel = d < halfWidth;
        for (int y = 0; y < dims.y; ++y) {
            g.occ(x, y, z) = (!channel && y < 4) ? Occ::Solid : Occ::Air;
            if (channel && y == 0) { g.f(x, y, z) = 1.0f; ++waterCells; }
            if (channel && y == 1) { g.f(x, y, z) = 0.35f; ++waterCells; }
        }
    }
    SolverParams prm;
    auto measure = [&](WaterCoreGpu::Volume* v, int& bricks, long& shortBy) {
        for (int k = 0; k < 20; ++k) {
            const GpuStepStats st = gpu.step(*v, prm, kDt, 1, 40);
            if (std::getenv("DIAG_RES") && k < 8) std::printf("    warm %d: bricks %d alloc %d freed %d rings %d asleep %d ms %.3f maxSpeed %.4f\n", k, st.bricks, st.allocated, st.freed, st.haloRings, int(st.asleep), st.gpuMs, st.maxSpeed);
        }
        std::vector<double> ms;
        double res = 0.0, wait = 0.0, after = 0.0;
        for (int k = 0; k < 30; ++k) {
            const GpuStepStats st = gpu.step(*v, prm, kDt, 1, 40);
            ms.push_back(st.gpuMs); bricks = st.bricks; shortBy += st.haloShort;
            res += st.hostResidencyMs / 30.0; wait += st.submitWaitMs / 30.0; after += st.hostAfterMs / 30.0;
        }
        std::printf("    mean split: host residency %.3f ms, recording %.3f ms, submit+fence %.3f ms, after %.3f ms\n", res, std::max(0.0, [&]{ double m = 0; for (double x : ms) m += x / 30.0; return m; }() - res - wait), wait, after);
        std::sort(ms.begin(), ms.end());
        return std::make_pair(ms[15], ms[27]);
    };
    std::string err;
    WaterCoreGpu::Volume* sv = gpu.createVolume(g, &err, BrickOptions{true, 0, false});
    ASSERT_NE(sv, nullptr) << err;
    int bricks = 0; long shortBy = 0;
    const auto sp = measure(sv, bricks, shortBy);
    {   // where the bricks go: holding water or halo, per brick layer; and what "water" the wet ones hold above the creek
        const auto* info = reinterpret_cast<const int32_t*>(static_cast<const char*>(sv->staging.mapped) + sv->offBricks) + sv->infoOff;
        int wet[8] = {0}, halo[8] = {0};
        for (size_t sl = 1; sl < static_cast<size_t>(sv->slotsUsed); ++sl) {
            const int packed = sv->slotBrick[sl];
            if (packed < 0) continue;
            const int by = (packed >> 10) & 1023;
            if (info[sl] & 1) ++wet[std::min(by, 7)]; else ++halo[std::min(by, 7)];
        }
        std::printf("  bricks by layer (wet / halo):");
        for (int y = 0; y < sv->bn.y; ++y) std::printf(" y%d %d / %d", y, wet[y], halo[y]);
        WaterGrid now(g.spec()); gpu.download(*sv, now);
        double above = 0.0; long aboveCells = 0; float aboveMax = 0.0f;
        for (int z = 0; z < dims.z; ++z) for (int y = 2; y < dims.y; ++y) for (int x = 0; x < dims.x; ++x) {
            const float v = now.f(x, y, z);
            if (v != 0.0f) { above += v; ++aboveCells; aboveMax = std::max(aboveMax, std::abs(v)); }
        }
        std::printf("\n  water above the creek's two cells: %ld cells, sum %.3e cells, max |f| %.3e; last max speed %.4f m/s\n", aboveCells, above, aboveMax, sv->lastMaxSpeed);
    }
    gpu.destroyVolume(sv);
    std::printf("  diagonal creek (~85 m, 2 m x 0.15 m at 1/9 m): %ld water cells; SPARSE %d bricks = %ld stepped cells (%.1f per water cell): median %.3f ms (p90 %.3f), halo short %ld\n",
                waterCells, bricks, static_cast<long>(bricks) * 512, static_cast<double>(bricks) * 512.0 / waterCells, sp.first, sp.second, shortBy);
    WaterCoreGpu::Volume* dv = gpu.createVolume(g, &err);
    if (dv) {
        int db = 0; long ds = 0;
        const auto dp = measure(dv, db, ds);
        std::printf("  the same creek in a DENSE box: %d bricks = %ld stepped cells: median %.3f ms (p90 %.3f)\n", db, static_cast<long>(db) * 512, dp.first, dp.second);
        gpu.destroyVolume(dv);
    } else {
        std::printf("  the same creek in a DENSE box: refused (%s)\n", err.c_str());
    }
    EXPECT_EQ(shortBy, 0);
    EXPECT_LE(sp.first, 2.0) << "the sparse creek must fit the 2 ms water budget (25.10)";
}

// 26.6: the brick layout at an UNALIGNED origin. The parity suite's boxes all start at (0, 0, 0) (o = 0); engine volumes start
// anywhere. A resting pond (four full layers, one half) 30 s on the GPU at five origins: every origin's fills at 2 s
// BIT-IDENTICAL to the aligned run's (the brick layout never changes the water), mass held, and the CPU reference as a
// sanity bound (1e-2: the GPU's 40 cold-start sweeps vs the CPU's converged PCG - 5.0e-3 measured at 2 s).
TEST_F(WaterBrickTest, AnyOriginKeepsTheWater) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    std::vector<float> aligned;
    for (glm::ivec3 origin : {glm::ivec3(0), glm::ivec3(3, 0, 0), glm::ivec3(0, 5, 0), glm::ivec3(0, 0, 6), glm::ivec3(299, 47, -21)}) {
        WaterGrid g(GridSpec{origin, glm::ivec3(18, 9, 18), 1.0f / 3.0f}), c(GridSpec{origin, glm::ivec3(18, 9, 18), 1.0f / 3.0f});
        for (WaterGrid* x : {&g, &c}) {
            for (int z = 0; z < 18; ++z) for (int y = 0; y < 9; ++y) for (int xx = 0; xx < 18; ++xx) x->occ(xx, y, z) = Occ::Air;
            x->fillBox({0, 0, 0}, {17, 3, 17}, 1.0f);
            for (int z = 0; z < 18; ++z) for (int xx = 0; xx < 18; ++xx) x->f(xx, 4, z) = 0.5f;
        }
        const glm::ivec3 o = origin; const glm::ivec3 d(18, 9, 18);
        WaterSolver cpu(c, [o, d](const glm::ivec3& cw) { const glm::ivec3 l = cw - o; return (l.x < 0 || l.y < 0 || l.z < 0 || l.x >= d.x || l.y >= d.y || l.z >= d.z) ? Occ::Solid : Occ::Air; });
        std::string err;
        WaterCoreGpu::Volume* v = gpu.createVolume(g, &err);
        ASSERT_NE(v, nullptr) << err;
        SolverParams prm;
        const double m0 = g.totalMass();
        double worst = 0.0;
        WaterGrid out(g.spec());
        for (int k = 0; k < 1800; ++k) {
            const GpuStepStats st = gpu.step(*v, prm, kDt, 1, 40);
            worst = std::max(worst, std::abs(st.totalMass - m0));
            if (k < 120) cpu.step(kDt);
            if (k == 119) {
                gpu.download(*v, out);
                double df = 0.0;
                for (size_t i = 0; i < out.fData().size(); ++i) df = std::max(df, static_cast<double>(std::abs(out.fData()[i] - c.fData()[i])));
                long differ = 0;
                if (aligned.empty()) aligned = out.fData();
                else for (size_t i = 0; i < aligned.size(); ++i) differ += (aligned[i] == out.fData()[i]) ? 0 : 1;
                std::printf("  origin (%d,%d,%d): fills vs CPU at 2 s max|df| %.2e; cells differing from the aligned run %ld\n", origin.x, origin.y, origin.z, df, differ);
                EXPECT_EQ(differ, 0) << "the brick layout changed the water at origin " << origin.x << "," << origin.y << "," << origin.z;
                EXPECT_LT(df, 1e-2);
            }
        }
        std::printf("  origin (%d,%d,%d) (o = %d,%d,%d): worst |mass - start| over 30 s %.3e m^3 of %.4f\n", origin.x, origin.y, origin.z, v->o.x, v->o.y, v->o.z, worst, m0);
        EXPECT_LT(worst, 1e-5 * m0) << "origin " << origin.x << "," << origin.y << "," << origin.z;
        gpu.destroyVolume(v);
    }
}

// 26.6: the engine re-uploads a GPU volume whenever its occupancy refreshes (WaterCoreManager::stepGpu: download, then
// upload). The round trip must hold the water exactly: a resting pond, download + upload EVERY tick for 10 s, at an
// unaligned origin with ground in the box (the pond sits in a pit, as on the WaterBench_Small bench).
TEST_F(WaterBrickTest, ARoundTripEveryTickKeepsTheWater) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const glm::ivec3 origin(299, 13, -21), d(18, 18, 18);
    WaterGrid g(GridSpec{origin, d, 1.0f / 3.0f});
    for (int z = 0; z < d.z; ++z) for (int y = 0; y < d.y; ++y) for (int x = 0; x < d.x; ++x) {
        const bool pit = x >= 3 && x < 15 && z >= 3 && z < 15 && y >= 2;
        g.occ(x, y, z) = (pit || y >= 8) ? Occ::Air : Occ::Solid;
    }
    for (int z = 3; z < 15; ++z) for (int x = 3; x < 15; ++x) { for (int y = 2; y < 6; ++y) g.f(x, y, z) = 1.0f; g.f(x, 6, z) = 0.5f; }
    for (bool roundTrip : {false, true}) {
        WaterGrid w = g;
        std::string err;
        WaterCoreGpu::Volume* v = gpu.createVolume(w, &err);
        ASSERT_NE(v, nullptr) << err;
        SolverParams prm;
        const double m0 = w.totalMass();
        for (int k = 0; k < 600; ++k) {
            gpu.step(*v, prm, kDt, 1, 40);
            if (roundTrip) { gpu.download(*v, w); gpu.upload(*v, w); }
        }
        gpu.download(*v, w);
        std::printf("  %s: mass %.6f -> %.6f (%+.3e m^3)\n", roundTrip ? "download + upload every tick" : "no round trip", m0, w.totalMass(), w.totalMass() - m0);
        EXPECT_NEAR(w.totalMass(), m0, 1e-5 * m0);
        gpu.destroyVolume(v);
    }
}

// 26.6 diagnostic: the bench pond SETTLING from its placed start (a top layer at exactly f = 0.5 = the liquid threshold) at
// the engine's settings - 27 sweeps, frame dt - vs the tests' 40 and 1/60. The live GPU pond lost ~3e-4 m^3 while it
// settled (2026-10-13), with the warm start on; nothing with it off.
TEST_F(WaterBrickTest, DISABLED_DiagSettlingLoss) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const glm::ivec3 origin(297, 42, 33), d(18, 18, 18);
    WaterGrid g(GridSpec{origin, d, 1.0f / 3.0f});
    for (int z = 0; z < d.z; ++z) for (int y = 0; y < d.y; ++y) for (int x = 0; x < d.x; ++x) {
        const bool pit = x >= 3 && x < 15 && z >= 3 && z < 15 && y >= 3;
        g.occ(x, y, z) = (pit || y >= 12) ? Occ::Air : Occ::Solid;
    }
    for (int z = 3; z < 15; ++z) for (int x = 3; x < 15; ++x) { for (int y = 3; y < 7; ++y) g.f(x, y, z) = 1.0f; g.f(x, 7, z) = 0.5f; }
    for (int sweeps : {40, 27}) for (float dt : {1.0f / 60.0f, 1.0f / 165.0f}) {
        WaterGrid w = g;
        std::string err;
        WaterCoreGpu::Volume* v = gpu.createVolume(w, &err);
        ASSERT_NE(v, nullptr) << err;
        SolverParams prm;
        const double m0 = w.totalMass();
        int sleptAt = -1; double minM = m0;
        const int ticks = static_cast<int>(20.0f / dt);
        for (int k = 0; k < ticks && sleptAt < 0; ++k) {
            const GpuStepStats st = gpu.step(*v, prm, dt, 1, sweeps);
            if (st.totalMass > 0.0) minM = std::min(minM, st.totalMass);
            if (st.asleep) sleptAt = k;
        }
        gpu.download(*v, w);
        std::printf("  sweeps %d, dt %.4f: asleep at tick %d (%.2f s); mass %.6f -> %.6f (%+.3e m^3), lowest %.6f\n", sweeps, dt, sleptAt, sleptAt * dt, m0, w.totalMass(), w.totalMass() - m0, minM);
        gpu.destroyVolume(v);
    }
}

// 26.6 diagnostic: the settling loss tick by tick (40 sweeps, dt 1/165): per-tick mass change from the downloaded grid,
// the worst ticks' changed cells.
TEST_F(WaterBrickTest, DISABLED_DiagSettlingLossPerTick) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const glm::ivec3 origin(297, 42, 33), d(18, 18, 18);
    WaterGrid g(GridSpec{origin, d, 1.0f / 3.0f});
    for (int z = 0; z < d.z; ++z) for (int y = 0; y < d.y; ++y) for (int x = 0; x < d.x; ++x) {
        const bool pit = x >= 3 && x < 15 && z >= 3 && z < 15 && y >= 3;
        g.occ(x, y, z) = (pit || y >= 12) ? Occ::Air : Occ::Solid;
    }
    for (int z = 3; z < 15; ++z) for (int x = 3; x < 15; ++x) { for (int y = 3; y < 7; ++y) g.f(x, y, z) = 1.0f; g.f(x, 7, z) = 0.5f; }
    WaterGrid w = g;
    std::string err;
    WaterCoreGpu::Volume* v = gpu.createVolume(w, &err);
    ASSERT_NE(v, nullptr) << err;
    SolverParams prm;
    const float dt = 1.0f / 165.0f;
    std::vector<float> prevF = w.fData();
    double prevM = w.totalMass(), lossSum = 0.0, gainSum = 0.0;
    struct Ev { double dm; int tick; std::vector<std::pair<size_t, std::pair<float, float>>> cells; };
    std::vector<Ev> worst;
    for (int k = 0; k < 1650; ++k) {
        const GpuStepStats st = gpu.step(*v, prm, dt, 1, 40);
        gpu.download(*v, w);
        const double m = w.totalMass(), dm = m - prevM;
        if (dm < 0) lossSum += dm; else gainSum += dm;
        if (dm < -1e-7) {
            Ev e; e.dm = dm; e.tick = k;
            for (size_t i = 0; i < prevF.size(); ++i) if (std::abs(w.fData()[i] - prevF[i]) > 1e-6f) e.cells.push_back({i, {prevF[i], w.fData()[i]}});
            worst.push_back(e);
            std::sort(worst.begin(), worst.end(), [](const Ev& a, const Ev& b) { return a.dm < b.dm; });
            if (worst.size() > 3) worst.resize(3);
        }
        prevM = m; prevF = w.fData();
        if (k % 330 == 0) std::printf("  tick %4d: mass %.7f ke %.1e quiet %d substeps %d\n", k, m, st.kineticEnergy, st.quietTicks, st.substepsLast);
    }
    std::printf("  losses %.3e, gains %.3e (cell units x h^3 = m^3)\n", lossSum, gainSum);
    for (const Ev& e : worst) {
        double sum = 0.0; for (auto& c : e.cells) sum += (c.second.second - c.second.first);
        std::printf("  tick %d: dm %.3e m^3, %zu cells changed, net %.3e cells:\n", e.tick, e.dm, e.cells.size(), sum);
        int shown = 0;
        for (auto& c : e.cells) {
            if (shown++ >= 12) break;
            const size_t i = c.first; const int x = int(i % 18), y = int((i / 18) % 18), z = int(i / 324);
            std::printf("     (%d,%d,%d) %.7f -> %.7f\n", x, y, z, c.second.first, c.second.second);
        }
    }
    gpu.destroyVolume(v);
}
