/**
 * Unit tests for VoxelLocation utilities
 * 
 * Tests voxel location state checks and placement calculations.
 */

#include <gtest/gtest.h>
#include "core/Types.h"
#include <glm/glm.hpp>

using namespace VulkanCube;

// ============================================================================
// VoxelLocation State Tests
// ============================================================================

TEST(VoxelLocationTest, DefaultConstruction) {
    VoxelLocation loc;
    EXPECT_EQ(loc.type, VoxelLocation::EMPTY);
    EXPECT_EQ(loc.chunk, nullptr);
    EXPECT_EQ(loc.localPos, glm::ivec3(-1));
    EXPECT_EQ(loc.worldPos, glm::ivec3(-1));
    EXPECT_EQ(loc.subcubePos, glm::ivec3(-1));
    EXPECT_EQ(loc.microcubePos, glm::ivec3(-1));
    EXPECT_EQ(loc.hitFace, -1);
}

TEST(VoxelLocationTest, IsValid_Empty) {
    VoxelLocation loc;
    loc.type = VoxelLocation::EMPTY;
    loc.chunk = nullptr;
    EXPECT_FALSE(loc.isValid());
}

TEST(VoxelLocationTest, IsValid_NoChunk) {
    VoxelLocation loc;
    loc.type = VoxelLocation::CUBE;
    loc.chunk = nullptr;
    EXPECT_FALSE(loc.isValid());
}

TEST(VoxelLocationTest, IsValid_CubeWithChunk) {
    VoxelLocation loc;
    loc.type = VoxelLocation::CUBE;
    loc.chunk = reinterpret_cast<Chunk*>(0x1); // Non-null pointer
    EXPECT_TRUE(loc.isValid());
}

TEST(VoxelLocationTest, IsValid_SubdividedWithChunk) {
    VoxelLocation loc;
    loc.type = VoxelLocation::SUBDIVIDED;
    loc.chunk = reinterpret_cast<Chunk*>(0x1);
    EXPECT_TRUE(loc.isValid());
}

// ============================================================================
// Type Checking Tests
// ============================================================================

TEST(VoxelLocationTest, IsCube_True) {
    VoxelLocation loc;
    loc.type = VoxelLocation::CUBE;
    EXPECT_TRUE(loc.isCube());
    EXPECT_FALSE(loc.isSubcube());
    EXPECT_FALSE(loc.isMicrocube());
}

TEST(VoxelLocationTest, IsCube_False) {
    VoxelLocation loc;
    loc.type = VoxelLocation::EMPTY;
    EXPECT_FALSE(loc.isCube());
    
    loc.type = VoxelLocation::SUBDIVIDED;
    EXPECT_FALSE(loc.isCube());
}

TEST(VoxelLocationTest, IsSubcube_True) {
    VoxelLocation loc;
    loc.type = VoxelLocation::SUBDIVIDED;
    loc.subcubePos = glm::ivec3(1, 2, 3);
    loc.microcubePos = glm::ivec3(-1);
    EXPECT_TRUE(loc.isSubcube());
    EXPECT_FALSE(loc.isCube());
    EXPECT_FALSE(loc.isMicrocube());
}

TEST(VoxelLocationTest, IsSubcube_False_NotSubdivided) {
    VoxelLocation loc;
    loc.type = VoxelLocation::CUBE;
    loc.subcubePos = glm::ivec3(1, 2, 3);
    EXPECT_FALSE(loc.isSubcube());
}

TEST(VoxelLocationTest, IsSubcube_False_InvalidSubcubePos) {
    VoxelLocation loc;
    loc.type = VoxelLocation::SUBDIVIDED;
    loc.subcubePos = glm::ivec3(-1);
    EXPECT_FALSE(loc.isSubcube());
}

