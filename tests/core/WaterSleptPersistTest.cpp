// WaterCore 35 (plan A2c) - a pond that slept into the record still wakes after a save and a reload (P7). The slept boxes (32) are
// persisted as world_meta.water_slept (serializeSlept / loadSlept); a load REPLACES the list; a wake that could not read the box's
// chunks keeps the box instead of forgetting it. CPU volumes and an in-memory span store - no device, no editor.
#include <gtest/gtest.h>

#include "core/water/WaterCore.h"
#include "core/water/WaterCoreManager.h"

#include <chrono>
#include <cstdio>
#include <map>
#include <string>
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

// WaterCore 36 (plan A2d, 32.2 item 4): the wake-on-touch default is a pinned contract. Red shown with the constant flipped to false.
TEST(WaterWakeDefault, TouchingASleptPondWakesItByDefault) {
    EXPECT_TRUE(WaterCoreManager::kWakeOnTouchByDefault) << "the shipped default is not 'touching wakes' (WaterCore 32/36)";
    EXPECT_EQ(WaterCoreManager::s_wakeOnTouch, WaterCoreManager::kWakeOnTouchByDefault) << "a test left wake_on_touch changed";
    // behaviour, with no setting touched: outside the box nothing wakes (the control), inside it wakes holding the record's water
    WaterCoreManager m([](const glm::ivec3& v) -> int { return v.y < 0 ? 1 : 0; });
    SpanStore st; st.install(m);
    sleepPond(m, {0, 0, 0}, {3, 3, 3});
    EXPECT_TRUE(m.wakeSleptTouching({bodyAt({20.0f, 1.0f, 20.0f})}).empty()) << "a body outside the box woke the pond";
    EXPECT_EQ(m.wakeSleptTouching({bodyAt({2.0f, 1.0f, 2.0f})}).size(), 1u) << "a body moving into a slept pond did not wake it";
    EXPECT_NEAR(m.totalMass(), 16.0 * 0.5, 1e-3);
}

// 39.4 (the step-2 look preview): with the test delay a touched pond does not wake at once, wakes once the delay has passed even with
// its body gone, and 0 restores the shipped wake. The delay is reset by a scope guard - it never leaks into another test.
TEST(WaterWakeDelay, ADelayedWakeFiresAfterTheDelayEvenWithTheBodyGone) {
    struct Restore { ~Restore() { WaterCoreManager::s_wakeDelaySForTest = 0.0; } } restore;
    WaterCoreManager m([](const glm::ivec3& v) -> int { return v.y < 0 ? 1 : 0; });
    SpanStore st; st.install(m);
    sleepPond(m, {0, 0, 0}, {3, 3, 3});
    WaterCoreManager::s_wakeDelaySForTest = 0.05;
    EXPECT_TRUE(m.wakeSleptTouching({bodyAt({2.0f, 1.0f, 2.0f})}).empty()) << "the delayed wake fired at the first touch";
    EXPECT_EQ(m.sleptVolumes().size(), 1u);
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(60)) {}
    EXPECT_EQ(m.wakeSleptTouching({}).size(), 1u) << "the delayed wake did not fire once the delay had passed";
    EXPECT_NEAR(m.totalMass(), 16.0 * 0.5, 1e-3);
    // the control: an untouched pond never wakes under the delay
    WaterCoreManager k([](const glm::ivec3& v) -> int { return v.y < 0 ? 1 : 0; });
    SpanStore st2; st2.install(k);
    sleepPond(k, {0, 0, 0}, {3, 3, 3});
    EXPECT_TRUE(k.wakeSleptTouching({}).empty());
    EXPECT_EQ(k.sleptVolumes().size(), 1u);
}

