#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>

#include "core/LocationRegistry.h"
#include "core/ResidentSpawner.h"

using namespace Phyxel::Core;

// ============================================================================
// ResidentSpawner — residents as a streaming population driven by persisted
// Locations (docs/WorldForge.md follow-up; retires the "residents are not
// persisted / remote residents fall through evicted chunks" gaps). Headless:
// all world effects go through hooks. Red-before-green: these ran RED against
// a stub update() that never spawned.
// ============================================================================

namespace {

Location makeLoc(const std::string& id, LocationType type, float x, float z, float y = 20.0f) {
    Location l;
    l.id = id;
    l.name = id;
    l.type = type;
    l.position = {x, y, z};
    return l;
}

struct Harness {
    LocationRegistry registry;
    ResidentSpawner spawner;
    std::set<std::string> alive;                 // what "the world" currently holds
    std::map<std::string, ResidentPlan> spawned; // every spawn call, by name
    int spawnCalls = 0, despawnCalls = 0;
    bool ready = true;                           // groundReady answer

    Harness() {
        spawner.configure(&registry, nullptr, nullptr);
        ResidentSpawner::Hooks h;
        h.groundReady = [this](const glm::vec3&) { return ready; };
        h.exists = [this](const std::string& n) { return alive.count(n) > 0; };
        h.spawn = [this](const ResidentPlan& p) {
            ++spawnCalls;
            alive.insert(p.name);
            spawned[p.name] = p;
            return true;
        };
        h.despawn = [this](const std::string& n) {
            ++despawnCalls;
            alive.erase(n);
        };
        spawner.setHooks(std::move(h));
        spawner.setEnabled(true);
    }
    void tick() { spawner.update(1.0f); }        // 1 s > any internal throttle
};

}  // namespace

// THE red driver: ready ground + Home/Work/Tavern locations => residents spawn (one per
// location, the ResidentPlanner contract), and the same tick twice never double-spawns.
TEST(ResidentSpawnerTest, SpawnsResidentsWhenGroundReady) {
    Harness h;
    h.registry.addLocation(makeLoc("home_a", LocationType::Home, 10, 10));
    h.registry.addLocation(makeLoc("smithy_a", LocationType::Work, 20, 10));
    h.registry.addLocation(makeLoc("tavern_a", LocationType::Tavern, 30, 10));
    h.tick();
    EXPECT_EQ(h.spawner.activeCount(), 3);
    EXPECT_EQ(h.spawnCalls, 3);
    h.tick();
    EXPECT_EQ(h.spawnCalls, 3) << "steady state must not respawn";
}

// Evicted ground despawns its residents; returning respawns them with identical names.
TEST(ResidentSpawnerTest, DespawnsOnEvictRespawnsOnReturn) {
    Harness h;
    h.registry.addLocation(makeLoc("home_a", LocationType::Home, 10, 10));
    h.tick();
    ASSERT_EQ(h.spawner.activeCount(), 1);
    const std::string name = h.spawned.begin()->first;
    h.ready = false;
    h.tick();
    EXPECT_EQ(h.spawner.activeCount(), 0);
    EXPECT_EQ(h.despawnCalls, 1);
    EXPECT_TRUE(h.alive.empty());
    h.ready = true;
    h.tick();
    EXPECT_EQ(h.spawner.activeCount(), 1);
    EXPECT_TRUE(h.alive.count(name)) << "respawn must reuse the deterministic name";
}

// An NPC with the plan's name that already exists (the settlement build spawned it this
// session) is ADOPTED, not duplicated — and the spawner then owns its evict lifecycle.
TEST(ResidentSpawnerTest, AdoptsBuildSpawnedResidents) {
    Harness h;
    h.registry.addLocation(makeLoc("home_a", LocationType::Home, 10, 10));
    h.alive.insert("res_home_a");   // the build's spawn, named by the ResidentPlanner contract
    h.tick();
    EXPECT_EQ(h.spawnCalls, 0) << "existing resident must not be double-spawned";
    EXPECT_EQ(h.spawner.activeCount(), 1) << "but it must be adopted";
    h.ready = false;
    h.tick();
    EXPECT_EQ(h.despawnCalls, 1) << "adopted resident despawns on evict like any other";
}

