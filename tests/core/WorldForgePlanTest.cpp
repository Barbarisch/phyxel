#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <set>

#include "core/FlowField.h"
#include "core/HydrologyMap.h"
#include "core/WaterBodyIndex.h"
#include "core/WorldForgePlan.h"

using namespace Phyxel;

// ============================================================================
// WorldForgePlan (docs/WorldForge.md M0) — L2 invariants measured on the REAL
// bake output over a synthetic-but-physical terrain fixture (no live engine):
// a tilted plain draining to a west ocean, a dendritic three-level valley
// system (two leaf pairs → two order-2 branches → an order-3 main stem), and a
// closed inland depression that Priority-Flood fills to a lake. Red-before-
// green: these ran RED against the stub bake (0 sites) first.
// ============================================================================

namespace {

// Distance from point (px,pz) to segment (ax,az)-(bx,bz).
float segDist(float px, float pz, float ax, float az, float bx, float bz) {
    const float abx = bx - ax, abz = bz - az;
    const float len2 = abx * abx + abz * abz;
    float t = len2 > 0.0f ? ((px - ax) * abx + (pz - az) * abz) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float dx = px - (ax + abx * t), dz = pz - (az + abz * t);
    return std::sqrt(dx * dx + dz * dz);
}

// The fixture terrain. World box [0,2048]² (64x64 cells at 32 u), sea level 16.
//  • base plane rises eastward: below sea at the west edge (ocean outlet).
//  • dendritic valleys carved as gaussian troughs along hand-placed segments:
//    four leaves → two branches → one main stem toward the ocean. With a small
//    river threshold the confluences yield Strahler order 3 on the main stem.
//  • a closed bowl at (1500, 320) fills to its spill → an inland lake.
float fixtureHeight(float x, float z) {
    float h = 12.0f + 0.02f * x;
    auto valley = [&](float ax, float az, float bx, float bz, float depth, float width) {
        const float d = segDist(x, z, ax, az, bx, bz);
        h -= depth * std::exp(-(d * d) / (width * width));
    };
    // Main stem (the order-3 river) and its two order-2 branches + four leaves.
    valley(0.0f, 1024.0f, 1200.0f, 1024.0f, 14.0f, 180.0f);
    valley(1200.0f, 1024.0f, 1800.0f, 700.0f, 10.0f, 120.0f);
    valley(1200.0f, 1024.0f, 1800.0f, 1350.0f, 10.0f, 120.0f);
    valley(1800.0f, 700.0f, 2040.0f, 500.0f, 7.0f, 90.0f);
    valley(1800.0f, 700.0f, 2040.0f, 900.0f, 7.0f, 90.0f);
    valley(1800.0f, 1350.0f, 2040.0f, 1150.0f, 7.0f, 90.0f);
    valley(1800.0f, 1350.0f, 2040.0f, 1550.0f, 7.0f, 90.0f);
    // Closed lake bowl (no outlet below its rim → fills to spill).
    {
        const float dx = x - 1500.0f, dz = z - 320.0f;
        const float d2 = dx * dx + dz * dz;
        h -= 12.0f * std::exp(-d2 / (150.0f * 150.0f));
    }
    return h;
}

struct Fixture {
    static constexpr float kOrigin = 0.0f;
    static constexpr int kCells = 64;
    static constexpr float kCell = 32.0f;
    static constexpr float kSea = 16.0f;
    static constexpr int kRiverThreshold = 12;

    HydrologyMap hydro;
    FlowField flow;
    WaterBodyIndex bodies;

    Fixture()
        : hydro(fixtureHeight, kOrigin, kOrigin, kCells, kCells, kCell, kSea),
          flow(fixtureHeight, kOrigin, kOrigin, kCells, kCells, kCell, kSea, kRiverThreshold),
          bodies(hydro, fixtureHeight) {}

    std::shared_ptr<const WorldForgePlan> bake(const WorldForgeParams& p,
                                               uint32_t seed = 777001) const {
        return WorldForgePlan::bake(p, seed, fixtureHeight, hydro, flow, bodies,
                                    [](int, int) { return std::string("Grass"); });
    }

    static WorldForgeParams defaultParams() {
        WorldForgeParams p;
        p.enabled = true;
        p.siteCount = 5;
        p.regionRadius = 900.0f;   // region centre (1024,1024); keeps sites inside the box
        p.minSpacing = 300.0f;
        p.maxSpacing = 1200.0f;
        return p;
    }
};

}  // namespace

