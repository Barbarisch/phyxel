#include <gtest/gtest.h>

#include "core/ChunkManager.h"
#include "core/NPCManager.h"
#include "core/NavGraph.h"

using namespace Phyxel;
using namespace Phyxel::Core;

// ============================================================================
// NAV OBSTACLES (playable-town) — static placed objects (wells, woodpiles,
// furniture) are NOT chunk voxels, so the NavGraph routed straight through them
// and NPC collision turned each into a treadmill. RED (measured live): village
// residents orbited the street well for minutes at constant distance; 6/14
// never converged. NPCManager now rasterizes provider-supplied obstacle boxes
// into the nav build; these tests pin that the graph honors them.
// ============================================================================

namespace {
struct FlatWorld {
    ChunkManager cm;
    FlatWorld() {
        cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
        auto owned = std::make_unique<Phyxel::Chunk>(glm::ivec3(0, 0, 0));
        owned->initializeForLoading();
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z)
                owned->addCube(glm::ivec3(x, 15, z));   // floor: stand at y=16
        cm.chunkMap[glm::ivec3(0, 0, 0)] = owned.get();
        cm.chunks.push_back(std::move(owned));
    }
};

bool nodeInWall(const NavGraph::PathResult& r, int wallX, int z0, int z1) {
    for (const auto& n : r.nodes)
        if (n.x == wallX && n.z >= z0 && n.z <= z1) return true;
    return false;
}
} // namespace

TEST(NavObstacleTest, ObstacleBoxesBlockAndRerouteNavGraph) {
    FlatWorld w;
    NPCManager npc;
    npc.setChunkManager(&w.cm);

    const glm::vec3 from(5.5f, 17.0f, 15.5f), to(25.5f, 17.0f, 15.5f);
    NavAgentProfile agent;

    // Control: no obstacles — the direct route crosses the x=15 line mid-field.
    npc.buildNavGrid();
    ASSERT_NE(npc.getNavGraph(), nullptr);
    auto r1 = npc.getNavGraph()->findPath(from, to, agent);
    ASSERT_TRUE(r1.found);
    EXPECT_TRUE(nodeInWall(r1, 15, 4, 27)) << "control path didn't cross the field";

    // A 3-cube-tall obstacle wall at x=15, z=4..27 (gaps at both z ends): the path
    // must reroute around it and no node may sit inside a blocked column.
    npc.setNavObstacleProvider([]() {
        return std::vector<std::pair<glm::ivec3, glm::ivec3>>{
            {glm::ivec3(15, 16, 4), glm::ivec3(15, 18, 27)}};
    });
    npc.buildNavGrid();
    auto r2 = npc.getNavGraph()->findPath(from, to, agent);
    ASSERT_TRUE(r2.found) << "gaps exist at z<4 and z>27 - path should reroute";
    EXPECT_FALSE(nodeInWall(r2, 15, 4, 27)) << "path went THROUGH the obstacle";

    // Full-span wall (z=0..31): no way around on this chunk -> honestly not found.
    npc.setNavObstacleProvider([]() {
        return std::vector<std::pair<glm::ivec3, glm::ivec3>>{
            {glm::ivec3(15, 16, 0), glm::ivec3(15, 18, 31)}};
    });
    npc.buildNavGrid();
    auto r3 = npc.getNavGraph()->findPath(from, to, agent);
    EXPECT_FALSE(r3.found) << "full wall must block entirely";

    // Oversized boxes (a mis-fed structure bbox) are SKIPPED, not applied: the
    // 40x40x40 box would cover the whole chunk, but the path must stay open.
    npc.setNavObstacleProvider([]() {
        return std::vector<std::pair<glm::ivec3, glm::ivec3>>{
            {glm::ivec3(0, 0, 0), glm::ivec3(39, 39, 39)}};
    });
    npc.buildNavGrid();
    auto r4 = npc.getNavGraph()->findPath(from, to, agent);
    EXPECT_TRUE(r4.found) << "oversized box should be skipped, not block the world";
}
