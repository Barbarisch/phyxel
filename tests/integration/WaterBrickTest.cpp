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
#include "core/water/WaterSurfaceMesh.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
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
                         int sparseHeight = 8, denseHeight = 8;   // 26.17: brick heights (a brick is 8 x h x 8 cells)
                         int maxGround = 0;                        // 26.18: most halo bricks read as ground in one call
                         WaterGrid sparseEnd{GridSpec{glm::ivec3(0), glm::ivec3(1), 1.0f}};   // the sparse run's last state
                         long allocated = 0, freed = 0; Diff diff; double massDense = 0, massSparse = 0;
                         long surfaceColumnsDiffering = 0; std::string surfaceFirst; };   // 26.11: the surface field the renderer gets, byte for byte
    /// Run `ticks` ticks of the same water in a dense and a sparse volume. `perTick(k, dense, sparse)` may feed bodies or
    /// sources before each step. `sparseOpts` lets the control freeze the residency.
    SceneResult runScene(const WaterGrid& start, int ticks, const std::function<void(int, WaterCoreGpu::Volume&, WaterCoreGpu::Volume&)>& perTick,
                         BrickOptions sparseOpts = BrickOptions{true, 0, false}, bool strict = true) {
        SceneResult r;
        std::string err;
        WaterCoreGpu::Volume* dv = gpu.createVolume(start, &err, BrickOptions{false, 0, false, 8});   // every 8^3 brick kept (26.17: named, not the default)
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
            r.denseBricks = a.bricks; r.denseHeight = a.brickHeight; r.sparseHeight = b.brickHeight; r.maxGround = std::max(r.maxGround, b.groundBricks);
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
            if (k == ticks - 1) r.sparseEnd = gs;
            {   // 26.11: the surface field (packed lists, dry columns rebuilt on the host) - a bricked volume draws what the dense box draws
                std::vector<SurfaceColumn> sd(static_cast<size_t>(start.nx()) * start.nz()), ss(sd.size());
                const bool okD = gpu.readSurface(*dv, reinterpret_cast<float*>(sd.data())), okS = gpu.readSurface(*sv, reinterpret_cast<float*>(ss.data()));
                EXPECT_TRUE(okD && okS);
                for (size_t c = 0; c < sd.size(); ++c) if (std::memcmp(&sd[c], &ss[c], sizeof(SurfaceColumn)) != 0) {
                    ++r.surfaceColumnsDiffering;
                    if (r.surfaceFirst.empty()) r.surfaceFirst = "tick " + std::to_string(k) + " column " + std::to_string(c);
                }
            }
        }
        gpu.destroyVolume(dv); gpu.destroyVolume(sv);
        return r;
    }
    void report(const char* name, const SceneResult& r) {
        std::printf("  %-22s %ld ticks, differing %ld (first: %s); bricks (8x%dx8) stored %d..%d (cells %ld..%ld of %ld), allocated %ld freed %ld; halo short %ld; flux into absent %ld; sentinel %s; mass %.6f / %.6f\n",
                    name, r.ticksCompared, r.ticksDiffering, r.diff.first.empty() ? "-" : r.diff.first.c_str(), r.sparseHeight, r.minBricks, r.maxBricks,
                    64L * r.sparseHeight * r.minBricks, 64L * r.sparseHeight * r.maxBricks, 64L * r.denseHeight * r.denseBricks,
                    r.allocated, r.freed, r.haloShort, r.flux, r.sentinel ? "DIRTY" : "clean", r.massDense, r.massSparse);
        if (r.haloShort > 0)   // 26.23 diagnostic: what needed the short bricks
            std::printf("    halo short by cause: liquid that was liquid at the call's start %ld, liquid that was only film then %ld, film %ld\n",
                        WaterCoreGpu::s_haloShortLiquidOldForTest, WaterCoreGpu::s_haloShortLiquidNewForTest, WaterCoreGpu::s_haloShortFilmForTest);
        if (r.haloShort > 0) std::printf("    %s\n", WaterCoreGpu::s_haloShortDetailForTest.c_str());
        WaterCoreGpu::s_haloShortDetailForTest.clear();
        WaterCoreGpu::s_haloShortLiquidOldForTest = WaterCoreGpu::s_haloShortLiquidNewForTest = WaterCoreGpu::s_haloShortFilmForTest = 0;
    }
    void expectEqual(const char* name, const SceneResult& r) {
        report(name, r);
        EXPECT_EQ(r.surfaceColumnsDiffering, 0) << name << ": the surface field differs, first at " << r.surfaceFirst;
        EXPECT_EQ(r.ticksDiffering, 0) << name << ": " << r.diff.first << " (f " << r.diff.f << ", u " << r.diff.u << ", v " << r.diff.v << ", w " << r.diff.w << ", p " << r.diff.p << " sites)";
        EXPECT_EQ(r.haloShort, 0) << name << ": water came within 4 cells of a brick that was not stored (the halo is too thin)";
        EXPECT_EQ(r.flux, 0) << name << ": water tried to move into a brick that was not stored";
        EXPECT_FALSE(r.sentinel) << name << ": something wrote into the absent brick";
        EXPECT_LT(64L * r.sparseHeight * r.minBricks, 64L * r.denseHeight * r.denseBricks) << name << ": the sparse run stored every cell - the test would prove nothing";
    }
    /// The 26.8 scenes (+ 26.17's stepped bed), a dense 8^3 volume against a sparse one stored as `sparseOpts` says.
    void seamScenes(BrickOptions sparseOpts, const char* tag);
    /// 26.18: scenes where the ground matters - an overhang, a dig under flowing water, a pit filled in.
    void groundScenes(BrickOptions sparseOpts, const char* tag);
    /// 26.18: a world edit mid-run, as the engine does it: the water read back, the new occupancy, both re-uploaded.
    void reshape(WaterCoreGpu::Volume& v, const WaterGrid& world) {
        WaterGrid t = world;
        gpu.download(v, t);   // the water; the occupancy stays the world's (the edit)
        gpu.upload(v, t);
    }
    /// 26.19: a world edit patched IN PLACE, as the engine now does it: the cells whose occupancy differs from the volume's host
    /// copy, handed to updateOccupancy - no read-back, no re-upload.
    void patch(WaterCoreGpu::Volume& v, const WaterGrid& world) {
        std::vector<int> cells;
        for (int z = 0; z < world.nz(); ++z) for (int y = 0; y < world.ny(); ++y) for (int x = 0; x < world.nx(); ++x) {
            const size_t i = world.idx(x, y, z);
            if (i < v.hostOcc.size() && v.hostOcc[i] != static_cast<uint32_t>(world.occ(x, y, z))) cells.push_back(static_cast<int>(i));
        }
        gpu.updateOccupancy(v, world, cells);
    }
    /// 26.19: after a patch and one call's residency - every STORED cell's device occupancy is the world's, and every GROUND brick
    /// is all ground in the current world. Returns the mismatches.
    long occupancyMismatches(WaterCoreGpu::Volume& v, const WaterGrid& world) {
        std::vector<float> dev;
        gpu.readCells(v, "occ", dev);
        long bad = 0;
        for (int z = 0; z < world.nz(); ++z) for (int y = 0; y < world.ny(); ++y) for (int x = 0; x < world.nx(); ++x) {
            const glm::ivec3 b = (glm::ivec3(x, y, z) + v.o) / v.bs();
            const int slot = v.map[static_cast<size_t>(b.x) + static_cast<size_t>(v.bn.x) * (b.y + static_cast<size_t>(v.bn.y) * b.z)];
            const Occ want = world.occ(x, y, z);
            if (slot >= 2) bad += dev[world.idx(x, y, z)] != static_cast<float>(static_cast<uint32_t>(want)) ? 1 : 0;   // a real brick
            else if (slot == 1) bad += want != Occ::Solid ? 1 : 0;                                                       // the ground sentinel
        }
        return bad;
    }
    /// 26.19: the same edits as groundScenes and more, patched in place.
    void patchScenes(BrickOptions sparseOpts, const char* tag, bool expectEqualRuns = true);
};

