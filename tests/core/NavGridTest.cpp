#include <gtest/gtest.h>
#include "core/NavGrid.h"
#include <unordered_set>

using namespace Phyxel::Core;

// ============================================================================
// Helper: simple voxel world using a set
// ============================================================================
namespace {

struct IVec3Hash {
    size_t operator()(const glm::ivec3& v) const {
        return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 10) ^ (std::hash<int>()(v.z) << 20);
    }
};

class TestWorld {
public:
    void addVoxel(const glm::ivec3& pos) { m_voxels.insert(pos); }
    void removeVoxel(const glm::ivec3& pos) { m_voxels.erase(pos); }

    VoxelQueryFunc getQueryFunc() const {
        return [this](const glm::ivec3& pos) -> bool {
            return m_voxels.count(pos) > 0;
        };
    }

    // Create a flat ground at given Y for XZ range
    void createFloor(int y, int minX, int maxX, int minZ, int maxZ) {
        for (int x = minX; x <= maxX; ++x)
            for (int z = minZ; z <= maxZ; ++z)
                addVoxel(glm::ivec3(x, y, z));
    }

private:
    std::unordered_set<glm::ivec3, IVec3Hash> m_voxels;
};

} // anonymous namespace

// ============================================================================
// NavGrid Construction
// ============================================================================

TEST(NavGridTest, ConstructWithQueryFunc) {
    TestWorld world;
    NavGrid grid(world.getQueryFunc());
    EXPECT_EQ(grid.cellCount(), 0u);
}

TEST(NavGridTest, BuildEmptyRegion) {
    TestWorld world; // No voxels
    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(3, 3));

    EXPECT_EQ(grid.cellCount(), 16u); // 4x4 cells
    EXPECT_EQ(grid.walkableCellCount(), 0u); // No surfaces
}

// ============================================================================
// Surface Detection
// ============================================================================

TEST(NavGridTest, FlatFloorWalkable) {
    TestWorld world;
    world.createFloor(10, 0, 3, 0, 3); // 4x4 floor at Y=10

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(3, 3));

    EXPECT_EQ(grid.cellCount(), 16u);
    EXPECT_EQ(grid.walkableCellCount(), 16u);

    const NavCell* cell = grid.getCell(2, 2);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->surfaceY, 10);
    EXPECT_TRUE(cell->walkable);
}

TEST(NavGridTest, SurfaceWithLowCeiling) {
    TestWorld world;
    world.createFloor(10, 0, 3, 0, 3); // Floor at Y=10
    // Ceiling at Y=12 (only 1 block headroom between floor and ceiling)
    world.createFloor(12, 0, 3, 0, 3);

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(3, 3));

    // NavGrid finds topmost solid = Y=12 (ceiling). Headroom above Y=12 is fine.
    // Cells are walkable on top of the ceiling, not between floor and ceiling.
    const NavCell* cell = grid.getCell(1, 1);
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->surfaceY, 12); // Top surface is the ceiling
    EXPECT_TRUE(cell->walkable);   // Headroom above ceiling is clear
}

TEST(NavGridTest, SurfaceWithExactHeadroom) {
    TestWorld world;
    world.createFloor(10, 0, 3, 0, 3); // Floor at Y=10
    // Ceiling at Y=13 (2 blocks headroom at Y=11,12 — exactly MIN_HEADROOM)
    world.createFloor(13, 0, 3, 0, 3);

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(3, 3));

    EXPECT_EQ(grid.walkableCellCount(), 16u); // 2 blocks headroom is sufficient
}

TEST(NavGridTest, CellOutsideRegionReturnsNull) {
    TestWorld world;
    world.createFloor(10, 0, 3, 0, 3);

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(3, 3));

    EXPECT_EQ(grid.getCell(5, 5), nullptr);
    EXPECT_EQ(grid.getCell(-1, 0), nullptr);
}

// ============================================================================
// Neighbor Queries
// ============================================================================

