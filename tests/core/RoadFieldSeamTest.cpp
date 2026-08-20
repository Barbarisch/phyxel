#include <gtest/gtest.h>

#include <cmath>
#include <iostream>
#include <memory>

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

// Bridge fixture (shared by the deck / rail / pier tests): pin two sites either side of
// the nearest order>=3 stem on the canonical seed so the plan bakes a span. plan == null
// means the seed offers no stem within 5 km (tests skip).
struct BridgeWorld {
    WorldGenerator gen{WorldGenerator::GenerationType::Perlin, 20260816};
    const WorldForgePlan* plan = nullptr;
    BridgeWorld() {
        const FlowField* flow = gen.riverNetwork();
        if (!flow) return;
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
        if (!found) return;
        const glm::vec2 dir = flow->flowDirAt(stem.x, stem.y);
        if (glm::length(dir) < 0.01f) return;
        const glm::vec2 perp = glm::normalize(glm::vec2(-dir.y, dir.x));
        WorldRecipe r = testRecipe(gen);
        r.worldforge.regionRadius = 8192.0f;   // the stem may be far from the origin
        r.worldforge.sitePins = {
            {static_cast<int>(stem.x + perp.x * 140.0f), static_cast<int>(stem.y + perp.y * 140.0f)},
            {static_cast<int>(stem.x - perp.x * 140.0f), static_cast<int>(stem.y - perp.y * 140.0f)}};
        gen.applyRecipe(r);
        if (gen.worldForge() && !gen.worldForge()->bridges().empty()) plan = gen.worldForge();
    }
};

int floorDiv32(int a) { return a >= 0 ? a / 32 : (a - 31) / 32; }

// Generate the chunk holding one world cell (initializeForLoading wires the voxel-manager
// callbacks — the FloraMarginTest pattern; a bare Chunk throws bad_function_call).
std::unique_ptr<Chunk> genChunkAt(WorldGenerator& gen, const glm::ivec3& chunkCoord) {
    auto chunk = std::make_unique<Chunk>(chunkCoord * 32);
    chunk->initializeForLoading();
    gen.generateChunk(*chunk, chunkCoord);
    return chunk;
}

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
    BridgeWorld w;
    if (!w.plan) GTEST_SKIP() << "no order>=3 channel within 5 km of origin on this seed";

    const auto& b = w.plan->bridges()[0];
    const glm::vec2 mid = (b.a + b.b) * 0.5f;
    const int mx = static_cast<int>(std::lround(mid.x)), mz = static_cast<int>(std::lround(mid.y));
    auto col = w.gen.sampleSurface(mx, mz);
    EXPECT_NE(col.bridgeDeckY, INT_MIN) << "deck column not marked in ColumnSample";
    EXPECT_GT(col.bridgeDeckY, col.surfaceY) << "deck must clear the carved bed";
    EXPECT_GT(col.roadClass, 0) << "a bridge column is a road column";

    // The deck is physically emitted: generate the chunk holding the deck cell and read it.
    const glm::ivec3 cc(floorDiv32(mx), floorDiv32(col.bridgeDeckY), floorDiv32(mz));
    auto chunk = genChunkAt(w.gen, cc);
    const Cube* deck = chunk->getCubeAt(glm::ivec3(mx - cc.x * 32, col.bridgeDeckY - cc.y * 32,
                                                   mz - cc.z * 32));
    ASSERT_NE(deck, nullptr) << "no voxel at the deck cell";
}

