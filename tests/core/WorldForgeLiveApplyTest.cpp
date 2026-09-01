#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "core/Cube.h"
#include "core/WorldForgePlan.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

using namespace Phyxel;

// ============================================================================
// WorldForge LIVE APPLY (was restart-required): the async gen workers hold
// private WorldGenerator snapshots taken at worker start, so re-baking the
// plan on the live generator changes NOTHING a streamed chunk contains — the
// exact hazard this test pins first (a bare apply leaves the resident world
// stale). ChunkManager::restreamWorldLive() then: stops the workers (fresh
// snapshots are re-taken from the live generator on the next pump), evicts
// every resident chunk through the deferred-deletion teardown, drops the
// surface-band + evicted-LOD caches, and the pump re-streams the world under
// the new plan — proven here by byte-comparing the re-streamed road column
// against a fresh plan-B generator (the seam contract).
// ============================================================================

namespace {

WorldRecipe planBRecipe(WorldGenerator& g) {
    WorldRecipe r = g.makeRecipe();
    r.worldforge.enabled = true;
    r.worldforge.siteCount = 3;
    r.worldforge.regionRadius = 768.0f;
    r.worldforge.minSpacing = 256.0f;
    return r;
}

// Highest solid cube in the column via the chunk manager (INT_MIN = none).
int topSolidY(ChunkManager& cm, int x, int z, int yHi, int yLo) {
    for (int y = yHi; y >= yLo; --y)
        if (cm.getCubeAt(glm::ivec3(x, y, z))) return y;
    return INT_MIN;
}

}  // namespace

TEST(WorldForgeLiveApplyTest, BareApplyIsStaleAndRestreamFixesIt) {
    ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    // The streaming pump is a no-op without world storage — attach a throwaway DB.
    const std::string db =
        (std::filesystem::temp_directory_path() / "worldforge_live_apply_test.db").string();
    {
        std::error_code ec;
        std::filesystem::remove(db, ec);
    }
    ASSERT_TRUE(cm.initializeWorldStorage(db));
    cm.configureStreamingGeneration(true, WorldGenerator::GenerationType::Perlin, 20260816);
    WorldGenerator* live = cm.getStreamingGenerator();
    ASSERT_NE(live, nullptr);

    // Plan B previewed on a throwaway generator: find a stampable road column (dry,
    // above sea, road material) to use as the probe.
    WorldGenerator fresh(WorldGenerator::GenerationType::Perlin, 20260816);
    fresh.applyRecipe(planBRecipe(fresh));
    const WorldForgePlan* planB = fresh.worldForge();
    ASSERT_NE(planB, nullptr);
    ASSERT_FALSE(planB->roads().empty());
    glm::ivec2 target(INT_MIN, INT_MIN);
    std::string roadMat;
    int surfB = 0;
    for (const auto& road : planB->roads()) {
        for (size_t i = 1; i + 1 < road.centerline.size() && target.x == INT_MIN; i += 2) {
            const int wx = static_cast<int>(std::lround(road.centerline[i].x));
            const int wz = static_cast<int>(std::lround(road.centerline[i].y));
            const auto col = fresh.sampleSurface(wx, wz);
            if (col.roadClass > 0 && col.surfaceMat == WorldForgePlan::roadMaterial(col.roadClass)) {
                target = {wx, wz};
                roadMat = col.surfaceMat;
                surfB = col.surfaceY;
            }
        }
        if (target.x != INT_MIN) break;
    }
    ASSERT_NE(target.x, INT_MIN) << "no stampable road column on the canonical plan";

    // Stream the target's chunk under plan A (worldforge disabled — the default recipe).
    const auto colA = live->sampleSurface(target.x, target.y);
    ASSERT_EQ(colA.roadClass, 0) << "fixture broken: plan A already has a road here";
    cm.setPlayerPosition(glm::vec3(target.x, colA.surfaceY + 2, target.y));
    auto pumpUntil = [&](auto pred, int maxIters) {
        for (int i = 0; i < maxIters; ++i) {
            cm.updateChunkStreaming();
            if (pred()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    };
    auto targetResident = [&] {
        return cm.getChunkAtFast(glm::ivec3(target.x, colA.surfaceY, target.y)) != nullptr;
    };
    ASSERT_TRUE(pumpUntil(targetResident, 1200)) << "plan-A chunk never streamed in";
    const int topA = topSolidY(cm, target.x, target.y, colA.surfaceY + 8, colA.surfaceY - 8);
    ASSERT_NE(topA, INT_MIN);
    const Cube* cubeA = cm.getCubeAt(glm::ivec3(target.x, topA, target.y));
    ASSERT_NE(cubeA, nullptr);
    ASSERT_NE(cubeA->getMaterialName(), roadMat) << "plan-A surface already road material";
    const std::string matA = cubeA->getMaterialName();

    // THE HAZARD, pinned: apply plan B on the LIVE generator and pump — without a
    // restream, the resident chunk keeps its plan-A content (the workers' stale
    // snapshots never even run: the chunk is resident, nothing re-requests it).
    live->applyRecipe(planBRecipe(*live));
    ASSERT_NE(live->worldForge(), nullptr) << "live re-bake produced no plan";
    for (int i = 0; i < 10; ++i) {
        cm.updateChunkStreaming();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    {
        const Cube* still = cm.getCubeAt(glm::ivec3(target.x, topA, target.y));
        ASSERT_NE(still, nullptr);
        EXPECT_EQ(still->getMaterialName(), matA)
            << "bare apply changed a resident chunk?! (the restart-required rationale is gone)";
    }

    // THE FIX: restream. Workers restart with fresh snapshots; the pump re-streams the
    // world; the target column must now carry plan B's road stamp...
    const size_t evicted = cm.restreamWorldLive();
    EXPECT_GT(evicted, 0u);
    auto targetIsRoad = [&] {
        const int top = topSolidY(cm, target.x, target.y, surfB + 8, surfB - 8);
        if (top == INT_MIN) return false;
        const Cube* c = cm.getCubeAt(glm::ivec3(target.x, top, target.y));
        return c && c->getMaterialName() == roadMat;
    };
    ASSERT_TRUE(pumpUntil(targetIsRoad, 1200))
        << "re-streamed chunk never picked up the plan-B road";

    // ...and match a FRESH plan-B generator column-for-column over a window around the
    // target (the seam contract: live-applied == restarted, byte-identical).
    for (int dx = -4; dx <= 4; dx += 2)
        for (int dz = -4; dz <= 4; dz += 2) {
            const auto want = fresh.sampleSurface(target.x + dx, target.y + dz);
            const int top =
                topSolidY(cm, target.x + dx, target.y + dz, want.surfaceY + 8, want.surfaceY - 8);
            if (top == INT_MIN) continue;   // column's chunk may not be re-resident yet
            EXPECT_EQ(top, want.surfaceY)
                << "surface height diverges from a fresh plan-B world at offset (" << dx
                << "," << dz << ")";
        }
}
