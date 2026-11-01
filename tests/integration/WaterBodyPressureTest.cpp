// WaterCore 31 (M4) - the water's level round a wet body, as the volume's call writes it for the debris solver (docs/WaterCore.md
// 31.7): wc_body_pressure.comp's map against the host's surface field of the same call, on a dam break (a sloped, moving surface -
// a flat pond cannot tell a window one column off). Red control: every window one column off in x (WaterCoreGpu::s_pwShiftForTest).
// And the chunk rule's shape (FloraMarginTest): the same water in a volume whose box straddles more of the world gives the same map.
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"
#include "core/water/WaterCoreGpu.h"
#include "core/water/WaterCoreManager.h"
#include "core/water/WaterSurfaceMesh.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Phyxel::Core::Water;
using Phyxel::Testing::VulkanTestFixture;

namespace {
class WaterBodyPressureTest : public VulkanTestFixture {};

// the host's version of the kernel's per-column rule (wc_body_pressure.comp columnLevel)
float hostLevel(const SurfaceColumn& c, float bodyTop) {
    if (c.runs < 0.0f) return c.top[0] > c.bottom[0] ? c.top[0] : NAN;
    const int n = std::min(static_cast<int>(c.runs + 0.5f), 4);
    float best = NAN;
    for (int r = 0; r < n; ++r)
        if (c.top[r] > c.bottom[r] && c.bottom[r] < bodyTop && (std::isnan(best) || c.top[r] > best)) best = c.top[r];
    return best;
}

struct MapCheck { long cells = 0, ring = 0, off = 0, nanMismatch = 0; float worst = 0.0f; bool ok = false; };

// a dam break in a box (boxHiX voxels wide); after `ticks` steps, one window round a 1/3 m body at (bx, by, bz): compared to the host
MapCheck runOnce(VkDevice d, VkPhysicalDevice pd, VkQueue q, uint32_t qf, int boxHiX, int ticks, glm::vec3 body, std::vector<float>* mapOut, bool tallCells = true) {
    MapCheck out;
    auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
    WaterCoreManager mgr(state);
    std::string err;
    if (!mgr.initGpu(d, pd, q, qf, "shaders", &err)) { ADD_FAILURE() << err; return out; }
    mgr.setDroplets(false);
    const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(boxHiX, 2, 7), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
    if (!id) { ADD_FAILURE() << err; return out; }
    mgr.setAutoSleep(id, false);
    if (!tallCells) mgr.setTallBand(id, 0.0f);
    mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(2, 1, 7), 1.0f, nullptr);   // 3 m of 2 m deep water against dry ground: it runs
    if (!mgr.makePressureTargetsForTest(&err)) { ADD_FAILURE() << err; return out; }
    const glm::vec3 he(1.0f / 6.0f);
    AvRecord rec;
    for (int k = 0; k < ticks; ++k) {
        const auto st = mgr.setPressureWindows({{body, he, 0.0f, 7u}}, static_cast<uint32_t>(k + 1));
        if (st.windows != 1) { ADD_FAILURE() << "no window asked (outside " << st.outside << ", too big " << st.tooBig << ")"; return out; }
        mgr.step(id, 1, 1.0f / 60.0f, &rec);
    }
    int32_t o[4], dm[4]; float meta[4]; std::vector<float> map;
    if (!mgr.readPressureWindowForTest(0, o, dm, meta, map)) { ADD_FAILURE() << "window read failed"; return out; }
    const WaterSurfaceField* fld = nullptr;
    for (const WaterSurfaceField* f : mgr.surfaceFields()) if (f->source == id) fld = f;
    if (!fld) { ADD_FAILURE() << "no host field"; return out; }
    const float h = fld->h, bodyTop = body.y + glm::length(he), rFoot = glm::length(he);
    const glm::vec2 ctr = (glm::vec2(o[0], o[2]) + 0.5f * glm::vec2(dm[0], dm[2])) * h;
    for (int lz = 0; lz < dm[2]; ++lz)
        for (int lx = 0; lx < dm[0]; ++lx) {
            const glm::vec2 cc = (glm::vec2(o[0] + lx, o[2] + lz) + 0.5f) * h;
            if (glm::length(cc - ctr) <= rFoot) continue;   // the footprint: interpolated, checked through the ring it is made from
            const int x = o[0] + lx - fld->origin.x, z = o[2] + lz - fld->origin.z;
            if (x < 0 || z < 0 || x >= fld->nx || z >= fld->nz) continue;
            const float want = hostLevel(fld->at(x, z), bodyTop), got = map[lx + dm[0] * lz];
            ++out.ring;
            if (std::isnan(want) != std::isnan(got)) { ++out.nanMismatch; continue; }
            if (std::isnan(want)) continue;
            const float e = std::abs(want - got);
            out.worst = std::max(out.worst, e);
            if (e > 1e-5f) ++out.off;
        }
    out.cells = static_cast<long>(map.size());
    if (mapOut) *mapOut = map;
    out.ok = true;
    return out;
}
}  // namespace

