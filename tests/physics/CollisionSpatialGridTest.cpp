#include <gtest/gtest.h>
#include "physics/CollisionSpatialGrid.h"
#include <glm/glm.hpp>
#include <memory>

using namespace Phyxel::Physics;
using Entity = CollisionSpatialGrid::CollisionEntity;

// Helper to make a CollisionEntity without a real Bullet shape
static std::shared_ptr<Entity> makeCube(const glm::vec3& center = {0, 0, 0}) {
    return std::make_shared<Entity>(nullptr, Entity::CUBE, center);
}

static std::shared_ptr<Entity> makeSubcube(const glm::vec3& center = {0, 0, 0}) {
    return std::make_shared<Entity>(nullptr, Entity::SUBCUBE, center);
}

// CollisionSpatialGrid has a 32x32x32 array of vectors (~786KB) so it must be
// heap-allocated to avoid stack overflow on Windows (1MB default stack).
static auto makeGrid() {
    return std::make_unique<CollisionSpatialGrid>();
}

// ============================================================================
// Construction / Defaults
// ============================================================================

TEST(CollisionSpatialGridTest, EmptyGridCounters) {
    auto grid = makeGrid();
    EXPECT_EQ(grid->getTotalEntityCount(), 0u);
    EXPECT_EQ(grid->getCubeEntityCount(), 0u);
    EXPECT_EQ(grid->getSubcubeEntityCount(), 0u);
    EXPECT_EQ(grid->getOccupiedCellCount(), 0u);
}

TEST(CollisionSpatialGridTest, EmptyGridValidates) {
    auto grid = makeGrid();
    EXPECT_TRUE(grid->validateGrid());
}

// ============================================================================
// Add Entity
// ============================================================================

TEST(CollisionSpatialGridTest, AddSingleCube) {
    auto grid = makeGrid();
    grid->addEntity({0, 0, 0}, makeCube());

    EXPECT_EQ(grid->getTotalEntityCount(), 1u);
    EXPECT_EQ(grid->getCubeEntityCount(), 1u);
    EXPECT_EQ(grid->getSubcubeEntityCount(), 0u);
    EXPECT_EQ(grid->getOccupiedCellCount(), 1u);
}

TEST(CollisionSpatialGridTest, AddSingleSubcube) {
    auto grid = makeGrid();
    grid->addEntity({5, 5, 5}, makeSubcube());

    EXPECT_EQ(grid->getTotalEntityCount(), 1u);
    EXPECT_EQ(grid->getCubeEntityCount(), 0u);
    EXPECT_EQ(grid->getSubcubeEntityCount(), 1u);
}

TEST(CollisionSpatialGridTest, AddMultipleToSameCell) {
    auto grid = makeGrid();
    grid->addEntity({10, 10, 10}, makeCube());
    grid->addEntity({10, 10, 10}, makeSubcube());
    grid->addEntity({10, 10, 10}, makeSubcube());

    EXPECT_EQ(grid->getTotalEntityCount(), 3u);
    EXPECT_EQ(grid->getCubeEntityCount(), 1u);
    EXPECT_EQ(grid->getSubcubeEntityCount(), 2u);
    EXPECT_EQ(grid->getOccupiedCellCount(), 1u);
}

TEST(CollisionSpatialGridTest, AddToDifferentCells) {
    auto grid = makeGrid();
    grid->addEntity({0, 0, 0}, makeCube());
    grid->addEntity({1, 0, 0}, makeCube());
    grid->addEntity({0, 1, 0}, makeCube());

    EXPECT_EQ(grid->getTotalEntityCount(), 3u);
    EXPECT_EQ(grid->getOccupiedCellCount(), 3u);
}

TEST(CollisionSpatialGridTest, AddAtGridCorners) {
    auto grid = makeGrid();
    grid->addEntity({0, 0, 0}, makeCube());
    grid->addEntity({31, 0, 0}, makeCube());
    grid->addEntity({0, 31, 0}, makeCube());
    grid->addEntity({0, 0, 31}, makeCube());
    grid->addEntity({31, 31, 31}, makeCube());

    EXPECT_EQ(grid->getTotalEntityCount(), 5u);
    EXPECT_EQ(grid->getOccupiedCellCount(), 5u);
    EXPECT_TRUE(grid->validateGrid());
}

// ============================================================================
// Invalid Positions (out of bounds)
// ============================================================================

TEST(CollisionSpatialGridTest, AddOutOfBoundsIgnored) {
    auto grid = makeGrid();
    grid->addEntity({-1, 0, 0}, makeCube());
    grid->addEntity({32, 0, 0}, makeCube());
    grid->addEntity({0, -5, 0}, makeCube());
    grid->addEntity({0, 0, 100}, makeCube());

    EXPECT_EQ(grid->getTotalEntityCount(), 0u);
}

