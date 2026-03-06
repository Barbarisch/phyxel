/**
 * Unit tests for CoordinateUtils
 * 
 * Tests coordinate system conversions between world, chunk, and local coordinates.
 * CoordinateUtils is a pure utility class with static functions - perfect for unit testing!
 */

#include <gtest/gtest.h>
#include "utils/CoordinateUtils.h"

using namespace Phyxel::Utils;

// ============================================================================
// worldToChunkCoord Tests
// ============================================================================

TEST(CoordinateUtilsTest, WorldToChunkCoord_Origin) {
    // World origin should map to chunk origin
    glm::ivec3 worldPos(0, 0, 0);
    glm::ivec3 chunkCoord = CoordinateUtils::worldToChunkCoord(worldPos);
    
    EXPECT_EQ(chunkCoord.x, 0);
    EXPECT_EQ(chunkCoord.y, 0);
    EXPECT_EQ(chunkCoord.z, 0);
}

TEST(CoordinateUtilsTest, WorldToChunkCoord_PositiveValues) {
    // Positive values: simple division by chunk size (32)
    glm::ivec3 worldPos(35, 64, 100);
    glm::ivec3 chunkCoord = CoordinateUtils::worldToChunkCoord(worldPos);
    
    EXPECT_EQ(chunkCoord.x, 1);  // 35 / 32 = 1 (floor division)
    EXPECT_EQ(chunkCoord.y, 2);  // 64 / 32 = 2
    EXPECT_EQ(chunkCoord.z, 3);  // 100 / 32 = 3
}

TEST(CoordinateUtilsTest, WorldToChunkCoord_ChunkBoundary) {
    // Chunk boundaries: positions 31 and 32 are in different chunks
    glm::ivec3 worldPos31(31, 31, 31);
    glm::ivec3 worldPos32(32, 32, 32);
    
    glm::ivec3 chunk31 = CoordinateUtils::worldToChunkCoord(worldPos31);
    glm::ivec3 chunk32 = CoordinateUtils::worldToChunkCoord(worldPos32);
    
    // Position 31 is still in chunk 0
    EXPECT_EQ(chunk31.x, 0);
    EXPECT_EQ(chunk31.y, 0);
    EXPECT_EQ(chunk31.z, 0);
    
    // Position 32 is in chunk 1
    EXPECT_EQ(chunk32.x, 1);
    EXPECT_EQ(chunk32.y, 1);
    EXPECT_EQ(chunk32.z, 1);
}

TEST(CoordinateUtilsTest, WorldToChunkCoord_NegativeValues) {
    // Negative values: floor division (important!)
    // -1 should be in chunk -1, not chunk 0
    glm::ivec3 worldPos(-1, -1, -1);
    glm::ivec3 chunkCoord = CoordinateUtils::worldToChunkCoord(worldPos);
    
    EXPECT_EQ(chunkCoord.x, -1);
    EXPECT_EQ(chunkCoord.y, -1);
    EXPECT_EQ(chunkCoord.z, -1);
}

TEST(CoordinateUtilsTest, WorldToChunkCoord_NegativeChunkBoundary) {
    // Negative chunk boundary: -32 and -33 are in different chunks
    glm::ivec3 worldPos32(-32, -32, -32);
    glm::ivec3 worldPos33(-33, -33, -33);
    
    glm::ivec3 chunk32 = CoordinateUtils::worldToChunkCoord(worldPos32);
    glm::ivec3 chunk33 = CoordinateUtils::worldToChunkCoord(worldPos33);
    
    // -32 is the start of chunk -1
    EXPECT_EQ(chunk32.x, -1);
    EXPECT_EQ(chunk32.y, -1);
    EXPECT_EQ(chunk32.z, -1);
    
    // -33 is in chunk -2
    EXPECT_EQ(chunk33.x, -2);
    EXPECT_EQ(chunk33.y, -2);
    EXPECT_EQ(chunk33.z, -2);
}

// ============================================================================
// worldToLocalCoord Tests
// ============================================================================

TEST(CoordinateUtilsTest, WorldToLocalCoord_Origin) {
    // World origin maps to local origin
    glm::ivec3 worldPos(0, 0, 0);
    glm::ivec3 localCoord = CoordinateUtils::worldToLocalCoord(worldPos);
    
    EXPECT_EQ(localCoord.x, 0);
    EXPECT_EQ(localCoord.y, 0);
    EXPECT_EQ(localCoord.z, 0);
}

