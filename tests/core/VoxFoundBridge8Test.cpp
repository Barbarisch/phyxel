// docs/VoxelFoundationPlan.md F7.23 -- WorldForge bridges on the 1/8 m terrain (docs/evidence/voxfound_f7_prediction.txt
// B1-B4). RoadFieldSeamTest's bridge fixture: two sites pinned across the nearest order >= 3 channel (Perlin 20260816).
#include <gtest/gtest.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <set>

#include "core/FlowField.h"
#include "core/WorldForgePlan.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"
#include "core/voxfound/StorageLab.h"

using namespace Phyxel;
using namespace Phyxel::Core::VoxFound;

namespace {

WorldRecipe forgeRecipe(WorldGenerator& gen) {
    WorldRecipe r = gen.makeRecipe();
    r.worldforge.enabled = true;
    r.worldforge.siteCount = 3;
    r.worldforge.regionRadius = 768.0f;
    r.worldforge.minSpacing = 256.0f;
    return r;
}

struct BridgeWorld {
    WorldGenerator gen{WorldGenerator::GenerationType::Perlin, 20260816};
    const WorldForgePlan* plan = nullptr;
    BridgeWorld() {
        const FlowField* flow = gen.riverNetwork();
        if (!flow) return;
        glm::vec2 stem(0.0f);
        bool found = false;
        for (int r = 2; r < 40 && !found; ++r)
            for (int cx = -r; cx <= r && !found; ++cx)
                for (int cz = -r; cz <= r && !found; ++cz) {
                    if (std::max(std::abs(cx), std::abs(cz)) != r) continue;
                    const glm::vec2 p(cx * 128.0f + 64.0f, cz * 128.0f + 64.0f);
                    if (flow->orderAt(p.x, p.y) >= 3) { stem = p; found = true; }
                }
        if (!found) return;
        const glm::vec2 dir = flow->flowDirAt(stem.x, stem.y);
        if (glm::length(dir) < 0.01f) return;
        const glm::vec2 perp = glm::normalize(glm::vec2(-dir.y, dir.x));
        WorldRecipe r = forgeRecipe(gen);
        r.worldforge.regionRadius = 8192.0f;
        r.worldforge.sitePins = {
            {static_cast<int>(stem.x + perp.x * 140.0f), static_cast<int>(stem.y + perp.y * 140.0f)},
            {static_cast<int>(stem.x - perp.x * 140.0f), static_cast<int>(stem.y - perp.y * 140.0f)}};
        gen.applyRecipe(r);
        if (gen.worldForge() && !gen.worldForge()->bridges().empty()) plan = gen.worldForge();
    }
};

// Every bridged metre column along every span's deck line (and one metre either side, for the rails).
std::vector<glm::ivec2> bridgeColumns(BridgeWorld& w) {
    std::set<std::pair<int, int>> seen;
    std::vector<glm::ivec2> out;
    for (const auto& b : w.plan->bridges()) {
        const glm::vec2 d = b.b - b.a;
        const float len = glm::length(d);
        const glm::vec2 n = len > 0 ? glm::vec2(-d.y, d.x) / len : glm::vec2(0);
        for (float t = 0; t <= len; t += 0.5f)
            for (float s = -3.0f; s <= 3.0f; s += 1.0f) {
                const glm::vec2 p = b.a + d * (len > 0 ? t / len : 0.0f) + n * s;
                const int x = int(std::floor(p.x)), z = int(std::floor(p.y));
                if (!seen.insert({x, z}).second) continue;
                if (w.gen.sampleSurface(x, z).bridgeDeckY != INT_MIN) out.push_back({x, z});
            }
    }
    return out;
}

std::string matAt(const VoxelArea& a, int vx, int vy, int vz) { return a.palette[a.grid.at({vx, vy, vz})]; }

}  // namespace