// A5 diagnosis (31.9a): the dam break in the 12 m and the 17 m box with tall cells ON, tick by tick - the tall layer's state in each
// and the first tick their surfaces part (the 12 m box's columns compared)
TEST_F(WaterBodyPressureTest, DISABLED_DiagTallFloorPerTick) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    auto state = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
    struct Run { std::unique_ptr<WaterCoreManager> mgr; int id = 0; };
    std::vector<Run> runs;
    const char* wenv = std::getenv("A5_BOXES");
    std::vector<int> boxes = {11, 16};
    if (wenv) { boxes.clear(); int a = 0, b = 0; std::sscanf(wenv, "%d,%d", &a, &b); boxes = {a, b}; }
    for (int hiX : boxes) {
        Run r; r.mgr = std::make_unique<WaterCoreManager>(state);
        std::string err;
        ASSERT_TRUE(r.mgr->initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
        r.mgr->setDroplets(false);
        r.id = r.mgr->create(glm::ivec3(0, 0, 0), glm::ivec3(hiX, 2, 7), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        ASSERT_TRUE(r.id) << err;
        r.mgr->setAutoSleep(r.id, false);
        r.mgr->placeBox(glm::ivec3(0, 0, 0), glm::ivec3(2, 1, 7), 1.0f, nullptr);
        runs.push_back(std::move(r));
    }
    for (int k = 0; k < 40; ++k) {
        float worst = 0.0f; long diff = 0;
        const WaterSurfaceField* f[2] = {nullptr, nullptr};
        for (int i = 0; i < 2; ++i) {
            AvRecord rec; runs[i].mgr->step(runs[i].id, 1, 1.0f / 60.0f, &rec);
            for (const WaterSurfaceField* s : runs[i].mgr->surfaceFields()) if (s->source == runs[i].id) f[i] = s;
            const AvRecord* r = runs[i].mgr->find(runs[i].id);
            std::printf("  k %2d box %d: tall %d cols %d floor %.3f rest %.3f offs %d (%s) mass %.5f\n", k, i, int(r->tallActive), r->tallColumns,
                        r->tallFloorY, r->tallRestY, r->tallOffs, r->tallReason.c_str(), rec.mass);
        }
        if (f[0] && f[1] && f[0]->nz == f[1]->nz)
            for (int z = 0; z < f[0]->nz; ++z) for (int x = 0; x < f[0]->nx; ++x) {
                const SurfaceColumn &a = f[0]->at(x, z), &b = f[1]->at(x, z);
                const float ta = a.runs >= 1.0f ? a.top[0] : 0.0f, tb = b.runs >= 1.0f ? b.top[0] : 0.0f;
                if (std::abs(ta - tb) > 1e-5f) { ++diff; worst = std::max(worst, std::abs(ta - tb)); }
            }
        std::printf("  k %2d: %ld columns differ (worst %.4f m)\n", k, diff, worst);
    }
}

// A5 diagnosis 2: a walled pond at rest (x 0-4 m, a wall at voxel x 4 up to 3 m), a stone at tick 30; the same pond in a 12 m and a
// 17 m box (dry land past the wall) - with tall cells, the first tick their surfaces part
TEST_F(WaterBodyPressureTest, DISABLED_DiagTallFloorWalledPond) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    auto state = [](const glm::ivec3& m) -> int { return (m.y < 0 || (m.x >= 36 && m.x < 45 && m.y < 27)) ? 1 : 0; };
    const bool tallOff = std::getenv("A5_TALL_OFF") != nullptr;
    struct Run { std::unique_ptr<WaterCoreManager> mgr; int id = 0; };
    std::vector<Run> runs;
    for (int hiX : {11, 16}) {
        Run r; r.mgr = std::make_unique<WaterCoreManager>(state);
        std::string err;
        ASSERT_TRUE(r.mgr->initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
        r.mgr->setDroplets(false);
        r.id = r.mgr->create(glm::ivec3(0, 0, 0), glm::ivec3(hiX, 3, 7), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        ASSERT_TRUE(r.id) << err;
        r.mgr->setAutoSleep(r.id, false);
        if (tallOff) r.mgr->setTallBand(r.id, 0.0f);
        r.mgr->placeBox(glm::ivec3(0, 0, 0), glm::ivec3(3, 1, 7), 1.0f, nullptr);
        runs.push_back(std::move(r));
    }
    for (int k = 0; k < 120; ++k) {
        float worst = 0.0f; long diff = 0;
        const WaterSurfaceField* f[2] = {nullptr, nullptr};
        double mass[2] = {0, 0};
        for (int i = 0; i < 2; ++i) {
            if (k == 30) runs[i].mgr->addImpulse(glm::vec3(2.0f, 1.9f, 4.0f), 1.0f / 3.0f, 4.0f, glm::vec3(0.0f, -1.0f, 0.0f));
            AvRecord rec; runs[i].mgr->step(runs[i].id, 1, 1.0f / 60.0f, &rec);
            mass[i] = rec.mass;
            for (const WaterSurfaceField* s : runs[i].mgr->surfaceFields()) if (s->source == runs[i].id) f[i] = s;
            const AvRecord* r = runs[i].mgr->find(runs[i].id);
            if (k < 4 || (k >= 34 && k < 41) || k % 20 == 0)
                std::printf("  k %3d box %d: tall %d cols %d floor %.3f rest %.3f offs %d (%s) mass %.5f\n", k, i, int(r->tallActive), r->tallColumns,
                            r->tallFloorY, r->tallRestY, r->tallOffs, r->tallReason.c_str(), rec.mass);
        }
        if (f[0] && f[1] && f[0]->nz == f[1]->nz)
            for (int z = 0; z < f[0]->nz; ++z) for (int x = 0; x < 36; ++x) {
                const SurfaceColumn &a = f[0]->at(x, z), &b = f[1]->at(x, z);
                const float ta = a.runs >= 1.0f ? a.top[0] : 0.0f, tb = b.runs >= 1.0f ? b.top[0] : 0.0f;
                if (std::abs(ta - tb) > 1e-5f) { ++diff; worst = std::max(worst, std::abs(ta - tb)); }
            }
        if (diff && k < 41 && f[0] && f[1]) {
            int xlo = 1 << 30, xhi = -1; int shown = 0;
            for (int z = 0; z < f[0]->nz; ++z) for (int x = 0; x < 36; ++x) {
                const SurfaceColumn &a = f[0]->at(x, z), &b = f[1]->at(x, z);
                const float ta = a.runs >= 1.0f ? a.top[0] : 0.0f, tb = b.runs >= 1.0f ? b.top[0] : 0.0f;
                if (std::abs(ta - tb) > 1e-5f) { xlo = std::min(xlo, x); xhi = std::max(xhi, x);
                    if (shown++ < 6) std::printf("    col (%d,%d): runs %.0f/%.0f top %.4f/%.4f bottom %.4f/%.4f\n", x, z, a.runs, b.runs, a.top[0], b.top[0], a.bottom[0], b.bottom[0]); }
            }
            std::printf("    x range %d..%d\n", xlo, xhi);
            auto colW = [](const SurfaceColumn& c) { double s = 0; const int n = std::min(static_cast<int>(c.runs + 0.5f), 4); for (int r = 0; r < n; ++r) s += std::max(0.0f, c.top[r] - c.bottom[r]); return s; };
            int lost = 0, lx0 = 1 << 30, lx1 = -1, lz0 = 1 << 30, lz1 = -1; double lostM = 0, worstCol = 0; int wx = -1, wz = -1;
            for (int z = 0; z < f[0]->nz; ++z) for (int x = 0; x < f[0]->nx; ++x) {
                const double d = colW(f[0]->at(x, z)) - colW(f[1]->at(x, z));
                if (d > 0.02) { ++lost; lostM += d; lx0 = std::min(lx0, x); lx1 = std::max(lx1, x); lz0 = std::min(lz0, z); lz1 = std::max(lz1, z); }
                if (std::abs(d) > worstCol) { worstCol = std::abs(d); wx = x; wz = z; }
            }
            std::printf("    columns with > 2 cm less water in the wide box: %d (x %d..%d, z %d..%d), %.3f m3; worst column (%d,%d) %.3f m\n", lost, lx0, lx1, lz0, lz1, lostM / 81.0, wx, wz, worstCol);
            if (wx >= 0) { const SurfaceColumn &a = f[0]->at(wx, wz), &b = f[1]->at(wx, wz);
                std::printf("    worst: runs %.0f/%.0f  a top %.3f %.3f bot %.3f %.3f | b top %.3f %.3f bot %.3f %.3f\n", a.runs, b.runs, a.top[0], a.top[1], a.bottom[0], a.bottom[1], b.top[0], b.top[1], b.bottom[0], b.bottom[1]); }
        }
        if (k < 4 || (k >= 29 && k < 40) || k % 10 == 0) std::printf("  k %3d: %ld columns differ (worst %.5f m), mass %.5f / %.5f\n", k, diff, worst, mass[0], mass[1]);
    }
}

TEST_F(WaterBodyPressureTest, TheLevelRoundABodyIsTheSurfaceItSitsIn) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const bool gpuSurf = WaterCoreGpu::s_gpuSurface;
    WaterCoreGpu::s_gpuSurface = true;   // the map reads the device's dense surface field
    const glm::vec3 body(3.6f, 1.2f, 4.0f);   // at the dam break's front
    std::vector<float> a, b;
    const MapCheck m = runOnce(device, physicalDevice, queue, queueFamilyIndex, 11, 40, body, &a);
    std::printf("  shipped: %ld cells, %ld ring columns compared, %ld off (worst %.2e m), %ld wet/dry disagree\n", m.cells, m.ring, m.off, m.worst, m.nanMismatch);
    WaterCoreGpu::s_pwShiftForTest = true;
    const MapCheck red = runOnce(device, physicalDevice, queue, queueFamilyIndex, 11, 40, body, nullptr);
    WaterCoreGpu::s_pwShiftForTest = false;
    std::printf("  RED (one column off): %ld off (worst %.2e m), %ld wet/dry disagree\n", red.off, red.worst, red.nanMismatch);
    // the box reaches 5 m further. Both boxes are wider than the front travels in 40 ticks (~2 sqrt(g h) = 9 m/s: from 3 m to ~9 m) - a
    // first version's 8 m box was reached by the front, and its wall's reflection made different water (144 of 144 cells, 4 cm)
    // tall cells ON (shipped) since 49: the first version ran them off here - a dam break went tall at tick 1 and the floor's regrids
    // lost water in the wider box (31.9 a: up to 17 cm by tick 40); a dam break is not at rest now (49.2 item 1)
    const MapCheck wide = runOnce(device, physicalDevice, queue, queueFamilyIndex, 16, 40, body, &b);
    long diff = 0; float worst = 0.0f;
    if (a.size() == b.size())
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::isnan(a[i]) != std::isnan(b[i])) { ++diff; continue; }
            if (!std::isnan(a[i]) && std::abs(a[i] - b[i]) > 1e-5f) { ++diff; worst = std::max(worst, std::abs(a[i] - b[i])); }
        }
    std::printf("  a box 5 m wider: %zu vs %zu cells, %ld differ (worst %.2e m)\n", a.size(), b.size(), diff, worst);
    WaterCoreGpu::s_gpuSurface = gpuSurf;
    ASSERT_TRUE(m.ok && red.ok && wide.ok);
    EXPECT_GT(m.ring, 100) << "the ring was compared";   // a 1/3 m body's 12 x 12 window: 120 ring columns
    EXPECT_EQ(m.off + m.nanMismatch, 0) << "the map is not the host's surface";
    EXPECT_GT(red.off + red.nanMismatch, 0) << "RED CONTROL: a window one column off went unseen";
    EXPECT_EQ(a.size(), b.size());
    EXPECT_EQ(diff, 0) << "the map depends on the volume's box";
}

