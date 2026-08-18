#include <gtest/gtest.h>

#include <cmath>
#include <iostream>

#include "core/Chunk.h"
#include "core/WorldForgePlan.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

using namespace Phyxel;

// ============================================================================
// WorldForge M1 — the road FIELD in chunk generation (docs/WorldForge.md).
// Roads must be a pure function of world position ("chunks must not be
// visible"): stamped into ColumnSample by sampleColumn via the baked plan,
// identical across generator copies (the streaming-worker contract), and the
// flora planner must keep trunks off the road corridor. Red-before-green:
// these ran RED with the plan baked but sampleColumn not yet consuming it.
// ============================================================================

namespace {

// The canonical small test world's config (samples/game_definitions/worldforge_test.json):
// Perlin seed 20260816, 3 sites in a 768 u region. Flora tuned DENSE so an ungated build
// could not plausibly leave the road corridor trunk-free by luck.
WorldRecipe testRecipe(WorldGenerator& gen) {
    WorldRecipe r = gen.makeRecipe();
    r.worldforge.enabled = true;
    r.worldforge.siteCount = 3;
    r.worldforge.regionRadius = 768.0f;
    r.worldforge.minSpacing = 256.0f;
    for (auto& b : r.biomes) {
        b.floraDensity = 0.6f;
        b.floraSpacing = 3;
    }
    return r;
}

struct ForgeWorld {
    WorldGenerator gen;
    ForgeWorld() : gen(WorldGenerator::GenerationType::Perlin, 20260816) {
        gen.applyRecipe(testRecipe(gen));
    }
};

}  // namespace

// Fixture guard: the canonical seed must actually yield a plan with roads.
TEST(RoadFieldSeamTest, TestWorldHasRoads) {
    ForgeWorld w;
    ASSERT_NE(w.gen.worldForge(), nullptr);
    EXPECT_GE(w.gen.worldForge()->sites().size(), 2u);
    ASSERT_FALSE(w.gen.worldForge()->roads().empty());
}

// THE stamp invariant (the red driver): columns on a road centerline carry roadClass and
// the road's surface material — except where the river or the sea wins.
TEST(RoadFieldSeamTest, RoadColumnsStampedWithRoadMaterial) {
    ForgeWorld w;
    const WorldForgePlan* plan = w.gen.worldForge();
    ASSERT_NE(plan, nullptr);
    ASSERT_FALSE(plan->roads().empty());
    int stamped = 0, sampled = 0;
    for (const auto& road : plan->roads()) {
        for (size_t i = 1; i + 1 < road.centerline.size(); i += 2) {
            const int wx = static_cast<int>(std::lround(road.centerline[i].x));
            const int wz = static_cast<int>(std::lround(road.centerline[i].y));
            const auto col = w.gen.sampleSurface(wx, wz);
            ++sampled;
            if (col.riverOrder > 0) continue;   // river wins (bridges: honest V1 gap)
            if (col.surfaceY < static_cast<int>(w.gen.getTerrainParams().seaLevelY)) continue;
            ++stamped;
            EXPECT_EQ(col.roadClass, road.cls)
                << "unstamped road column at (" << wx << "," << wz << ")";
            EXPECT_EQ(col.surfaceMat, WorldForgePlan::roadMaterial(road.cls));
        }
    }
    ASSERT_GT(sampled, 20);
    EXPECT_GT(stamped, sampled / 2) << "most centerline columns should be stampable";
}

// Exclusivity: a stamped road column is never a riverbed and never below sea level.
TEST(RoadFieldSeamTest, RoadNeverOverridesRiverOrSea) {
    ForgeWorld w;
    const WorldForgePlan* plan = w.gen.worldForge();
    ASSERT_NE(plan, nullptr);
    for (const auto& road : plan->roads())
        for (const auto& pt : road.centerline) {
            const auto col = w.gen.sampleSurface(static_cast<int>(std::lround(pt.x)),
                                                 static_cast<int>(std::lround(pt.y)));
            if (col.roadClass > 0) {
                EXPECT_EQ(col.riverOrder, 0);
                EXPECT_GE(col.surfaceY, static_cast<int>(w.gen.getTerrainParams().seaLevelY));
            }
        }
}