TEST(VoxelLocationTest, IsSubcube_False_HasMicrocube) {
    VoxelLocation loc;
    loc.type = VoxelLocation::SUBDIVIDED;
    loc.subcubePos = glm::ivec3(1, 2, 3);
    loc.microcubePos = glm::ivec3(0, 1, 2);
    EXPECT_FALSE(loc.isSubcube()); // Should be microcube instead
}

TEST(VoxelLocationTest, IsMicrocube_True) {
    VoxelLocation loc;
    loc.type = VoxelLocation::SUBDIVIDED;
    loc.microcubePos = glm::ivec3(0, 1, 2);
    EXPECT_TRUE(loc.isMicrocube());
    EXPECT_FALSE(loc.isCube());
    EXPECT_FALSE(loc.isSubcube());
}

TEST(VoxelLocationTest, IsMicrocube_False_NotSubdivided) {
    VoxelLocation loc;
    loc.type = VoxelLocation::CUBE;
    loc.microcubePos = glm::ivec3(0, 1, 2);
    EXPECT_FALSE(loc.isMicrocube());
}

TEST(VoxelLocationTest, IsMicrocube_False_InvalidMicrocubePos) {
    VoxelLocation loc;
    loc.type = VoxelLocation::SUBDIVIDED;
    loc.microcubePos = glm::ivec3(-1);
    EXPECT_FALSE(loc.isMicrocube());
}

// ============================================================================
// Equality Operator Tests
// ============================================================================

TEST(VoxelLocationTest, Equality_BothEmpty) {
    VoxelLocation loc1, loc2;
    EXPECT_TRUE(loc1 == loc2);
    EXPECT_FALSE(loc1 != loc2);
}

TEST(VoxelLocationTest, Equality_DifferentType) {
    VoxelLocation loc1, loc2;
    loc1.type = VoxelLocation::CUBE;
    loc2.type = VoxelLocation::EMPTY;
    EXPECT_FALSE(loc1 == loc2);
    EXPECT_TRUE(loc1 != loc2);
}

TEST(VoxelLocationTest, Equality_DifferentChunk) {
    VoxelLocation loc1, loc2;
    loc1.chunk = reinterpret_cast<Chunk*>(0x1);
    loc2.chunk = reinterpret_cast<Chunk*>(0x2);
    EXPECT_FALSE(loc1 == loc2);
}

TEST(VoxelLocationTest, Equality_DifferentLocalPos) {
    VoxelLocation loc1, loc2;
    loc1.localPos = glm::ivec3(1, 2, 3);
    loc2.localPos = glm::ivec3(4, 5, 6);
    EXPECT_FALSE(loc1 == loc2);
}

TEST(VoxelLocationTest, Equality_DifferentWorldPos) {
    VoxelLocation loc1, loc2;
    loc1.worldPos = glm::ivec3(10, 20, 30);
    loc2.worldPos = glm::ivec3(40, 50, 60);
    EXPECT_FALSE(loc1 == loc2);
}

TEST(VoxelLocationTest, Equality_DifferentSubcubePos) {
    VoxelLocation loc1, loc2;
    loc1.subcubePos = glm::ivec3(1, 1, 1);
    loc2.subcubePos = glm::ivec3(2, 2, 2);
    EXPECT_FALSE(loc1 == loc2);
}

TEST(VoxelLocationTest, Equality_DifferentMicrocubePos) {
    VoxelLocation loc1, loc2;
    loc1.microcubePos = glm::ivec3(0, 0, 0);
    loc2.microcubePos = glm::ivec3(1, 1, 1);
    EXPECT_FALSE(loc1 == loc2);
}