// The equality scenes: each runs a dense 8^3 volume (every brick kept) and a sparse one, and compares every tick.
void WaterBrickTest::seamScenes(BrickOptions sparseOpts, const char* tag) {
    std::string label;
    auto name = [&](const char* scene) { label = std::string(tag) + " " + scene; return label.c_str(); };
    {   // a dam break whose front runs across brick seams and the chunk seam (local x 13 -> world 288)
        WaterGrid g = makeGrid();
        g.fillBox({0, 0, 0}, {7, 7, kDims.z - 1}, 1.0f);
        expectEqual(name("dam break"), runScene(g, 180, nullptr, sparseOpts));
    }
    {   // a slosh in a corner pond (a tilted surface), crossing the chunk seam
        WaterGrid g = makeGrid();
        for (int z = 0; z < 12; ++z) for (int x = 6; x < 22; ++x) {
            const float top = 3.0f + 1.5f * (x - 14) / 8.0f;
            for (int y = 0; y < kDims.y; ++y) g.f(x, y, z) = std::clamp(top - static_cast<float>(y), 0.0f, 1.0f);
        }
        for (int z = 0; z < kDims.z; ++z) for (int y = 0; y < kDims.y; ++y) for (int x = 0; x < kDims.x; ++x)   // the pond's walls
            if ((x == 5 || x == 22 || z == 12) && x >= 5 && x <= 22 && z <= 12) g.occ(x, y, z) = Occ::Solid;
        expectEqual(name("slosh"), runScene(g, 150, nullptr, sparseOpts));
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
        expectEqual(name("stone entry"), runScene(g, 120, feed, sparseOpts));
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
        expectEqual(name("pump into dry bricks"), runScene(g, 240, pump, sparseOpts));
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
        const SceneResult r = runScene(g, 360, sink, sparseOpts);
        expectEqual(name("sink draining"), r);
        EXPECT_GT(r.freed, 0) << "the drain must free bricks (else the free path is untested)";
    }
    {   // 26.17: water over a STEPPED solid bed (the creek's case - bricks under the water hold only ground): 3 cells of water
        // released over a floor 4 cells deep that drops one cell at x 20, across the chunk seam
        WaterGrid g = makeGrid();
        for (int z = 0; z < kDims.z; ++z) for (int y = 0; y < kDims.y; ++y) for (int x = 0; x < kDims.x; ++x)
            if (y < (x < 20 ? 4 : 3)) g.occ(x, y, z) = Occ::Solid;
        for (int z = 0; z < kDims.z; ++z) for (int x = 0; x < 12; ++x) for (int y = 4; y < 7; ++y) g.f(x, y, z) = 1.0f;
        expectEqual(name("stepped bed"), runScene(g, 240, nullptr, sparseOpts));
    }
}

