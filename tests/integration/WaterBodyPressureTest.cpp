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
    // TALL CELLS OFF in both (31.9, found by this test): the tall floor is chosen from volume-wide numbers (its "the water changed"
    // tolerance scales with the BOX's area) - with tall cells the same dam break in a 12 m and a 17 m box differed by up to 17 cm by
    // tick 40; with them off, 0 of 144 cells. The map's own rule is checked box-independent here; the floor's is logged, not fixed.
    std::vector<float> a0;
    runOnce(device, physicalDevice, queue, queueFamilyIndex, 11, 40, body, &a0, false);
    a = a0;
    const MapCheck wide = runOnce(device, physicalDevice, queue, queueFamilyIndex, 16, 40, body, &b, false);
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