// Two settlements plan INDEPENDENTLY: each cluster gets its own innkeeper/tavern instead of
// one global evening target (the failure a whole-registry planResidents pass would produce).
TEST(ResidentSpawnerTest, TwoSettlementsKeepTheirOwnTaverns) {
    Harness h;
    // Settlement A near the origin; settlement B 400 u away (>> kClusterLinkU).
    h.registry.addLocation(makeLoc("home_a", LocationType::Home, 10, 10));
    h.registry.addLocation(makeLoc("tavern_a", LocationType::Tavern, 30, 10));
    h.registry.addLocation(makeLoc("home_b", LocationType::Home, 410, 10));
    h.registry.addLocation(makeLoc("tavern_b", LocationType::Tavern, 430, 10));
    h.tick();
    EXPECT_EQ(h.spawner.activeCount(), 4);
    int innkeepers = 0;
    for (const auto& [name, plan] : h.spawned)
        if (plan.role == "innkeeper") ++innkeepers;
    EXPECT_EQ(innkeepers, 2) << "each settlement must plan its own tavern/innkeeper";
}

// A location that disappears (demolition) orphans its resident -> despawned on next tick.
TEST(ResidentSpawnerTest, OrphanedResidentDespawnsWhenLocationRemoved) {
    Harness h;
    h.registry.addLocation(makeLoc("home_a", LocationType::Home, 10, 10));
    h.tick();
    ASSERT_EQ(h.spawner.activeCount(), 1);
    h.registry.removeLocation("home_a");
    h.tick();
    EXPECT_EQ(h.spawner.activeCount(), 0);
    EXPECT_TRUE(h.alive.empty());
}

// Registry growth is picked up (a new settlement built later gains residents).
TEST(ResidentSpawnerTest, NewLocationsGainResidents) {
    Harness h;
    h.registry.addLocation(makeLoc("home_a", LocationType::Home, 10, 10));
    h.tick();
    ASSERT_EQ(h.spawner.activeCount(), 1);
    h.registry.addLocation(makeLoc("home_b", LocationType::Home, 500, 10));
    h.tick();
    EXPECT_EQ(h.spawner.activeCount(), 2);
}

// Non-resident location types (Market/Wilderness/Custom) spawn nobody.
TEST(ResidentSpawnerTest, NonResidentLocationTypesSpawnNobody) {
    Harness h;
    h.registry.addLocation(makeLoc("market_a", LocationType::Market, 10, 10));
    h.registry.addLocation(makeLoc("wild_a", LocationType::Wilderness, 20, 10));
    h.tick();
    EXPECT_EQ(h.spawner.activeCount(), 0);
    EXPECT_EQ(h.spawnCalls, 0);
}

// The locations themselves round-trip through JSON (the world_meta persistence payload).
TEST(ResidentSpawnerTest, LocationRegistryRoundTripsForPersistence) {
    LocationRegistry a;
    a.addLocation(makeLoc("home_a", LocationType::Home, 10.5f, -22.25f, 33.0f));
    a.addLocation(makeLoc("tavern_a", LocationType::Tavern, 30, 10));
    LocationRegistry b;
    b.fromJson(a.toJson());
    ASSERT_EQ(b.size(), 2u);
    const Location* home = b.getLocation("home_a");
    ASSERT_NE(home, nullptr);
    EXPECT_EQ(home->type, LocationType::Home);
    EXPECT_FLOAT_EQ(home->position.x, 10.5f);
    EXPECT_FLOAT_EQ(home->position.z, -22.25f);
}
