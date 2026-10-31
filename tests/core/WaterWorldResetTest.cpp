// WaterCore 47 (plan A2i) - the water core across world changes. A scene transition, a project switch or an editor scene attaches
// another world's storage; nothing of the last world's water may stay (volumes, slept boxes, body rows, droplets, wakes, the record's
// caches), and the world left must keep its live water (written back before its chunks are saved). CPU volumes and an in-memory
// span store per world - no device, no editor. Red shown with resetWorld()'s body emptied (every count below stayed at the last
// world's).
#include <gtest/gtest.h>

#include "core/water/WaterCore.h"
#include "core/water/WaterCoreManager.h"

#include <map>
#include <string>
#include <vector>

using namespace Phyxel::Core::Water;

namespace {

// one world's chunk spans: what sleep() writes is what a wake reads back
struct World {
    std::map<std::pair<int, int>, std::vector<ColumnRun>> cols;
    std::string bodies, slept;   // its world_meta
    void install(WaterCoreManager& m) {
        m.setSpanIo([this](const std::vector<ColumnRuns>& in, float, float, long* u, long* c) {
                        for (const auto& col : in) cols[{col.x, col.z}] = col.runs;
                        if (u) *u = 0; if (c) *c = 1; },
                    [this](const glm::ivec3& lo, const glm::ivec3& hi, std::vector<ColumnRuns>& out) {
                        out.clear();
                        for (int z = lo.z; z <= hi.z; ++z) for (int x = lo.x; x <= hi.x; ++x) {
                            const auto it = cols.find({x, z});
                            if (it == cols.end() || it->second.empty()) continue;
                            ColumnRuns c; c.x = x; c.z = z; c.runs = it->second; out.push_back(std::move(c));
                        }
                    });
    }
    double stored() const { double m = 0.0; for (const auto& [k, runs] : cols) for (const auto& r : runs) m += r.topY - r.bottomY; return m; }
};

int pond(WaterCoreManager& m, glm::ivec3 lo, glm::ivec3 hi, float depth) {
    std::string err;
    const int id = m.create(lo, hi, 1.0f / 9.0f, "eulerian", &err, "cpu");
    EXPECT_NE(id, 0) << err;
    m.placeBox(lo, glm::ivec3(hi.x, lo.y, hi.z), depth, nullptr);
    return id;
}

MovingSolid bodyAt(glm::vec3 c) { MovingSolid b; b.centre = c; b.halfExtents = glm::vec3(1.0f / 6.0f); b.velocity = glm::vec3(0, -2, 0); b.id = 1; return b; }

// the editor's sequence (Application::waterWorldLeaving / waterWorldChanged): write back, save the records, reset, attach, restore
void leave(WaterCoreManager& m, World& w) {
    m.writeBackAll();
    w.bodies = m.bodies().serialize(); w.slept = m.serializeSlept();
}
void enter(WaterCoreManager& m, World& w) {
    m.resetWorld();
    w.install(m);
    if (!w.bodies.empty()) EXPECT_TRUE(m.bodies().load(w.bodies));
    EXPECT_TRUE(m.loadSlept(w.slept));
}

auto occFn = [](const glm::ivec3& v) -> int { return v.y < 0 ? 1 : 0; };

}  // namespace

TEST(WaterWorldReset, NothingOfTheLastWorldStays) {
    WaterCoreManager m(occFn);
    World a; a.install(m);
    const int slept = pond(m, {0, 0, 0}, {3, 3, 3}, 0.5f);
    ASSERT_TRUE(m.sleep(slept, true).ok);
    const int awake = pond(m, {10, 0, 10}, {13, 3, 13}, 0.5f);
    ASSERT_EQ(m.list().size(), 1u);
    ASSERT_EQ(m.sleptVolumes().size(), 1u);
    ASSERT_GT(m.bodies().size(), 0u);
    ASSERT_GT(m.totalMass(), 7.9);
    const auto r = m.resetWorld();
    EXPECT_EQ(r.volumes, 1); EXPECT_EQ(r.slept, 1); EXPECT_GT(r.bodies, 0);
    EXPECT_TRUE(m.list().empty()) << "a live volume of the last world survived";
    EXPECT_TRUE(m.sleptVolumes().empty()) << "a slept box of the last world survived";
    EXPECT_EQ(m.bodies().size(), 0u) << "body rows of the last world survived";
    EXPECT_EQ(m.totalMass(), 0.0);
    EXPECT_EQ(m.dropletStats().alive, 0);
    EXPECT_EQ(m.pacedStats().building, 0);
    EXPECT_EQ(m.worldResets(), 1);
    // the core still works, and ids keep counting (an id from the last world never names a new volume)
    const int next = pond(m, {0, 0, 0}, {3, 3, 3}, 0.25f);
    EXPECT_GT(next, awake);
    EXPECT_NEAR(m.totalMass(), 16.0 * 0.25, 1e-3);
}

