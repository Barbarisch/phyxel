#include <gtest/gtest.h>
#include "core/WorldGenerator.h"
#include "core/Chunk.h"
#include <glm/glm.hpp>

namespace Phyxel {

class WorldGeneratorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code if needed
    }

    void TearDown() override {
        // Cleanup code if needed
    }
};

TEST_F(WorldGeneratorTest, ChunkAddCube) {
    std::cout << "Test started" << std::endl;
    
    std::cout << "Creating chunk..." << std::endl;
    auto chunk = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    std::cout << "Initializing chunk..." << std::endl;
    chunk->initializeForLoading();
    std::cout << "Chunk initialized. Size: " << chunk->getCubeCount() << std::endl;
    
    std::cout << "Adding cube..." << std::endl;
    bool result = chunk->addCube(glm::ivec3(0, 0, 0));
    std::cout << "Cube added. Result: " << result << std::endl;
    
    EXPECT_TRUE(result);
    EXPECT_NE(chunk->getCubeAt(glm::ivec3(0, 0, 0)), nullptr);
}

TEST_F(WorldGeneratorTest, GenerateFlatWorld) {
    WorldGenerator generator(WorldGenerator::GenerationType::Flat);
    auto chunk = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    chunk->initializeForLoading();
    
    generator.generateChunk(*chunk, glm::ivec3(0, 0, 0));
    
    // Check that cubes exist at y <= 16
    // We can check a few sample points
    
    // Should exist
    EXPECT_NE(chunk->getCubeAt(glm::ivec3(0, 0, 0)), nullptr);
    EXPECT_NE(chunk->getCubeAt(glm::ivec3(0, 16, 0)), nullptr);
    EXPECT_NE(chunk->getCubeAt(glm::ivec3(15, 10, 15)), nullptr);
    
    // Should not exist
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(0, 17, 0)), nullptr);
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(31, 31, 31)), nullptr);
}

// ── Phase 4.4 stage 3: generator uniform fast path ───────────────────────────────
// A chunk that is fully below every column's surface (depth >= 4 everywhere, one deep
// material) must land in the UNIFORM store representation — this is what feeds the sealed
// classifier for streamed terrain. Written RED: today the per-voxel addCube loop splits the
// store dense on the first write, so generated buried chunks can never seal.

TEST_F(WorldGeneratorTest, DeepBuriedChunkGeneratesUniformStore) {
    WorldGenerator generator(WorldGenerator::GenerationType::Flat);   // surface at Y=16 everywhere
    Chunk chunk(glm::ivec3(0, -32, 0));
    chunk.initializeForLoading();

    generator.generateChunk(chunk, glm::ivec3(0, -1, 0));   // world Y -32..-1: depth 17..48

    const ChunkVoxelStore& store = chunk.getVoxelStore();
    EXPECT_EQ(store.solidCount(), ChunkVoxelStore::kVoxels);
    EXPECT_TRUE(store.isUniform())
        << "fully-buried generated chunks must use the uniform representation";
    // Unit env has no biomes.json loaded -> materialForColumn's deep fallback is "Stone".
    EXPECT_EQ(store.material(0), "Stone");
    EXPECT_TRUE(store.visible(0));
    EXPECT_EQ(chunk.materializedCubeCount(), 0u);
}

// Equivalence guard: the fast path must produce EXACTLY what the per-voxel path produced —
// asserted per-voxel against the same rule the slow loop applies (Flat: solid iff wy <= 16).
TEST_F(WorldGeneratorTest, DeepChunkFastPathMatchesPerVoxelRule) {
    WorldGenerator generator(WorldGenerator::GenerationType::Flat);
    Chunk deep(glm::ivec3(0, -32, 0));
    deep.initializeForLoading();
    generator.generateChunk(deep, glm::ivec3(0, -1, 0));
    for (size_t i = 0; i < ChunkVoxelStore::kVoxels; ++i) {
        ASSERT_TRUE(deep.getVoxelStore().solid(i)) << "missing voxel at " << i;
        ASSERT_EQ(deep.getVoxelStore().material(i), "Stone");
    }
}

