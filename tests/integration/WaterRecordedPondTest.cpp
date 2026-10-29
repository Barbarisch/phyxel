// WaterCore 32 - a pond that slept into the record is still water to debris, wherever the camera is, and a body moving through it
// wakes it (docs/WaterCore.md 32). Live, before: a Wood block thrown into a slept pond sank to the floor with the camera 100 m away
// (it floated with the camera at the pond: the record was read only through its draw tiles), and nothing woke the pond.
// Needs a Vulkan device (the pond is a GPU volume, as shipped).
#include <gtest/gtest.h>
#include "IntegrationTestFixture.h"
#include "core/water/WaterCoreManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <vector>

using namespace Phyxel::Core::Water;
using Phyxel::Testing::VulkanTestFixture;

namespace {
class WaterRecordedPondTest : public VulkanTestFixture {};

// the chunk spans, stood in for (as WaterRecordTilesTest's FakeRecord), with a revision the manager's record cache keys on
struct FakeRecord {
    std::map<std::pair<int, int>, std::vector<ColumnRun>> cols;
    uint64_t revision = 1;
    double total() const { double m = 0.0; for (const auto& [k, v] : cols) for (const auto& r : v) m += static_cast<double>(r.topY - r.bottomY); return m; }
    void install(WaterCoreManager& mgr) {
        mgr.setSpanIo(
            [this](const std::vector<ColumnRuns>& runs, float yLo, float yHi, long* unwritten, long* chunks) {
                for (const ColumnRuns& c : runs) {
                    if (c.held) continue;
                    auto& v = cols[{c.x, c.z}];
                    v.erase(std::remove_if(v.begin(), v.end(), [&](const ColumnRun& r) { return r.topY > yLo && r.bottomY < yHi; }), v.end());
                    for (const ColumnRun& r : c.runs) v.push_back(r);
                }
                ++revision;
                if (unwritten) *unwritten = 0;
                if (chunks) *chunks = 1;
            },
            [this](const glm::ivec3& lo, const glm::ivec3& hi, std::vector<ColumnRuns>& out) {
                out.clear();
                const float yLo = static_cast<float>(lo.y), yHi = static_cast<float>(hi.y + 1);
                for (int z = lo.z; z <= hi.z; ++z) for (int x = lo.x; x <= hi.x; ++x) {
                    const auto it = cols.find({x, z});
                    if (it == cols.end()) continue;
                    ColumnRuns c; c.x = x; c.z = z;
                    for (ColumnRun r : it->second) {
                        r.bottomY = std::max(r.bottomY, yLo); r.topY = std::min(r.topY, yHi);
                        if (r.topY <= r.bottomY) continue;
                        r.mass = static_cast<double>(r.topY - r.bottomY);
                        c.runs.push_back(r);
                    }
                    if (!c.runs.empty()) out.push_back(std::move(c));
                }
            });
        mgr.setSpanRevisionQuery([this] { return revision; });
    }
};

// an 8 x 8 m pond 1 m deep in a box 3 m high, slept into the record
struct SleptPond {
    WaterCoreManager mgr{[](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; }};
    FakeRecord record;
    bool ok = false;
    double placed = 0.0;
    SleptPond(VulkanTestFixture& fx, VkDevice d, VkPhysicalDevice pd, VkQueue q, uint32_t qf) {
        (void)fx;
        std::string err;
        if (!mgr.initGpu(d, pd, q, qf, "shaders", &err)) { ADD_FAILURE() << err; return; }
        mgr.setDroplets(false);
        record.install(mgr);
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(7, 2, 7), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        if (!id) { ADD_FAILURE() << err; return; }
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(7, 0, 7), 1.0f, nullptr);
        AvRecord rec;
        for (int k = 0; k < 10; ++k) mgr.step(id, 1, 1.0f / 60.0f, &rec);
        placed = rec.mass;
        const WriteBackRecord wb = mgr.sleep(id, true);
        if (!wb.ok) { ADD_FAILURE() << "sleep: " << wb.error; return; }
        ok = mgr.list().empty() && mgr.sleptVolumes().size() == 1;
        if (!ok) ADD_FAILURE() << "after the sleep: " << mgr.list().size() << " volumes, " << mgr.sleptVolumes().size() << " remembered";
    }
};
}  // namespace

TEST_F(WaterRecordedPondTest, TheRecordIsWaterWhereverTheCameraIs) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    SleptPond p(*this, device, physicalDevice, queue, queueFamilyIndex);
    ASSERT_TRUE(p.ok);
    std::printf("  placed %.4f m^3, the record holds %.4f m^3\n", p.placed, p.record.total());
    float near = 0.0f, far = 0.0f, dry = 0.0f; glm::vec2 fl(0.0f);
    p.mgr.setViewPoint(glm::vec3(4.0f, 2.0f, 4.0f)); p.mgr.surfaceFields();
    const bool nearWet = p.mgr.columnWater(3, 3, near, fl);
    p.mgr.setViewPoint(glm::vec3(1000.0f, 2.0f, 1000.0f)); p.mgr.surfaceFields();   // the record's draw tiles are gone from here
    const bool farWet = p.mgr.columnWater(3, 3, far, fl);
    const bool dryWet = p.mgr.columnWater(20, 20, dry, fl);
    std::printf("  column (3, 3): camera at the pond %s %.4f m, 1 km away %s %.4f m; (20, 20) %s\n", nearWet ? "wet" : "dry", near, farWet ? "wet" : "dry", far,
                dryWet ? "wet" : "dry");
    EXPECT_TRUE(nearWet);
    EXPECT_TRUE(farWet) << "with the camera far the slept pond read dry (a floater sank to the floor)";
    EXPECT_NEAR(far, near, 2e-3f) << "the water's level depends on the camera";
    EXPECT_NEAR(far, 1.0f, 0.01f);
    EXPECT_FALSE(dryWet);
    // the record changes (water drained there): read again at once, not after the cache's age
    p.record.cols.erase({3, 3}); ++p.record.revision;
    EXPECT_FALSE(p.mgr.columnWater(3, 3, far, fl)) << "the record cache kept water the record no longer holds";
}