// Fixture self-check: the hand-built terrain must actually produce the hydrology the
// invariants lean on (an ocean outlet, an inland lake, an order-3 river). If this fails
// the terrain needs retuning — the plan is not under test here.
TEST(WorldForgePlanTest, FixtureTerrainIsHydrologicallyViable) {
    Fixture f;
    EXPECT_TRUE(f.hydro.hasOutlet()) << "west edge must dip below sea level";
    EXPECT_GE(f.flow.maxOrder(), 3) << "dendritic valleys must yield an order-3 main stem";
    bool hasInlandLake = false;
    for (const auto& b : f.bodies.bodies())
        if (b.cls != WaterBodyIndex::Class::Ocean && b.areaCells >= 3) hasInlandLake = true;
    EXPECT_TRUE(hasInlandLake) << "the closed bowl must fill to an inland lake";
}

// THE red driver: a viable region must yield exactly siteCount sites, each with a
// derived nonzero seed and a tier preset footprint.
TEST(WorldForgePlanTest, ViableRegionYieldsRequestedSiteCount) {
    Fixture f;
    auto plan = f.bake(Fixture::defaultParams());
    ASSERT_EQ(plan->sites().size(), 5u);
    for (const auto& s : plan->sites()) {
        EXPECT_NE(s.seed, 0u);
        EXPECT_GT(s.width, 0);
        EXPECT_GT(s.depth, 0);
        EXPECT_TRUE(s.tier == "hamlet" || s.tier == "village" || s.tier == "town");
    }
}

// Tier mix: best-scored site is the town, next two are villages, the rest hamlets.
TEST(WorldForgePlanTest, TierMixIsOneTownTwoVillages) {
    Fixture f;
    auto plan = f.bake(Fixture::defaultParams());
    ASSERT_EQ(plan->sites().size(), 5u);
    int towns = 0, villages = 0, hamlets = 0;
    for (const auto& s : plan->sites()) {
        if (s.tier == "town") ++towns;
        else if (s.tier == "village") ++villages;
        else ++hamlets;
    }
    EXPECT_EQ(towns, 1);
    EXPECT_EQ(villages, 2);
    EXPECT_EQ(hamlets, 2);
}

// No site sits in water: not on a wet bake cell, and its centre is above sea level.
TEST(WorldForgePlanTest, SitesNotInWater) {
    Fixture f;
    auto plan = f.bake(Fixture::defaultParams());
    ASSERT_FALSE(plan->sites().empty());
    for (const auto& s : plan->sites()) {
        const float h = fixtureHeight(static_cast<float>(s.pos.x), static_cast<float>(s.pos.y));
        EXPECT_GT(h, Fixture::kSea) << "site " << s.id << " below sea level";
        const float wl = f.hydro.waterLevelAt(static_cast<float>(s.pos.x),
                                              static_cast<float>(s.pos.y));
        EXPECT_LT(wl, h + 0.01f) << "site " << s.id << " sits on a wet bake cell";
    }
}

TEST(WorldForgePlanTest, SitesRespectMinSpacing) {
    Fixture f;
    auto plan = f.bake(Fixture::defaultParams());
    const auto& sites = plan->sites();
    ASSERT_GE(sites.size(), 2u);
    for (size_t i = 0; i < sites.size(); ++i)
        for (size_t j = i + 1; j < sites.size(); ++j) {
            const glm::vec2 d = glm::vec2(sites[i].pos - sites[j].pos);
            EXPECT_GE(glm::length(d), 300.0f)
                << "sites " << sites[i].id << " and " << sites[j].id << " too close";
        }
}

TEST(WorldForgePlanTest, SitesWithinRegion) {
    Fixture f;
    auto plan = f.bake(Fixture::defaultParams());
    const glm::vec2 centre(Fixture::kOrigin + Fixture::kCells * Fixture::kCell * 0.5f,
                           Fixture::kOrigin + Fixture::kCells * Fixture::kCell * 0.5f);
    ASSERT_FALSE(plan->sites().empty());
    for (const auto& s : plan->sites())
        // Half a bake cell of slack: candidates are cell CENTRES, refined within their cell.
        EXPECT_LE(glm::length(glm::vec2(s.pos) - centre), 900.0f + Fixture::kCell)
            << "site " << s.id << " outside the region radius";
}

// Boundary clamp is echoed (FeatureDesignKeys API rule): siteCount 20 → 8.
TEST(WorldForgePlanTest, SiteCountClamped) {
    Fixture f;
    WorldForgeParams p = Fixture::defaultParams();
    p.siteCount = 20;
    auto plan = f.bake(p);
    EXPECT_EQ(plan->params().siteCount, 8);
    EXPECT_LE(plan->sites().size(), 8u);
    EXPECT_GE(plan->sites().size(), 3u);
}