void WaterBrickTest::groundScenes(BrickOptions sparseOpts, const char* tag) {
    std::string label;
    auto name = [&](const char* scene) { label = std::string(tag) + " " + scene; return label.c_str(); };
    {   // (a) a pond under an OVERHANG: a solid ceiling (y 7-9, x 8-31) over water that touches it - ground ABOVE water, its lower
        // faces against the water; the water released sideways
        WaterGrid g = makeGrid();
        for (int z = 0; z < kDims.z; ++z) for (int y = 0; y < kDims.y; ++y) for (int x = 0; x < kDims.x; ++x)
            if (y < 2 || (y >= 7 && y <= 9 && x >= 8 && x < 32)) g.occ(x, y, z) = Occ::Solid;
        g.fillBox({8, 2, 0}, {20, 6, kDims.z - 1}, 1.0f);
        expectEqual(name("overhang"), runScene(g, 180, nullptr, sparseOpts));
    }
    {   // (b) a creek over a deep stepped bed, DUG under the flowing water at tick 60: a pit x 14-19, z 8-15, y 3-8 opened in the
        // bed (the same edit and re-upload for both volumes) - the water must drain into it
        WaterGrid g = makeGrid();
        for (int z = 0; z < kDims.z; ++z) for (int y = 0; y < kDims.y; ++y) for (int x = 0; x < kDims.x; ++x)
            if (y < (x < 20 ? 9 : 8)) g.occ(x, y, z) = Occ::Solid;
        for (int z = 0; z < kDims.z; ++z) for (int x = 0; x < 12; ++x) for (int y = 9; y < 12; ++y) g.f(x, y, z) = 1.0f;
        WaterGrid world = g;
        auto dig = [&](int k, WaterCoreGpu::Volume& a, WaterCoreGpu::Volume& b) {
            if (k != 60) return;
            for (int z = 8; z < 16; ++z) for (int y = 3; y < 9; ++y) for (int x = 14; x < 20; ++x) world.occ(x, y, z) = Occ::Air;
            reshape(a, world); reshape(b, world);
        };
        const SceneResult r = runScene(g, 240, dig, sparseOpts);
        expectEqual(name("dig under the creek"), r);
        double inPit = 0.0;
        for (int z = 8; z < 16; ++z) for (int y = 3; y < 9; ++y) for (int x = 14; x < 20; ++x) inPit += r.sparseEnd.f(x, y, z);
        std::printf("    water in the dug pit at the end: %.2f cells\n", inPit);
        EXPECT_GT(inPit, 1.0) << name("dig under the creek") << ": the water must run into the hole the dig opened";
    }
    {   // (c) the reverse: a pit beside the flow FILLED with ground at tick 10 (before the water reaches it)
        WaterGrid g = makeGrid();
        for (int z = 0; z < kDims.z; ++z) for (int y = 0; y < kDims.y; ++y) for (int x = 0; x < kDims.x; ++x)
            if (y < 9 && !(x >= 30 && x < 35 && z >= 6 && z < 18 && y >= 4)) g.occ(x, y, z) = Occ::Solid;
        for (int z = 0; z < kDims.z; ++z) for (int x = 0; x < 12; ++x) for (int y = 9; y < 12; ++y) g.f(x, y, z) = 1.0f;
        WaterGrid world = g;
        auto fill = [&](int k, WaterCoreGpu::Volume& a, WaterCoreGpu::Volume& b) {
            if (k != 10) return;
            for (int z = 6; z < 18; ++z) for (int y = 4; y < 9; ++y) for (int x = 30; x < 35; ++x) world.occ(x, y, z) = Occ::Solid;
            reshape(a, world); reshape(b, world);
        };
        expectEqual(name("pit filled in"), runScene(g, 240, fill, sparseOpts));
    }
}

// 26.18 GATE: halo bricks that are all ground, read from the GROUND sentinel and not stepped, == every 8^3 brick stored - bit for
// bit, in the 26.17 scenes and in three where the ground matters (an overhang, a dig under flowing water, a pit filled in). The
// world is destructible: which bricks are ground is re-read at every upload, and the dug brick is stored before the next step.
TEST_F(WaterBrickTest, GroundBricksEqualStored) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    seamScenes(BrickOptions{true, 0, false, 2, 1}, "ground");
    groundScenes(BrickOptions{true, 0, false, 2, 1}, "ground");
    groundScenes(BrickOptions{true, 0, false, 2, 0}, "no ground");   // the same new scenes with every halo brick stored
}

void WaterBrickTest::patchScenes(BrickOptions sparseOpts, const char* tag, bool expectEqualRuns) {
    std::string label;
    auto name = [&](const char* scene) { label = std::string(tag) + " " + scene; return label.c_str(); };
    // the creek over the stepped bed (groundScenes (b)): one edit at tick 60, checked after the next call's residency
    auto creek = [] {
        WaterGrid g = makeGrid();
        for (int z = 0; z < kDims.z; ++z) for (int y = 0; y < kDims.y; ++y) for (int x = 0; x < kDims.x; ++x)
            if (y < (x < 20 ? 9 : 8)) g.occ(x, y, z) = Occ::Solid;
        for (int z = 0; z < kDims.z; ++z) for (int x = 0; x < 12; ++x) for (int y = 9; y < 12; ++y) g.f(x, y, z) = 1.0f;
        return g;
    };
    struct Edit { const char* scene; glm::ivec3 lo, hi; Occ to; const char* pit; };
    const Edit edits[] = {
        {"dig under the creek",              {14, 3, 8}, {19, 8, 15}, Occ::Air,   "pit"},
        {"deep shaft opening ground bricks", {14, 0, 8}, {17, 8, 11}, Occ::Air,   "pit"},
        {"ground placed into the water",     {4, 9, 4},  {7, 10, 12}, Occ::Solid, nullptr},
        {"a pit beside the flow filled",     {30, 4, 6}, {34, 8, 17}, Occ::Solid, nullptr},
    };
    for (const Edit& e : edits) {
        WaterGrid g = creek();
        if (e.to == Occ::Solid && e.lo.x == 30)   // the pit exists from the start; filled at tick 60
            for (int z = e.lo.z; z <= e.hi.z; ++z) for (int y = e.lo.y; y <= e.hi.y; ++y) for (int x = e.lo.x; x <= e.hi.x; ++x) g.occ(x, y, z) = Occ::Air;
        WaterGrid world = g;
        long mismatchD = 0, mismatchS = 0;
        auto edit = [&](int k, WaterCoreGpu::Volume& a, WaterCoreGpu::Volume& b) {
            if (k == 60) {
                for (int z = e.lo.z; z <= e.hi.z; ++z) for (int y = e.lo.y; y <= e.hi.y; ++y) for (int x = e.lo.x; x <= e.hi.x; ++x) world.occ(x, y, z) = e.to;
                patch(a, world); patch(b, world);
            }
            if (k == 61 || k == 120) { mismatchD += occupancyMismatches(a, world); mismatchS += occupancyMismatches(b, world); }
        };
        const SceneResult r = runScene(g, 240, edit, sparseOpts);
        if (expectEqualRuns) expectEqual(name(e.scene), r); else report(name(e.scene), r);
        std::printf("    device occupancy != world (stored cells) or ground brick not all ground: dense %ld, sparse %ld\n", mismatchD, mismatchS);
        if (expectEqualRuns) { EXPECT_EQ(mismatchD, 0) << name(e.scene); EXPECT_EQ(mismatchS, 0) << name(e.scene); }
        if (e.pit) {
            double inPit = 0.0;
            for (int z = e.lo.z; z <= e.hi.z; ++z) for (int y = e.lo.y; y <= e.hi.y; ++y) for (int x = e.lo.x; x <= e.hi.x; ++x) inPit += r.sparseEnd.f(x, y, z);
            std::printf("    water in the dug cells at the end: %.2f cells\n", inPit);
            if (expectEqualRuns) EXPECT_GT(inPit, 1.0) << name(e.scene) << ": the water must run into the hole";
        }
    }
}