TEST_F(WaterRecordedPondTest, ASleptPondWakesWhenABodyMovesThroughIt) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    MovingSolid stone; stone.centre = glm::vec3(4.0f, 1.6f, 4.0f); stone.halfExtents = glm::vec3(1.0f / 6.0f); stone.velocity = glm::vec3(0.0f, -3.0f, 0.0f);
    MovingSolid away = stone; away.centre = glm::vec3(30.0f, 1.6f, 30.0f);
    {   // RED: waking off - the pond stays in the record
        SleptPond p(*this, device, physicalDevice, queue, queueFamilyIndex);
        ASSERT_TRUE(p.ok);
        struct Restore { ~Restore() { WaterCoreManager::s_wakeOnTouch = WaterCoreManager::kWakeOnTouchByDefault; } } restore;   // 36: never leaks 'off'
        WaterCoreManager::s_wakeOnTouch = false;
        const auto ids = p.mgr.wakeSleptTouching({stone});
        EXPECT_TRUE(ids.empty() && p.mgr.list().empty()) << "RED CONTROL: woke with waking off";
    }
    SleptPond p(*this, device, physicalDevice, queue, queueFamilyIndex);
    ASSERT_TRUE(p.ok);
    EXPECT_TRUE(p.mgr.wakeSleptTouching({away}).empty()) << "a body outside the box woke the pond";
    const auto ids = p.mgr.wakeSleptTouching({stone});
    ASSERT_EQ(ids.size(), 1u) << "the stone moving into the slept pond did not wake it";
    const auto vols = p.mgr.list();
    ASSERT_EQ(vols.size(), 1u);
    std::printf("  woken: box (%d %d %d)-(%d %d %d), mass %.4f m^3 (the record %.4f), %.1f ms\n", vols[0].minVoxel.x, vols[0].minVoxel.y, vols[0].minVoxel.z,
                vols[0].maxVoxel.x, vols[0].maxVoxel.y, vols[0].maxVoxel.z, vols[0].mass, p.record.total(), p.mgr.sleptWakeStats().lastMs);
    EXPECT_EQ(vols[0].minVoxel, glm::ivec3(0, 0, 0));
    EXPECT_EQ(vols[0].maxVoxel, glm::ivec3(7, 2, 7));
    EXPECT_NEAR(vols[0].mass, p.record.total(), 1e-3) << "the woken pond does not hold the record's water";
    EXPECT_TRUE(p.mgr.sleptVolumes().empty()) << "woken, it is no longer remembered as slept";
}

// 39 (A2f step 1d): a destroyed volume's compute pipelines are kept for the next one. The same dam break run twice in one manager -
// the first on pipelines built from disk, the second on the first's, kept - must write the same record, byte for byte (every binding
// is rewritten at each create; a kept pipeline still pointing at a freed buffer would read garbage or lose the device).
TEST_F(WaterRecordedPondTest, AVolumeOnKeptPipelinesStepsTheSameAsOnBuiltOnes) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    WaterCoreManager mgr{[](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; }};
    std::string err;
    ASSERT_TRUE(mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
    mgr.setDroplets(false);
    FakeRecord record; record.install(mgr);
    std::vector<std::map<std::pair<int, int>, std::vector<ColumnRun>>> written;
    std::vector<int> reused;
    for (int run = 0; run < 3; ++run) {
        record.cols.clear();
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(5, 2, 3), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        ASSERT_NE(id, 0) << err;
        reused.push_back(mgr.gpuCreateSplitForTest().pipelinesReused);
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(1, 1, 3), 1.0f, nullptr);   // a dam: 2 m of water at one end
        AvRecord rec;
        for (int k = 0; k < 40; ++k) mgr.step(id, 1, 1.0f / 60.0f, &rec);
        const WriteBackRecord wb = mgr.sleep(id, true);
        ASSERT_TRUE(wb.ok) << wb.error;
        written.push_back(record.cols);
        std::printf("  run %d: pipelines reused %d, record %.6f m^3 in %zu columns\n", run, reused.back(), record.total(), record.cols.size());
    }
    EXPECT_EQ(reused[0], 0) << "the first volume of a manager has no kept pipelines";
    EXPECT_EQ(reused[1], mgr.gpuCreateSplitForTest().pipelines) << "the second volume did not take the first's pipelines";
    for (int run = 1; run < 3; ++run) {
        ASSERT_EQ(written[run].size(), written[0].size()) << "run " << run;
        for (const auto& [k, v] : written[0]) {
            const auto& w = written[run].at(k);
            ASSERT_EQ(w.size(), v.size()) << "run " << run << " column " << k.first << "," << k.second;
            for (size_t i = 0; i < v.size(); ++i) {
                EXPECT_EQ(w[i].bottomY, v[i].bottomY) << "run " << run << " column " << k.first << "," << k.second;
                EXPECT_EQ(w[i].topY, v[i].topY) << "run " << run << " column " << k.first << "," << k.second;
            }
        }
    }
}

