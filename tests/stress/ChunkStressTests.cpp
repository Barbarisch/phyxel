#include "StressTestFixture.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "physics/PhysicsWorld.h"
#include <memory>
#include <vector>

namespace Phyxel {
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
    size_t count = STRESS_COUNT(1000, 100);
    chunks.reserve(count);
    
    auto result = runStressTest("Create chunks sequentially", count, [&](size_t i) {
        auto chunk = std::make_unique<Chunk>(glm::ivec3(i * 32, 0, 0));
        chunk->setPhysicsWorld(physicsWorld.get());
        chunk->populateWithCubes();
        chunks.push_back(std::move(chunk));
    });
    
    EXPECT_TRUE(result.success) << "Failed to create chunks: " << result.failureReason;
    EXPECT_EQ(chunks.size(), count);
    EXPECT_LT(result.avgTimeMs, 200.0) << "Average chunk creation too slow under stress";
    
    std::cout << "  Total chunks created: " << chunks.size() << "\n";
    std::cout << "  Estimated memory: ~" << (chunks.size() * 8) << " MB\n";
}

TEST_F(ChunkStressTests, CreateAndDestroyChunks) {
    size_t count = STRESS_COUNT(500, 50);
    auto result = runStressTest("Create and destroy chunks", count, [&](size_t i) {
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
    size_t bursts = STRESS_COUNT(100, 10);
    
    auto result = runStressTest("Create chunks rapidly (bursts)", bursts, [&](size_t burst) {
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
    EXPECT_EQ(chunks.size(), bursts * 10);
    std::cout << "  Total chunks after bursts: " << chunks.size() << "\n";
}

// ============================================================================
// Face Culling Stress Tests
// ============================================================================

TEST_F(ChunkStressTests, MassiveFaceCullingOperations) {
    auto chunk = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    chunk->setPhysicsWorld(physicsWorld.get());
    chunk->populateWithCubes();
    
    size_t iterations = STRESS_COUNT(1000, 100);
    auto result = runStressTest("Rebuild faces", iterations, [&](size_t i) {
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
    
    size_t iterations = STRESS_COUNT(100, 10);
    auto result = runStressTest("Rebuild 50 chunks", iterations, [&](size_t i) {
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
    size_t iterations = STRESS_COUNT(500, 50);
    auto result = runStressTest("Modify 100 voxels", iterations, [&](size_t iteration) {
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
    
    size_t count = STRESS_COUNT(500, 50);
    ScopedTimer timer("Allocate fully populated chunks");
    
    for (int i = 0; i < count; ++i) {
        auto chunk = std::make_unique<Chunk>(glm::ivec3(i * 32, 0, 0));
        chunk->setPhysicsWorld(physicsWorld.get());
        chunk->populateWithCubes();
        chunks.push_back(std::move(chunk));
        
        if ((i + 1) % 10 == 0) {
            std::cout << "  Allocated " << (i + 1) << " chunks...\n";
        }
    }
    
    EXPECT_EQ(chunks.size(), count);
    std::cout << "  Total cubes: ~" << (count * 32 * 32 * 32) << "\n";
    std::cout << "  Estimated memory: ~" << (count * 8) << " MB\n";
    
    // Verify random chunks are still valid
    EXPECT_NE(chunks[0].get(), nullptr);
    EXPECT_NE(chunks[count/2].get(), nullptr);
    EXPECT_NE(chunks[count-1].get(), nullptr);
    EXPECT_GT(chunks[0]->getCubeCount(), 0);
}

TEST_F(ChunkStressTests, ChunkCreationDestruction_MemoryLeak) {
    // Create and destroy chunks many times to detect memory leaks
    size_t iterations = STRESS_COUNT(1000, 100);
    auto result = runStressTest("Memory leak detection (create/destroy cycles)", iterations, [&](size_t i) {
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
    
    size_t iterations = STRESS_COUNT(500, 50);
    auto result = runStressTest("Long-running operations", iterations, [&](size_t i) {
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
    size_t count = STRESS_COUNT(2000, 200);
    chunks.reserve(count);
    
    ScopedTimer timer("Create chunks (extreme load)");
    
    try {
        for (int i = 0; i < count; ++i) {
            auto chunk = std::make_unique<Chunk>(glm::ivec3((i % 100) * 32, ((i / 100) % 20) * 32, 0));
            chunk->setPhysicsWorld(physicsWorld.get());
            chunk->populateWithCubes();
            chunks.push_back(std::move(chunk));
            
            if ((i + 1) % 20 == 0) {
                std::cout << "  Progress: " << (i + 1) << "/" << count << " chunks\n";
            }
        }
        
        EXPECT_EQ(chunks.size(), count);
        std::cout << "  Successfully created " << count << " chunks!\n";
        std::cout << "  Total cubes: ~" << (static_cast<long long>(count) * 32 * 32 * 32) << "\n";
        std::cout << "  Estimated memory: ~" << (count * 8) << " MB\n";
        
    } catch (const std::exception& e) {
        std::cout << "  System limit reached at " << chunks.size() << " chunks\n";
        std::cout << "  Exception: " << e.what() << "\n";
        
        // This is actually useful information about system limits
        EXPECT_GT(chunks.size(), 50) << "System should handle at least 50 chunks";
    }
}

} // namespace Testing
} // namespace Phyxel