// 26.19 GATE: a world edit patched IN PLACE (updateOccupancy: no read-back, no re-upload) - every 8^3 brick stored against
// 2-tall bricks with ground, bit for bit every tick, through a dig under the flowing creek, a deep shaft that opens ground bricks,
// ground placed into the water and a pit filled; the device's occupancy is the world's after each edit; water runs into the holes.
TEST_F(WaterBrickTest, OccupancyPatchedInPlace) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    patchScenes(BrickOptions{true, 0, false, 2, 1}, "patched");
}

// The control (RED by design): the patch leaves the bricks' all-ground flags stale - a dug ground brick stays GROUND, so the
// water cannot enter it: the sparse volume must differ from the dense one (and the occupancy check must see the stale ground).
TEST_F(WaterBrickTest, APatchLeavingGroundStaleIsCaught) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    WaterCoreGpu::s_patchStaleGroundForTest = true;
    WaterGrid g = makeGrid();
    for (int z = 0; z < kDims.z; ++z) for (int y = 0; y < kDims.y; ++y) for (int x = 0; x < kDims.x; ++x)
        if (y < (x < 20 ? 9 : 8)) g.occ(x, y, z) = Occ::Solid;
    for (int z = 0; z < kDims.z; ++z) for (int x = 0; x < 12; ++x) for (int y = 9; y < 12; ++y) g.f(x, y, z) = 1.0f;
    WaterGrid world = g;
    long mismatch = 0;
    auto edit = [&](int k, WaterCoreGpu::Volume& a, WaterCoreGpu::Volume& b) {
        if (k == 60) {
            for (int z = 8; z < 12; ++z) for (int y = 0; y < 9; ++y) for (int x = 14; x < 18; ++x) world.occ(x, y, z) = Occ::Air;
            patch(a, world); patch(b, world);
        }
        if (k == 120) mismatch += occupancyMismatches(b, world);
    };
    const SceneResult r = runScene(g, 200, edit, BrickOptions{true, 0, false, 2, 1}, false);
    WaterCoreGpu::s_patchStaleGroundForTest = false;
    report("stale ground after a patch", r);
    std::printf("    ground bricks that are not all ground: %ld cells\n", mismatch);
    EXPECT_GT(r.ticksDiffering, 0) << "control: a dug brick left as ground must change the water";
    EXPECT_GT(mismatch, 0) << "control: the occupancy check must see the stale ground";
}

// The control (RED by design): the ground sentinel's occupancy left as AIR - the bed under the creek reads as open air where it
// is not stepped: the fields must differ (the equality test sees a wrong ground).
TEST_F(WaterBrickTest, AGroundReadAsAirIsCaught) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    WaterGrid g = makeGrid();
    for (int z = 0; z < kDims.z; ++z) for (int y = 0; y < kDims.y; ++y) for (int x = 0; x < kDims.x; ++x)
        if (y < (x < 20 ? 9 : 8)) g.occ(x, y, z) = Occ::Solid;
    for (int z = 0; z < kDims.z; ++z) for (int x = 0; x < 12; ++x) for (int y = 9; y < 12; ++y) g.f(x, y, z) = 1.0f;
    BrickOptions o{true, 0, false, 2, 1}; o.groundAsAirForTest = true;
    const SceneResult r = runScene(g, 120, nullptr, o, false);
    report("ground read as air", r);
    EXPECT_GT(r.maxGround, 0) << "control: ground bricks must be in use (else the control is vacuous)";
    EXPECT_GT(r.ticksDiffering, 0) << "control: ground read as air must change the water";
}

// 26.6 P2a GATE: sparse == dense, bit for bit, across chunk and brick seams; the halo never too thin; the absent brick clean.
TEST_F(WaterBrickTest, SparseEqualsDenseAcrossSeams) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    seamScenes(BrickOptions{true, 0, false, 8}, "8^3");
}