// 39.7 (A2f step 1e): a wake uploads its grid once - at the first step, after the seed - where it uploaded the empty grid inside
// create and the seeded one again at the first step. The same slept pond woken both ways, kicked and stepped, must write the same
// record byte for byte (WaterCoreManager::s_wakeUploadsTwiceForTest is the old way, the control).
TEST_F(WaterRecordedPondTest, AWakeWithOneUploadStepsTheSameAsWithTwo) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    struct Restore { ~Restore() { WaterCoreManager::s_wakeUploadsTwiceForTest = false; } } restore;
    std::map<std::pair<int, int>, std::vector<ColumnRun>> written[2];
    for (int twice = 0; twice < 2; ++twice) {
        WaterCoreManager::s_wakeUploadsTwiceForTest = twice != 0;
        SleptPond p(*this, device, physicalDevice, queue, queueFamilyIndex);
        ASSERT_TRUE(p.ok);
        MovingSolid stone; stone.centre = glm::vec3(4.0f, 1.6f, 4.0f); stone.halfExtents = glm::vec3(1.0f / 6.0f); stone.velocity = glm::vec3(0.0f, -3.0f, 0.0f);
        const auto ids = p.mgr.wakeSleptTouching({stone});
        ASSERT_EQ(ids.size(), 1u);
        p.mgr.addRadialImpulse(glm::vec3(4.0f, 1.0f, 4.0f), 1.5f, 2.0f, 0.5f);   // a splash, so the water moves
        AvRecord rec;
        for (int k = 0; k < 40; ++k) p.mgr.step(ids[0], 1, 1.0f / 60.0f, &rec);
        std::printf("  %s: uploads %ld, mass %.6f, KE %.6g\n", twice ? "two uploads (before)" : "one upload", static_cast<long>(rec.gpuUploads), static_cast<double>(rec.mass), static_cast<double>(rec.kineticEnergy));
        const WriteBackRecord wb = p.mgr.sleep(ids[0], true);
        ASSERT_TRUE(wb.ok) << wb.error;
        written[twice] = p.record.cols;
    }
    ASSERT_EQ(written[0].size(), written[1].size());
    bool moved = false;
    for (const auto& [k, v] : written[1]) {
        const auto& w = written[0].at(k);
        ASSERT_EQ(w.size(), v.size()) << "column " << k.first << "," << k.second;
        for (size_t i = 0; i < v.size(); ++i) {
            EXPECT_EQ(w[i].bottomY, v[i].bottomY) << "column " << k.first << "," << k.second;
            EXPECT_EQ(w[i].topY, v[i].topY) << "column " << k.first << "," << k.second;
            if (std::abs(v[i].topY - 1.0f) > 1e-4f) moved = true;
        }
    }
    EXPECT_TRUE(moved) << "the kick did not move the water: the comparison would pass on a still pond";
}

namespace {
using Cols = std::map<std::pair<int, int>, std::vector<ColumnRun>>;
// the number of columns whose runs differ (bottom or top, bit for bit); -1 if the column sets differ
long columnsDiffering(const Cols& a, const Cols& b) {
    if (a.size() != b.size()) return -1;
    long n = 0;
    for (const auto& [k, v] : a) {
        const auto it = b.find(k);
        if (it == b.end()) return -1;
        bool diff = it->second.size() != v.size();
        for (size_t i = 0; !diff && i < v.size(); ++i) diff = it->second[i].bottomY != v[i].bottomY || it->second[i].topY != v[i].topY;
        if (diff) ++n;
    }
    return n;
}
struct RestoreLocality { WaterLocality saved = WaterCoreManager::s_locality; ~RestoreLocality() { WaterCoreManager::s_locality = saved; WaterCoreGpu::s_keptSkipsClearForTest = false; } };
}  // namespace

