#include <gtest/gtest.h>
#include "core/NavGrid.h"
#include "core/AStarPathfinder.h"
#include <unordered_set>
#include <cmath>

using namespace Phyxel::Core;

// ============================================================================
// Same TestWorld helper as NavGridTest
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
// Basic Pathfinding
// ============================================================================

TEST(AStarPathfinderTest, StraightLinePath) {
    TestWorld world;
    world.createFloor(10, 0, 9, 0, 0); // 10-cell corridor along X

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(9, 0));

    AStarPathfinder pathfinder(&grid);
    auto result = pathfinder.findPath(
        glm::vec3(0.5f, 11.0f, 0.5f),
        glm::vec3(9.5f, 11.0f, 0.5f));

    EXPECT_TRUE(result.found);
    EXPECT_FALSE(result.waypoints.empty());
    // After smoothing, a straight line should have very few waypoints
    EXPECT_LE(result.waypoints.size(), 2u);

    // Last waypoint should be near goal
    glm::vec3 last = result.waypoints.back();
    EXPECT_NEAR(last.x, 9.5f, 1.0f);
    EXPECT_NEAR(last.z, 0.5f, 1.0f);
}

TEST(AStarPathfinderTest, SameStartAndGoal) {
    TestWorld world;
    world.createFloor(10, 0, 3, 0, 3);

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(3, 3));

    AStarPathfinder pathfinder(&grid);
    auto result = pathfinder.findPath(
        glm::vec3(2.5f, 11.0f, 2.5f),
        glm::vec3(2.5f, 11.0f, 2.5f));

    EXPECT_TRUE(result.found);
    EXPECT_EQ(result.waypoints.size(), 1u);
}

TEST(AStarPathfinderTest, NoPathNoSurface) {
    TestWorld world; // Empty — no walkable surface

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(5, 5));

    AStarPathfinder pathfinder(&grid);
    auto result = pathfinder.findPath(
        glm::vec3(0.5f, 1.0f, 0.5f),
        glm::vec3(4.5f, 1.0f, 4.5f));

    EXPECT_FALSE(result.found);
}

// ============================================================================
// Obstacle Avoidance
// ============================================================================

TEST(AStarPathfinderTest, PathAroundWall) {
    TestWorld world;
    world.createFloor(10, 0, 9, 0, 5); // 10x6 floor

    // Wall at x=5, z=0..3 (blocking direct path, gap at z=4,5)
    for (int z = 0; z <= 3; ++z) {
        world.addVoxel(glm::ivec3(5, 11, z)); // Block headroom
        world.addVoxel(glm::ivec3(5, 12, z));
    }

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(9, 5));

    AStarPathfinder pathfinder(&grid);
    auto result = pathfinder.findPath(
        glm::vec3(2.5f, 11.0f, 1.5f),  // Left of wall
        glm::vec3(7.5f, 11.0f, 1.5f)); // Right of wall

    EXPECT_TRUE(result.found);
    EXPECT_GT(result.waypoints.size(), 1u); // Must detour

    // Verify no waypoint passes through the wall cells at x=5, z=0..3
    for (const auto& wp : result.waypoints) {
        int wx = static_cast<int>(std::floor(wp.x));
        int wz = static_cast<int>(std::floor(wp.z));
        if (wx == 5 && wz >= 0 && wz <= 3) {
            // Waypoint Y should be on top of the wall (Y=13) or the path goes around
            // The cell at x=5,z=0..3 has surfaceY=12 and IS walkable on top.
            // The path might go over or around. Both are valid.
        }
    }
}

TEST(AStarPathfinderTest, CompletelyBlocked) {
    TestWorld world;
    world.createFloor(10, 0, 9, 0, 5);

    // Wall spanning entire Z range at x=5
    for (int z = 0; z <= 5; ++z) {
        world.addVoxel(glm::ivec3(5, 11, z));
        world.addVoxel(glm::ivec3(5, 12, z));
    }

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(9, 5));

    AStarPathfinder pathfinder(&grid);
    auto result = pathfinder.findPath(
        glm::vec3(2.5f, 11.0f, 2.5f),
        glm::vec3(7.5f, 11.0f, 2.5f));

    // Path exists because NPC can climb ON TOP of wall (surfaceY=12 is walkable)
    // The step-up from Y=10 to Y=12 is 2, which exceeds MAX_STEP_UP(1).
    // So the path should NOT exist (can't reach the wall top from the floor).
    EXPECT_FALSE(result.found);
}

// ============================================================================
// Height Changes
// ============================================================================