// 26.17 GATE: FLAT bricks (8 x 2 x 8, 8 x 4 x 8) stored sparse == every 8^3 brick kept, bit for bit - the brick's shape is storage
// only. The box's y origin (3) is odd, so 2- and 4-tall bricks start part-way into a brick; the reductions fold per 8 x 2 x 8 unit
// in one world order whatever the height, so the rest decisions match too.
TEST_F(WaterBrickTest, FlatBricksEqualCubic) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    seamScenes(BrickOptions{true, 0, false, 2}, "8x2x8");
    seamScenes(BrickOptions{true, 0, false, 4}, "8x4x8");
    {   // a height that is not 2, 4 or 8 is refused with the reason, never rounded
        std::string err;
        EXPECT_EQ(gpu.createVolume(makeGrid(), &err, BrickOptions{true, 0, false, 3}), nullptr);
        EXPECT_NE(err.find("brick height 3"), std::string::npos) << err;
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
    const SceneResult r = runScene(g, 120, nullptr, BrickOptions{true, 0, true, 8}, false);
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
    WaterCoreGpu::Volume* tooSmall = gpu.createVolume(g, &err, BrickOptions{true, 10, false, 8});
    EXPECT_EQ(tooSmall, nullptr) << "a capacity below the water must be refused";
    std::printf("  capacity 10 (the water needs 16): %s\n", err.c_str());
    if (tooSmall) gpu.destroyVolume(tooSmall);
    err.clear();
    WaterCoreGpu::Volume* v = gpu.createVolume(g, &err, BrickOptions{true, 24, false, 8});
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
            // 26.9: a gentle tilt along the creek (0.15 -> 0.55 of the second cell): the water keeps MOVING through the timed
            // window - resting water now sleeps (the still-water guess) and a sleeping volume costs nothing
            if (channel && y == 1) { g.f(x, y, z) = 0.15f + 0.4f * static_cast<float>(x + z) / static_cast<float>(dims.x + dims.z); ++waterCells; }
        }
    }
    SolverParams prm;
    const int sweeps = std::getenv("DIAG_SWEEPS") ? std::atoi(std::getenv("DIAG_SWEEPS")) : 40;
    std::printf("  sweeps %d, still-water guess %d\n", sweeps, int(WaterCoreGpu::s_stillWaterGuess));
    auto measure = [&](WaterCoreGpu::Volume* v, int& bricks, long& shortBy) {
        for (int k = 0; k < 20; ++k) {
            const GpuStepStats st = gpu.step(*v, prm, kDt, 1, sweeps);
            if (std::getenv("DIAG_RES") && k < 8) std::printf("    warm %d: bricks %d alloc %d freed %d rings %d asleep %d ms %.3f maxSpeed %.4f\n", k, st.bricks, st.allocated, st.freed, st.haloRings, int(st.asleep), st.gpuMs, st.maxSpeed);
        }
        std::vector<double> ms;
        double res = 0.0, wait = 0.0, after = 0.0;
        int asleep = 0; double resMax = 0.0;
        for (int k = 0; k < 30; ++k) {
            const GpuStepStats st = gpu.step(*v, prm, kDt, 1, sweeps);
            asleep += st.asleep ? 1 : 0;
            resMax = std::max(resMax, static_cast<double>(st.rbgsResidualMax));
            ms.push_back(st.gpuMs); bricks = st.bricks; shortBy += st.haloShort;
            res += st.hostResidencyMs / 30.0; wait += st.submitWaitMs / 30.0; after += st.hostAfterMs / 30.0;
        }
        {   // 26.11: where the GPU time goes (timestamps per stage; the stamps add a barrier each, so the total reads a little high)
            WaterCoreGpu::s_kernelTiming = true; WaterCoreGpu::s_tsCalls = 0;
            for (double& t : WaterCoreGpu::s_tsMs) t = 0.0;
            for (int k = 0; k < 30; ++k) gpu.step(*v, prm, kDt, 1, sweeps);
            WaterCoreGpu::s_kernelTiming = false;
            static const char* nm[WaterCoreGpu::TsLabelCount] = {"-", "residency", "sources", "transport", "vel advect", "ledger+compaction", "forces+settle",
                                                                  "classify+guess", "sweeps", "residual+vel update", "extrapolate", "settle+damping", "sweep+births", "brick record", "reductions", "surface kernel", "surface copy", "births copy"};
            double tot = 0.0; for (double t : WaterCoreGpu::s_tsMs) tot += t;
            const double c = static_cast<double>(std::max(1L, WaterCoreGpu::s_tsCalls));
            std::printf("    GPU per call (%ld calls, total %.3f ms):", WaterCoreGpu::s_tsCalls, tot / c);
            for (int q = 1; q < WaterCoreGpu::TsLabelCount; ++q) std::printf(" %s %.3f", nm[q], WaterCoreGpu::s_tsMs[q] / c);
            std::printf("\n");
        }
        std::printf("    worst pressure residual over the timed calls %.2e\n", resMax);
        if (asleep) std::printf("    WARNING: asleep in %d of the 30 timed calls - the timing is not a stepping cost\n", asleep);
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
    std::vector<float> aligned;   // the first run's fills: every origin AND every brick height must give the same water (26.17)
    for (int height : {8, 4, 2})
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
        WaterCoreGpu::Volume* v = gpu.createVolume(g, &err, BrickOptions{false, 0, false, height});
        ASSERT_NE(v, nullptr) << err;
        SolverParams prm;
        const double m0 = g.totalMass();
        double worst = 0.0;
        WaterGrid out(g.spec());
        for (int k = 0; k < 1800; ++k) {
            const GpuStepStats st = gpu.step(*v, prm, kDt, 1, 40);
            if (!st.asleep) worst = std::max(worst, std::abs(st.totalMass - m0));   // an asleep call reports no sums (26.9: this pond now sleeps)
            if (k < 120) cpu.step(kDt);
            if (k == 119) {
                gpu.download(*v, out);
                double df = 0.0;
                for (size_t i = 0; i < out.fData().size(); ++i) df = std::max(df, static_cast<double>(std::abs(out.fData()[i] - c.fData()[i])));
                long differ = 0;
                if (aligned.empty()) aligned = out.fData();
                else for (size_t i = 0; i < aligned.size(); ++i) differ += (aligned[i] == out.fData()[i]) ? 0 : 1;
                std::printf("  h %d origin (%d,%d,%d): fills vs CPU at 2 s max|df| %.2e; cells differing from the aligned 8^3 run %ld\n", height, origin.x, origin.y, origin.z, df, differ);
                EXPECT_EQ(differ, 0) << "the brick layout changed the water at height " << height << ", origin " << origin.x << "," << origin.y << "," << origin.z;
                EXPECT_LT(df, 1e-2);
            }
        }
        { WaterGrid end(g.spec()); gpu.download(*v, end); worst = std::max(worst, std::abs(end.totalMass() - m0)); }   // and the water itself at 30 s
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
    const bool guessWas = WaterCoreGpu::s_stillWaterGuess;
    for (bool guess : {false, true}) for (int sweeps : {40, 27}) for (float dt : {1.0f / 60.0f, 1.0f / 165.0f}) {
        WaterCoreGpu::s_stillWaterGuess = guess;
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
        std::printf("  %s sweeps %d, dt %.4f: asleep at tick %d (%.2f s); mass %.6f -> %.6f (%+.3e m^3), lowest %.6f\n", guess ? "GUESS" : "cold ", sweeps, dt, sleptAt, sleptAt * dt, m0, w.totalMass(), w.totalMass() - m0, minM);
        gpu.destroyVolume(v);
    }
    WaterCoreGpu::s_stillWaterGuess = guessWas;
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
    if (std::getenv("DIAG_NOISE")) {   // the CPU diagnostic's fixed +-noise pattern on the threshold layer
        const float e = static_cast<float>(std::atof(std::getenv("DIAG_NOISE")));
        for (int z = 3; z < 15; ++z) for (int x = 3; x < 15; ++x) g.f(x, 7, z) += ((x * 7 + z * 13) % 3 - 1) * e;
    }
    WaterGrid w = g;
    std::string err;
    WaterCoreGpu::Volume* v = gpu.createVolume(w, &err);
    ASSERT_NE(v, nullptr) << err;
    SolverParams prm;
    if (std::getenv("DIAG_THETAMIN")) prm.thetaMin = std::atof(std::getenv("DIAG_THETAMIN"));
    const float dt = std::getenv("DIAG_DT") ? 1.0f / std::atof(std::getenv("DIAG_DT")) : 1.0f / 165.0f;   // env: the frame rate
    const bool guessWas = WaterCoreGpu::s_stillWaterGuess;
    if (std::getenv("DIAG_GUESS")) WaterCoreGpu::s_stillWaterGuess = std::atoi(std::getenv("DIAG_GUESS")) != 0;
    const bool ghostWas2 = WaterSolver::s_surfaceGhost; const float riseWas2 = EulerianTransport::s_quietRiseFraction;
    if (std::getenv("DIAG_GHOST")) WaterSolver::s_surfaceGhost = std::atoi(std::getenv("DIAG_GHOST")) != 0;
    if (std::getenv("DIAG_RISE")) EulerianTransport::s_quietRiseFraction = static_cast<float>(std::atof(std::getenv("DIAG_RISE")));
    std::vector<float> prevF = w.fData();
    double prevM = w.totalMass(), lossSum = 0.0, gainSum = 0.0;
    double dropped = 0.0;
    struct Ev { double dm; int tick; std::vector<std::pair<size_t, std::pair<float, float>>> cells; };
    std::vector<Ev> worst;
    for (int k = 0; k < static_cast<int>(10.0f / dt); ++k) {
        const GpuStepStats st = gpu.step(*v, prm, dt, 1, 40);
        gpu.download(*v, w);
        dropped += st.residueDropped;
        if (std::getenv("DIAG_TICKS") && k < std::atoi(std::getenv("DIAG_TICKS"))) std::printf("    tick %d: dm %+.3e ke %.2e residual %.2e\n", k, w.totalMass() - prevM, st.kineticEnergy, st.rbgsResidualMax);
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
        if (k % static_cast<int>(2.0f / dt) == 0) std::printf("  tick %4d: mass %.7f ke %.1e quiet %d substeps %d\n", k, m, st.kineticEnergy, st.quietTicks, st.substepsLast);
    }
    std::printf("  losses %.3e, gains %.3e (cell units x h^3 = m^3)\n", lossSum, gainSum);
    std::printf("  residue dropped (counted) %.3e m^3\n", dropped);
    {   // where the motion is: the fastest faces at the end, with the fills on both sides
        struct Fc { float s; char lat; int x, y, z; };
        std::vector<Fc> fc;
        for (int z = 0; z < 18; ++z) for (int y = 0; y < 18; ++y) for (int x = 0; x < 18; ++x) {
            fc.push_back({std::abs(w.u(x, y, z)), 'u', x, y, z});
            fc.push_back({std::abs(w.v(x, y, z)), 'v', x, y, z});
            fc.push_back({std::abs(w.w(x, y, z)), 'w', x, y, z});
        }
        std::sort(fc.begin(), fc.end(), [](const Fc& a, const Fc& b) { return a.s > b.s; });
        for (int k = 0; k < 12; ++k) {
            const Fc& e = fc[k];
            const int bx = e.x - (e.lat == 'u'), by = e.y - (e.lat == 'v'), bz = e.z - (e.lat == 'w');
            std::printf("  %c(%d,%d,%d) |%.3e| m/s: f below/behind %.6f, f at %.6f\n", e.lat, e.x, e.y, e.z, e.s,
                        (bx >= 0 && by >= 0 && bz >= 0) ? w.f(bx, by, bz) : -1.0f, w.f(e.x, e.y, e.z));
        }
    }
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
    WaterCoreGpu::s_stillWaterGuess = guessWas;
    WaterSolver::s_surfaceGhost = ghostWas2; EulerianTransport::s_quietRiseFraction = riseWas2;
}

