#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "core/ChunkVoxelManager.h"
#include "core/Cube.h"
#include "core/Subcube.h"
#include "core/Microcube.h"

using namespace Phyxel;

// ============================================================================
// Bulk-operation collision deferral — the build-freeze fix. Placing a structure runs tens of
// thousands of voxel adds; each per-voxel collision-shape build dominated (a 65k-voxel tavern froze
// ~14s in a Debug build). In bulk mode the per-voxel collision add must be SKIPPED for ALL three
// voxel levels (endBulkOperation rebuilds collision once). The CUBE path originally lacked the bulk
// guard that subcube/microcube had — this test would have caught that gap (solution-auditor finding).
// ============================================================================

namespace {
// A ChunkVoxelManager wired to in-memory backing storage + a counting addCollision callback, so we
// can assert exactly how many per-voxel collision adds fire.
struct Harness {
    std::vector<std::unique_ptr<Cube>> cubes{32768};   // 32^3, pre-sized like a real chunk
    std::vector<std::unique_ptr<Subcube>> subs;
    std::vector<std::unique_ptr<Microcube>> micros;
    glm::ivec3 origin{0, 0, 0};
    int collisionAdds = 0;
    bool bulk = true;
    ChunkVoxelManager vm;

    Harness() {
        vm.setCallbacks(
            [this]() -> std::vector<std::unique_ptr<Cube>>& { return cubes; },
            [this]() -> std::vector<std::unique_ptr<Subcube>>& { return subs; },
            [this]() -> std::vector<std::unique_ptr<Microcube>>& { return micros; },
            [this]() -> const glm::ivec3& { return origin; },
            [](bool) {}, [](bool) {}, []() {},
            [this](const glm::ivec3&) { ++collisionAdds; },   // addCollision — counted
            [](const glm::ivec3&) {},                          // removeCollision
            [](const glm::ivec3&) {},                          // updateNeighborCollisions
            [this]() -> bool { return bulk; },                 // isInBulkOperation
            []() {});                                          // updateVulkanBuffer
    }
};
} // namespace

// In bulk mode, NONE of cube/subcube/microcube fire a per-voxel collision add.
TEST(ChunkVoxelManagerBulkTest, CollisionDeferredDuringBulkForAllLevels) {
    Harness h;
    h.bulk = true;
    EXPECT_TRUE(h.vm.addCube(glm::ivec3(1, 1, 1), "Stone"));
    EXPECT_TRUE(h.vm.addSubcube(glm::ivec3(2, 2, 2), glm::ivec3(0, 0, 0), "Wood"));
    EXPECT_TRUE(h.vm.addMicrocube(glm::ivec3(3, 3, 3), glm::ivec3(0, 0, 0), glm::ivec3(0, 0, 0), "Wood"));
    EXPECT_EQ(h.collisionAdds, 0)
        << "per-voxel collision was NOT deferred in bulk mode (the cube path was the perf gap)";
}

// TEETH: outside bulk mode the per-voxel collision DOES fire — proving the guard isn't always-off
// (a fix that just deleted m_addCollision would pass the test above but fail this one).
TEST(ChunkVoxelManagerBulkTest, CollisionAddedWhenNotInBulk) {
    Harness h;
    h.bulk = false;
    EXPECT_TRUE(h.vm.addCube(glm::ivec3(1, 1, 1), "Stone"));
    EXPECT_EQ(h.collisionAdds, 1) << "collision not added for a normal (non-bulk) cube placement";
    EXPECT_TRUE(h.vm.addSubcube(glm::ivec3(4, 4, 4), glm::ivec3(0, 0, 0), "Wood"));
    EXPECT_EQ(h.collisionAdds, 2) << "collision not added for a normal (non-bulk) subcube placement";
}
