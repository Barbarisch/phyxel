// WaterCore 35 (plan A2c) - a pond that slept into the record still wakes after a save and a reload (P7). The slept boxes (32) are
// persisted as world_meta.water_slept (serializeSlept / loadSlept); a load REPLACES the list; a wake that could not read the box's
// chunks keeps the box instead of forgetting it. CPU volumes and an in-memory span store - no device, no editor.
#include <gtest/gtest.h>

#include "core/water/WaterCore.h"
#include "core/water/WaterCoreManager.h"

#include <map>
#include <nlohmann/json.hpp>
#include <vector>

using namespace Phyxel::Core::Water;

namespace {

// the chunk spans, stood in for: what sleep() writes is what a wake reads back; `resident` = false reads nothing (a chunk not loaded)
struct SpanStore {
    std::map<std::pair<int, int>, std::vector<ColumnRun>> cols;
    bool resident = true;
    void install(WaterCoreManager& m) {
        m.setSpanIo([this](const std::vector<ColumnRuns>& in, float, float, long* u, long* c) {
                        for (const auto& col : in) cols[{col.x, col.z}] = col.runs;
                        if (u) *u = 0; if (c) *c = 1; },
                    [this](const glm::ivec3& lo, const glm::ivec3& hi, std::vector<ColumnRuns>& out) {
                        out.clear();
                        if (!resident) return;
                        for (int z = lo.z; z <= hi.z; ++z) for (int x = lo.x; x <= hi.x; ++x) {
                            const auto it = cols.find({x, z});
                            if (it == cols.end() || it->second.empty()) continue;
                            ColumnRuns c; c.x = x; c.z = z; c.runs = it->second; out.push_back(std::move(c));
                        }
                    });
    }
};

// one pond: a 1/9 m CPU volume over x, z 0..3 (floor at y 0), half a metre of water, slept into the store
int sleepPond(WaterCoreManager& m, glm::ivec3 lo, glm::ivec3 hi) {
    std::string err;
    const int id = m.create(lo, hi, 1.0f / 9.0f, "eulerian", &err, "cpu");
    EXPECT_NE(id, 0) << err;
    m.placeBox(glm::ivec3(lo.x, lo.y, lo.z), glm::ivec3(hi.x, lo.y, hi.z), 0.5f, nullptr);
    const WriteBackRecord r = m.sleep(id, true);   // a flat placed layer: forced (the quiet-tick count is not what is tested)
    EXPECT_TRUE(r.ok) << r.error;
    return id;
}

MovingSolid bodyAt(glm::vec3 c) { MovingSolid b; b.centre = c; b.halfExtents = glm::vec3(1.0f / 6.0f); b.velocity = glm::vec3(0, -2, 0); b.id = 1; return b; }

}  // namespace

TEST(WaterSleptPersist, TheBoxesRoundTripBitExact) {
    int occ = 0;
    auto occFn = [&occ](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : occ; };
    WaterCoreManager a(occFn);
    SpanStore st; st.install(a);
    sleepPond(a, {0, 0, 0}, {3, 3, 3});
    sleepPond(a, {-12, 0, -40}, {-9, 2, -37});   // a second pond, negative coordinates
    ASSERT_EQ(a.sleptVolumes().size(), 2u);
    const std::string saved = a.serializeSlept();
    WaterCoreManager b(occFn);
    ASSERT_TRUE(b.loadSlept(saved)) << b.sleptLoadStats().error;
    EXPECT_EQ(b.sleptLoadStats().restored, 2);
    EXPECT_EQ(b.sleptLoadStats().skipped, 0);
    ASSERT_EQ(b.sleptVolumes().size(), 2u);
    for (size_t i = 0; i < 2; ++i) {
        const auto& x = a.sleptVolumes()[i]; const auto& y = b.sleptVolumes()[i];
        EXPECT_EQ(x.lo, y.lo); EXPECT_EQ(x.hi, y.hi); EXPECT_EQ(x.cellSize, y.cellSize);   // bit-exact: 1/9 through JSON
        EXPECT_EQ(x.transport, y.transport); EXPECT_EQ(x.backend, y.backend); EXPECT_EQ(x.sweeps, y.sweeps);
        EXPECT_EQ(x.partialWake, y.partialWake); EXPECT_EQ(x.recordTiles, y.recordTiles); EXPECT_EQ(x.tallBandM, y.tallBandM);
    }
    EXPECT_EQ(b.serializeSlept(), saved) << "a reload, saved again, wrote something else";
    // and the restored box WAKES: a body moving into it - the same box, seeded with the record's water
    st.install(b);
    const auto ids = b.wakeSleptTouching({bodyAt({2.0f, 1.0f, 2.0f})});
    ASSERT_EQ(ids.size(), 1u) << "the restored box did not wake";
    EXPECT_EQ(b.sleptVolumes().size(), 1u);
    EXPECT_NEAR(b.totalMass(), 16.0 * 0.5, 1e-3) << "the woken pond does not hold the record's water";
}