// Rails + piers (the red driver for the parapet/pier emission). Coverage, not existence —
// the far-road lesson: BOTH deck edges must carry the rail mark along >=80% of the span
// interior, the walkway between them must NEVER be rail-marked (traversability is the
// invariant), and physically the rail cell holds a 2/3-height SUBCUBE parapet (sub-voxel
// per the detail rule — a full cube there is a defect) while a pier column is solid from
// the carved bed to under the deck.
TEST(RoadFieldSeamTest, BridgeRailsGuardDeckEdgesAndPiersReachTheBed) {
    BridgeWorld w;
    if (!w.plan) GTEST_SKIP() << "no order>=3 channel within 5 km of origin on this seed";

    const auto& b = w.plan->bridges()[0];
    const glm::vec2 ab = b.b - b.a;
    const float len = glm::length(ab);
    ASSERT_GT(len, 6.0f) << "degenerate span";
    const glm::vec2 dir = ab / len;
    const glm::vec2 perp(-dir.y, dir.x);
    const float half = WorldForgePlan::roadHalfWidth(b.cls);

    int steps = 0, leftRail = 0, rightRail = 0, walkwayViolations = 0;
    glm::ivec2 railCell(INT_MIN, INT_MIN), pierCell(INT_MIN, INT_MIN);
    const int win = static_cast<int>(std::ceil(half)) + 1;
    for (float t = 1.0f; t <= len - 1.0f; t += 1.0f) {
        ++steps;
        const glm::vec2 c = b.a + dir * t;
        bool l = false, r = false;
        for (int ox = -win; ox <= win; ++ox)
            for (int oz = -win; oz <= win; ++oz) {
                const int wx = static_cast<int>(std::lround(c.x)) + ox;
                const int wz = static_cast<int>(std::lround(c.y)) + oz;
                const float lat = glm::dot(glm::vec2(wx, wz) - b.a, perp);   // signed side
                const auto col = w.gen.sampleSurface(wx, wz);
                if (!col.bridgeRail) {
                    if (col.bridgePierTopY != INT_MIN && pierCell.x == INT_MIN)
                        pierCell = {wx, wz};
                    continue;
                }
                if (lat > 0.0f) l = true; else r = true;
                railCell = {wx, wz};
                if (std::abs(lat) <= half - 1.2f) ++walkwayViolations;  // rail INSIDE the walkway
            }
        if (l) ++leftRail;
        if (r) ++rightRail;
    }
    ASSERT_GT(steps, 0);
    EXPECT_GE(leftRail, static_cast<int>(0.8f * steps)) << "left deck edge not railed";
    EXPECT_GE(rightRail, static_cast<int>(0.8f * steps)) << "right deck edge not railed";
    EXPECT_EQ(walkwayViolations, 0) << "rail marks intrude into the walkway";
    ASSERT_NE(railCell.x, INT_MIN) << "no rail-marked column anywhere on the span";

    // Physical parapet: subcubes at deckY+1, 2/3 tall, and NOT a full cube.
    {
        const auto col = w.gen.sampleSurface(railCell.x, railCell.y);
        ASSERT_NE(col.bridgeDeckY, INT_MIN);
        const int ry = col.bridgeDeckY + 1;
        const glm::ivec3 cc(floorDiv32(railCell.x), floorDiv32(ry), floorDiv32(railCell.y));
        auto chunk = genChunkAt(w.gen, cc);
        const glm::ivec3 lp(railCell.x - cc.x * 32, ry - cc.y * 32, railCell.y - cc.z * 32);
        EXPECT_EQ(chunk->getCubeAt(lp), nullptr) << "parapet emitted as a FULL cube (defect)";
        EXPECT_TRUE(chunk->hasSubcubeAt(lp, glm::ivec3(0, 0, 0))) << "no parapet subcubes";
        EXPECT_TRUE(chunk->hasSubcubeAt(lp, glm::ivec3(2, 1, 2))) << "parapet not 2 subcubes tall";
        EXPECT_FALSE(chunk->hasSubcubeAt(lp, glm::ivec3(0, 2, 0))) << "parapet reaches full height";
    }

    // Piers: expected exactly when the span is long enough for an interior station.
    if (len >= 2.0f * WorldForgePlan::kPierSpacing) {
        ASSERT_NE(pierCell.x, INT_MIN) << "span of " << len << " u has no pier column";
        const auto col = w.gen.sampleSurface(pierCell.x, pierCell.y);
        ASSERT_GT(col.bridgePierTopY, col.surfaceY) << "pier has no height above the bed";
        // Solid at EVERY level from the bed to under the deck (invariant at depth, not
        // in aggregate — a floating pier segment must fail this).
        for (int wy = col.surfaceY + 1; wy <= col.bridgePierTopY; ++wy) {
            const glm::ivec3 cc(floorDiv32(pierCell.x), floorDiv32(wy), floorDiv32(pierCell.y));
            auto chunk = genChunkAt(w.gen, cc);
            const glm::ivec3 lp(pierCell.x - cc.x * 32, wy - cc.y * 32, pierCell.y - cc.z * 32);
            EXPECT_NE(chunk->getCubeAt(lp), nullptr)
                << "pier gap at y=" << wy << " (" << pierCell.x << "," << pierCell.y << ")";
        }
    } else {
        EXPECT_EQ(pierCell.x, INT_MIN) << "short span sprouted a pier";
    }
}