// 40.1 item 7 (L5's red): the test-only far-dry switch - off (the shipped state) a pond wakes wherever the viewer is; on, a pond
// beyond the record's drawn reach never wakes, one within it still does. Reset by a scope guard.
TEST(WaterFarRecordDry, OffItChangesNothingOnAFarPondNeverWakes) {
    struct Restore { ~Restore() { WaterCoreManager::s_testFarRecordDry = false; } } restore;
    EXPECT_FALSE(WaterCoreManager::s_testFarRecordDry) << "a test left the far-dry switch on";
    auto occFn = [](const glm::ivec3& v) -> int { return v.y < 0 ? 1 : 0; };
    {   // off, the viewer 200 m away: the touch wakes it (locality never decides an outcome)
        WaterCoreManager m(occFn); SpanStore st; st.install(m);
        sleepPond(m, {0, 0, 0}, {3, 3, 3});
        m.setViewPoint(glm::vec3(200.0f, 1.0f, 0.0f));
        EXPECT_FALSE(m.farRecordDryForTest(glm::vec2(0.0f), glm::vec2(4.0f)));
        EXPECT_EQ(m.wakeSleptTouching({bodyAt({2.0f, 1.0f, 2.0f})}).size(), 1u) << "with the switch off a far pond did not wake";
    }
    WaterCoreManager::s_testFarRecordDry = true;
    {   // on, far: no wake
        WaterCoreManager m(occFn); SpanStore st; st.install(m);
        sleepPond(m, {0, 0, 0}, {3, 3, 3});
        m.setViewPoint(glm::vec3(200.0f, 1.0f, 0.0f));
        EXPECT_TRUE(m.farRecordDryForTest(glm::vec2(0.0f), glm::vec2(4.0f)));
        EXPECT_TRUE(m.wakeSleptTouching({bodyAt({2.0f, 1.0f, 2.0f})}).empty()) << "the switch on, a far pond woke";
        EXPECT_EQ(m.sleptVolumes().size(), 1u);
    }
    {   // on, near (the control): it wakes
        WaterCoreManager m(occFn); SpanStore st; st.install(m);
        sleepPond(m, {0, 0, 0}, {3, 3, 3});
        m.setViewPoint(glm::vec3(10.0f, 1.0f, 2.0f));
        EXPECT_FALSE(m.farRecordDryForTest(glm::vec2(0.0f), glm::vec2(4.0f)));
        EXPECT_EQ(m.wakeSleptTouching({bodyAt({2.0f, 1.0f, 2.0f})}).size(), 1u) << "the switch on, a near pond did not wake";
    }
}

// 39 (A2f step 1): the cheaper wake must make the SAME volume, cell for cell. The fingerprints below were taken from the code before
// step 1 (2026-10-26) - any change to what a create or a wake produces turns them red. The world: a floor, scattered solid micro
// cells and an Unknown patch (the hold boundary), read at 1/9 m (one sample a cell) and 1/3 m (the 3x3x3 lattice).
namespace {
int patternState(const glm::ivec3& m) {
    if (m.y < 0) return 1;
    const uint32_t hsh = static_cast<uint32_t>(m.x * 73856093) ^ static_cast<uint32_t>(m.y * 19349663) ^ static_cast<uint32_t>(m.z * 83492791);
    if (hsh % 7u == 0u) return 1;
    if (m.x >= 30 && m.x < 36 && m.z >= 30 && m.z < 36 && m.y >= 18) return 2;
    return 0;
}
uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const auto* b = static_cast<const unsigned char*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    return h;
}
uint64_t gridPrint(const WaterGrid& cg) {
    auto& g = const_cast<WaterGrid&>(cg);
    uint64_t h = 1469598103934665603ull;
    for (int z = 0; z < g.nz(); ++z) for (int y = 0; y < g.ny(); ++y) for (int x = 0; x < g.nx(); ++x) {
        const Occ o = g.occ(x, y, z); h = fnv(h, &o, sizeof o); }
    h = fnv(h, g.fData().data(), g.fData().size() * sizeof(float));
    h = fnv(h, g.uData().data(), g.uData().size() * sizeof(float));
    h = fnv(h, g.vData().data(), g.vData().size() * sizeof(float));
    h = fnv(h, g.wData().data(), g.wData().size() * sizeof(float));
    return h;
}
}  // namespace

TEST(WaterWakeSame, ACreateAndAWakeMakeTheSameVolumeCellForCell) {
    const std::map<std::string, uint64_t> want = {{"create_1/9", 0x74fccb2bfd545cf6ull}, {"create_1/3", 0xf2184c12c831cd32ull},
                                                  {"wake_1/9", 0x47f942147d559e17ull}};
    for (const int threads : {1, 4}) {   // step 1b: one thread, and the read split over four (patternState only reads)
    SCOPED_TRACE("occupancy threads " + std::to_string(threads));
    std::map<std::string, uint64_t> got;
    for (const float h : {1.0f / 9.0f, 1.0f / 3.0f}) {
        WaterCoreManager m(patternState);
        m.setStateQueryThreadSafe(threads > 1, threads);
        std::string err;
        const int id = m.create({0, 0, 0}, {5, 3, 5}, h, "eulerian", &err, "cpu");
        ASSERT_NE(id, 0) << err;
        got[h < 0.2f ? "create_1/9" : "create_1/3"] = gridPrint(*m.gridForTest(id));
    }
    {   // a wake: slept into the record and woken from it (create + the seed from the spans)
        WaterCoreManager m(patternState);
        m.setStateQueryThreadSafe(threads > 1, threads);
        SpanStore st; st.install(m);
        sleepPond(m, {0, 0, 0}, {3, 3, 3});
        const auto ids = m.wakeSleptTouching({bodyAt({2.0f, 1.0f, 2.0f})});
        ASSERT_EQ(ids.size(), 1u);
        got["wake_1/9"] = gridPrint(*m.gridForTest(ids[0]));
    }
    for (const auto& [k, v] : got) {
        std::printf("  threads %d  %s = 0x%016llxull\n", threads, k.c_str(), static_cast<unsigned long long>(v));
        EXPECT_EQ(v, want.at(k)) << k << ": the volume differs from the one the code before step 1 made";
    }
    }
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