TEST(WaterWorldReset, ASceneLeftAwakeKeepsItsWaterAndComesBack) {
    WaterCoreManager m(occFn);
    World a, b;
    a.install(m);
    const int id = pond(m, {0, 0, 0}, {3, 3, 3}, 0.5f);   // awake: never slept
    const double held = m.totalMass();
    ASSERT_NEAR(held, 8.0, 1e-3);
    // A -> B
    leave(m, a);
    EXPECT_NEAR(a.stored(), held, 1e-4) << "the awake pond's water did not reach A's record";
    enter(m, b);
    EXPECT_TRUE(m.list().empty());
    EXPECT_TRUE(m.sleptVolumes().empty()) << "A's slept box is in B";
    EXPECT_EQ(m.bodies().size(), 0u) << "A's body rows are in B (B has no water_bodies key)";
    EXPECT_TRUE(m.wakeSleptTouching({bodyAt({2.0f, 1.0f, 2.0f})}).empty()) << "a body in B woke A's pond";
    // B -> A
    leave(m, b);
    EXPECT_EQ(b.stored(), 0.0) << "B's record holds water";
    enter(m, a);
    ASSERT_EQ(m.sleptVolumes().size(), 1u) << "A's slept box was not restored";
    EXPECT_GT(m.bodies().size(), 0u);
    double rows = 0.0; for (const auto& r : m.bodies().records()) if (r.origin == "av") rows += r.mass;
    EXPECT_NEAR(rows, held, 1e-4) << "A's body rows do not hold its water";
    const auto ids = m.wakeSleptTouching({bodyAt({2.0f, 1.0f, 2.0f})});
    ASSERT_EQ(ids.size(), 1u) << "A's pond did not wake after the round trip";
    EXPECT_NE(ids[0], id);
    EXPECT_NEAR(m.totalMass(), held, 1e-4) << "A's pond came back with other water";
}

TEST(WaterWorldReset, TheRedWithoutTheResetTheLastWorldLeaks) {
    // the control: the same A -> B as above with resetWorld() a no-op (before 47) - the checks above must see the leak
    struct Off { Off() { WaterCoreManager::s_resetWorldOffForTest = true; } ~Off() { WaterCoreManager::s_resetWorldOffForTest = false; } } off;
    WaterCoreManager m(occFn);
    World a, b;
    a.install(m);
    pond(m, {0, 0, 0}, {3, 3, 3}, 0.5f);
    const int slept = pond(m, {10, 0, 10}, {13, 3, 13}, 0.5f);
    ASSERT_TRUE(m.sleep(slept, true).ok);
    m.resetWorld();   // what B's load did before 47: nothing
    b.install(m);
    EXPECT_EQ(m.list().size(), 1u) << "the red did not reproduce: A's live volume should be in B";
    EXPECT_EQ(m.sleptVolumes().size(), 1u) << "the red did not reproduce: A's slept box should be in B";
    EXPECT_GT(m.bodies().size(), 0u);
    EXPECT_EQ(m.worldResets(), 0);
}

TEST(WaterWorldReset, HeldColumnsAreReportedNotSilentlyLost) {
    // a column of the box reads Unknown (its chunk not resident): a forced write-back cannot write it, and says how much went
    // the pond fills while everything is known; then voxel column (3, 0) goes Unknown (its chunk unloaded) - the pack revision moves
    bool gone = false; uint64_t rev = 1;
    auto occ = [&gone](const glm::ivec3& v) -> int { return v.y < 0 ? 1 : ((gone && v.x >= 27 && v.z < 9) ? 2 : 0); };
    WaterCoreManager m(occ, [&rev]() { return rev; });
    World a; a.install(m);
    pond(m, {0, 0, 0}, {3, 3, 3}, 0.5f);
    gone = true; ++rev;
    const auto wb = m.writeBackAll();
    EXPECT_EQ(wb.volumes, 1);
    EXPECT_GT(wb.heldLostM3, 0.0) << "the held column's water vanished without a report";
    EXPECT_NEAR(wb.storedM3 + wb.heldLostM3, 8.0, 0.6) << "stored + lost is not what the pond held";
}