namespace {
// 49: one run of the walled pond (x 0-4 m, 2 m deep, a wall at micro x 36-44 up to 3 m) in a box reaching voxel hiX, a stone at tick 30;
// its surface over the pond (x < 36 cells) per tick, its mass per tick, and the tall layer's books
struct PondRun { std::vector<std::vector<float>> tops; std::vector<double> mass; bool everTall = false; long regrids = 0, expandHeld = 0; std::string lastReason; };
PondRun walledPond(VkDevice d, VkPhysicalDevice pd, VkQueue q, uint32_t qf, int hiX, int ticks, bool damBreak = false) {
    PondRun out;
    auto state = [damBreak](const glm::ivec3& m) -> int { return (m.y < 0 || (!damBreak && m.x >= 36 && m.x < 45 && m.y < 27)) ? 1 : 0; };
    WaterCoreManager mgr(state);
    std::string err;
    if (!mgr.initGpu(d, pd, q, qf, "shaders", &err)) { ADD_FAILURE() << err; return out; }
    mgr.setDroplets(false);
    const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(hiX, damBreak ? 2 : 3, 7), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
    if (!id) { ADD_FAILURE() << err; return out; }
    mgr.setAutoSleep(id, false);
    mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(damBreak ? 2 : 3, 1, 7), 1.0f, nullptr);
    for (int k = 0; k < ticks; ++k) {
        if (k == 30 && !damBreak) mgr.addImpulse(glm::vec3(2.0f, 1.9f, 4.0f), 1.0f / 3.0f, 4.0f, glm::vec3(0.0f, -1.0f, 0.0f));
        AvRecord rec; mgr.step(id, 1, 1.0f / 60.0f, &rec);
        out.mass.push_back(rec.mass);
        const AvRecord* r = mgr.find(id);
        if (r) { out.everTall = out.everTall || r->tallActive; out.regrids = r->tallRegrids; out.expandHeld = r->tallExpandHeld; out.lastReason = r->tallReason; }
        const WaterSurfaceField* f = nullptr;
        for (const WaterSurfaceField* s : mgr.surfaceFields()) if (s->source == id) f = s;
        std::vector<float> t;
        if (f) for (int z = 0; z < f->nz; ++z) for (int x = 0; x < 36; ++x) { const SurfaceColumn& c = f->at(x, z); t.push_back(c.runs >= 1.0f ? c.top[0] : 0.0f); }
        out.tops.push_back(std::move(t));
    }
    return out;
}
}  // namespace