TEST(WaterSleptPersist, ALoadReplacesTheList) {
    auto occFn = [](const glm::ivec3& m) -> int { return m.y < 0 ? 1 : 0; };
    WaterCoreManager m(occFn);
    SpanStore st; st.install(m);
    sleepPond(m, {0, 0, 0}, {3, 3, 3});
    ASSERT_EQ(m.sleptVolumes().size(), 1u);
    // the world switch: a world saved with no slept ponds (no key at all) leaves none of the last world's boxes
    EXPECT_TRUE(m.loadSlept(""));
    EXPECT_TRUE(m.sleptVolumes().empty()) << "the last world's slept box survived loading a world without one";
    sleepPond(m, {0, 0, 0}, {3, 3, 3});
    EXPECT_TRUE(m.loadSlept(R"({"v":1,"boxes":[]})"));
    EXPECT_TRUE(m.sleptVolumes().empty());
    // malformed: none, and false - never a merge with what was there
    sleepPond(m, {0, 0, 0}, {3, 3, 3});
    EXPECT_FALSE(m.loadSlept("{not json"));
    EXPECT_TRUE(m.sleptVolumes().empty());
    EXPECT_FALSE(m.sleptLoadStats().error.empty());
    // an unknown version: nothing taken, its boxes counted as skipped
    EXPECT_FALSE(m.loadSlept(R"({"v":2,"boxes":[{"lo":[0,0,0],"hi":[3,3,3],"cellSize":0.1111111}]})"));
    EXPECT_TRUE(m.sleptVolumes().empty());
    EXPECT_EQ(m.sleptLoadStats().skipped, 1);
}

TEST(WaterSleptPersist, BadBoxesAreSkippedAndCounted) {
    WaterCoreManager m([](const glm::ivec3& v) -> int { return v.y < 0 ? 1 : 0; });
    const std::string j = R"({"v":1,"boxes":[
        {"lo":[0,0,0],"hi":[3,3,3],"cellSize":0.11111111},
        {"lo":[0,0,0],"hi":[3,3,3],"cellSize":0.2},
        {"lo":[5,0,0],"hi":[3,3,3],"cellSize":0.11111111},
        {"lo":[0,0],"hi":[3,3,3],"cellSize":0.11111111},
        {"hi":[3,3,3],"cellSize":1.0}]})";
    EXPECT_TRUE(m.loadSlept(j));
    EXPECT_EQ(m.sleptLoadStats().restored, 1);
    EXPECT_EQ(m.sleptLoadStats().skipped, 4) << "a cell size off the grid, an inverted box, a short corner, a missing corner";
    ASSERT_EQ(m.sleptVolumes().size(), 1u);
    EXPECT_FLOAT_EQ(m.sleptVolumes()[0].cellSize, 1.0f / 9.0f) << "the cell size is snapped to the exact grid fraction";
}

// 35.1 item 2: a wake that could not read its chunks keeps the box (RED before 35: forgotten as "woken empty"); one that read every
// cell and found no water forgets it (the control: the record there is really gone)
TEST(WaterSleptPersist, AWakeThatCannotReadItsChunksKeepsTheBox) {
    int unknown = 0;   // 2 = Unknown occupancy everywhere above the floor (the chunk not resident)
    WaterCoreManager m([&unknown](const glm::ivec3& v) -> int { return v.y < 0 ? 1 : unknown; });
    SpanStore st; st.install(m);
    sleepPond(m, {0, 0, 0}, {3, 3, 3});
    ASSERT_EQ(m.sleptVolumes().size(), 1u);
    st.resident = false; unknown = 2;   // the chunk streamed out (or has not streamed in after the reload)
    EXPECT_TRUE(m.wakeSleptTouching({bodyAt({2.0f, 1.0f, 2.0f})}).empty());
    EXPECT_EQ(m.sleptVolumes().size(), 1u) << "a wake that could not read the box's chunks forgot the pond";
    EXPECT_EQ(m.sleptWakeStats().deferredNotResident, 1);
    EXPECT_EQ(m.sleptWakeStats().wokenEmpty, 0);
    EXPECT_EQ(m.volumeBoxes().size(), 0u) << "the deferred wake left a volume behind";
    // within the retry time: not tried again (a wake is a create - every frame would stall every frame)
    EXPECT_TRUE(m.wakeSleptTouching({bodyAt({2.0f, 1.0f, 2.0f})}).empty());
    EXPECT_EQ(m.sleptWakeStats().deferredNotResident, 1);
    // the control: every cell known, the record really gone -> forgotten
    WaterCoreManager k([](const glm::ivec3& v) -> int { return v.y < 0 ? 1 : 0; });
    SpanStore st2; st2.install(k);
    sleepPond(k, {0, 0, 0}, {3, 3, 3});
    st2.cols.clear();   // drained
    EXPECT_TRUE(k.wakeSleptTouching({bodyAt({2.0f, 1.0f, 2.0f})}).empty());
    EXPECT_TRUE(k.sleptVolumes().empty());
    EXPECT_EQ(k.sleptWakeStats().wokenEmpty, 1);
    EXPECT_EQ(k.sleptWakeStats().deferredNotResident, 0);
}