// L3 (41): a pond that sleeps near the viewer keeps its GPU buffers and its next wake takes them - no allocation, no pool grow. The
// same 12 x 12 m pond (more bricks than a new volume's 510 slots, so a fresh wake grows), slept and woken with the memory freed
// (the control: today's path) and kept, kicked and stepped 40 ticks: the written record must match byte for byte. Then the kept
// path with an adopted buffer's zero fill skipped (s_keptSkipsClearForTest): reported, to show whether the equality half sees it.
TEST_F(WaterRecordedPondTest, AWakeOnKeptMemoryStepsTheSameAsAFreshOne) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    RestoreLocality restore;
    Cols written[3];
    long grows[3] = {0, 0, 0}, allocated[3] = {0, 0, 0};
    bool hit[3] = {false, false, false};
    for (int run = 0; run < 3; ++run) {
        WaterCoreManager::s_locality.testKeepMemoryOff = run == 0;
        WaterCoreGpu::s_keptSkipsClearForTest = run == 2;
        WaterCoreManager mgr{[](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; }};
        std::string err;
        ASSERT_TRUE(mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
        mgr.setDroplets(false);
        FakeRecord record; record.install(mgr);
        mgr.setViewPoint(glm::vec3(6.0f, 2.0f, 6.0f));
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(11, 2, 11), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        ASSERT_NE(id, 0) << err;
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(11, 0, 11), 1.0f, nullptr);
        mgr.addRadialImpulse(glm::vec3(3.0f, 1.0f, 3.0f), 1.5f, 2.0f, 0.5f);   // its first life moves, so a kept buffer holds stale water
        AvRecord rec;
        for (int k = 0; k < 20; ++k) mgr.step(id, 1, 1.0f / 60.0f, &rec);
        WriteBackRecord wb = mgr.sleep(id, true);
        ASSERT_TRUE(wb.ok) << wb.error;
        const auto km0 = mgr.keptMemoryStats();
        const long grows0 = mgr.gpuGrowSplit().grows;
        MovingSolid stone; stone.centre = glm::vec3(6.0f, 1.6f, 6.0f); stone.halfExtents = glm::vec3(1.0f / 6.0f); stone.velocity = glm::vec3(0.0f, -3.0f, 0.0f);
        const auto ids = mgr.wakeSleptTouching({stone});
        ASSERT_EQ(ids.size(), 1u);
        mgr.addRadialImpulse(glm::vec3(6.0f, 1.0f, 6.0f), 1.5f, 2.0f, 0.5f);
        mgr.step(ids[0], 1, 1.0f / 60.0f, &rec);   // the first step uploads the seeded grid (39.7): the grow, if any, is here
        const auto km1 = mgr.keptMemoryStats();
        grows[run] = mgr.gpuGrowSplit().grows - grows0;
        allocated[run] = km1.buffersAllocated - km0.buffersAllocated;
        hit[run] = km1.lastWakeHit;
        for (int k = 0; k < 39; ++k) mgr.step(ids[0], 1, 1.0f / 60.0f, &rec);
        std::printf("  %s: kept %zu pond(s) %.1f MB at the sleep, wake hit %d, buffers allocated %ld, pool grows %ld, mass %.6f, KE %.6g\n",
                    run == 0 ? "memory freed (control)" : (run == 1 ? "memory kept" : "kept, no clear (red)"), km0.ponds, km0.heldBytes / 1048576.0,
                    hit[run] ? 1 : 0, allocated[run], grows[run], static_cast<double>(rec.mass), static_cast<double>(rec.kineticEnergy));
        wb = mgr.sleep(ids[0], true);
        ASSERT_TRUE(wb.ok) << wb.error;
        written[run] = record.cols;
    }
    EXPECT_FALSE(hit[0]) << "the control kept its memory";
    EXPECT_GE(grows[0], 1) << "the fresh wake did not grow: the pond is too small to show the grow the kept path saves";
    EXPECT_TRUE(hit[1]) << "the kept wake did not take its kept set";
    EXPECT_EQ(grows[1], 0) << "the kept wake grew its pool";
    EXPECT_LT(allocated[1], allocated[0] / 4) << "the kept wake still allocated most of its buffers";
    const long d = columnsDiffering(written[0], written[1]);
    EXPECT_EQ(d, 0) << "a wake on kept memory wrote a different record than a fresh one (" << d << " columns; -1 = different columns)";
    std::printf("  red (no clear of adopted buffers): %ld column(s) differ from the control (-1 = different columns)\n", columnsDiffering(written[0], written[2]));
}

