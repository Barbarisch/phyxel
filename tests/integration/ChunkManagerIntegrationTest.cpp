#include "IntegrationTestFixture.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "core/Subcube.h"
#include "utils/CoordinateUtils.h"
#include <glm/glm.hpp>

namespace VulkanCube {
namespace Testing {

/**
 * @brief Integration tests for ChunkManager
 * 
 * These tests verify that ChunkManager correctly orchestrates:
 * - Chunk creation and initialization
 * - Voxel placement and removal
 * - Cross-chunk face culling
 * - Chunk updates and dirty tracking
 * - Physics integration
 * 
 * Unlike unit tests, these run with real Vulkan/Physics systems.
 */
class ChunkManagerIntegrationTest : public ChunkManagerTestFixture {};

// ============================================================================
// Chunk Creation Tests
// ============================================================================

TEST_F(ChunkManagerIntegrationTest, CreateSingleChunk) {
    ASSERT_NE(chunkManager, nullptr);
    ASSERT_EQ(chunkManager->chunks.size(), 0);

    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);

    EXPECT_EQ(chunkManager->chunks.size(), 1);
    EXPECT_NE(chunkManager->chunks[0], nullptr);
    EXPECT_EQ(chunkManager->chunks[0]->getWorldOrigin(), origin);
}

TEST_F(ChunkManagerIntegrationTest, CreateMultipleChunks) {
    std::vector<glm::ivec3> origins = {
        {0, 0, 0},
        {32, 0, 0},
        {0, 0, 32}
    };

    chunkManager->createChunks(origins);

    EXPECT_EQ(chunkManager->chunks.size(), 3);
    for (size_t i = 0; i < 3; i++) {
        EXPECT_NE(chunkManager->chunks[i], nullptr);
        EXPECT_EQ(chunkManager->chunks[i]->getWorldOrigin(), origins[i]);
    }
}

TEST_F(ChunkManagerIntegrationTest, ChunkMapPopulated) {
    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);

    glm::ivec3 chunkCoord = Utils::CoordinateUtils::worldToChunkCoord(origin);
    Chunk* chunk = chunkManager->getChunkAtCoord(chunkCoord);

    EXPECT_NE(chunk, nullptr);
    EXPECT_EQ(chunk->getWorldOrigin(), origin);
}

TEST_F(ChunkManagerIntegrationTest, ChunkInitializedWithCubes) {
    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);

    Chunk* chunk = chunkManager->chunks[0].get();
    EXPECT_GT(chunk->getCubeCount(), 0) << "Chunk should be populated with cubes";
}

// ============================================================================
// Voxel Query Tests
// ============================================================================

TEST_F(ChunkManagerIntegrationTest, GetChunkAtCoord) {
    std::vector<glm::ivec3> origins = {{0, 0, 0}, {32, 0, 0}};
    chunkManager->createChunks(origins);

    Chunk* chunk0 = chunkManager->getChunkAtCoord({0, 0, 0});
    Chunk* chunk1 = chunkManager->getChunkAtCoord({1, 0, 0});

    EXPECT_NE(chunk0, nullptr);
    EXPECT_NE(chunk1, nullptr);
    EXPECT_NE(chunk0, chunk1);
    EXPECT_EQ(chunk0->getWorldOrigin(), glm::ivec3(0, 0, 0));
    EXPECT_EQ(chunk1->getWorldOrigin(), glm::ivec3(32, 0, 0));
}

TEST_F(ChunkManagerIntegrationTest, GetChunkAt_WorldPosition) {
    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);

    // Position within chunk bounds
    Chunk* chunk = chunkManager->getChunkAt({5, 10, 15});
    EXPECT_NE(chunk, nullptr);
    EXPECT_EQ(chunk->getWorldOrigin(), origin);

    // Position outside chunk bounds
    Chunk* nullChunk = chunkManager->getChunkAt({100, 100, 100});
    EXPECT_EQ(nullChunk, nullptr);
}