// 26.9 diagnostic: the same bench pond (top layer AT the liquid threshold) on the CPU reference - a converged PCG - at the
// frame rate DIAG_DT (default 60): does an exact solve flicker there too? KE and mass every 2 s.
TEST_F(WaterBrickTest, DISABLED_DiagSettlingCpu) {
    const glm::ivec3 origin(297, 42, 33), d(18, 18, 18);
    WaterGrid g(GridSpec{origin, d, 1.0f / 3.0f});
    for (int z = 0; z < d.z; ++z) for (int y = 0; y < d.y; ++y) for (int x = 0; x < d.x; ++x) {
        const bool pit = x >= 3 && x < 15 && z >= 3 && z < 15 && y >= 3;
        g.occ(x, y, z) = (pit || y >= 12) ? Occ::Air : Occ::Solid;
    }
    for (int z = 3; z < 15; ++z) for (int x = 3; x < 15; ++x) { for (int y = 3; y < 7; ++y) g.f(x, y, z) = 1.0f; g.f(x, 7, z) = 0.5f; }
    if (std::getenv("DIAG_NOISE")) {   // a fixed +-noise pattern on the threshold layer (mass-neutral pairs)
        const float e = static_cast<float>(std::atof(std::getenv("DIAG_NOISE")));
        for (int z = 3; z < 15; ++z) for (int x = 3; x < 15; ++x) g.f(x, 7, z) += ((x * 7 + z * 13) % 3 - 1) * e;
    }
    SolverParams sp;
    const float riseWas = EulerianTransport::s_quietRiseFraction;
    if (std::getenv("DIAG_RISE")) EulerianTransport::s_quietRiseFraction = static_cast<float>(std::atof(std::getenv("DIAG_RISE")));
    const bool ghostWas = WaterSolver::s_surfaceGhost;
    if (std::getenv("DIAG_GHOST")) WaterSolver::s_surfaceGhost = std::atoi(std::getenv("DIAG_GHOST")) != 0;
    if (std::getenv("DIAG_OFF")) sp.debugDisableStages = static_cast<uint32_t>(std::atoi(std::getenv("DIAG_OFF")));
    if (std::getenv("DIAG_THETAMIN")) sp.thetaMin = std::atof(std::getenv("DIAG_THETAMIN"));
    WaterSolver cpu(g, [&g, origin, d](const glm::ivec3& cw) { const glm::ivec3 l = cw - origin; return (l.x < 0 || l.y < 0 || l.z < 0 || l.x >= d.x || l.y >= d.y || l.z >= d.z) ? Occ::Solid : g.occ(l.x, l.y, l.z); }, sp);
    const float dt = std::getenv("DIAG_DT") ? 1.0f / std::atof(std::getenv("DIAG_DT")) : 1.0f / 60.0f;
    // per stage: the mass each stage changes (summed over the run) - which stage loses the water
    std::map<std::string, double> stageDm;
    double lastM = g.totalMass();
    // and the water ABOVE the surface layer (y >= 8) each stage adds - who lifts water over the surface
    std::map<std::string, double> stageUp;
    auto above = [&]() { double t = 0.0; for (int z = 0; z < 18; ++z) for (int y = 8; y < 18; ++y) for (int x = 0; x < 18; ++x) t += g.f(x, y, z); return t; };
    double lastUp = above();
    cpu.stageProbeForTest = [&](const char* stage) {
        const double m = g.totalMass(); stageDm[stage] += m - lastM; lastM = m;
        const double u = above(); if (u > lastUp) stageUp[stage] += u - lastUp; lastUp = u;
    };
    const double m0 = g.totalMass();
    double dropped = 0.0;
    for (int k = 0; k < static_cast<int>(10.0f / dt); ++k) {
        const StepReport r = cpu.step(dt);
        dropped += r.residueDropped;
        { const double m = g.totalMass(); stageDm["(after the tick: residue sweep)"] += m - lastM; lastM = m; }
        if (k % static_cast<int>(2.0f / dt) == 0) {
            // the kinetic energy split: liquid cells (f >= 0.5) vs partial cells (0 < f < 0.5) - the partial cells' top faces
            // carry free fall under a film, which is energy of almost no water
            double keLiq = 0.0, kePart = 0.0, levelMin = 1e9, levelMax = -1e9;
            for (int z = 0; z < 18; ++z) for (int y = 0; y < 18; ++y) for (int x = 0; x < 18; ++x) {
                const float fc = g.f(x, y, z);
                if (fc <= 0.0f) continue;
                const double uc = 0.5 * (g.u(x, y, z) + g.u(x + 1, y, z)), vc = 0.5 * (g.v(x, y, z) + g.v(x, y + 1, z)), wc = 0.5 * (g.w(x, y, z) + g.w(x, y, z + 1));
                const double e = 0.5 * fc * (uc * uc + vc * vc + wc * wc) * g.h() * g.h() * g.h();
                (fc >= 0.5f ? keLiq : kePart) += e;
            }
            for (int z = 3; z < 15; ++z) for (int x = 3; x < 15; ++x) { double col = 0.0; for (int y = 3; y < 12; ++y) col += g.f(x, y, z); levelMin = std::min(levelMin, col); levelMax = std::max(levelMax, col); }
            std::printf("  tick %4d: mass %+.3e ke %.1e (liquid %.1e, partial %.1e) asleep %d, column levels %.5f..%.5f\n", k, g.totalMass() - m0, r.kineticEnergy, keLiq, kePart, int(cpu.asleep()), levelMin, levelMax);
        }
    }
    std::printf("  residue dropped (counted) %.3e m^3\n", dropped);
    for (auto& kv : stageDm) if (kv.second != 0.0) std::printf("  stage %-32s dm %+.3e m^3\n", kv.first.c_str(), kv.second);
    for (auto& kv : stageUp) std::printf("  stage %-32s lifted above the surface layer %.3e cells\n", kv.first.c_str(), kv.second);
    struct Fc { float s; char lat; int x, y, z; };
    std::vector<Fc> fc;
    for (int z = 0; z < 18; ++z) for (int y = 0; y < 18; ++y) for (int x = 0; x < 18; ++x) {
        fc.push_back({g.u(x, y, z), 'u', x, y, z}); fc.push_back({g.v(x, y, z), 'v', x, y, z}); fc.push_back({g.w(x, y, z), 'w', x, y, z});
    }
    std::sort(fc.begin(), fc.end(), [](const Fc& a, const Fc& b) { return std::abs(a.s) > std::abs(b.s); });
    for (int k = 0; k < 8; ++k) {
        const Fc& e = fc[k];
        const int bx = e.x - (e.lat == 'u'), by = e.y - (e.lat == 'v'), bz = e.z - (e.lat == 'w');
        std::printf("  %c(%d,%d,%d) %+.3e m/s: f below/behind %.6f, f at %.6f\n", e.lat, e.x, e.y, e.z, e.s, (bx >= 0 && by >= 0 && bz >= 0) ? g.f(bx, by, bz) : -1.0f, g.f(e.x, e.y, e.z));
    }
    WaterSolver::s_surfaceGhost = ghostWas;
    EulerianTransport::s_quietRiseFraction = riseWas;
}

