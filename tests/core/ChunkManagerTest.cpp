#include <gtest/gtest.h>
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "core/Cube.h"
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

// ===================================================================
// Chunk Data Operations (the data layer ChunkManager orchestrates)
// Tests Chunk-level voxel operations without Vulkan
// ===================================================================

class ChunkDataTest : public ::testing::Test {
protected:
    std::unique_ptr<Chunk> makeChunk(const glm::ivec3& origin = glm::ivec3(0, 0, 0)) {
        auto chunk = std::make_unique<Chunk>(origin);
        chunk->initializeForLoading();
        return chunk;
    }
};

// --- Basic cube operations ---

TEST_F(ChunkDataTest, AddCubeReturnsTrue) {
    auto chunk = makeChunk();
    EXPECT_TRUE(chunk->addCube(glm::ivec3(0, 0, 0)));
}

TEST_F(ChunkDataTest, AddCubeWithMaterial) {
    auto chunk = makeChunk();
    EXPECT_TRUE(chunk->addCube(glm::ivec3(5, 5, 5), "Stone"));
    auto* cube = chunk->getCubeAt(glm::ivec3(5, 5, 5));
    ASSERT_NE(cube, nullptr);
    EXPECT_EQ(cube->getMaterialName(), "Stone");
}

TEST_F(ChunkDataTest, GetCubeAtReturnsNullForEmpty) {
    auto chunk = makeChunk();
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(0, 0, 0)), nullptr);
}

TEST_F(ChunkDataTest, GetCubeAtReturnsCubeAfterAdd) {
    auto chunk = makeChunk();
    chunk->addCube(glm::ivec3(10, 20, 30));
    EXPECT_NE(chunk->getCubeAt(glm::ivec3(10, 20, 30)), nullptr);
}

TEST_F(ChunkDataTest, AddCubeOutOfBoundsReturnsFalse) {
    auto chunk = makeChunk();
    EXPECT_FALSE(chunk->addCube(glm::ivec3(32, 0, 0)));
    EXPECT_FALSE(chunk->addCube(glm::ivec3(-1, 0, 0)));
    EXPECT_FALSE(chunk->addCube(glm::ivec3(0, 32, 0)));
    EXPECT_FALSE(chunk->addCube(glm::ivec3(0, 0, -1)));
}

TEST_F(ChunkDataTest, RemoveCube) {
    auto chunk = makeChunk();
    chunk->addCube(glm::ivec3(5, 5, 5));
    EXPECT_NE(chunk->getCubeAt(glm::ivec3(5, 5, 5)), nullptr);

    EXPECT_TRUE(chunk->removeCube(glm::ivec3(5, 5, 5)));
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(5, 5, 5)), nullptr);
}

TEST_F(ChunkDataTest, RemoveNonexistentCubeReturnsFalse) {
    auto chunk = makeChunk();
    EXPECT_FALSE(chunk->removeCube(glm::ivec3(5, 5, 5)));
}

// --- Position correctness ---

TEST_F(ChunkDataTest, CubePositionMatchesInsert) {
    auto chunk = makeChunk();
    glm::ivec3 pos(7, 14, 21);
    chunk->addCube(pos);
    auto* cube = chunk->getCubeAt(pos);
    ASSERT_NE(cube, nullptr);
    EXPECT_EQ(cube->getPosition(), pos);
}

// --- All corners of chunk ---

TEST_F(ChunkDataTest, AllCornerPositions) {
    auto chunk = makeChunk();
    glm::ivec3 corners[] = {
        {0,0,0}, {31,0,0}, {0,31,0}, {0,0,31},
        {31,31,0}, {31,0,31}, {0,31,31}, {31,31,31}
    };
    for (const auto& pos : corners) {
        EXPECT_TRUE(chunk->addCube(pos)) << "Failed at corner " << pos.x << "," << pos.y << "," << pos.z;
    }
    for (const auto& pos : corners) {
        EXPECT_NE(chunk->getCubeAt(pos), nullptr) << "Missing at corner " << pos.x << "," << pos.y << "," << pos.z;
    }
}

// --- Material persistence through operations ---

TEST_F(ChunkDataTest, AllMaterials) {
    auto chunk = makeChunk();
    std::vector<std::string> materials = {"Wood", "Metal", "Glass", "Rubber", "Stone", "Ice", "Cork", "glow", "Default"};
    for (size_t i = 0; i < materials.size(); ++i) {
        chunk->addCube(glm::ivec3(static_cast<int>(i), 0, 0), materials[i]);
    }
    for (size_t i = 0; i < materials.size(); ++i) {
        auto* cube = chunk->getCubeAt(glm::ivec3(static_cast<int>(i), 0, 0));
        ASSERT_NE(cube, nullptr);
        EXPECT_EQ(cube->getMaterialName(), materials[i]);
    }
}