TEST(VoxFoundBridge8, DeckParapetAndPiersOnTheEighthGrid) {
    BridgeWorld w;
    if (!w.plan) GTEST_SKIP() << "no order>=3 channel within 5 km of origin on this seed";
    const auto cols = bridgeColumns(w);
    ASSERT_GT(cols.size(), 4u);
    int decks = 0, deckBad = 0, rails = 0, railBad = 0, piers = 0, pierGaps = 0, redDeckWood = 0;
    for (const glm::ivec2& m : cols) {
        const auto c = w.gen.sampleSurface(m.x, m.y);
        const int y0 = std::min(c.surfaceY, int(std::floor(c.bedHeight))) - 2, y1 = c.bridgeDeckY + 3;
        const VoxelArea a = buildTerrainArea(w.gen, {m.x, y0, m.y}, {1, y1 - y0, 1});
        const int deckV = (c.bridgeDeckY - y0) * 8;
        if (c.bridgeDeckY > c.surfaceY) {
            ++decks;
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 8; ++y)
                    for (int z = 0; z < 8; ++z) deckBad += matAt(a, x, deckV + y, z) != "Wood";
            setBridgeStub(true);
            const VoxelArea r = buildTerrainArea(w.gen, {m.x, y0, m.y}, {1, y1 - y0, 1});
            setBridgeStub(false);
            for (int x = 0; x < 8; ++x)
                for (int y = 0; y < 8; ++y)
                    for (int z = 0; z < 8; ++z) redDeckWood += matAt(r, x, deckV + y, z) == "Wood";
            if (c.bridgeRail) {
                ++rails;
                int planks = 0;
                for (int x = 0; x < 8; ++x)
                    for (int y = 0; y < 8; ++y)
                        for (int z = 0; z < 8; ++z) planks += matAt(a, x, deckV + 8 + y, z) == "WoodPlanks";
                railBad += planks != 5 * 64;
            }
        }
        if (c.bridgePierTopY != INT_MIN) {
            ++piers;
            const int topV = (c.bridgePierTopY + 1 - y0) * 8;   // exclusive
            for (int x = 0; x < 8; ++x)
                for (int z = 0; z < 8; ++z) {
                    int y = 0;
                    while (y < topV && a.grid.at({x, y, z})) ++y;   // solid from the area floor up...
                    pierGaps += y < topV;                            // ...through the pier's top metre
                }
        }
    }
    std::printf("[F7.23] columns %zu | decks %d (non-Wood voxels %d; RED Wood voxels %d) | rails %d (bad %d) | piers %d "
                "(gapped voxel columns %d)\n",
                cols.size(), decks, deckBad, redDeckWood, rails, railBad, piers, pierGaps);
    ASSERT_GT(decks, 0);
    EXPECT_EQ(deckBad, 0);
    EXPECT_EQ(redDeckWood, 0) << "the red: no deck without the stamp";
    EXPECT_EQ(railBad, 0);
    EXPECT_EQ(pierGaps, 0);
}

TEST(VoxFoundBridge8, ChunkedEqualsWholeAcrossABridge) {
    BridgeWorld w;
    if (!w.plan) GTEST_SKIP() << "no bridge on this seed";
    const auto& b = w.plan->bridges().front();
    const glm::vec2 mid = (b.a + b.b) * 0.5f;
    const int x0 = int(std::floor(mid.x)) - 8, z0 = int(std::floor(mid.y)) - 8;
    int lo = 1 << 30, hi = -(1 << 30);
    for (int x = x0 - 1; x <= x0 + 16; ++x)
        for (int z = z0 - 1; z <= z0 + 16; ++z) {
            const auto c = w.gen.sampleSurface(x, z);
            lo = std::min(lo, std::min(c.surfaceY, int(std::floor(c.bedHeight))) - 2);
            hi = std::max(hi, std::max(c.surfaceY, c.bridgeDeckY == INT_MIN ? c.surfaceY : c.bridgeDeckY) + 3);
        }
    const VoxelArea whole = buildTerrainArea(w.gen, {x0, lo, z0}, {16, hi - lo, 16});
    const VoxelArea left = buildTerrainArea(w.gen, {x0, lo, z0}, {8, hi - lo, 16});
    const VoxelArea right = buildTerrainArea(w.gen, {x0 + 8, lo, z0}, {8, hi - lo, 16});
    size_t bad = 0, wood = 0;
    for (int vx = 0; vx < whole.grid.dim.x; ++vx)
        for (int vy = 0; vy < whole.grid.dim.y; ++vy)
            for (int vz = 0; vz < whole.grid.dim.z; ++vz) {
                const VoxelArea& part = vx < left.grid.dim.x ? left : right;
                const int px = vx < left.grid.dim.x ? vx : vx - left.grid.dim.x;
                const std::string m = matAt(whole, vx, vy, vz);
                bad += m != matAt(part, px, vy, vz);
                wood += m == "Wood";
            }
    std::printf("[F7.23 B4] area 16 x %d x 16 m across the bridge: %zu Wood voxels, %zu differing\n", hi - lo, wood, bad);
    EXPECT_EQ(bad, 0u);
}

