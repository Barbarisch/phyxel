// OccupancyCoverageTest — every static-voxel writer must keep the chunk's VoxelOccupancyGrid in
// step with its voxel store (DebrisInteractionPlan 1c step 5).
//
// Since 1c, the grid is the ONE occupancy: CPU physics (characters, furniture) collides against it,
// and lighting and GPU debris read a packed copy of it. A writer that changes the store but not the
// grid leaves all three wrong at once — characters walk on a broken floor, light stops at a wall
// that is gone, debris rests on air. The audit (plan, 1c step 5) found the writers that skip it;
// each gets a case here, written red before its fix.

#include <gtest/gtest.h>

#include "core/Chunk.h"
#include "core/ChunkManager.h"
#include "core/ObjectTemplateManager.h"
#include "physics/VoxelOccupancyGrid.h"

#include <filesystem>
#include <fstream>

using namespace Phyxel;

namespace {

/// Build a chunk exactly the way the streaming WORKER does (ChunkStreamingManager): dense storage
/// with no-op callbacks, terrain written while loading, voxel maps, one forced grid rebuild — then
/// handed to the main thread (initialize). This is the state every streamed chunk is played in.
void buildLikeTheStreamingWorker(Chunk& c) {
    c.initializeForLoading();
    for (int x = 0; x < 8; ++x)
        for (int z = 0; z < 8; ++z)
            for (int y = 0; y < 4; ++y)
                c.addCube(glm::ivec3(x, y, z), "Stone");
    c.initializeVoxelMaps();
    c.forcePhysicsRebuild();
    c.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
}

/// One cube, one subcube and one microcube, each in its own cube cell.
std::string writeCoverageTemplate() {
    const auto path = std::filesystem::temp_directory_path() / "occ_coverage_probe.voxel";
    std::ofstream f(path);
    f << "C 0 0 0 Stone\n"
         "S 1 0 0 1 0 1 Stone\n"
         "M 2 0 0 0 0 0 2 0 2 Stone\n";
    return path.string();
}

}  // namespace

// GAP 1 (verified in code): the worker's initializeForLoading() put the chunk in physics "bulk
// mode" and nothing on the streaming path ever took it out, so every per-voxel edit afterwards
// skipped the grid. A player break, a blast's cube breaks, clear_region, undo — none of it reached
// collision, lighting or debris in a streamed world.
TEST(OccupancyCoverage, EditsToAStreamedChunkReachTheGrid) {
    Chunk c(glm::ivec3(0, 0, 0));
    buildLikeTheStreamingWorker(c);
    const auto& g = c.getOccupancyGrid();
    ASSERT_TRUE(g.isCubeFilled({2, 3, 2})) << "the worker's rebuild must have filled the grid";

    // Break a cube (player B key, DamageSystem cube break, clear_region).
    ASSERT_TRUE(c.removeCube({2, 3, 2}));
    EXPECT_FALSE(g.isCubeFilled({2, 3, 2})) << "a broken cube still collides: debris/characters rest on air";

    // Place a cube (player C key, undo/redo re-add).
    ASSERT_TRUE(c.addCube({5, 4, 5}, "Stone"));
    EXPECT_TRUE(g.isCubeFilled({5, 4, 5})) << "a placed cube does not collide";

    // Place a subcube and a microcube (sub-voxel building).
    ASSERT_TRUE(c.addSubcube({6, 4, 6}, {1, 0, 1}, "Stone"));
    EXPECT_TRUE(g.isCubeFilled({6, 4, 6}));
    EXPECT_TRUE(g.isSubdivided({6, 4, 6}));
    EXPECT_TRUE(g.isSubcubeFilled({6, 4, 6}, {1, 0, 1})) << "a placed subcube does not collide";

    ASSERT_TRUE(c.addMicrocube({7, 4, 7}, {0, 0, 0}, {2, 0, 2}, "Stone"));
    EXPECT_TRUE(g.isCubeFilled({7, 4, 7}));
    EXPECT_TRUE(g.isMicrocubeFilled({7, 4, 7}, {0, 0, 0}, {2, 0, 2})) << "a placed microcube does not collide";
}