TEST(NavGridTest, FlatFloorNeighbors) {
    TestWorld world;
    world.createFloor(10, 0, 4, 0, 4); // 5x5 floor

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(4, 4));

    // Center cell should have 8 neighbors
    const NavCell* center = grid.getCell(2, 2);
    ASSERT_NE(center, nullptr);
    auto neighbors = grid.getNeighbors(center);
    EXPECT_EQ(neighbors.size(), 8u);

    // Corner cell should have 3 neighbors
    const NavCell* corner = grid.getCell(0, 0);
    ASSERT_NE(corner, nullptr);
    neighbors = grid.getNeighbors(corner);
    EXPECT_EQ(neighbors.size(), 3u);

    // Edge cell should have 5 neighbors
    const NavCell* edge = grid.getCell(0, 2);
    ASSERT_NE(edge, nullptr);
    neighbors = grid.getNeighbors(edge);
    EXPECT_EQ(neighbors.size(), 5u);
}

TEST(NavGridTest, StepUpOneBlock) {
    TestWorld world;
    // Low floor at Y=10 for x=0..2
    world.createFloor(10, 0, 2, 0, 0);
    // High floor at Y=11 for x=3..5 (1 block step up)
    world.createFloor(11, 3, 5, 0, 0);

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(5, 0));

    const NavCell* low = grid.getCell(2, 0);
    ASSERT_NE(low, nullptr);
    EXPECT_EQ(low->surfaceY, 10);

    const NavCell* high = grid.getCell(3, 0);
    ASSERT_NE(high, nullptr);
    EXPECT_EQ(high->surfaceY, 11);

    // low -> high should be a valid neighbor (step up = 1)
    auto neighbors = grid.getNeighbors(low);
    bool foundHigh = false;
    for (auto* n : neighbors) {
        if (n->x == 3 && n->z == 0) foundHigh = true;
    }
    EXPECT_TRUE(foundHigh);
}

TEST(NavGridTest, StepUpTwoBlocksBlocked) {
    TestWorld world;
    world.createFloor(10, 0, 2, 0, 0);
    world.createFloor(12, 3, 5, 0, 0); // 2 block step up — too high

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(5, 0));

    const NavCell* low = grid.getCell(2, 0);
    auto neighbors = grid.getNeighbors(low);
    bool foundHigh = false;
    for (auto* n : neighbors) {
        if (n->x == 3 && n->z == 0) foundHigh = true;
    }
    EXPECT_FALSE(foundHigh); // Step up of 2 is not allowed
}

TEST(NavGridTest, StepDownTwoBlocksAllowed) {
    TestWorld world;
    world.createFloor(12, 0, 2, 0, 0);
    world.createFloor(10, 3, 5, 0, 0); // 2 block drop — should be OK

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(5, 0));

    const NavCell* high = grid.getCell(2, 0);
    auto neighbors = grid.getNeighbors(high);
    bool foundLow = false;
    for (auto* n : neighbors) {
        if (n->x == 3 && n->z == 0) foundLow = true;
    }
    EXPECT_TRUE(foundLow); // Step down of 2 is allowed
}

TEST(NavGridTest, StepDownThreeBlocksBlocked) {
    TestWorld world;
    world.createFloor(13, 0, 2, 0, 0);
    world.createFloor(10, 3, 5, 0, 0); // 3 block drop — too much

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(5, 0));

    const NavCell* high = grid.getCell(2, 0);
    auto neighbors = grid.getNeighbors(high);
    bool foundLow = false;
    for (auto* n : neighbors) {
        if (n->x == 3 && n->z == 0) foundLow = true;
    }
    EXPECT_FALSE(foundLow);
}

TEST(NavGridTest, DiagonalBlockedByOrthogonal) {
    TestWorld world;
    world.createFloor(10, 0, 2, 0, 2);
    // Remove walkable surface at (1,0) and (0,1) to block diagonal movement from (0,0) to (1,1)
    world.removeVoxel(glm::ivec3(1, 10, 0)); // Remove surface at (1,0)
    world.removeVoxel(glm::ivec3(0, 10, 1)); // Remove surface at (0,1)

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(2, 2));

    const NavCell* origin = grid.getCell(0, 0);
    ASSERT_NE(origin, nullptr);
    auto neighbors = grid.getNeighbors(origin);

    // Should NOT have (1,1) as neighbor because both orthogonals are unwalkable
    bool foundDiag = false;
    for (auto* n : neighbors) {
        if (n->x == 1 && n->z == 1) foundDiag = true;
    }
    EXPECT_FALSE(foundDiag);
}