// L3 (41.4 item 2): churn and the cap. One pond slept and woken 12 times on kept memory: after the first wake nothing is allocated
// but the few buffers made later (lazily), the kept bytes stay the same. Then three ponds at 0, 40 and 80 m from the viewer with a cap
// that holds two: the farthest gives its memory up; with memory_m 20 only the nearest keeps it.
TEST_F(WaterRecordedPondTest, KeptMemoryChurnsWithoutGrowingAndTheCapDropsTheFarthest) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    RestoreLocality restore;
    WaterCoreManager::s_locality.memoryM = 256.0f; WaterCoreManager::s_locality.memoryCapMb = 4096.0f;
    WaterCoreManager mgr{[](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; }};
    std::string err;
    ASSERT_TRUE(mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
    mgr.setDroplets(false);
    FakeRecord record; record.install(mgr);
    mgr.setViewPoint(glm::vec3(4.0f, 2.0f, 4.0f));
    auto pond = [&](int x0) {
        const int id = mgr.create(glm::ivec3(x0, 0, 0), glm::ivec3(x0 + 7, 2, 7), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        EXPECT_NE(id, 0) << err;
        mgr.placeBox(glm::ivec3(x0, 0, 0), glm::ivec3(x0 + 7, 0, 7), 1.0f, nullptr);
        AvRecord rec; for (int k = 0; k < 5; ++k) mgr.step(id, 1, 1.0f / 60.0f, &rec);
        EXPECT_TRUE(mgr.sleep(id, true).ok);
    };
    auto wake = [&](float x) {
        MovingSolid s; s.centre = glm::vec3(x, 1.6f, 4.0f); s.halfExtents = glm::vec3(1.0f / 6.0f); s.velocity = glm::vec3(0.0f, -3.0f, 0.0f);
        const auto ids = mgr.wakeSleptTouching({s});
        return ids.empty() ? 0 : ids[0];
    };
    pond(0);
    const unsigned long long held0 = mgr.keptMemoryStats().heldBytes;
    ASSERT_GT(held0, 0ull) << "the pond beside the viewer did not keep its memory";
    std::vector<long> alloc;
    for (int c = 0; c < 12; ++c) {
        const long a0 = mgr.keptMemoryStats().buffersAllocated;
        const int id = wake(4.0f);
        ASSERT_NE(id, 0);
        EXPECT_TRUE(mgr.keptMemoryStats().lastWakeHit) << "cycle " << c;
        AvRecord rec; for (int k = 0; k < 5; ++k) mgr.step(id, 1, 1.0f / 60.0f, &rec);
        ASSERT_TRUE(mgr.sleep(id, true).ok);
        alloc.push_back(mgr.keptMemoryStats().buffersAllocated - a0);
        EXPECT_EQ(mgr.keptMemoryStats().heldBytes, held0) << "cycle " << c << ": the kept bytes changed";
        mgr.updateKeptMemory(-1.0);   // what was not adopted, freed
    }
    std::printf("  churn: kept %.2f MB; buffers allocated per cycle:", held0 / 1048576.0);
    for (long a : alloc) std::printf(" %ld", a);
    std::printf("\n");
    for (size_t c = 1; c < alloc.size(); ++c) EXPECT_EQ(alloc[c], alloc[0]) << "cycle " << c << ": the allocations grow with the churn";
    EXPECT_LE(alloc[0], 3) << "a kept wake still allocates";
    // the cap: two of three fit
    pond(40); pond(80);
    EXPECT_EQ(mgr.keptMemoryStats().ponds, 3u);
    WaterCoreManager::s_locality.memoryCapMb = static_cast<float>(2.5 * held0 / 1048576.0);
    mgr.updateKeptMemory(-1.0);
    const auto km = mgr.keptMemoryStats();
    std::printf("  cap %.1f MB: %zu ponds kept, %.1f MB, released by the cap %ld\n", WaterCoreManager::s_locality.memoryCapMb, km.ponds, km.heldBytes / 1048576.0, km.releasedCap);
    EXPECT_EQ(km.ponds, 2u);
    EXPECT_EQ(km.releasedCap, 1);
    ASSERT_NE(wake(84.0f), 0); EXPECT_FALSE(mgr.keptMemoryStats().lastWakeHit) << "the farthest pond kept its memory over the cap";
    // the reach: from 20 m only the nearest
    WaterCoreManager::s_locality.memoryM = 20.0f;
    mgr.updateKeptMemory(-1.0);
    EXPECT_EQ(mgr.keptMemoryStats().ponds, 1u);
    EXPECT_GE(mgr.keptMemoryStats().releasedFar, 1);
    ASSERT_NE(wake(44.0f), 0); EXPECT_FALSE(mgr.keptMemoryStats().lastWakeHit) << "a pond 36 m away kept its memory with memory_m 20";
    ASSERT_NE(wake(4.0f), 0); EXPECT_TRUE(mgr.keptMemoryStats().lastWakeHit) << "the pond at the viewer lost its memory";
    EXPECT_EQ(mgr.keptMemoryStats().ponds, 0u);
}

namespace {
struct RestorePace { ~RestorePace() { WaterCoreManager::s_wakePace = WaterCoreManager::kWakePaceByDefault;
                                      WaterCoreManager::s_wakeBudgetMs = WaterCoreManager::kWakeBudgetMsDefault; WaterCoreManager::s_paceLateForTest = false; } };
// a stone held 1.4 m over the 1 m pond (its lowest point): ~0.54 s to the water, due at half that (16 ticks)
MovingSolid stoneAbove() {
    MovingSolid s; s.centre = glm::vec3(4.0f, 2.7f, 4.0f); s.halfExtents = glm::vec3(1.0f / 6.0f); s.velocity = glm::vec3(0.0f); s.id = 7;
    return s;
}
// L4: drive the touch frame by frame (one physics tick a frame) until the paced wake is live; the id, and the frames it took
int pacedWake(WaterCoreManager& mgr, const MovingSolid& s, int* frames, std::vector<std::function<void(int)>> during = {}) {
    uint64_t tick = 1000;
    for (int f = 0; f < 40; ++f) {
        mgr.setPhysicsClock(tick, 1.0 / 60.0);
        mgr.wakeSleptTouching({s});
        for (auto& d : during) d(f);
        if (!mgr.list().empty()) { if (frames) *frames = f + 1; return mgr.list()[0].id; }
        ++tick;
    }
    return 0;
}
}  // namespace

// L4 (42.3): a wake built over frames, live before its body can reach the water, makes the same pond: woken immediately and paced
// (the smallest budget: about a stage a frame), stepped once (the immediate wake's first step uploads; the paced one uploaded in its
// stages), kicked, stepped 39 more - the written record byte for byte. The paced wake must take more than one frame and go live
// before its deadline (not forced by it).
TEST_F(WaterRecordedPondTest, APacedWakeMakesTheSamePondAsAnImmediateOne) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    RestorePace restore;
    Cols written[2];
    int frames = 0;
    WaterCoreManager::PacedStats ps;
    for (int paced = 0; paced < 2; ++paced) {
        WaterCoreManager::s_wakePace = paced != 0; WaterCoreManager::s_wakeBudgetMs = WaterCoreManager::kWakeBudgetMsMin;
        SleptPond p(*this, device, physicalDevice, queue, queueFamilyIndex);
        ASSERT_TRUE(p.ok);
        int id = 0;
        if (paced) id = pacedWake(p.mgr, stoneAbove(), &frames);
        else { p.mgr.setPhysicsClock(1000, 1.0 / 60.0); const auto ids = p.mgr.wakeSleptTouching({stoneAbove()}); id = ids.empty() ? 0 : ids[0]; }
        ASSERT_NE(id, 0) << (paced ? "the paced wake never went live" : "the immediate wake did not wake");
        AvRecord rec;
        p.mgr.step(id, 1, 1.0f / 60.0f, &rec);
        p.mgr.addRadialImpulse(glm::vec3(4.0f, 1.0f, 4.0f), 1.5f, 2.0f, 0.5f);
        for (int k = 0; k < 39; ++k) p.mgr.step(id, 1, 1.0f / 60.0f, &rec);
        std::printf("  %s: uploads %ld, mass %.6f, KE %.6g\n", paced ? "paced" : "immediate", static_cast<long>(rec.gpuUploads), static_cast<double>(rec.mass), static_cast<double>(rec.kineticEnergy));
        if (paced) ps = p.mgr.pacedStats();
        ASSERT_TRUE(p.mgr.sleep(id, true).ok);
        written[paced] = p.record.cols;
    }
    std::printf("  paced: live after %d frame(s), %ld stages, the longest %.2f ms, due in %.3f s, forced by the deadline %ld, immediate %ld\n",
                frames, ps.stages, ps.maxStageMs, ps.lastDueS, ps.forcedByDeadline, ps.immediate);
    EXPECT_EQ(ps.started, 1); EXPECT_EQ(ps.live, 1); EXPECT_EQ(ps.immediate, 0);
    EXPECT_GE(frames, 2) << "the wake was not spread over frames (the test would not show pacing)";
    EXPECT_EQ(ps.forcedByDeadline, 0) << "the deadline forced the wake: it was not paced to completion";
    EXPECT_NEAR(ps.lastDueS, 0.25, 0.03) << "the deadline is not half the stone's ~0.54 s to the water";
    const long d = columnsDiffering(written[0], written[1]);
    EXPECT_EQ(d, 0) << "the paced wake wrote a different record (" << d << " columns; -1 = different columns)";
    bool moved = false;
    for (const auto& [k, v] : written[0]) for (const auto& r : v) if (std::abs(r.topY - 1.0f) > 1e-4f) moved = true;
    EXPECT_TRUE(moved) << "the kick did not move the water: the comparison would pass on a still pond";
}