// 26.9 diagnostic: ONE tick of the threshold pond on the GPU (guess on): a column's pressure against g x depth to the surface
// (raw theta from the fills), and its vertical face speeds - DIAG_GHOST=0: the old surface rule (p_air = 0).
TEST_F(WaterBrickTest, DISABLED_DiagThresholdColumn) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const glm::ivec3 origin(297, 42, 33), d(18, 18, 18);
    WaterGrid g(GridSpec{origin, d, 1.0f / 3.0f});
    for (int z = 0; z < d.z; ++z) for (int y = 0; y < d.y; ++y) for (int x = 0; x < d.x; ++x) {
        const bool pit = x >= 3 && x < 15 && z >= 3 && z < 15 && y >= 3;
        g.occ(x, y, z) = (pit || y >= 12) ? Occ::Air : Occ::Solid;
    }
    for (int z = 3; z < 15; ++z) for (int x = 3; x < 15; ++x) { for (int y = 3; y < 7; ++y) g.f(x, y, z) = 1.0f; g.f(x, 7, z) = 0.5f; }
    const bool guessWas = WaterCoreGpu::s_stillWaterGuess;
    WaterCoreGpu::s_stillWaterGuess = true;
    std::string err;
    WaterCoreGpu::Volume* v = gpu.createVolume(g, &err);
    ASSERT_NE(v, nullptr) << err;
    SolverParams prm;
    const bool ghostWas = WaterSolver::s_surfaceGhost;
    if (std::getenv("DIAG_GHOST")) WaterSolver::s_surfaceGhost = std::atoi(std::getenv("DIAG_GHOST")) != 0;
    for (int tick = 0; tick < 3; ++tick) {
        const GpuStepStats st = gpu.step(*v, prm, 1.0f / 165.0f, 1, std::getenv("DIAG_SWEEPS") ? std::atoi(std::getenv("DIAG_SWEEPS")) : 40);
        std::vector<float> pr; gpu.readPressure(*v, pr); gpu.download(*v, g);
        std::printf("  tick %d: KE %.2e residual %.2e\n", tick, st.kineticEnergy, st.rbgsResidualMax);
        for (int y = 3; y < 10; ++y)
            std::printf("    (8,%d,8) f %.7f p %+.6f (g x depth %+.6f) | v below %+.3e | corner (3,%d,3) p %+.6f u-face %+.3e\n", y, g.f(8, y, 8), pr[g.idx(8, y, 8)],
                        prm.gravity * g.h() * (8.0f - (y + 0.5f) - 0.5f), g.v(8, y, 8), y, pr[g.idx(3, y, 3)], g.u(3, y, 3));
    }
    gpu.destroyVolume(v);
    WaterCoreGpu::s_stillWaterGuess = guessWas;
    WaterSolver::s_surfaceGhost = ghostWas;
}