TEST(CollisionSpatialGridTest, GetEntitiesOutOfBoundsReturnsEmpty) {
    auto grid = makeGrid();
    grid->addEntity({5, 5, 5}, makeCube());

    auto& result = grid->getEntitiesAt({-1, 0, 0});
    EXPECT_TRUE(result.empty());
}

// ============================================================================
// Get Entities
// ============================================================================

TEST(CollisionSpatialGridTest, GetEntitiesReturnsCorrectCell) {
    auto grid = makeGrid();
    auto cube = makeCube({5, 5, 5});
    grid->addEntity({5, 5, 5}, cube);

    auto& entities = grid->getEntitiesAt({5, 5, 5});
    ASSERT_EQ(entities.size(), 1u);
    EXPECT_EQ(entities[0], cube);
}

TEST(CollisionSpatialGridTest, GetEntitiesEmptyCellReturnsEmpty) {
    auto grid = makeGrid();
    auto& entities = grid->getEntitiesAt({15, 15, 15});
    EXPECT_TRUE(entities.empty());
}

TEST(CollisionSpatialGridTest, GetEntitiesConstVersion) {
    auto grid = makeGrid();
    grid->addEntity({3, 3, 3}, makeCube());

    const auto& constGrid = *grid;
    const auto& entities = constGrid.getEntitiesAt({3, 3, 3});
    EXPECT_EQ(entities.size(), 1u);
}

// ============================================================================
// Remove Entity
// ============================================================================

TEST(CollisionSpatialGridTest, RemoveSingleEntity) {
    auto grid = makeGrid();
    auto cube = makeCube();
    grid->addEntity({0, 0, 0}, cube);
    EXPECT_EQ(grid->getTotalEntityCount(), 1u);

    grid->removeEntity({0, 0, 0}, cube);
    EXPECT_EQ(grid->getTotalEntityCount(), 0u);
    EXPECT_EQ(grid->getCubeEntityCount(), 0u);
}

TEST(CollisionSpatialGridTest, RemoveSubcubeCounterUpdates) {
    auto grid = makeGrid();
    auto sub = makeSubcube();
    grid->addEntity({1, 1, 1}, sub);
    grid->removeEntity({1, 1, 1}, sub);
    EXPECT_EQ(grid->getSubcubeEntityCount(), 0u);
}

TEST(CollisionSpatialGridTest, RemoveNonexistentEntityNoEffect) {
    auto grid = makeGrid();
    auto cube1 = makeCube();
    auto cube2 = makeCube();
    grid->addEntity({0, 0, 0}, cube1);

    grid->removeEntity({0, 0, 0}, cube2);  // cube2 was never added
    EXPECT_EQ(grid->getTotalEntityCount(), 1u);
}

TEST(CollisionSpatialGridTest, RemoveFromWrongPositionNoEffect) {
    auto grid = makeGrid();
    auto cube = makeCube();
    grid->addEntity({5, 5, 5}, cube);

    grid->removeEntity({6, 6, 6}, cube);  // Wrong cell
    EXPECT_EQ(grid->getTotalEntityCount(), 1u);
}

TEST(CollisionSpatialGridTest, RemoveFromOutOfBoundsNoEffect) {
    auto grid = makeGrid();
    auto cube = makeCube();
    grid->addEntity({0, 0, 0}, cube);

    grid->removeEntity({-1, 0, 0}, cube);
    EXPECT_EQ(grid->getTotalEntityCount(), 1u);
}

// ============================================================================
// Remove All At
// ============================================================================

TEST(CollisionSpatialGridTest, RemoveAllAtPosition) {
    auto grid = makeGrid();
    grid->addEntity({5, 5, 5}, makeCube());
    grid->addEntity({5, 5, 5}, makeSubcube());
    grid->addEntity({5, 5, 5}, makeSubcube());
    EXPECT_EQ(grid->getTotalEntityCount(), 3u);

    grid->removeAllAt({5, 5, 5});
    EXPECT_EQ(grid->getTotalEntityCount(), 0u);
    EXPECT_EQ(grid->getCubeEntityCount(), 0u);
    EXPECT_EQ(grid->getSubcubeEntityCount(), 0u);
    EXPECT_EQ(grid->getOccupiedCellCount(), 0u);
}

TEST(CollisionSpatialGridTest, RemoveAllAtDoesNotAffectOtherCells) {
    auto grid = makeGrid();
    grid->addEntity({5, 5, 5}, makeCube());
    grid->addEntity({10, 10, 10}, makeCube());

    grid->removeAllAt({5, 5, 5});
    EXPECT_EQ(grid->getTotalEntityCount(), 1u);
    EXPECT_EQ(grid->getOccupiedCellCount(), 1u);
}