// L4 (42.3): the record changes while the wake is built (another write to the pond's columns) - the build is stale, so it is
// rebuilt at once, and the pond holds the CHANGED record (as an immediate wake after the change would). And a stone at the water
// (no time to pace) wakes immediately.
TEST_F(WaterRecordedPondTest, APacedWakeRebuildsWhenTheRecordChangesAndABodyAtTheWaterIsImmediate) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    RestorePace restore;
    WaterCoreManager::s_wakePace = true; WaterCoreManager::s_wakeBudgetMs = WaterCoreManager::kWakeBudgetMsMin;
    {
        SleptPond p(*this, device, physicalDevice, queue, queueFamilyIndex);
        ASSERT_TRUE(p.ok);
        const double before = p.record.total();
        // after the seed (stage 2, frame 2 at a stage a frame): drained before it, the seed reads the drained record and nothing is stale
        auto drain = [&](int f) { if (f == 3) { p.record.cols.erase({3, 3}); p.record.cols.erase({3, 4}); ++p.record.revision; } };
        int frames = 0;
        const int id = pacedWake(p.mgr, stoneAbove(), &frames, {drain});
        ASSERT_NE(id, 0);
        const auto ps = p.mgr.pacedStats();
        double mass = 0.0; for (const auto& v : p.mgr.list()) mass += v.mass;
        std::printf("  record %.4f -> %.4f m^3 during the build; the woken pond %.4f m^3; rebuilt %ld, live %ld, frames %d\n", before, p.record.total(), mass, ps.rebuilt, ps.live, frames);
        EXPECT_EQ(ps.rebuilt, 1) << "the stale build went live";
        EXPECT_NEAR(mass, p.record.total(), 1e-3) << "the woken pond does not hold the record as it is now";
    }
    {
        SleptPond p(*this, device, physicalDevice, queue, queueFamilyIndex);
        ASSERT_TRUE(p.ok);
        MovingSolid atWater = stoneAbove(); atWater.centre.y = 1.1f;
        p.mgr.setPhysicsClock(1000, 1.0 / 60.0);
        const auto ids = p.mgr.wakeSleptTouching({atWater});
        EXPECT_EQ(ids.size(), 1u) << "a body at the water did not wake the pond at once";
        EXPECT_EQ(p.mgr.pacedStats().immediate, 1);
        EXPECT_EQ(p.mgr.pacedStats().started, 0);
    }
}