// 49 (A5): the tall floor does not depend on where the volume's box ends. The same walled pond, at rest, a stone dropped in it (the
// trough expands the floor, the next call collapses it lower) in a 12 m and a 17 m box - tall cells ON (shipped): the water the same
// column for column every tick, the mass kept. Red (before 49): the 17 m box lost 2.37 m^3 of 64 at the re-collapse, 2592 columns
// differed. Control: tall cells off - equal (measured 49.1).
TEST_F(WaterBodyPressureTest, TheTallFloorDoesNotDependOnTheBox) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const PondRun a = walledPond(device, physicalDevice, queue, queueFamilyIndex, 11, 90);
    const PondRun b = walledPond(device, physicalDevice, queue, queueFamilyIndex, 16, 90);
    ASSERT_EQ(a.tops.size(), b.tops.size());
    long worstCols = 0; float worst = 0.0f; int firstTick = -1; double massErr = 0.0;
    for (size_t k = 0; k < a.tops.size(); ++k) {
        long cols = 0;
        if (a.tops[k].size() == b.tops[k].size())
            for (size_t i = 0; i < a.tops[k].size(); ++i) { const float e = std::abs(a.tops[k][i] - b.tops[k][i]); if (e > 1e-5f) { ++cols; worst = std::max(worst, e); } }
        else cols = 1 << 30;
        if (cols && firstTick < 0) firstTick = static_cast<int>(k);
        worstCols = std::max(worstCols, cols);
        massErr = std::max({massErr, std::abs(a.mass[k] - 64.0), std::abs(b.mass[k] - 64.0)});
    }
    std::printf("  tall: 12 m box regrids %ld held %ld (%s) | 17 m box regrids %ld held %ld (%s)\n", a.regrids, a.expandHeld, a.lastReason.c_str(), b.regrids, b.expandHeld, b.lastReason.c_str());
    std::printf("  surfaces: worst tick %ld columns differ (worst %.2e m, first tick %d); mass error max %.2e m3\n", worstCols, worst, firstTick, massErr);
    EXPECT_TRUE(a.everTall && b.everTall) << "the tall cells never ran - the test proves nothing";
    EXPECT_GE(std::min(a.regrids, b.regrids), 3) << "the stone's trough did not expand and re-collapse the floor";
    EXPECT_EQ(a.expandHeld + b.expandHeld, 0) << "an expand ran short of slots";
    EXPECT_LT(massErr, 1e-4) << "the tall floor lost or made water";
    EXPECT_EQ(worstCols, 0) << "the water depends on where the box ends";
}

// 49 item 1: a dam break is not at rest - its wet tops are flat at the first call, but it stands beside open ground
TEST_F(WaterBodyPressureTest, ADamBreakIsNotAtRest) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    const PondRun r = walledPond(device, physicalDevice, queue, queueFamilyIndex, 11, 20, true);
    std::printf("  dam break: ever tall %d, reason '%s', mass error %.2e\n", int(r.everTall), r.lastReason.c_str(), r.mass.empty() ? -1.0 : std::abs(r.mass.back() - 48.0));
    EXPECT_FALSE(r.everTall) << "a dam break went tall";
    ASSERT_FALSE(r.mass.empty());
    EXPECT_LT(std::abs(r.mass.back() - 48.0), 1e-4);
}