// B3 on a span long enough for piers: RoadFieldSeamTest's long-span fixture (WorldForgeStressTest's mountains plan).
TEST(VoxFoundBridge8, PiersReachTheGroundOnALongSpan) {
    WorldGenerator gen(WorldGenerator::GenerationType::Mountains, 424242);
    WorldRecipe r = gen.makeRecipe();
    r.worldforge.enabled = true;
    r.worldforge.siteCount = 8;
    r.worldforge.regionRadius = 2048.0f;
    r.worldforge.minSpacing = 400.0f;
    gen.applyRecipe(r);
    const WorldForgePlan* plan = gen.worldForge();
    ASSERT_NE(plan, nullptr);
    int piers = 0, gaps = 0, redGaps = 0;
    float worstLift = 0.0f;   // how far below surfaceY + 1 the 1/8 m ground lies under a pier (what the cube rule misses)
    for (const auto& b : plan->bridges()) {
        const glm::vec2 ab = b.b - b.a;
        const float len = glm::length(ab);
        if (len < 2.0f * WorldForgePlan::kPierSpacing) continue;
        const glm::vec2 dir = ab / len;
        const int n = int(std::floor(len / WorldForgePlan::kPierSpacing)) - 1;
        for (int k = 1; k <= n; ++k) {
            const glm::vec2 st = b.a + dir * (len * k / float(n + 1));
            for (int ox = -3; ox <= 3; ++ox)
                for (int oz = -3; oz <= 3; ++oz) {
                    const int wx = int(std::lround(st.x)) + ox, wz = int(std::lround(st.y)) + oz;
                    const auto c = gen.sampleSurface(wx, wz);
                    if (c.bridgePierTopY == INT_MIN) continue;
                    ++piers;
                    const int y0 = std::min(c.surfaceY, int(std::floor(c.bedHeight))) - 2;
                    const int topV = (c.bridgePierTopY + 1 - y0) * 8;
                    auto gapped = [&](const VoxelArea& a) {
                        int g = 0;
                        for (int x = 0; x < 8; ++x)
                            for (int z = 0; z < 8; ++z) {
                                int y = 0;
                                while (y < topV && a.grid.at({x, y, z})) ++y;
                                g += y < topV;
                            }
                        return g;
                    };
                    const VoxelArea a = buildTerrainArea(gen, {wx, y0, wz}, {1, c.bridgeDeckY + 2 - y0, 1});
                    gaps += gapped(a);
                    setBridgeStub(true);
                    redGaps += gapped(buildTerrainArea(gen, {wx, y0, wz}, {1, c.bridgeDeckY + 2 - y0, 1}));
                    setBridgeStub(false);
                    worstLift = std::max(worstLift, float(c.surfaceY + 1) - (c.bedHeight + 0.5f));
                }
        }
    }
    std::printf("[F7.23 B3] pier columns %d: gapped voxel columns %d (RED %d); the continuous ground lies up to %.3f m "
                "below the cube pier's first metre\n", piers, gaps, redGaps, worstLift);
    ASSERT_GT(piers, 0) << "the fixture has no pier columns";
    EXPECT_EQ(gaps, 0);
    EXPECT_GT(redGaps, 0);
}
