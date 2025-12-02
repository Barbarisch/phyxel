#include <gtest/gtest.h>
#include "core/WorldGenerator.h"
#include "core/Chunk.h"
#include <glm/glm.hpp>

namespace VulkanCube {

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
    bool result = chunk->addCube(glm::ivec3(0, 0, 0), glm::vec3(1.0f));
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

TEST_F(WorldGeneratorTest, CustomGenerator) {
    WorldGenerator generator(WorldGenerator::GenerationType::Custom);
    
    // Custom generator that only creates a cube at (1, 1, 1)
    generator.setCustomGenerator([](const glm::ivec3& chunkCoord, const glm::ivec3& localPos, glm::vec3& outColor) {
        if (localPos == glm::ivec3(1, 1, 1)) {
            outColor = glm::vec3(1.0f, 0.0f, 0.0f);
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

} // namespace VulkanCube