// L4 (42.4 item 3): the first frame drawn after a paced wake goes live is the uploaded state, as after an immediate wake (39.10's
// TheFrameAfterAnUploadDrawsTheUploadedState with the wake paced).
TEST_F(WaterRecordedPondTest, TheFrameAfterAPacedWakeDrawsTheUploadedState) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    RestorePace restore;
    WaterCoreManager::s_wakePace = true; WaterCoreManager::s_wakeBudgetMs = WaterCoreManager::kWakeBudgetMsMin;
    SleptPond p(*this, device, physicalDevice, queue, queueFamilyIndex);
    ASSERT_TRUE(p.ok);
    const int id = pacedWake(p.mgr, stoneAbove(), nullptr);
    ASSERT_NE(id, 0);
    p.mgr.setRealtime(true);
    p.mgr.addRadialImpulse(glm::vec3(4.0f, 1.0f, 4.0f), 2.0f, 3.0f, 0.5f);   // the first call moves the water
    WaterSurfaceField uploaded;
    extractSurfaceField(*p.mgr.gridForTest(id), uploaded);
    AvRecord rec;
    p.mgr.step(id, 1, 1.0f / 60.0f, &rec);
    const WaterSurfaceField* drawn = nullptr;
    for (const WaterSurfaceField* f : p.mgr.surfaceFields()) if (f && f->source == id) drawn = f;
    ASSERT_NE(drawn, nullptr);
    ASSERT_EQ(drawn->cols.size(), uploaded.cols.size());
    EXPECT_EQ(std::memcmp(drawn->cols.data(), uploaded.cols.data(), uploaded.cols.size() * sizeof(SurfaceColumn)), 0)
        << "the frame after the paced wake did not draw the uploaded state";
    p.mgr.setRealtime(false);
}

// 39.10: the upload's host work (host copies, the all-ground marking, scatterSlots) and the CPU surface extraction split over
// threads. The same dam break, uploaded and stepped by a manager on one thread and by one on eight, must write the same record
// byte for byte; the second half sleeps and wakes it (the seeded upload) and steps again. 54 x 27 x 72 cells: over the threshold.
TEST_F(WaterRecordedPondTest, AnUploadOnEightThreadsStepsTheSameAsOnOne) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    std::map<std::pair<int, int>, std::vector<ColumnRun>> written[2];
    for (int t = 0; t < 2; ++t) {
        WaterCoreManager mgr{[](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : ((m.x * 7 + m.z * 13) % 41 == 0 && m.y < 9 ? 1 : 0); }};
        mgr.setStateQueryThreadSafe(t == 1, 8);
        std::string err;
        ASSERT_TRUE(mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
        mgr.setDroplets(false);
        FakeRecord record; record.install(mgr);
        const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(5, 2, 7), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
        ASSERT_NE(id, 0) << err;
        mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(2, 1, 7), 1.0f, nullptr);   // a dam: 2 m of water over half the box
        AvRecord rec;
        for (int k = 0; k < 30; ++k) mgr.step(id, 1, 1.0f / 60.0f, &rec);
        WriteBackRecord wb = mgr.sleep(id, true);
        ASSERT_TRUE(wb.ok) << wb.error;
        MovingSolid stone; stone.centre = glm::vec3(3.0f, 1.0f, 4.0f); stone.halfExtents = glm::vec3(1.0f / 6.0f); stone.velocity = glm::vec3(0.0f, -3.0f, 0.0f);
        const auto ids = mgr.wakeSleptTouching({stone});
        ASSERT_EQ(ids.size(), 1u);
        for (int k = 0; k < 30; ++k) mgr.step(ids[0], 1, 1.0f / 60.0f, &rec);
        std::printf("  %d thread(s): bricks %ld, mass %.6f, KE %.6g\n", t == 1 ? 8 : 1, mgr.gpuUploadSplit().bricks, static_cast<double>(rec.mass), static_cast<double>(rec.kineticEnergy));
        wb = mgr.sleep(ids[0], true);
        ASSERT_TRUE(wb.ok) << wb.error;
        written[t] = record.cols;
    }
    ASSERT_EQ(written[0].size(), written[1].size());
    for (const auto& [k, v] : written[0]) {
        const auto& w = written[1].at(k);
        ASSERT_EQ(w.size(), v.size()) << "column " << k.first << "," << k.second;
        for (size_t i = 0; i < v.size(); ++i) {
            EXPECT_EQ(w[i].bottomY, v[i].bottomY) << "column " << k.first << "," << k.second;
            EXPECT_EQ(w[i].topY, v[i].topY) << "column " << k.first << "," << k.second;
        }
    }
}