TEST(CoordinateUtilsTest, WorldToLocalCoord_PositiveValues) {
    // Positive values: modulo operation
    glm::ivec3 worldPos(35, 64, 100);
    glm::ivec3 localCoord = CoordinateUtils::worldToLocalCoord(worldPos);
    
    EXPECT_EQ(localCoord.x, 3);   // 35 % 32 = 3
    EXPECT_EQ(localCoord.y, 0);   // 64 % 32 = 0
    EXPECT_EQ(localCoord.z, 4);   // 100 % 32 = 4
}

TEST(CoordinateUtilsTest, WorldToLocalCoord_ChunkBoundary) {
    // Chunk boundaries
    glm::ivec3 worldPos31(31, 31, 31);
    glm::ivec3 worldPos32(32, 32, 32);
    
    glm::ivec3 local31 = CoordinateUtils::worldToLocalCoord(worldPos31);
    glm::ivec3 local32 = CoordinateUtils::worldToLocalCoord(worldPos32);
    
    // Position 31 is at local 31 (max local coordinate)
    EXPECT_EQ(local31.x, 31);
    EXPECT_EQ(local31.y, 31);
    EXPECT_EQ(local31.z, 31);
    
    // Position 32 wraps back to local 0
    EXPECT_EQ(local32.x, 0);
    EXPECT_EQ(local32.y, 0);
    EXPECT_EQ(local32.z, 0);
}

TEST(CoordinateUtilsTest, WorldToLocalCoord_NegativeValues) {
    // Negative values: should map to positive local coordinates
    // -1 world position should be local 31 (last position in chunk)
    glm::ivec3 worldPos(-1, -1, -1);
    glm::ivec3 localCoord = CoordinateUtils::worldToLocalCoord(worldPos);
    
    EXPECT_EQ(localCoord.x, 31);
    EXPECT_EQ(localCoord.y, 31);
    EXPECT_EQ(localCoord.z, 31);
}

TEST(CoordinateUtilsTest, WorldToLocalCoord_ValidRange) {
    // All local coordinates should be in valid range [0, 31]
    for (int x = -100; x < 100; x += 7) {
        for (int y = -100; y < 100; y += 11) {
            for (int z = -100; z < 100; z += 13) {
                glm::ivec3 worldPos(x, y, z);
                glm::ivec3 localCoord = CoordinateUtils::worldToLocalCoord(worldPos);
                
                EXPECT_GE(localCoord.x, 0);
                EXPECT_LT(localCoord.x, 32);
                EXPECT_GE(localCoord.y, 0);
                EXPECT_LT(localCoord.y, 32);
                EXPECT_GE(localCoord.z, 0);
                EXPECT_LT(localCoord.z, 32);
            }
        }
    }
}

// ============================================================================
// chunkCoordToOrigin Tests
// ============================================================================

TEST(CoordinateUtilsTest, ChunkCoordToOrigin_Origin) {
    // Chunk origin should map to world origin
    glm::ivec3 chunkCoord(0, 0, 0);
    glm::ivec3 worldOrigin = CoordinateUtils::chunkCoordToOrigin(chunkCoord);
    
    EXPECT_EQ(worldOrigin.x, 0);
    EXPECT_EQ(worldOrigin.y, 0);
    EXPECT_EQ(worldOrigin.z, 0);
}

TEST(CoordinateUtilsTest, ChunkCoordToOrigin_PositiveChunks) {
    // Positive chunk coordinates
    glm::ivec3 chunkCoord(1, 2, 3);
    glm::ivec3 worldOrigin = CoordinateUtils::chunkCoordToOrigin(chunkCoord);
    
    EXPECT_EQ(worldOrigin.x, 32);   // 1 * 32
    EXPECT_EQ(worldOrigin.y, 64);   // 2 * 32
    EXPECT_EQ(worldOrigin.z, 96);   // 3 * 32
}

TEST(CoordinateUtilsTest, ChunkCoordToOrigin_NegativeChunks) {
    // Negative chunk coordinates
    glm::ivec3 chunkCoord(-1, -2, -3);
    glm::ivec3 worldOrigin = CoordinateUtils::chunkCoordToOrigin(chunkCoord);
    
    EXPECT_EQ(worldOrigin.x, -32);   // -1 * 32
    EXPECT_EQ(worldOrigin.y, -64);   // -2 * 32
    EXPECT_EQ(worldOrigin.z, -96);   // -3 * 32
}

// ============================================================================
// localToWorld Tests
// ============================================================================