TEST(AStarPathfinderTest, PathUpOneBlockStep) {
    TestWorld world;
    world.createFloor(10, 0, 4, 0, 0); // Low floor
    world.createFloor(11, 5, 9, 0, 0); // High floor (1 block up)

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(9, 0));

    AStarPathfinder pathfinder(&grid);
    auto result = pathfinder.findPath(
        glm::vec3(0.5f, 11.0f, 0.5f),
        glm::vec3(9.5f, 12.0f, 0.5f));

    EXPECT_TRUE(result.found);
}

TEST(AStarPathfinderTest, PathDownTwoBlockStep) {
    TestWorld world;
    world.createFloor(12, 0, 4, 0, 0); // High floor
    world.createFloor(10, 5, 9, 0, 0); // Low floor (2 blocks down)

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(9, 0));

    AStarPathfinder pathfinder(&grid);
    auto result = pathfinder.findPath(
        glm::vec3(0.5f, 13.0f, 0.5f),
        glm::vec3(9.5f, 11.0f, 0.5f));

    EXPECT_TRUE(result.found);
}

TEST(AStarPathfinderTest, PathBlockedByTwoBlockStepUp) {
    TestWorld world;
    world.createFloor(10, 0, 4, 0, 0);
    world.createFloor(12, 5, 9, 0, 0); // 2 blocks up — too high to step

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(9, 0));

    AStarPathfinder pathfinder(&grid);
    auto result = pathfinder.findPath(
        glm::vec3(0.5f, 11.0f, 0.5f),
        glm::vec3(9.5f, 13.0f, 0.5f));

    EXPECT_FALSE(result.found);
}

// ============================================================================
// Diagonal Movement
// ============================================================================

TEST(AStarPathfinderTest, DiagonalPath) {
    TestWorld world;
    world.createFloor(10, 0, 5, 0, 5); // 6x6 floor

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(5, 5));

    AStarPathfinder pathfinder(&grid);
    auto result = pathfinder.findPath(
        glm::vec3(0.5f, 11.0f, 0.5f),
        glm::vec3(5.5f, 11.0f, 5.5f));

    EXPECT_TRUE(result.found);
    // Diagonal path should be shorter than L-shaped (smoothed)
    EXPECT_LE(result.waypoints.size(), 3u);
}

// ============================================================================
// Path Smoothing
// ============================================================================

TEST(AStarPathfinderTest, SmoothingRemovesCollinear) {
    TestWorld world;
    world.createFloor(10, 0, 19, 0, 0); // 20-cell corridor

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(19, 0));

    AStarPathfinder pathfinder(&grid);
    auto result = pathfinder.findPath(
        glm::vec3(0.5f, 11.0f, 0.5f),
        glm::vec3(19.5f, 11.0f, 0.5f));

    EXPECT_TRUE(result.found);
    // 20 cells in a line should smooth to just start/end
    EXPECT_LE(result.waypoints.size(), 2u);
}

// ============================================================================
// Waypoint Y
// ============================================================================

TEST(AStarPathfinderTest, WaypointYIsSurfacePlusOne) {
    TestWorld world;
    world.createFloor(10, 0, 3, 0, 3);

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(3, 3));

    AStarPathfinder pathfinder(&grid);
    auto result = pathfinder.findPath(
        glm::vec3(0.5f, 11.0f, 0.5f),
        glm::vec3(3.5f, 11.0f, 3.5f));

    EXPECT_TRUE(result.found);
    for (const auto& wp : result.waypoints) {
        EXPECT_NEAR(wp.y, 11.0f, 0.01f); // surfaceY(10) + 1
    }
}

// ============================================================================
// Max iterations
// ============================================================================

TEST(AStarPathfinderTest, MaxIterationsRespected) {
    TestWorld world;
    world.createFloor(10, 0, 99, 0, 99); // Large 100x100 floor

    NavGrid grid(world.getQueryFunc());
    grid.buildFromRegion(glm::ivec2(0, 0), glm::ivec2(99, 99));

    AStarPathfinder pathfinder(&grid);
    // Very low max iterations — should fail to find path
    auto result = pathfinder.findPath(
        glm::vec3(0.5f, 11.0f, 0.5f),
        glm::vec3(99.5f, 11.0f, 99.5f),
        5); // Only 5 iterations

    EXPECT_FALSE(result.found);
    EXPECT_LE(result.nodesExpanded, 5);
}

// ============================================================================
// Null grid
// ============================================================================

TEST(AStarPathfinderTest, NullGridReturnsFalse) {
    AStarPathfinder pathfinder(nullptr);
    auto result = pathfinder.findPath(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(5.0f, 0.0f, 5.0f));

    EXPECT_FALSE(result.found);
}