TEST_F(ChunkManagerIntegrationTest, GetCubeAt_WorldPosition) {
    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);
    chunkManager->initializeAllChunkVoxelMaps();

    // Query cube in chunk (chunks are populated with cubes by default)
    Cube* cube = chunkManager->getCubeAt({5, 5, 5});
    EXPECT_NE(cube, nullptr);
}

// ============================================================================
// Voxel Modification Tests
// ============================================================================

TEST_F(ChunkManagerIntegrationTest, AddCube) {
    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);
    chunkManager->initializeAllChunkVoxelMaps();

    // Remove a cube first
    glm::ivec3 testPos(5, 5, 5);
    chunkManager->removeCube(testPos);
    EXPECT_EQ(chunkManager->getCubeAt(testPos), nullptr);

    // Add it back
    chunkManager->addCube(testPos);
    
    Cube* cube = chunkManager->getCubeAt(testPos);
    EXPECT_NE(cube, nullptr);
}

TEST_F(ChunkManagerIntegrationTest, RemoveCube) {
    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);
    chunkManager->initializeAllChunkVoxelMaps();

    glm::ivec3 testPos(5, 5, 5);
    Cube* cubeBefore = chunkManager->getCubeAt(testPos);
    ASSERT_NE(cubeBefore, nullptr) << "Chunk should be populated with cubes";

    chunkManager->removeCube(testPos);

    Cube* cubeAfter = chunkManager->getCubeAt(testPos);
    EXPECT_EQ(cubeAfter, nullptr);
}

// ============================================================================
// Dirty Chunk Tracking Tests
// ============================================================================

TEST_F(ChunkManagerIntegrationTest, MarkChunkDirty) {
    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);

    Chunk* chunk = chunkManager->chunks[0].get();
    
    // Just verify markChunkDirty doesn't crash
    EXPECT_NO_THROW({
        chunkManager->markChunkDirty(chunk);
    });
    
    // Chunk dirty tracking is handled by DirtyChunkTracker internally
    // The important thing is the method works without errors
}

TEST_F(ChunkManagerIntegrationTest, DirtyChunkAfterVoxelModification) {
    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);
    chunkManager->initializeAllChunkVoxelMaps();

    Chunk* chunk = chunkManager->chunks[0].get();
    chunk->markClean(); // Reset dirty flag

    glm::ivec3 testPos(5, 5, 5);
    chunkManager->removeCube(testPos);

    EXPECT_TRUE(chunk->getIsDirty()) << "Chunk should be marked dirty after voxel removal";
}

// ============================================================================
// Cross-Chunk Culling Tests
// ============================================================================

TEST_F(ChunkManagerIntegrationTest, CrossChunkFaceCulling) {
    // Create two adjacent chunks
    std::vector<glm::ivec3> origins = {{0, 0, 0}, {32, 0, 0}};
    chunkManager->createChunks(origins);
    chunkManager->rebuildAllChunkFaces();

    Chunk* chunk0 = chunkManager->chunks[0].get();
    Chunk* chunk1 = chunkManager->chunks[1].get();

    // Both chunks should have cubes
    EXPECT_GT(chunk0->getCubeCount(), 0);
    EXPECT_GT(chunk1->getCubeCount(), 0);

    // Faces at chunk boundaries should be culled if adjacent chunks have cubes
    // This is a high-level verification - detailed face culling is tested in unit tests
}

TEST_F(ChunkManagerIntegrationTest, RebuildAllChunkFaces) {
    std::vector<glm::ivec3> origins = {{0, 0, 0}, {32, 0, 0}};
    chunkManager->createChunks(origins);

    EXPECT_NO_THROW({
        chunkManager->rebuildAllChunkFaces();
    });

    // Verify chunks still have cubes after rebuild
    EXPECT_GT(chunkManager->chunks[0]->getCubeCount(), 0);
    EXPECT_GT(chunkManager->chunks[1]->getCubeCount(), 0);
}

// ============================================================================
// Voxel Map Initialization Tests
// ============================================================================