// Every site is reachable from every other over the road graph (union-find).
TEST(WorldForgePlanTest, RoadsConnectAllSites) {
    Fixture f;
    auto plan = f.bake(Fixture::defaultParams());
    const auto& sites = plan->sites();
    ASSERT_GE(sites.size(), 2u);
    ASSERT_FALSE(plan->roads().empty());
    std::vector<int> parent(sites.size());
    for (size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<int>(i);
    std::function<int(int)> find = [&](int v) { return parent[v] == v ? v : parent[v] = find(parent[v]); };
    for (const auto& r : plan->roads()) {
        ASSERT_GE(r.a, 0); ASSERT_LT(r.a, static_cast<int>(sites.size()));
        ASSERT_GE(r.b, 0); ASSERT_LT(r.b, static_cast<int>(sites.size()));
        parent[find(r.a)] = find(r.b);
    }
    for (size_t i = 1; i < sites.size(); ++i)
        EXPECT_EQ(find(0), find(static_cast<int>(i))) << "site " << sites[i].id << " disconnected";
}

// Road centerlines never run through standing water (lakes/ocean are impassable to the
// router; rivers are crossed, not followed — river cells are dry in the hydrology map).
TEST(WorldForgePlanTest, RoadsAvoidStandingWater) {
    Fixture f;
    auto plan = f.bake(Fixture::defaultParams());
    ASSERT_FALSE(plan->roads().empty());
    for (const auto& r : plan->roads())
        for (const auto& pt : r.centerline) {
            const float h = fixtureHeight(pt.x, pt.y);
            const float wl = f.hydro.waterLevelAt(pt.x, pt.y);
            EXPECT_LT(wl, h + 0.5f) << "road " << r.a << "-" << r.b
                                    << " runs through standing water at (" << pt.x << "," << pt.y << ")";
        }
}

// Roads are continuous: adjacent centerline points at most ~24 u apart (16 u resample + slack).
TEST(WorldForgePlanTest, RoadCenterlinesContinuous) {
    Fixture f;
    auto plan = f.bake(Fixture::defaultParams());
    ASSERT_FALSE(plan->roads().empty());
    for (const auto& r : plan->roads()) {
        ASSERT_GE(r.centerline.size(), 2u);
        for (size_t i = 1; i < r.centerline.size(); ++i)
            EXPECT_LE(glm::length(r.centerline[i] - r.centerline[i - 1]), 24.0f);
    }
}

// Where a centerline crosses the order-3 main stem, a crossing is recorded. Two pinned
// sites tight against the stem at x=1000 — the valley floor there is above sea level
// (west of x~900 the stem valley is a flooded estuary and roads can't enter), and the
// 150 u direct hop is decisively cheaper than the ~1 km headwater detour — so the
// crossing is deterministic, not router-mood-dependent.
TEST(WorldForgePlanTest, CrossingsMarkedWhereOrder3) {
    Fixture f;
    WorldForgeParams p = Fixture::defaultParams();
    p.siteCount = 3;
    p.sitePins = {{1000, 950}, {1000, 1100}};
    auto plan = f.bake(p);
    ASSERT_GE(plan->sites().size(), 2u);
    // Find the road between the two pinned sites (ids 0 and 1 — pins seat first).
    bool foundRoad = false, foundCrossing = false;
    for (const auto& r : plan->roads()) {
        if (!((r.a == 0 && r.b == 1) || (r.a == 1 && r.b == 0))) continue;
        foundRoad = true;
        for (const auto& c : r.crossings)
            if (c.riverOrder >= 3) foundCrossing = true;
    }
    // The pinned pair straddles the stem; even if the MST links them via a third site,
    // SOME road must cross the stem — fall back to scanning all roads.
    if (!foundRoad || !foundCrossing)
        for (const auto& r : plan->roads())
            for (const auto& c : r.crossings)
                if (c.riverOrder >= 3) foundCrossing = true;
    EXPECT_TRUE(foundCrossing) << "no order>=3 crossing recorded on any road";
}

// Pins are seated verbatim, first, with ids 0..n-1.
TEST(WorldForgePlanTest, SitePinsSeatedVerbatim) {
    Fixture f;
    WorldForgeParams p = Fixture::defaultParams();
    p.siteCount = 4;
    p.sitePins = {{800, 800}};
    auto plan = f.bake(p);
    ASSERT_GE(plan->sites().size(), 1u);
    EXPECT_EQ(plan->sites()[0].pos, glm::ivec2(800, 800));
    EXPECT_EQ(plan->sites()[0].id, 0);
}

// roadAt: a point ON a centerline reports its class within half-width; a far point misses.
TEST(WorldForgePlanTest, RoadAtHitsCenterlineMissesFar) {
    Fixture f;
    auto plan = f.bake(Fixture::defaultParams());
    ASSERT_FALSE(plan->roads().empty());
    const auto& r = plan->roads()[0];
    ASSERT_FALSE(r.centerline.empty());
    const glm::vec2 mid = r.centerline[r.centerline.size() / 2];
    const auto hit = plan->roadAt(mid.x, mid.y);
    EXPECT_EQ(hit.cls, r.cls);
    EXPECT_LE(hit.dist, 1.0f);
    const auto miss = plan->roadAt(Fixture::kOrigin - 500.0f, Fixture::kOrigin - 500.0f);
    EXPECT_EQ(miss.cls, 0);
}

// roadAt is the per-column generation hook — it must stay O(1). 1M queries over the road
// bbox in < 3 s (Debug slack on the ~0.5 us/query budget; an accidental O(segments) scan
// would take minutes).
TEST(WorldForgePlanTest, RoadQueryPerf) {
    Fixture f;
    auto plan = f.bake(Fixture::defaultParams());
    ASSERT_FALSE(plan->roads().empty());
    const auto t0 = std::chrono::steady_clock::now();
    volatile int acc = 0;
    for (int i = 0; i < 1000; ++i)
        for (int j = 0; j < 1000; ++j)
            acc += plan->roadAt(200.0f + i * 1.6f, 200.0f + j * 1.6f).cls;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    EXPECT_LT(ms, 3000) << "1M roadAt queries took " << ms << " ms";
}

// Byte-identical determinism: two independent bakes over freshly built inputs.
// There is NO process-wide plan cache (deliberate — see WorldForgePlan::bake), so this
// comparison is genuine, not a shared-pointer tautology.
TEST(WorldForgePlanTest, PlanDeterminism) {
    Fixture f1, f2;
    auto a = f1.bake(Fixture::defaultParams());
    auto b = f2.bake(Fixture::defaultParams());
    ASSERT_FALSE(a->sites().empty());
    EXPECT_EQ(a->toJson().dump(), b->toJson().dump());
    EXPECT_EQ(a->planHash(), b->planHash());
}

// Site seeds: stable across bakes, distinct across sites, derived from the world seed.
TEST(WorldForgePlanTest, SiteSeedDerivation) {
    Fixture f;
    auto a = f.bake(Fixture::defaultParams(), 777001);
    auto b = f.bake(Fixture::defaultParams(), 777001);
    auto c = f.bake(Fixture::defaultParams(), 999);
    ASSERT_FALSE(a->sites().empty());
    std::set<uint32_t> seeds;
    for (size_t i = 0; i < a->sites().size(); ++i) {
        EXPECT_EQ(a->sites()[i].seed, b->sites()[i].seed);
        seeds.insert(a->sites()[i].seed);
    }
    EXPECT_EQ(seeds.size(), a->sites().size()) << "site seeds must be distinct";
    // A different world seed at the same positions must derive different seeds.
    ASSERT_EQ(a->sites().size(), c->sites().size());
    bool anyDiffer = false;
    for (size_t i = 0; i < a->sites().size(); ++i)
        if (a->sites()[i].seed != c->sites()[i].seed) anyDiffer = true;
    EXPECT_TRUE(anyDiffer);
}

// An unviable region degrades honestly: a bake over open ocean yields zero sites (and a
// valid, empty plan) — never an invented site in the water.
TEST(WorldForgePlanTest, UnviableRegionYieldsFewerSitesHonestly) {
    // All-ocean terrain: everything below sea level.
    auto ocean = [](float, float) { return 4.0f; };
    HydrologyMap hydro(ocean, 0.0f, 0.0f, 32, 32, 32.0f, 16.0f);
    FlowField flow(ocean, 0.0f, 0.0f, 32, 32, 32.0f, 16.0f, 12);
    WaterBodyIndex bodies(hydro, ocean);
    WorldForgeParams p;
    p.enabled = true;
    p.siteCount = 5;
    p.regionRadius = 512.0f;
    auto plan = WorldForgePlan::bake(p, 1, ocean, hydro, flow, bodies,
                                     [](int, int) { return std::string("Sand"); });
    EXPECT_TRUE(plan->sites().empty());
    EXPECT_TRUE(plan->roads().empty());
}
