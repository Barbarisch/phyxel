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