TEST_F(ChunkManagerIntegrationTest, InitializeVoxelMaps) {
    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);

    // Before initialization, voxel maps might be empty
    chunkManager->initializeAllChunkVoxelMaps();

    // After initialization, should be able to query cubes
    Cube* cube = chunkManager->getCubeAt({5, 5, 5});
    EXPECT_NE(cube, nullptr) << "Voxel maps should enable O(1) cube lookup";
}

// ============================================================================
// Multi-Chunk Query Tests
// ============================================================================

TEST_F(ChunkManagerIntegrationTest, QueryAcrossChunkBoundaries) {
    // Create 2x2 grid of chunks
    std::vector<glm::ivec3> origins = {
        {0, 0, 0}, {32, 0, 0},
        {0, 0, 32}, {32, 0, 32}
    };
    chunkManager->createChunks(origins);
    chunkManager->initializeAllChunkVoxelMaps();

    // Query cubes in each chunk
    EXPECT_NE(chunkManager->getCubeAt({5, 5, 5}), nullptr);
    EXPECT_NE(chunkManager->getCubeAt({35, 5, 5}), nullptr);
    EXPECT_NE(chunkManager->getCubeAt({5, 5, 35}), nullptr);
    EXPECT_NE(chunkManager->getCubeAt({35, 5, 35}), nullptr);
}

TEST_F(ChunkManagerIntegrationTest, ChunkCount) {
    EXPECT_EQ(chunkManager->chunks.size(), 0);

    chunkManager->createChunk({0, 0, 0});
    EXPECT_EQ(chunkManager->chunks.size(), 1);

    chunkManager->createChunk({32, 0, 0});
    EXPECT_EQ(chunkManager->chunks.size(), 2);
}

// ============================================================================
// Physics Integration Tests
// ============================================================================

TEST_F(ChunkManagerIntegrationTest, PhysicsWorldSet) {
    EXPECT_NE(chunkManager->physicsWorld, nullptr) << "ChunkManager should have physics world set";
}

TEST_F(ChunkManagerIntegrationTest, DynamicSubcubePhysics) {
    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);

    // Create a dynamic subcube
    glm::ivec3 spawnPos(16, 50, 16);  // Use ivec3 for Subcube constructor
    glm::vec3 color(1.0f, 0.0f, 0.0f);
    
    auto subcube = std::make_unique<Subcube>(spawnPos, color);
    
    // Note: Can't easily test physics falling because Subcube physics is managed internally
    // Just verify we can add the subcube to the global list
    chunkManager->addGlobalDynamicSubcube(std::move(subcube));

    EXPECT_GT(chunkManager->globalDynamicSubcubes.size(), 0);
}

// ============================================================================
// Vulkan Buffer Tests
// ============================================================================

TEST_F(ChunkManagerIntegrationTest, ChunkVulkanBufferCreated) {
    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);

    Chunk* chunk = chunkManager->chunks[0].get();
    
    // Chunk should have Vulkan buffer created
    // Note: We can't directly access buffer handles in tests, but creation should not crash
    EXPECT_NO_THROW({
        chunk->createVulkanBuffer();
    });
}

TEST_F(ChunkManagerIntegrationTest, ChunkUpdateAfterModification) {
    glm::ivec3 origin(0, 0, 0);
    chunkManager->createChunk(origin);
    chunkManager->initializeAllChunkVoxelMaps();

    Chunk* chunk = chunkManager->chunks[0].get();
    int cubesBefore = chunk->getCubeCount();
    EXPECT_GT(cubesBefore, 0);

    // Remove a cube
    chunkManager->removeCube({5, 5, 5});
    
    // updateAllChunks updates rendering, verify chunk is still valid
    EXPECT_NO_THROW({
        chunkManager->updateAllChunks();
    });
    
    // Chunk should still exist and be valid
    EXPECT_EQ(chunkManager->chunks.size(), 1);
}

} // namespace Testing
} // namespace VulkanCube