// The control: a chunk that never went through the loading lifecycle was always fine — this pins
// that the case above fails for the lifecycle reason, not because edits never reach the grid.
TEST(OccupancyCoverage, ControlEditsToAnOrdinaryChunkReachTheGrid) {
    Chunk c(glm::ivec3(0, 0, 0));
    c.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    ASSERT_TRUE(c.addCube({2, 3, 2}, "Stone"));
    EXPECT_TRUE(c.getOccupancyGrid().isCubeFilled({2, 3, 2}));
    ASSERT_TRUE(c.removeCube({2, 3, 2}));
    EXPECT_FALSE(c.getOccupancyGrid().isCubeFilled({2, 3, 2}));
}

// GAP 4: the O(1) uniform fill (WorldGenerator's deep-stone chunks, the DB blob decoder's
// whole-chunk run) wrote the voxel store only. Outside bulk mode nothing rebuilt the grid, so
// /api generate_world's deep chunks were air to collision, lighting and debris.
TEST(OccupancyCoverage, UniformFillReachesTheGrid) {
    Chunk c(glm::ivec3(0, 0, 0));
    c.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    c.fillAllCubes("Stone");
    const auto& g = c.getOccupancyGrid();
    EXPECT_TRUE(g.isCubeFilled({0, 0, 0})) << "a uniformly filled chunk is air to collision";
    EXPECT_TRUE(g.isCubeFilled({31, 31, 31}));
    EXPECT_TRUE(g.isCubeFilled({17, 4, 29}));
}

// GAP 7: the light/debris pool skips a chunk whose (origin, revision) it already packed. Every
// grid counted its revision from 0, so a chunk evicted and re-created between two repack passes
// could present the cached revision with DIFFERENT contents, and the pool kept the old cells.
// Revisions must identify a grid state across grid objects, not just within one.
TEST(OccupancyCoverage, RevisionsDoNotRepeatAcrossGridObjects) {
    Phyxel::Physics::VoxelOccupancyGrid before, after;   // the same chunk origin, before and after a reload
    before.setCube({1, 1, 1}, true);
    after.setCube({9, 9, 9}, true);             // different contents, same number of edits
    EXPECT_NE(before.revision(), after.revision())
        << "two different grid states share revision " << after.revision()
        << ": the pool would keep the evicted chunk's cells";
}

// GAP 2: both template paths (spawnTemplate, spawnOrEraseMicro) put each touched chunk in bulk mode,
// add the voxels (which skips the grid), then call batchUpdateCollisions() - which rebuilds only if
// collisionNeedsUpdate happens to be set, and nothing in bulk mode sets it. So placed objects,
// flora, StructureForge fixtures and moved/rotated placed objects never reached the grid.
TEST(OccupancyCoverage, TemplateSpawnsReachTheGrid) {
    ChunkManager cm;
    cm.initialize(VK_NULL_HANDLE, VK_NULL_HANDLE);
    ObjectTemplateManager otm(&cm, nullptr);
    ASSERT_TRUE(otm.loadTemplate(writeCoverageTemplate()));

    // Cube-resolution path, into a chunk that already exists in its played state.
    cm.ensureChunkAt({4, 4, 4});
    Chunk* c = cm.getChunkAtFast({4, 4, 4});
    ASSERT_NE(c, nullptr);
    ASSERT_TRUE(otm.spawnTemplate("occ_coverage_probe", glm::vec3(4, 4, 4), true, 0));
    const auto& g = c->getOccupancyGrid();
    EXPECT_TRUE(g.isCubeFilled({4, 4, 4})) << "a spawned template's cube does not collide";
    EXPECT_TRUE(g.isSubcubeFilled({5, 4, 4}, {1, 0, 1})) << "a spawned template's subcube does not collide";
    EXPECT_TRUE(g.isMicrocubeFilled({6, 4, 4}, {0, 0, 0}, {2, 0, 2}))
        << "a spawned template's microcube does not collide";

    // Micro-resolution path (placed objects, StructureForge fixtures), another cell.
    ASSERT_TRUE(otm.spawnTemplateMicro("occ_coverage_probe", glm::ivec3(4, 10, 4) * 9, 0));
    EXPECT_TRUE(g.isCubeFilled({4, 10, 4})) << "a micro-spawned template's cube does not collide";
    EXPECT_TRUE(g.isMicrocubeFilled({6, 10, 4}, {0, 0, 0}, {2, 0, 2}))
        << "a micro-spawned template's microcube does not collide";
}