// Worker-copy contract: a copied generator (what every streaming gen worker holds) stamps
// IDENTICAL road columns — the plan rides the copy via shared_ptr; no per-copy re-derivation.
TEST(RoadFieldSeamTest, GeneratorCopyStampsIdenticalRoadColumns) {
    ForgeWorld w;
    const WorldForgePlan* plan = w.gen.worldForge();
    ASSERT_NE(plan, nullptr);
    ASSERT_FALSE(plan->roads().empty());
    WorldGenerator copy(w.gen);   // the ChunkStreamingManager worker snapshot
    const auto& road = plan->roads()[0];
    int roadColumns = 0;
    for (size_t i = 0; i < road.centerline.size(); ++i) {
        // A 5-column cross-section band around each centerline point.
        for (int dx = -2; dx <= 2; ++dx) {
            const int wx = static_cast<int>(std::lround(road.centerline[i].x)) + dx;
            const int wz = static_cast<int>(std::lround(road.centerline[i].y));
            const auto a = w.gen.sampleSurface(wx, wz);
            const auto b = copy.sampleSurface(wx, wz);
            EXPECT_EQ(a.roadClass, b.roadClass) << "copy divergence at (" << wx << "," << wz << ")";
            EXPECT_EQ(a.surfaceMat, b.surfaceMat);
            EXPECT_EQ(a.surfaceY, b.surfaceY);
            if (a.roadClass > 0) ++roadColumns;
        }
    }
    EXPECT_GT(roadColumns, 0) << "the band must actually contain road columns";
}

// Flora gate: no trunk/anchor lands on the road or its 2-column shoulder. Flora is tuned
// dense (density 0.6, spacing 3), so pre-gate this corridor WILL contain trunks — a
// genuine red, not a vacuous pass.
TEST(RoadFieldSeamTest, FloraKeepsOffTheRoadCorridor) {
    ForgeWorld w;
    const WorldForgePlan* plan = w.gen.worldForge();
    ASSERT_NE(plan, nullptr);
    ASSERT_FALSE(plan->roads().empty());
    const auto& road = plan->roads()[0];
    // Plan flora over the road's bbox (clamped to a manageable rect).
    glm::vec2 lo(1e30f), hi(-1e30f);
    for (const auto& p : road.centerline) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    const int minX = static_cast<int>(lo.x) - 8, minZ = static_cast<int>(lo.y) - 8;
    const int maxX = std::min(static_cast<int>(hi.x) + 8, minX + 512);
    const int maxZ = std::min(static_cast<int>(hi.y) + 8, minZ + 512);
    const auto flora = w.gen.planFlora(minX, minZ, maxX, maxZ, /*edgeInset=*/0);
    ASSERT_FALSE(flora.empty()) << "dense flora tuning must place plants in the corridor bbox";
    int violations = 0;
    for (const auto& f : flora) {
        const auto hit = plan->roadAt(static_cast<float>(f.worldX), static_cast<float>(f.worldZ));
        if (hit.cls > 0 && hit.dist <= WorldForgePlan::roadHalfWidth(hit.cls) + 2.0f) ++violations;
    }
    EXPECT_EQ(violations, 0) << violations << " trunks on the road corridor";
}