// 39.10: the frame after an upload draws the uploaded state on the CPU - one call behind, as every other frame draws the last
// COLLECTED call - instead of downloading the grid the call in flight is changing (9.7 ms on an 8 m pit). A woken pond, kicked so
// the first call moves it, stepped once in realtime (the call stays in flight): the drawn field must equal the uploaded state's.
// The control (s_uploadFallbackSyncsForTest, the old download) must NOT equal it - it draws the call's newer state - so the test
// tells the two apart.
TEST_F(WaterRecordedPondTest, TheFrameAfterAnUploadDrawsTheUploadedState) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    struct Restore { ~Restore() { WaterCoreManager::s_uploadFallbackSyncsForTest = false; } } restore;
    bool equal[2] = {false, false};
    for (int control = 0; control < 2; ++control) {
        WaterCoreManager::s_uploadFallbackSyncsForTest = control != 0;
        SleptPond p(*this, device, physicalDevice, queue, queueFamilyIndex);
        ASSERT_TRUE(p.ok);
        p.mgr.setRealtime(true);
        MovingSolid stone; stone.centre = glm::vec3(4.0f, 1.6f, 4.0f); stone.halfExtents = glm::vec3(1.0f / 6.0f); stone.velocity = glm::vec3(0.0f, -3.0f, 0.0f);
        const auto ids = p.mgr.wakeSleptTouching({stone});
        ASSERT_EQ(ids.size(), 1u);
        p.mgr.addRadialImpulse(glm::vec3(4.0f, 1.0f, 4.0f), 2.0f, 3.0f, 0.5f);   // the first call moves the water
        WaterSurfaceField uploaded;
        extractSurfaceField(*p.mgr.gridForTest(ids[0]), uploaded);   // the state the step below uploads
        AvRecord rec;
        p.mgr.step(ids[0], 1, 1.0f / 60.0f, &rec);
        const WaterSurfaceField* drawn = nullptr;
        for (const WaterSurfaceField* f : p.mgr.surfaceFields()) if (f && f->source == ids[0]) drawn = f;
        ASSERT_NE(drawn, nullptr);
        ASSERT_EQ(drawn->cols.size(), uploaded.cols.size());
        equal[control] = std::memcmp(drawn->cols.data(), uploaded.cols.data(), uploaded.cols.size() * sizeof(SurfaceColumn)) == 0;
        std::printf("  %s: the drawn field %s the uploaded state\n", control ? "control (download)" : "no download", equal[control] ? "equals" : "differs from");
        p.mgr.setRealtime(false);
    }
    EXPECT_TRUE(equal[0]) << "the frame after the upload did not draw the uploaded state";
    EXPECT_FALSE(equal[1]) << "the control drew the uploaded state too: the kick did not move the water in the first call, so the test cannot tell the two apart";
}

// L2 (43.2, 43.3 items 5 and 8): a GPU pond's ripple layer, stepped on the ripple GPU (12 x 12 m: 11,664 lattice cells, over
// kRippleGpuMinCells), is dropped when the viewer leaves the reach and its device state is freed; 20 fly-aways hold no more than
// the one state. Made again when the viewer comes back.
TEST_F(WaterRecordedPondTest, ARippleLayerBeyondTheReachHoldsNoDeviceState) {
    if (!isVulkanAvailable()) GTEST_SKIP();
    RestoreLocality restore;
    WaterCoreManager::s_locality.rippleReach = true;
    WaterCoreManager mgr{[](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; }};
    std::string err;
    ASSERT_TRUE(mgr.initGpu(device, physicalDevice, queue, queueFamilyIndex, "shaders", &err)) << err;
    ASSERT_TRUE(mgr.rippleGpuAvailable()) << mgr.rippleGpuError();
    mgr.setDroplets(false);
    const int id = mgr.create(glm::ivec3(0, 0, 0), glm::ivec3(11, 2, 11), 1.0f / 9.0f, "eulerian", &err, "gpu", 0);
    ASSERT_NE(id, 0) << err;
    mgr.placeBox(glm::ivec3(0, 0, 0), glm::ivec3(11, 0, 11), 1.0f, nullptr);
    mgr.setAutoSleep(id, false);
    auto frames = [&](const glm::vec3& view, int n) {
        // step() as the other GPU tests drive a volume (it steps the ripples too); update() leaves a still test pond unstepped.
        // A droplet's kick each frame keeps the layer awake: a body crossing a 1/9 m grid leaves no kinematic remainder (the grid
        // holds it whole - found writing this test), so a stone's rings on such a pond come from its droplets
        for (int k = 0; k < n; ++k) {
            mgr.setViewPoint(view); mgr.kickRipplesForTest(6.0f, 6.0f, 50.0f);
            AvRecord rec; mgr.step(id, 1, 1.0f / 60.0f, &rec);
        }
        return mgr.rippleStats();
    };
    const glm::vec3 nearV(6.0f, 3.0f, -2.0f), farV(80.0f, 3.0f, 6.0f);
    int maxStates = 0;
    for (int c = 0; c < 20; ++c) {
        const auto a = frames(nearV, 6);
        if (c == 0) std::printf("  near: layers %d, awake %d, cells %ld, gpu layers %d, device states %d\n",
                                a.layers, a.awake, a.cells, a.gpuLayers, a.gpuStates);
        EXPECT_EQ(a.slotsHeld, 1) << "cycle " << c;
        EXPECT_TRUE(a.gpu) << "cycle " << c << ": the layers stepped on the CPU";
        const auto b = frames(farV, 6);   // the ring is 3 deep: freed within a few frames of the drop
        EXPECT_EQ(b.slotsHeld, 0) << "cycle " << c;
        EXPECT_EQ(b.gpuStates, 0) << "cycle " << c << ": a dropped layer still holds its device state";
        maxStates = std::max({maxStates, a.gpuStates, b.gpuStates});
    }
    const auto f = frames(nearV, 6);
    std::printf("  20 fly-aways: made %ld, dropped %ld, the most device states held %d\n", f.made, f.dropped, maxStates);
    EXPECT_EQ(f.made, 21); EXPECT_EQ(f.dropped, 20);
    EXPECT_LE(maxStates, 1);
    EXPECT_EQ(f.gpuStates, 1) << "near again: the layer steps on the device (else the test measured nothing)";
}