// --- Dirty flag tracking ---

TEST_F(ChunkDataTest, AddCubeMarksDirty) {
    auto chunk = makeChunk();
    chunk->markClean();
    EXPECT_FALSE(chunk->getIsDirty());

    chunk->addCube(glm::ivec3(0, 0, 0));
    EXPECT_TRUE(chunk->getIsDirty());
}

TEST_F(ChunkDataTest, MarkCleanResetsDirty) {
    auto chunk = makeChunk();
    chunk->addCube(glm::ivec3(0, 0, 0)); // marks dirty
    chunk->markClean();
    EXPECT_FALSE(chunk->getIsDirty());
}

// --- Multiple cubes along axes ---

TEST_F(ChunkDataTest, FillRow) {
    auto chunk = makeChunk();
    for (int x = 0; x < 32; ++x) {
        chunk->addCube(glm::ivec3(x, 0, 0));
    }
    for (int x = 0; x < 32; ++x) {
        EXPECT_NE(chunk->getCubeAt(glm::ivec3(x, 0, 0)), nullptr);
    }
    // Adjacent positions should be empty
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(0, 1, 0)), nullptr);
}

// --- World origin ---

TEST_F(ChunkDataTest, WorldOriginStored) {
    glm::ivec3 origin(64, -32, 96);
    auto chunk = makeChunk(origin);
    EXPECT_EQ(chunk->getWorldOrigin(), origin);
}

// --- Re-adding same position updates material ---

TEST_F(ChunkDataTest, ReaddCubeUpdatesMaterial) {
    auto chunk = makeChunk();
    chunk->addCube(glm::ivec3(5, 5, 5), "Wood");
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(5, 5, 5))->getMaterialName(), "Wood");

    chunk->addCube(glm::ivec3(5, 5, 5), "Stone");
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(5, 5, 5))->getMaterialName(), "Stone");
}

// --- VoxelLocation resolution ---

TEST_F(ChunkDataTest, HasVoxelAtEmptyPosition) {
    auto chunk = makeChunk();
    // hasVoxelAt is on the voxelManager, test through chunk's voxel location
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(0, 0, 0)), nullptr);
}

TEST_F(ChunkDataTest, HasVoxelAfterAdd) {
    auto chunk = makeChunk();
    chunk->addCube(glm::ivec3(10, 10, 10));
    EXPECT_NE(chunk->getCubeAt(glm::ivec3(10, 10, 10)), nullptr);
}

// --- Index formula consistency ---
// Verify z + y*32 + x*1024 index mapping works at edges

TEST_F(ChunkDataTest, IndexMappingAtEdges) {
    auto chunk = makeChunk();
    // Place cubes at positions that test index boundary conditions
    glm::ivec3 positions[] = {
        {0, 0, 0},   // index 0
        {0, 0, 31},  // index 31 (max z)
        {0, 1, 0},   // index 32 (y stride)
        {1, 0, 0},   // index 1024 (x stride)
        {31, 31, 31}  // max index
    };
    for (const auto& pos : positions) {
        EXPECT_TRUE(chunk->addCube(pos)) << "Failed at " << pos.x << "," << pos.y << "," << pos.z;
    }
    for (const auto& pos : positions) {
        EXPECT_NE(chunk->getCubeAt(pos), nullptr) << "Missing at " << pos.x << "," << pos.y << "," << pos.z;
    }
    // Verify no cross-contamination at adjacent positions
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(0, 0, 1)), nullptr);
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(0, 1, 1)), nullptr);
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(1, 0, 1)), nullptr);
}

// --- Full chunk stress ---

TEST_F(ChunkDataTest, FillEntireChunk) {
    auto chunk = makeChunk();
    int count = 0;
    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y) {
            for (int z = 0; z < 32; ++z) {
                chunk->addCube(glm::ivec3(x, y, z));
                count++;
            }
        }
    }
    // All 32768 positions should have cubes
    for (int x = 0; x < 32; x += 8) {
        for (int y = 0; y < 32; y += 8) {
            for (int z = 0; z < 32; z += 8) {
                EXPECT_NE(chunk->getCubeAt(glm::ivec3(x, y, z)), nullptr);
            }
        }
    }
}

} // namespace Testing
} // namespace Phyxel