// The pier EMISSION path needs a span >= 2x kPierSpacing, and the canonical crossing is
// shorter — so hunt the 8-site stress plan (3 bridges on this seed) for a long span and
// prove a pier stands solid from the carved bed to under the deck at every level. Without
// this, only the no-pier branch of the fill code would ever run in tests.
TEST(RoadFieldSeamTest, BridgePiersStandSolidOnALongSpan) {
    // The WorldForgeStressTest mountain fixture — the only known plan with multiple
    // bridges on the tested seeds (Perlin 20260816 routes around its channels).
    WorldGenerator gen(WorldGenerator::GenerationType::Mountains, 424242);
    WorldRecipe r = gen.makeRecipe();
    r.worldforge.enabled = true;
    r.worldforge.siteCount = 8;
    r.worldforge.regionRadius = 2048.0f;
    r.worldforge.minSpacing = 400.0f;
    gen.applyRecipe(r);
    const WorldForgePlan* plan = gen.worldForge();
    ASSERT_NE(plan, nullptr);

    const WorldForgeBridgeSpan* span = nullptr;
    std::string lens;
    for (const auto& b : plan->bridges()) {
        const float len = glm::length(b.b - b.a);
        lens += std::to_string(len) + " ";
        if (len >= 2.0f * WorldForgePlan::kPierSpacing && !span) span = &b;
    }
    ASSERT_NE(span, nullptr) << "no span >= " << 2.0f * WorldForgePlan::kPierSpacing
                             << " u among bridges (lengths: " << lens
                             << ") — the pier path has no fixture";

    // Find a pier column near the first interior station.
    const glm::vec2 ab = span->b - span->a;
    const float len = glm::length(ab);
    const glm::vec2 dir = ab / len;
    const int n = static_cast<int>(std::floor(len / WorldForgePlan::kPierSpacing)) - 1;
    ASSERT_GE(n, 1);
    const glm::vec2 st = span->a + dir * (len / static_cast<float>(n + 1));
    glm::ivec2 pierCell(INT_MIN, INT_MIN);
    for (int ox = -2; ox <= 2 && pierCell.x == INT_MIN; ++ox)
        for (int oz = -2; oz <= 2 && pierCell.x == INT_MIN; ++oz) {
            const int wx = static_cast<int>(std::lround(st.x)) + ox;
            const int wz = static_cast<int>(std::lround(st.y)) + oz;
            if (gen.sampleSurface(wx, wz).bridgePierTopY != INT_MIN) pierCell = {wx, wz};
        }
    ASSERT_NE(pierCell.x, INT_MIN) << "no pier column near the first station";

    const auto col = gen.sampleSurface(pierCell.x, pierCell.y);
    ASSERT_GT(col.bridgePierTopY, col.surfaceY);
    EXPECT_EQ(col.bridgePierTopY, col.bridgeDeckY - 1) << "pier must meet the deck underside";
    std::unique_ptr<Chunk> chunk;
    glm::ivec3 cachedCc(INT_MIN);
    for (int wy = col.surfaceY + 1; wy <= col.bridgePierTopY; ++wy) {
        const glm::ivec3 cc(floorDiv32(pierCell.x), floorDiv32(wy), floorDiv32(pierCell.y));
        if (cc != cachedCc) {
            chunk = genChunkAt(gen, cc);
            cachedCc = cc;
        }
        const glm::ivec3 lp(pierCell.x - cc.x * 32, wy - cc.y * 32, pierCell.y - cc.z * 32);
        EXPECT_NE(chunk->getCubeAt(lp), nullptr)
            << "pier gap at y=" << wy << " (" << pierCell.x << "," << pierCell.y << ")";
    }
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
