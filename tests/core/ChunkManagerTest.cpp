#include <gtest/gtest.h>
#include "core/ChunkManager.h"
#include "utils/CoordinateUtils.h"
#include <glm/glm.hpp>
#include <memory>

namespace Phyxel {
namespace Testing {

// ===================================================================
// ChunkManager Static Coordinate Helpers
// Verifies ChunkManager's forwarding to CoordinateUtils
// ===================================================================

class ChunkManagerStaticTest : public ::testing::Test {};

TEST_F(ChunkManagerStaticTest, WorldToChunkCoord_PositiveOrigin) {
    EXPECT_EQ(ChunkManager::worldToChunkCoord(glm::ivec3(0, 0, 0)), glm::ivec3(0, 0, 0));
}

TEST_F(ChunkManagerStaticTest, WorldToChunkCoord_InsideChunk) {
    EXPECT_EQ(ChunkManager::worldToChunkCoord(glm::ivec3(15, 10, 31)), glm::ivec3(0, 0, 0));
}

TEST_F(ChunkManagerStaticTest, WorldToChunkCoord_ChunkBoundary) {
    EXPECT_EQ(ChunkManager::worldToChunkCoord(glm::ivec3(32, 0, 0)), glm::ivec3(1, 0, 0));
    EXPECT_EQ(ChunkManager::worldToChunkCoord(glm::ivec3(0, 32, 0)), glm::ivec3(0, 1, 0));
    EXPECT_EQ(ChunkManager::worldToChunkCoord(glm::ivec3(0, 0, 32)), glm::ivec3(0, 0, 1));
}

TEST_F(ChunkManagerStaticTest, WorldToChunkCoord_Negative) {
    EXPECT_EQ(ChunkManager::worldToChunkCoord(glm::ivec3(-1, 0, 0)), glm::ivec3(-1, 0, 0));
    EXPECT_EQ(ChunkManager::worldToChunkCoord(glm::ivec3(-32, 0, 0)), glm::ivec3(-1, 0, 0));
    EXPECT_EQ(ChunkManager::worldToChunkCoord(glm::ivec3(-33, 0, 0)), glm::ivec3(-2, 0, 0));
}

TEST_F(ChunkManagerStaticTest, WorldToLocalCoord_Origin) {
    EXPECT_EQ(ChunkManager::worldToLocalCoord(glm::ivec3(0, 0, 0)), glm::ivec3(0, 0, 0));
}

TEST_F(ChunkManagerStaticTest, WorldToLocalCoord_InsideChunk) {
    EXPECT_EQ(ChunkManager::worldToLocalCoord(glm::ivec3(5, 10, 31)), glm::ivec3(5, 10, 31));
}

TEST_F(ChunkManagerStaticTest, WorldToLocalCoord_ChunkBoundary) {
    EXPECT_EQ(ChunkManager::worldToLocalCoord(glm::ivec3(32, 0, 0)), glm::ivec3(0, 0, 0));
    EXPECT_EQ(ChunkManager::worldToLocalCoord(glm::ivec3(33, 0, 0)), glm::ivec3(1, 0, 0));
}

TEST_F(ChunkManagerStaticTest, WorldToLocalCoord_Negative) {
    // Negative world positions should still produce 0-31 local coords
    glm::ivec3 local = ChunkManager::worldToLocalCoord(glm::ivec3(-1, 0, 0));
    EXPECT_GE(local.x, 0);
    EXPECT_LT(local.x, 32);
}

TEST_F(ChunkManagerStaticTest, ChunkCoordToOrigin) {
    EXPECT_EQ(ChunkManager::chunkCoordToOrigin(glm::ivec3(0, 0, 0)), glm::ivec3(0, 0, 0));
    EXPECT_EQ(ChunkManager::chunkCoordToOrigin(glm::ivec3(1, 0, 0)), glm::ivec3(32, 0, 0));
    EXPECT_EQ(ChunkManager::chunkCoordToOrigin(glm::ivec3(1, 2, 3)), glm::ivec3(32, 64, 96));
    EXPECT_EQ(ChunkManager::chunkCoordToOrigin(glm::ivec3(-1, -1, -1)), glm::ivec3(-32, -32, -32));
}

TEST_F(ChunkManagerStaticTest, RoundTripConversion) {
    // worldPos → chunkCoord → origin → chunkCoord should be consistent
    glm::ivec3 worldPos(47, 100, -5);
    glm::ivec3 chunkCoord = ChunkManager::worldToChunkCoord(worldPos);
    glm::ivec3 origin = ChunkManager::chunkCoordToOrigin(chunkCoord);
    glm::ivec3 chunkCoordAgain = ChunkManager::worldToChunkCoord(origin);
    EXPECT_EQ(chunkCoord, chunkCoordAgain);
}

TEST_F(ChunkManagerStaticTest, LocalPlusOriginEqualsWorld) {
    // For any world position, origin + local should reconstruct it
    glm::ivec3 worldPos(47, 100, 5);
    glm::ivec3 chunkCoord = ChunkManager::worldToChunkCoord(worldPos);
    glm::ivec3 origin = ChunkManager::chunkCoordToOrigin(chunkCoord);
    glm::ivec3 local = ChunkManager::worldToLocalCoord(worldPos);
    EXPECT_EQ(origin + local, worldPos);
}

TEST_F(ChunkManagerStaticTest, LocalPlusOriginEqualsWorld_Negative) {
    glm::ivec3 worldPos(-10, -50, -1);
    glm::ivec3 chunkCoord = ChunkManager::worldToChunkCoord(worldPos);
    glm::ivec3 origin = ChunkManager::chunkCoordToOrigin(chunkCoord);
    glm::ivec3 local = ChunkManager::worldToLocalCoord(worldPos);
    EXPECT_EQ(origin + local, worldPos);
}

// (ChunkDataTest -- the Chunk data layer -- was retired with the Chunk store in D4.)

} // namespace Testing
} // namespace Phyxel