TEST(CoordinateUtilsTest, LocalToWorld_ChunkOrigin) {
    // Local origin in chunk 0 should be world origin
    glm::ivec3 chunkCoord(0, 0, 0);
    glm::ivec3 localPos(0, 0, 0);
    glm::ivec3 worldPos = CoordinateUtils::localToWorld(chunkCoord, localPos);
    
    EXPECT_EQ(worldPos.x, 0);
    EXPECT_EQ(worldPos.y, 0);
    EXPECT_EQ(worldPos.z, 0);
}

TEST(CoordinateUtilsTest, LocalToWorld_PositiveChunk) {
    // Local position in chunk 1
    glm::ivec3 chunkCoord(1, 1, 1);
    glm::ivec3 localPos(5, 10, 15);
    glm::ivec3 worldPos = CoordinateUtils::localToWorld(chunkCoord, localPos);
    
    EXPECT_EQ(worldPos.x, 37);  // 32 + 5
    EXPECT_EQ(worldPos.y, 42);  // 32 + 10
    EXPECT_EQ(worldPos.z, 47);  // 32 + 15
}

TEST(CoordinateUtilsTest, LocalToWorld_NegativeChunk) {
    // Local position in chunk -1
    glm::ivec3 chunkCoord(-1, -1, -1);
    glm::ivec3 localPos(0, 0, 0);
    glm::ivec3 worldPos = CoordinateUtils::localToWorld(chunkCoord, localPos);
    
    EXPECT_EQ(worldPos.x, -32);
    EXPECT_EQ(worldPos.y, -32);
    EXPECT_EQ(worldPos.z, -32);
}

// ============================================================================
// isValidLocalCoord Tests
// ============================================================================

TEST(CoordinateUtilsTest, IsValidLocalCoord_Valid) {
    // All coordinates in valid range [0, 31]
    EXPECT_TRUE(CoordinateUtils::isValidLocalCoord(glm::ivec3(0, 0, 0)));
    EXPECT_TRUE(CoordinateUtils::isValidLocalCoord(glm::ivec3(15, 15, 15)));
    EXPECT_TRUE(CoordinateUtils::isValidLocalCoord(glm::ivec3(31, 31, 31)));
    EXPECT_TRUE(CoordinateUtils::isValidLocalCoord(glm::ivec3(0, 31, 0)));
}

TEST(CoordinateUtilsTest, IsValidLocalCoord_Invalid) {
    // Coordinates outside valid range
    EXPECT_FALSE(CoordinateUtils::isValidLocalCoord(glm::ivec3(-1, 0, 0)));
    EXPECT_FALSE(CoordinateUtils::isValidLocalCoord(glm::ivec3(0, -1, 0)));
    EXPECT_FALSE(CoordinateUtils::isValidLocalCoord(glm::ivec3(0, 0, -1)));
    EXPECT_FALSE(CoordinateUtils::isValidLocalCoord(glm::ivec3(32, 0, 0)));
    EXPECT_FALSE(CoordinateUtils::isValidLocalCoord(glm::ivec3(0, 32, 0)));
    EXPECT_FALSE(CoordinateUtils::isValidLocalCoord(glm::ivec3(0, 0, 32)));
}

// ============================================================================
// Round-trip Tests (ensure conversions are reversible)
// ============================================================================

TEST(CoordinateUtilsTest, RoundTrip_WorldToChunkAndLocal) {
    // Test that world -> (chunk, local) -> world is identity
    std::vector<glm::ivec3> testPositions = {
        glm::ivec3(0, 0, 0),
        glm::ivec3(15, 20, 25),
        glm::ivec3(31, 31, 31),
        glm::ivec3(32, 32, 32),
        glm::ivec3(100, 200, 300),
        glm::ivec3(-1, -1, -1),
        glm::ivec3(-32, -32, -32),
        glm::ivec3(-50, -100, -150)
    };
    
    for (const auto& worldPos : testPositions) {
        glm::ivec3 chunkCoord = CoordinateUtils::worldToChunkCoord(worldPos);
        glm::ivec3 localCoord = CoordinateUtils::worldToLocalCoord(worldPos);
        glm::ivec3 reconstructed = CoordinateUtils::localToWorld(chunkCoord, localCoord);
        
        EXPECT_EQ(reconstructed.x, worldPos.x) << "Failed for worldPos: " << worldPos.x << ", " << worldPos.y << ", " << worldPos.z;
        EXPECT_EQ(reconstructed.y, worldPos.y) << "Failed for worldPos: " << worldPos.x << ", " << worldPos.y << ", " << worldPos.z;
        EXPECT_EQ(reconstructed.z, worldPos.z) << "Failed for worldPos: " << worldPos.x << ", " << worldPos.y << ", " << worldPos.z;
    }
}