// Bridges in the GENERATOR (placer #44): pin two sites straddling a real order>=3 channel
// (found from the live flow field, perpendicular via flowDirAt — no hardcoded coordinates),
// then assert the connecting road's span emits an actual plank deck: bridgeDeckY set above
// the carved bed on deck columns, and generateChunk places a Wood cube at the deck cell.
// RED before the sampleColumn/generateChunk wiring.
TEST(RoadFieldSeamTest, BridgeDeckEmittedOverOrder3Channel) {
    WorldGenerator gen(WorldGenerator::GenerationType::Perlin, 20260816);
    // Find an order>=3 channel cell within the plannable region of the canonical seed.
    const FlowField* flow = gen.riverNetwork();
    ASSERT_NE(flow, nullptr);
    glm::vec2 stem(0.0f);
    bool found = false;
    for (int r = 2; r < 40 && !found; ++r)   // ring search outward from the origin
        for (int cx = -r; cx <= r && !found; ++cx)
            for (int cz = -r; cz <= r && !found; ++cz) {
                if (std::max(std::abs(cx), std::abs(cz)) != r) continue;
                const glm::vec2 p(cx * 128.0f + 64.0f, cz * 128.0f + 64.0f);
                if (flow->orderAt(p.x, p.y) >= 3) {
                    stem = p;
                    found = true;
                }
            }
    if (!found) GTEST_SKIP() << "no order>=3 channel within 5 km of origin on this seed";
    const glm::vec2 dir = flow->flowDirAt(stem.x, stem.y);
    ASSERT_GT(glm::length(dir), 0.01f);
    const glm::vec2 perp = glm::normalize(glm::vec2(-dir.y, dir.x));

    WorldRecipe r = testRecipe(gen);
    r.worldforge.regionRadius = 8192.0f;   // the stem may be far from the origin
    r.worldforge.sitePins = {
        {static_cast<int>(stem.x + perp.x * 140.0f), static_cast<int>(stem.y + perp.y * 140.0f)},
        {static_cast<int>(stem.x - perp.x * 140.0f), static_cast<int>(stem.y - perp.y * 140.0f)}};
    gen.applyRecipe(r);
    const WorldForgePlan* plan = gen.worldForge();
    ASSERT_NE(plan, nullptr);
    ASSERT_FALSE(plan->bridges().empty()) << "pinned crossing produced no bridge span";

    const auto& b = plan->bridges()[0];
    const glm::vec2 mid = (b.a + b.b) * 0.5f;
    const int mx = static_cast<int>(std::lround(mid.x)), mz = static_cast<int>(std::lround(mid.y));
    auto col = gen.sampleSurface(mx, mz);
    EXPECT_NE(col.bridgeDeckY, INT_MIN) << "deck column not marked in ColumnSample";
    EXPECT_GT(col.bridgeDeckY, col.surfaceY) << "deck must clear the carved bed";
    EXPECT_GT(col.roadClass, 0) << "a bridge column is a road column";

    // The deck is physically emitted: generate the chunk holding the deck cell and read it.
    auto floorDiv = [](int a) { return a >= 0 ? a / 32 : (a - 31) / 32; };
    const glm::ivec3 cc(floorDiv(mx), floorDiv(col.bridgeDeckY), floorDiv(mz));
    Chunk chunk(glm::ivec3(cc.x * 32, cc.y * 32, cc.z * 32));
    chunk.initializeForLoading();   // wires the voxel-manager callbacks (FloraMarginTest pattern)
    gen.generateChunk(chunk, cc);
    const Cube* deck = chunk.getCubeAt(glm::ivec3(mx - cc.x * 32, col.bridgeDeckY - cc.y * 32,
                                                  mz - cc.z * 32));
    ASSERT_NE(deck, nullptr) << "no voxel at the deck cell";
}

// Off-road columns are untouched by the plan: identical to a worldforge-DISABLED world.
// (The "chunks must not be visible" twin: roads change ONLY the corridor.)
TEST(RoadFieldSeamTest, OffRoadColumnsIdenticalToDisabledWorld) {
    ForgeWorld w;
    WorldGenerator plain(WorldGenerator::GenerationType::Perlin, 20260816);
    WorldRecipe r = testRecipe(plain);
    r.worldforge.enabled = false;
    plain.applyRecipe(r);
    const WorldForgePlan* plan = w.gen.worldForge();
    ASSERT_NE(plan, nullptr);
    // A grid deliberately far outside the road network bbox.
    int checked = 0;
    for (int wx = 4000; wx <= 4400; wx += 50)
        for (int wz = 4000; wz <= 4400; wz += 50) {
            const auto a = w.gen.sampleSurface(wx, wz);
            const auto b = plain.sampleSurface(wx, wz);
            EXPECT_EQ(a.surfaceY, b.surfaceY);
            EXPECT_EQ(a.surfaceMat, b.surfaceMat);
            EXPECT_EQ(a.roadClass, 0);
            ++checked;
        }
    EXPECT_GT(checked, 0);
}
