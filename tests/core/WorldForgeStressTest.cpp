#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <iostream>

#include "core/WorldForgePlan.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

using namespace Phyxel;

// ============================================================================
// WorldForge M3 — the STRESS axis (docs/WorldForge.md; CLAUDE.md stress-test
// phase). The invariants proven at N=3 must hold at the V1 extremes: max site
// count (8) over the max-ish region, hostile mountain terrain (honest
// degradation, never an invented site), and the road WALKABILITY profile
// measured on real generated columns (L3-flavored geometry, not screenshots).
// ============================================================================

namespace {

WorldGenerator makeForgeWorld(WorldGenerator::GenerationType type, uint32_t seed,
                              int siteCount, float regionRadius, float minSpacing) {
    WorldGenerator gen(type, seed);
    WorldRecipe r = gen.makeRecipe();
    r.worldforge.enabled = true;
    r.worldforge.siteCount = siteCount;
    r.worldforge.regionRadius = regionRadius;
    r.worldforge.minSpacing = minSpacing;
    gen.applyRecipe(r);
    return gen;
}

void assertPlanInvariants(const WorldForgePlan& plan, float minSpacing) {
    // Every site dry + spaced; every road connected component spans all sites.
    const auto& sites = plan.sites();
    for (size_t i = 0; i < sites.size(); ++i)
        for (size_t j = i + 1; j < sites.size(); ++j)
            EXPECT_GE(glm::length(glm::vec2(sites[i].pos - sites[j].pos)), minSpacing)
                << "sites " << sites[i].id << "/" << sites[j].id << " too close at scale";
    if (sites.size() >= 2 && !plan.roads().empty()) {
        std::vector<int> parent(sites.size());
        for (size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<int>(i);
        std::function<int(int)> find = [&](int v) {
            return parent[v] == v ? v : parent[v] = find(parent[v]);
        };
        for (const auto& r : plan.roads()) parent[find(r.a)] = find(r.b);
        for (size_t i = 1; i < sites.size(); ++i)
            EXPECT_EQ(find(0), find(static_cast<int>(i)))
                << "site " << sites[i].id << " disconnected at scale";
    }
}

}  // namespace

// Count/extent extreme: 8 sites over a 2 km radius. The invariants must hold for EVERY
// pair/site, not just in aggregate (the 10-story-tower lesson).
TEST(WorldForgeStressTest, EightSitesOverFullRegion) {
    WorldGenerator gen = makeForgeWorld(WorldGenerator::GenerationType::Perlin, 20260816,
                                        8, 2048.0f, 400.0f);
    ASSERT_NE(gen.worldForge(), nullptr);
    const WorldForgePlan& plan = *gen.worldForge();
    EXPECT_EQ(plan.params().siteCount, 8);
    // The canonical seed's region is fertile — expect the full count (a drop below is a
    // siting regression, not honest degradation; the honest-degradation case is covered
    // by the ocean + mountain tests).
    EXPECT_EQ(plan.sites().size(), 8u);
    assertPlanInvariants(plan, 400.0f);
    // Tier mix at 8: 1 town, 2 villages, 5 hamlets.
    int towns = 0, villages = 0;
    for (const auto& s : plan.sites()) {
        if (s.tier == "town") ++towns;
        if (s.tier == "village") ++villages;
    }
    EXPECT_EQ(towns, 1);
    EXPECT_EQ(villages, 2);
}

// Degenerate-terrain extreme: a Mountains world. The planner must degrade HONESTLY —
// fewer (possibly zero) sites, every seated site still dry/spaced/connected — and must
// never invent a site to hit the requested count.
TEST(WorldForgeStressTest, MountainWorldDegradesHonestly) {
    WorldGenerator gen = makeForgeWorld(WorldGenerator::GenerationType::Mountains, 424242,
                                        8, 2048.0f, 400.0f);
    ASSERT_NE(gen.worldForge(), nullptr);
    const WorldForgePlan& plan = *gen.worldForge();
    EXPECT_LE(plan.sites().size(), 8u);
    assertPlanInvariants(plan, 400.0f);
    for (const auto& s : plan.sites()) {
        // Whatever was seated must be genuinely dry land above the sea.
        const auto col = gen.sampleSurface(s.pos.x, s.pos.y);
        EXPECT_GE(col.surfaceY, static_cast<int>(gen.getTerrainParams().seaLevelY))
            << "mountain-world site " << s.id << " below sea";
        EXPECT_EQ(col.riverOrder, 0) << "site " << s.id << " on a carved river";
    }
}

// Determinism at scale: the 8-site plan is byte-identical across independent generators
// (the delete-the-DB / regenerate-the-world guarantee, at the V1 maximum).
TEST(WorldForgeStressTest, EightSitePlanDeterministicAcrossGenerators) {
    WorldGenerator a = makeForgeWorld(WorldGenerator::GenerationType::Perlin, 20260816,
                                      8, 2048.0f, 400.0f);
    WorldGenerator b = makeForgeWorld(WorldGenerator::GenerationType::Perlin, 20260816,
                                      8, 2048.0f, 400.0f);
    ASSERT_NE(a.worldForge(), nullptr);
    ASSERT_NE(b.worldForge(), nullptr);
    EXPECT_EQ(a.worldForge()->planHash(), b.worldForge()->planHash());
    EXPECT_EQ(a.worldForge()->toJson().dump(), b.worldForge()->toJson().dump());
}

// L3-flavored road WALKABILITY profile, measured on real generated columns (deterministic
// geometry, not screenshots): walk every centerline at 1 u steps and count surface steps a
// character cannot climb (>1 cube). V1 roads DRAPE the terrain (grading is a logged gap),
// so some steep steps exist — this pins the measured budget so regressions surface. The
// bound is set from the FIRST measurement on the canonical world (recorded in
// docs/WorldForge.md), with headroom so it fails on real degradation, not noise.
TEST(WorldForgeStressTest, RoadStepProfileMostlyWalkable) {
    WorldGenerator gen = makeForgeWorld(WorldGenerator::GenerationType::Perlin, 20260816,
                                        3, 768.0f, 256.0f);
    ASSERT_NE(gen.worldForge(), nullptr);
    const WorldForgePlan& plan = *gen.worldForge();
    ASSERT_FALSE(plan.roads().empty());
    int steps = 0, tooSteep = 0;
    for (const auto& road : plan.roads()) {
        for (size_t i = 0; i + 1 < road.centerline.size(); ++i) {
            const glm::vec2 a = road.centerline[i], b = road.centerline[i + 1];
            const float len = glm::length(b - a);
            if (len < 1.0f) continue;
            int prevY = gen.sampleSurface(static_cast<int>(std::lround(a.x)),
                                          static_cast<int>(std::lround(a.y))).surfaceY;
            for (float t = 1.0f; t <= len; t += 1.0f) {
                const glm::vec2 p = a + (b - a) * (t / len);
                const int y = gen.sampleSurface(static_cast<int>(std::lround(p.x)),
                                                static_cast<int>(std::lround(p.y))).surfaceY;
                ++steps;
                if (std::abs(y - prevY) > 1) ++tooSteep;
                prevY = y;
            }
        }
    }
    ASSERT_GT(steps, 100);
    const double steepFrac = static_cast<double>(tooSteep) / steps;
    RecordProperty("steep_step_fraction", std::to_string(steepFrac));
    std::cout << "[WORLDFORGE] canonical road steep-step fraction: " << steepFrac << " ("
              << tooSteep << "/" << steps << ")\n";
    // Measured 0.0000 on the canonical world (2026-08-16) — slope-averse routing keeps the
    // draped roads fully climbable on gentle Perlin. Bound leaves headroom for seed drift.
    EXPECT_LT(steepFrac, 0.06) << tooSteep << "/" << steps
                               << " road steps are unclimbable (>1 cube) — the drape "
                                  "quality regressed (road grading gap: StructurePipelineGaps)";
}

// The SAME walkability measurement where draping actually hurts: a Mountains world. This is
// the honest datum for the road-grading gap — the canonical world's 0.0 says little about
// steep terrain. Measurement-first: the recorded number goes to docs/WorldForge.md; the
// bound only guards against gross regression (roads become mostly unwalkable).
TEST(WorldForgeStressTest, RoadStepProfileMeasuredOnMountains) {
    WorldGenerator gen = makeForgeWorld(WorldGenerator::GenerationType::Mountains, 424242,
                                        8, 2048.0f, 400.0f);
    ASSERT_NE(gen.worldForge(), nullptr);
    const WorldForgePlan& plan = *gen.worldForge();
    if (plan.roads().empty()) GTEST_SKIP() << "mountain seed yielded <2 sites (no roads)";
    int steps = 0, tooSteep = 0;
    for (const auto& road : plan.roads()) {
        for (size_t i = 0; i + 1 < road.centerline.size(); ++i) {
            const glm::vec2 a = road.centerline[i], b = road.centerline[i + 1];
            const float len = glm::length(b - a);
            if (len < 1.0f) continue;
            int prevY = gen.sampleSurface(static_cast<int>(std::lround(a.x)),
                                          static_cast<int>(std::lround(a.y))).surfaceY;
            for (float t = 1.0f; t <= len; t += 1.0f) {
                const glm::vec2 p = a + (b - a) * (t / len);
                const int y = gen.sampleSurface(static_cast<int>(std::lround(p.x)),
                                                static_cast<int>(std::lround(p.y))).surfaceY;
                ++steps;
                if (std::abs(y - prevY) > 1) ++tooSteep;
                prevY = y;
            }
        }
    }
    ASSERT_GT(steps, 100);
    const double steepFrac = static_cast<double>(tooSteep) / steps;
    RecordProperty("mountain_steep_step_fraction", std::to_string(steepFrac));
    std::cout << "[WORLDFORGE] mountain road steep-step fraction: " << steepFrac << " ("
              << tooSteep << "/" << steps << ")\n";
    EXPECT_LT(steepFrac, 0.5) << "mountain roads mostly unwalkable — grading gap got worse";
}
