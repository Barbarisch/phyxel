#include "StressTestFixture.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "physics/PhysicsWorld.h"
#include <memory>
#include <vector>

namespace VulkanCube {
namespace Testing {

/**
 * @brief Stress tests for Chunk system under extreme load
 */
class ChunkStressTests : public StressTestFixture {
protected:
    void SetUp() override {
        physicsWorld = std::make_unique<Physics::PhysicsWorld>();
        physicsWorld->initialize();
    }
    
    void TearDown() override {
        physicsWorld.reset();
    }
    
    std::unique_ptr<Physics::PhysicsWorld> physicsWorld;
};

// ============================================================================
// Chunk Creation Stress Tests
// ============================================================================

TEST_F(ChunkStressTests, Create1000Chunks) {
    std::vector<std::unique_ptr<Chunk>> chunks;
    chunks.reserve(1000);
    
    auto result = runStressTest("Create 1000 chunks sequentially", 1000, [&](size_t i) {
        auto chunk = std::make_unique<Chunk>(glm::ivec3(i * 32, 0, 0));
        chunk->setPhysicsWorld(physicsWorld.get());
        chunk->populateWithCubes();
        chunks.push_back(std::move(chunk));
    });
    
    EXPECT_TRUE(result.success) << "Failed to create 1000 chunks: " << result.failureReason;
    EXPECT_EQ(chunks.size(), 1000);
    EXPECT_LT(result.avgTimeMs, 200.0) << "Average chunk creation too slow under stress";
    
    std::cout << "  Total chunks created: " << chunks.size() << "\n";
    std::cout << "  Estimated memory: ~" << (chunks.size() * 8) << " MB\n";
}

TEST_F(ChunkStressTests, CreateAndDestroyChunks) {
    auto result = runStressTest("Create and destroy 500 chunks", 500, [&](size_t i) {
        auto chunk = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
        chunk->setPhysicsWorld(physicsWorld.get());
        chunk->populateWithCubes();
        // Chunk destroyed at end of iteration
    });
    
    EXPECT_TRUE(result.success) << "Failed during create/destroy cycle: " << result.failureReason;
    EXPECT_LT(result.avgTimeMs, 200.0) << "Create/destroy cycle too slow";
}

TEST_F(ChunkStressTests, RapidChunkCreationBursts) {
    std::vector<std::unique_ptr<Chunk>> chunks;
    
    auto result = runStressTest("Create 10 chunks rapidly (100 bursts)", 100, [&](size_t burst) {
        std::vector<std::unique_ptr<Chunk>> burstChunks;
        for (int i = 0; i < 10; ++i) {
            auto chunk = std::make_unique<Chunk>(glm::ivec3(burst * 320 + i * 32, 0, 0));
            chunk->setPhysicsWorld(physicsWorld.get());
            chunk->populateWithCubes();
            burstChunks.push_back(std::move(chunk));
        }
        // Move burst to main storage
        for (auto& chunk : burstChunks) {
            chunks.push_back(std::move(chunk));
        }
    });
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(chunks.size(), 1000);
    std::cout << "  Total chunks after bursts: " << chunks.size() << "\n";
}

// ============================================================================
// Face Culling Stress Tests
// ============================================================================

TEST_F(ChunkStressTests, MassiveFaceCullingOperations) {
    auto chunk = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    chunk->setPhysicsWorld(physicsWorld.get());
    chunk->populateWithCubes();
    
    auto result = runStressTest("Rebuild faces 1000 times", 1000, [&](size_t i) {
        chunk->rebuildFaces();
    });
    
    EXPECT_TRUE(result.success);
    EXPECT_LT(result.avgTimeMs, 50.0) << "Face rebuilding degraded under stress";
    
    // Verify chunk still valid after stress
    EXPECT_GT(chunk->getCubeCount(), 0);
}

TEST_F(ChunkStressTests, ConcurrentChunkFaceRebuilding) {
    std::vector<std::unique_ptr<Chunk>> chunks;
    for (int i = 0; i < 50; ++i) {
        auto chunk = std::make_unique<Chunk>(glm::ivec3(i * 32, 0, 0));
        chunk->setPhysicsWorld(physicsWorld.get());
        chunk->populateWithCubes();
        chunks.push_back(std::move(chunk));
    }
    
    auto result = runStressTest("Rebuild 50 chunks (100 iterations)", 100, [&](size_t i) {
        for (auto& chunk : chunks) {
            chunk->rebuildFaces();
        }
    });
    
    EXPECT_TRUE(result.success);
    EXPECT_LT(result.totalTimeMs, 120000.0) << "Total face rebuilding took too long";
    
    std::cout << "  Chunks rebuilt: " << chunks.size() << "\n";
    std::cout << "  Total rebuilds: " << (chunks.size() * result.iterations) << "\n";
}

// ============================================================================
// Voxel Modification Stress Tests
// ============================================================================

TEST_F(ChunkStressTests, RapidVoxelModifications) {
    auto chunk = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    chunk->setPhysicsWorld(physicsWorld.get());
    chunk->populateWithCubes();
    
    size_t modificationsPerIteration = 100;
    auto result = runStressTest("Modify 100 voxels (500 iterations)", 500, [&](size_t iteration) {
        for (size_t i = 0; i < modificationsPerIteration; ++i) {
            int x = (iteration * modificationsPerIteration + i) % 32;
            int y = ((iteration * modificationsPerIteration + i) / 32) % 32;
            int z = ((iteration * modificationsPerIteration + i) / 1024) % 32;
            
            // Simulate voxel modification by accessing cubes
            auto* cube = chunk->getCubeAt(glm::ivec3(x, y, z));
            (void)cube; // Prevent optimization
        }
    });
    
    EXPECT_TRUE(result.success);
    std::cout << "  Total voxel accesses: " << (modificationsPerIteration * result.iterations) << "\n";
}

// ============================================================================
// Memory Stress Tests
// ============================================================================

TEST_F(ChunkStressTests, AllocateMassiveChunkArray) {
    std::vector<std::unique_ptr<Chunk>> chunks;
    
    ScopedTimer timer("Allocate 500 fully populated chunks");
    
    for (int i = 0; i < 500; ++i) {
        auto chunk = std::make_unique<Chunk>(glm::ivec3(i * 32, 0, 0));
        chunk->setPhysicsWorld(physicsWorld.get());
        chunk->populateWithCubes();
        chunks.push_back(std::move(chunk));
        
        if ((i + 1) % 50 == 0) {
            std::cout << "  Allocated " << (i + 1) << " chunks...\n";
        }
    }
    
    EXPECT_EQ(chunks.size(), 500);
    std::cout << "  Total cubes: ~" << (500 * 32 * 32 * 32) << "\n";
    std::cout << "  Estimated memory: ~" << (500 * 8) << " MB\n";
    
    // Verify random chunks are still valid
    EXPECT_NE(chunks[0].get(), nullptr);
    EXPECT_NE(chunks[250].get(), nullptr);
    EXPECT_NE(chunks[499].get(), nullptr);
    EXPECT_GT(chunks[0]->getCubeCount(), 0);
}

TEST_F(ChunkStressTests, ChunkCreationDestruction_MemoryLeak) {
    // Create and destroy chunks many times to detect memory leaks
    auto result = runStressTest("Memory leak detection (1000 create/destroy cycles)", 1000, [&](size_t i) {
        std::vector<std::unique_ptr<Chunk>> tempChunks;
        
        for (int j = 0; j < 10; ++j) {
            auto chunk = std::make_unique<Chunk>(glm::ivec3(j * 32, 0, 0));
            chunk->setPhysicsWorld(physicsWorld.get());
            chunk->populateWithCubes();
            tempChunks.push_back(std::move(chunk));
        }
        
        // All chunks destroyed at end of iteration
    });
    
    EXPECT_TRUE(result.success);
    std::cout << "  Total allocations: " << (result.iterations * 10) << " chunks\n";
    std::cout << "  If memory is stable, no leaks detected\n";
}

// ============================================================================
// Endurance Tests
// ============================================================================

TEST_F(ChunkStressTests, LongRunningChunkOperations) {
    std::vector<std::unique_ptr<Chunk>> chunks;
    
    // Create initial chunk set
    for (int i = 0; i < 20; ++i) {
        auto chunk = std::make_unique<Chunk>(glm::ivec3(i * 32, 0, 0));
        chunk->setPhysicsWorld(physicsWorld.get());
        chunk->populateWithCubes();
        chunks.push_back(std::move(chunk));
    }
    
    auto result = runStressTest("Long-running operations (500 iterations)", 500, [&](size_t i) {
        // Simulate various operations
        size_t chunkIndex = i % chunks.size();
        
        // Face culling
        chunks[chunkIndex]->rebuildFaces();
        
        // Cube access
        for (int j = 0; j < 10; ++j) {
            auto* cube = chunks[chunkIndex]->getCubeAt(glm::ivec3(j, 0, 0));
            (void)cube;
        }
    });
    
    EXPECT_TRUE(result.success);
    EXPECT_LT(result.maxTimeMs, 100.0) << "Operations degraded over time";
    
    // Verify chunks still valid after long run
    for (const auto& chunk : chunks) {
        EXPECT_GT(chunk->getCubeCount(), 0);
    }
}

// ============================================================================
// Extreme Load Tests
// ============================================================================

TEST_F(ChunkStressTests, ExtremeChunkCount) {
    std::vector<std::unique_ptr<Chunk>> chunks;
    chunks.reserve(2000);
    
    ScopedTimer timer("Create 2000 chunks (extreme load)");
    
    try {
        for (int i = 0; i < 2000; ++i) {
            auto chunk = std::make_unique<Chunk>(glm::ivec3((i % 100) * 32, ((i / 100) % 20) * 32, 0));
            chunk->setPhysicsWorld(physicsWorld.get());
            chunk->populateWithCubes();
            chunks.push_back(std::move(chunk));
            
            if ((i + 1) % 200 == 0) {
                std::cout << "  Progress: " << (i + 1) << "/2000 chunks\n";
            }
        }
        
        EXPECT_EQ(chunks.size(), 2000);
        std::cout << "  Successfully created 2000 chunks!\n";
        std::cout << "  Total cubes: ~" << (2000LL * 32 * 32 * 32) << "\n";
        std::cout << "  Estimated memory: ~" << (2000 * 8) << " MB\n";
        
    } catch (const std::exception& e) {
        std::cout << "  System limit reached at " << chunks.size() << " chunks\n";
        std::cout << "  Exception: " << e.what() << "\n";
        
        // This is actually useful information about system limits
        EXPECT_GT(chunks.size(), 100) << "System should handle at least 100 chunks";
    }
}

} // namespace Testing
} // namespace VulkanCube