// ============================================================================
// FindNearestWalkable
// ============================================================================

TEST(NavGridTest, FindNearestWalkableCenter) {
    TestWorld world;
    world.createFloor(10, 0, 4, 0, 4);

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(4, 4));

    const NavCell* cell = grid.findNearestWalkable(glm::vec3(2.5f, 11.0f, 2.5f));
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->x, 2);
    EXPECT_EQ(cell->z, 2);
}

TEST(NavGridTest, FindNearestWalkableNearby) {
    TestWorld world;
    // Only walkable at (3, 3)
    world.addVoxel(glm::ivec3(3, 10, 3));

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(5, 5));

    const NavCell* cell = grid.findNearestWalkable(glm::vec3(1.5f, 11.0f, 1.5f));
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->x, 3);
    EXPECT_EQ(cell->z, 3);
}

TEST(NavGridTest, FindNearestWalkableNone) {
    TestWorld world; // Empty — no walkable cells

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(3, 3));

    const NavCell* cell = grid.findNearestWalkable(glm::vec3(1.5f, 0.0f, 1.5f));
    EXPECT_EQ(cell, nullptr);
}

// ============================================================================
// Rebuild
// ============================================================================

TEST(NavGridTest, RebuildCellAfterVoxelChange) {
    TestWorld world;
    world.createFloor(10, 0, 3, 0, 3);

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(3, 3));

    EXPECT_EQ(grid.walkableCellCount(), 16u);

    // Remove floor at (1,1)
    world.removeVoxel(glm::ivec3(1, 10, 1));
    grid.rebuildCell(1, 1);

    const NavCell* cell = grid.getCell(1, 1);
    ASSERT_NE(cell, nullptr);
    EXPECT_FALSE(cell->walkable);
    EXPECT_EQ(grid.walkableCellCount(), 15u);

    // Add it back
    world.addVoxel(glm::ivec3(1, 10, 1));
    grid.rebuildCell(1, 1);

    cell = grid.getCell(1, 1);
    ASSERT_NE(cell, nullptr);
    EXPECT_TRUE(cell->walkable);
    EXPECT_EQ(grid.walkableCellCount(), 16u);
}

// ============================================================================
// Wall detection
// ============================================================================

TEST(NavGridTest, WallBlocksWalkability) {
    TestWorld world;
    world.createFloor(10, 0, 4, 0, 0); // Floor

    // Place a 2-high wall at x=2 (blocks headroom)
    world.addVoxel(glm::ivec3(2, 11, 0));
    world.addVoxel(glm::ivec3(2, 12, 0));

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(4, 0));

    // x=2 has highest voxel at y=12, headroom check starts at y=13
    // Surface should be at y=12 (top of wall). Is there headroom above it? Yes (no voxels above).
    // So it would be walkable ON TOP of the wall.
    const NavCell* wallCell = grid.getCell(2, 0);
    ASSERT_NE(wallCell, nullptr);
    EXPECT_EQ(wallCell->surfaceY, 12); // Top of wall
    EXPECT_TRUE(wallCell->walkable); // Can stand on top

    // But neighbors: cell at x=1 (surface Y=10) → step up to Y=12 = +2, which exceeds MAX_STEP_UP(1)
    const NavCell* before = grid.getCell(1, 0);
    ASSERT_NE(before, nullptr);
    auto neighbors = grid.getNeighbors(before);
    bool canReachWall = false;
    for (auto* n : neighbors) {
        if (n->x == 2) canReachWall = true;
    }
    EXPECT_FALSE(canReachWall); // Can't step up 2 blocks
}