TEST(VoxelLocationTest, Equality_AllFieldsMatch) {
    VoxelLocation loc1, loc2;
    Chunk* testChunk = reinterpret_cast<Chunk*>(0x12345);
    
    loc1.type = VoxelLocation::SUBDIVIDED;
    loc1.chunk = testChunk;
    loc1.localPos = glm::ivec3(1, 2, 3);
    loc1.worldPos = glm::ivec3(10, 20, 30);
    loc1.subcubePos = glm::ivec3(0, 1, 2);
    loc1.microcubePos = glm::ivec3(1, 0, 1);
    
    loc2.type = VoxelLocation::SUBDIVIDED;
    loc2.chunk = testChunk;
    loc2.localPos = glm::ivec3(1, 2, 3);
    loc2.worldPos = glm::ivec3(10, 20, 30);
    loc2.subcubePos = glm::ivec3(0, 1, 2);
    loc2.microcubePos = glm::ivec3(1, 0, 1);
    
    EXPECT_TRUE(loc1 == loc2);
    EXPECT_FALSE(loc1 != loc2);
}

// ============================================================================
// Adjacent Placement Position Tests
// ============================================================================

TEST(VoxelLocationTest, GetAdjacentPlacementPosition_NoFace) {
    VoxelLocation loc;
    loc.worldPos = glm::ivec3(5, 10, 15);
    loc.hitFace = -1;
    
    glm::ivec3 adjacent = loc.getAdjacentPlacementPosition();
    EXPECT_EQ(adjacent, glm::ivec3(5, 10, 15)); // Returns worldPos when no face
}

TEST(VoxelLocationTest, GetAdjacentPlacementPosition_PositiveX) {
    VoxelLocation loc;
    loc.worldPos = glm::ivec3(5, 10, 15);
    loc.hitFace = 0; // +X face
    
    glm::ivec3 adjacent = loc.getAdjacentPlacementPosition();
    EXPECT_EQ(adjacent, glm::ivec3(6, 10, 15));
}

TEST(VoxelLocationTest, GetAdjacentPlacementPosition_NegativeX) {
    VoxelLocation loc;
    loc.worldPos = glm::ivec3(5, 10, 15);
    loc.hitFace = 1; // -X face
    
    glm::ivec3 adjacent = loc.getAdjacentPlacementPosition();
    EXPECT_EQ(adjacent, glm::ivec3(4, 10, 15));
}

TEST(VoxelLocationTest, GetAdjacentPlacementPosition_PositiveY) {
    VoxelLocation loc;
    loc.worldPos = glm::ivec3(5, 10, 15);
    loc.hitFace = 2; // +Y face
    
    glm::ivec3 adjacent = loc.getAdjacentPlacementPosition();
    EXPECT_EQ(adjacent, glm::ivec3(5, 11, 15));
}

TEST(VoxelLocationTest, GetAdjacentPlacementPosition_NegativeY) {
    VoxelLocation loc;
    loc.worldPos = glm::ivec3(5, 10, 15);
    loc.hitFace = 3; // -Y face
    
    glm::ivec3 adjacent = loc.getAdjacentPlacementPosition();
    EXPECT_EQ(adjacent, glm::ivec3(5, 9, 15));
}

TEST(VoxelLocationTest, GetAdjacentPlacementPosition_PositiveZ) {
    VoxelLocation loc;
    loc.worldPos = glm::ivec3(5, 10, 15);
    loc.hitFace = 4; // +Z face
    
    glm::ivec3 adjacent = loc.getAdjacentPlacementPosition();
    EXPECT_EQ(adjacent, glm::ivec3(5, 10, 16));
}

TEST(VoxelLocationTest, GetAdjacentPlacementPosition_NegativeZ) {
    VoxelLocation loc;
    loc.worldPos = glm::ivec3(5, 10, 15);
    loc.hitFace = 5; // -Z face
    
    glm::ivec3 adjacent = loc.getAdjacentPlacementPosition();
    EXPECT_EQ(adjacent, glm::ivec3(5, 10, 14));
}