TEST(CollisionSpatialGridTest, RemoveAllAtOutOfBoundsNoEffect) {
    auto grid = makeGrid();
    grid->addEntity({0, 0, 0}, makeCube());

    grid->removeAllAt({-1, -1, -1});
    EXPECT_EQ(grid->getTotalEntityCount(), 1u);
}

// ============================================================================
// Clear
// ============================================================================

TEST(CollisionSpatialGridTest, ClearRemovesEverything) {
    auto grid = makeGrid();
    for (int i = 0; i < 10; ++i) {
        grid->addEntity({i, i, i}, makeCube());
        grid->addEntity({i, i, i}, makeSubcube());
    }
    EXPECT_EQ(grid->getTotalEntityCount(), 20u);

    grid->clear();
    EXPECT_EQ(grid->getTotalEntityCount(), 0u);
    EXPECT_EQ(grid->getCubeEntityCount(), 0u);
    EXPECT_EQ(grid->getSubcubeEntityCount(), 0u);
    EXPECT_EQ(grid->getOccupiedCellCount(), 0u);
    EXPECT_TRUE(grid->validateGrid());
}

// ============================================================================
// Reserve
// ============================================================================

TEST(CollisionSpatialGridTest, ReserveDoesNotChangeCount) {
    auto grid = makeGrid();
    grid->reserve(1000);
    EXPECT_EQ(grid->getTotalEntityCount(), 0u);
    EXPECT_TRUE(grid->validateGrid());
}

// ============================================================================
// Entity Type Properties
// ============================================================================

TEST(CollisionSpatialGridTest, EntityTypeDetection) {
    auto cube = makeCube();
    auto sub = makeSubcube();

    EXPECT_TRUE(cube->isCube());
    EXPECT_FALSE(cube->isSubcube());
    EXPECT_TRUE(sub->isSubcube());
    EXPECT_FALSE(sub->isCube());
}

TEST(CollisionSpatialGridTest, EntityWorldCenter) {
    glm::vec3 center(10.5f, 20.5f, 30.5f);
    auto entity = std::make_shared<Entity>(nullptr, Entity::CUBE, center, 1.0f);

    EXPECT_EQ(entity->worldCenter, center);
    EXPECT_FLOAT_EQ(entity->boundingRadius, 1.0f);
}

TEST(CollisionSpatialGridTest, SubcubeHierarchyData) {
    auto sub = makeSubcube();
    // Default hierarchy data should be zero
    EXPECT_EQ(sub->parentChunkPos, glm::ivec3(0));
    EXPECT_EQ(sub->subcubeLocalPos, glm::ivec3(0));
}

TEST(CollisionSpatialGridTest, EntityNotInCompoundByDefault) {
    auto entity = makeCube();
    EXPECT_FALSE(entity->isInCompound);
}

// ============================================================================
// Validate Grid
// ============================================================================

TEST(CollisionSpatialGridTest, ValidateAfterMixedOperations) {
    auto grid = makeGrid();
    auto c1 = makeCube();
    auto c2 = makeCube();
    auto s1 = makeSubcube();
    auto s2 = makeSubcube();
    auto s3 = makeSubcube();

    grid->addEntity({0, 0, 0}, c1);
    grid->addEntity({0, 0, 0}, s1);
    grid->addEntity({15, 15, 15}, c2);
    grid->addEntity({15, 15, 15}, s2);
    grid->addEntity({31, 31, 31}, s3);

    EXPECT_TRUE(grid->validateGrid());
    EXPECT_EQ(grid->getTotalEntityCount(), 5u);
    EXPECT_EQ(grid->getCubeEntityCount(), 2u);
    EXPECT_EQ(grid->getSubcubeEntityCount(), 3u);

    grid->removeEntity({0, 0, 0}, c1);
    EXPECT_TRUE(grid->validateGrid());
    EXPECT_EQ(grid->getCubeEntityCount(), 1u);

    grid->removeAllAt({15, 15, 15});
    EXPECT_TRUE(grid->validateGrid());
    EXPECT_EQ(grid->getTotalEntityCount(), 2u);  // s1 at (0,0,0) + s3 at (31,31,31)
}

// ============================================================================
// Stress: many entities
// ============================================================================

TEST(CollisionSpatialGridTest, FillAllCells) {
    auto grid = makeGrid();
    int count = 0;
    // Put one cube in each cell
    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y) {
            for (int z = 0; z < 32; ++z) {
                grid->addEntity({x, y, z}, makeCube());
                count++;
            }
        }
    }
    EXPECT_EQ(grid->getTotalEntityCount(), static_cast<size_t>(count));
    EXPECT_EQ(grid->getOccupiedCellCount(), 32u * 32u * 32u);
    EXPECT_TRUE(grid->validateGrid());
}
