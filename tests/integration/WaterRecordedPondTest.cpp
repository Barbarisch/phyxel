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