TEST(VoxelLocationTest, GetAdjacentPlacementPosition_AllFaces) {
    VoxelLocation loc;
    loc.worldPos = glm::ivec3(0, 0, 0);
    
    // Test all 6 faces from origin
    struct FaceTest {
        int faceID;
        glm::ivec3 expected;
    };
    
    FaceTest tests[] = {
        {0, glm::ivec3(1, 0, 0)},   // +X
        {1, glm::ivec3(-1, 0, 0)},  // -X
        {2, glm::ivec3(0, 1, 0)},   // +Y
        {3, glm::ivec3(0, -1, 0)},  // -Y
        {4, glm::ivec3(0, 0, 1)},   // +Z
        {5, glm::ivec3(0, 0, -1)},  // -Z
    };
    
    for (const auto& test : tests) {
        loc.hitFace = test.faceID;
        glm::ivec3 adjacent = loc.getAdjacentPlacementPosition();
        EXPECT_EQ(adjacent, test.expected) << "Failed for face " << test.faceID;
    }
}

TEST(VoxelLocationTest, GetAdjacentPlacementPosition_InvalidFaceID) {
    VoxelLocation loc;
    loc.worldPos = glm::ivec3(5, 10, 15);
    loc.hitFace = 99; // Invalid face ID
    
    glm::ivec3 adjacent = loc.getAdjacentPlacementPosition();
    EXPECT_EQ(adjacent, glm::ivec3(5, 10, 15)); // Returns worldPos for invalid face
}

TEST(VoxelLocationTest, GetAdjacentPlacementPosition_NegativeWorldPos) {
    VoxelLocation loc;
    loc.worldPos = glm::ivec3(-5, -10, -15);
    loc.hitFace = 0; // +X
    
    glm::ivec3 adjacent = loc.getAdjacentPlacementPosition();
    EXPECT_EQ(adjacent, glm::ivec3(-4, -10, -15));
}

// ============================================================================
// IVec3Hash Tests
// ============================================================================

TEST(IVec3HashTest, Hash_Origin) {
    IVec3Hash hasher;
    glm::ivec3 vec(0, 0, 0);
    // Hash function produces a hash value even for origin
    // Just verify it's consistent
    std::size_t hash1 = hasher(vec);
    std::size_t hash2 = hasher(vec);
    EXPECT_EQ(hash1, hash2);
}

TEST(IVec3HashTest, Hash_DifferentVectors) {
    IVec3Hash hasher;
    glm::ivec3 vec1(1, 2, 3);
    glm::ivec3 vec2(4, 5, 6);
    
    std::size_t hash1 = hasher(vec1);
    std::size_t hash2 = hasher(vec2);
    
    EXPECT_NE(hash1, hash2);
}

TEST(IVec3HashTest, Hash_SameVectorsSameHash) {
    IVec3Hash hasher;
    glm::ivec3 vec1(10, 20, 30);
    glm::ivec3 vec2(10, 20, 30);
    
    std::size_t hash1 = hasher(vec1);
    std::size_t hash2 = hasher(vec2);
    
    EXPECT_EQ(hash1, hash2);
}

TEST(IVec3HashTest, Hash_NegativeValues) {
    IVec3Hash hasher;
    glm::ivec3 vec(-5, -10, -15);
    std::size_t hash = hasher(vec);
    
    // Should produce a valid hash (no assertion about specific value)
    EXPECT_NE(hash, 0); // Unlikely to be exactly 0 for non-zero input
}

TEST(IVec3HashTest, Hash_PermutationsDifferent) {
    IVec3Hash hasher;
    glm::ivec3 vec1(1, 2, 3);
    glm::ivec3 vec2(3, 2, 1);
    glm::ivec3 vec3(2, 1, 3);
    
    std::size_t hash1 = hasher(vec1);
    std::size_t hash2 = hasher(vec2);
    std::size_t hash3 = hasher(vec3);
    
    // Different permutations should produce different hashes
    EXPECT_NE(hash1, hash2);
    EXPECT_NE(hash1, hash3);
    EXPECT_NE(hash2, hash3);
}