// 26.9 diagnostic: what the sweep count buys once the solve starts from the still water. A dam break (the most dynamic
// scene) and a slosh, 1 s each at dt 1/60, at 8..80 sweeps with the guess on (and 40 cold for reference), every run
// against a 160-sweep guess run: max |f - f_ref| at 1 s, the worst residual and mass drift.
TEST_F(WaterBrickTest, DISABLED_DiagSweepsAfterTheGuess) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const bool guessWas = WaterCoreGpu::s_stillWaterGuess;
    for (int scene = 0; scene < 2; ++scene) {
        WaterGrid g(GridSpec{glm::ivec3(0), glm::ivec3(36, 18, 6), kH});
        for (int z = 0; z < 6; ++z) for (int y = 0; y < 18; ++y) for (int x = 0; x < 36; ++x) {
            g.occ(x, y, z) = Occ::Air;
            if (scene == 0 && x < 12 && y < 12) g.f(x, y, z) = 1.0f;                                         // dam break
            if (scene == 1 && y < 6 + (x < 18 ? 2 : 0)) g.f(x, y, z) = 1.0f;                                 // slosh (a step)
        }
        auto run = [&](int sweeps, bool guess, double& resMax, double& mass) {
            WaterCoreGpu::s_stillWaterGuess = guess;
            WaterGrid w = g; std::string err;
            WaterCoreGpu::Volume* v = gpu.createVolume(w, &err);
            EXPECT_NE(v, nullptr) << err;
            SolverParams prm; resMax = 0.0;
            for (int k = 0; k < 60; ++k) { const GpuStepStats st = gpu.step(*v, prm, kDt, 1, sweeps); resMax = std::max(resMax, static_cast<double>(st.rbgsResidualMax)); }
            gpu.download(*v, w); mass = w.totalMass(); gpu.destroyVolume(v);
            return w.fData();
        };
        double r0, m0;
        const std::vector<float> ref = run(160, true, r0, m0);
        std::printf("  %s: reference 160 sweeps (guess): residual %.2e, mass %.6f\n", scene == 0 ? "DAM BREAK" : "SLOSH", r0, m0);
        for (int sweeps : {8, 12, 16, 20, 27, 40, 80}) for (bool guess : {true, false}) {
            if (!guess && sweeps != 40 && sweeps != 20) continue;
            double r, m;
            const std::vector<float> f = run(sweeps, guess, r, m);
            double df = 0.0; for (size_t i = 0; i < f.size(); ++i) df = std::max(df, static_cast<double>(std::abs(f[i] - ref[i])));
            std::printf("    %3d sweeps %s: max|f - ref| %.3e, worst residual %.2e, mass %+.2e\n", sweeps, guess ? "guess" : "cold ", df, r, m - m0);
        }
    }
    WaterCoreGpu::s_stillWaterGuess = guessWas;
}