// Surface chunks must be untouched by the fast path (mixed materials, air above surface).
TEST_F(WorldGeneratorTest, SurfaceChunkStaysPerVoxel) {
    WorldGenerator generator(WorldGenerator::GenerationType::Flat);
    Chunk chunk(glm::ivec3(0, 0, 0));
    chunk.initializeForLoading();
    generator.generateChunk(chunk, glm::ivec3(0, 0, 0));

    EXPECT_FALSE(chunk.getVoxelStore().isUniform());
    EXPECT_TRUE(chunk.getVoxelStore().solid(0));                       // below surface
    EXPECT_FALSE(chunk.getVoxelStore().solid(31 + 31 * 32 + 31 * 1024));  // above surface
}

// Pure-sky chunks must stay uniform-air (no writes at all).
TEST_F(WorldGeneratorTest, SkyChunkStaysUniformAir) {
    WorldGenerator generator(WorldGenerator::GenerationType::Flat);
    Chunk chunk(glm::ivec3(0, 64, 0));
    chunk.initializeForLoading();
    generator.generateChunk(chunk, glm::ivec3(0, 2, 0));   // world Y 64..95, all above 16

    EXPECT_EQ(chunk.getVoxelStore().solidCount(), 0u);
    EXPECT_TRUE(chunk.getVoxelStore().isUniform());
}

TEST_F(WorldGeneratorTest, CustomGenerator) {
    WorldGenerator generator(WorldGenerator::GenerationType::Custom);
    
    // Custom generator that only creates a cube at (1, 1, 1)
    generator.setCustomGenerator([](const glm::ivec3& chunkCoord, const glm::ivec3& localPos) {
        if (localPos == glm::ivec3(1, 1, 1)) {
            return true;
        }
        return false;
    });
    
    auto chunk = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    chunk->initializeForLoading();
    generator.generateChunk(*chunk, glm::ivec3(0, 0, 0));
    
    EXPECT_NE(chunk->getCubeAt(glm::ivec3(1, 1, 1)), nullptr);
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(0, 0, 0)), nullptr);
    EXPECT_EQ(chunk->getCubeAt(glm::ivec3(2, 2, 2)), nullptr);
}

TEST_F(WorldGeneratorTest, Determinism) {
    // Same seed should produce same result for Random generation
    uint32_t seed = 12345;
    WorldGenerator generator1(WorldGenerator::GenerationType::Random, seed);
    WorldGenerator generator2(WorldGenerator::GenerationType::Random, seed);
    
    auto chunk1 = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    auto chunk2 = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    chunk1->initializeForLoading();
    chunk2->initializeForLoading();
    
    generator1.generateChunk(*chunk1, glm::ivec3(0, 0, 0));
    generator2.generateChunk(*chunk2, glm::ivec3(0, 0, 0));
    
    // Compare chunks
    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y) {
            for (int z = 0; z < 32; ++z) {
                glm::ivec3 pos(x, y, z);
                bool hasCube1 = (chunk1->getCubeAt(pos) != nullptr);
                bool hasCube2 = (chunk2->getCubeAt(pos) != nullptr);
                EXPECT_EQ(hasCube1, hasCube2) << "Mismatch at " << x << "," << y << "," << z;
            }
        }
    }
}

TEST_F(WorldGeneratorTest, DifferentSeedsProduceDifferentResults) {
    // Different seeds should produce different results (statistically)
    WorldGenerator generator1(WorldGenerator::GenerationType::Random, 12345);
    WorldGenerator generator2(WorldGenerator::GenerationType::Random, 67890);
    
    auto chunk1 = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    auto chunk2 = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    chunk1->initializeForLoading();
    chunk2->initializeForLoading();
    
    generator1.generateChunk(*chunk1, glm::ivec3(0, 0, 0));
    generator2.generateChunk(*chunk2, glm::ivec3(0, 0, 0));
    
    int differences = 0;
    for (int x = 0; x < 32; ++x) {
        for (int y = 0; y < 32; ++y) {
            for (int z = 0; z < 32; ++z) {
                glm::ivec3 pos(x, y, z);
                bool hasCube1 = (chunk1->getCubeAt(pos) != nullptr);
                bool hasCube2 = (chunk2->getCubeAt(pos) != nullptr);
                if (hasCube1 != hasCube2) {
                    differences++;
                }
            }
        }
    }
    
    EXPECT_GT(differences, 0);
}

} // namespace Phyxel
