#include "BenchmarkFixture.h"
#include "core/ChunkManager.h"
#include "core/Chunk.h"
#include "physics/PhysicsWorld.h"
#include <memory>

namespace VulkanCube {
namespace Testing {

/**
 * @brief Benchmark tests for Chunk and ChunkManager performance
 */
class ChunkBenchmarks : public BenchmarkFixture {
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
// Chunk Creation Benchmarks
// ============================================================================

TEST_F(ChunkBenchmarks, SingleChunkCreation) {
    auto result = runBenchmark("Create single chunk", 100, 1, [this]() {
        auto chunk = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
        chunk->setPhysicsWorld(physicsWorld.get());
        chunk->populateWithCubes();
    });
    
    // Chunk creation with 32³ cubes should be reasonably fast
    EXPECT_LT(result.averageMs, 150.0) << "Chunk creation is too slow";
}

TEST_F(ChunkBenchmarks, MultipleChunkCreation) {
    auto result = runBenchmark("Create 10 chunks", 50, 10, [this]() {
        std::vector<std::unique_ptr<Chunk>> chunks;
        for (int i = 0; i < 10; ++i) {
            auto chunk = std::make_unique<Chunk>(glm::ivec3(i * 32, 0, 0));
            chunk->setPhysicsWorld(physicsWorld.get());
            chunk->populateWithCubes();
            chunks.push_back(std::move(chunk));
        }
    });
    
    EXPECT_LT(result.averageMs, 1300.0) << "Creating 10 chunks is too slow";
}

// NOTE: ChunkManager benchmarks re-enabled after fixing callback explosion (issue #6).
// ChunkVoxelManager now uses stored callbacks via setCallbacks(), with no std::function
// arguments needed at call sites.

TEST_F(ChunkBenchmarks, ChunkManagerCreation) {
    auto result = runBenchmark("ChunkManager create chunk", 50, 1, [this]() {
        auto testManager = std::make_unique<ChunkManager>();
        testManager->setPhysicsWorld(physicsWorld.get());
        testManager->createChunk(glm::ivec3(0, 0, 0));
        // Let unique_ptr clean up automatically
    });
    
    EXPECT_LT(result.averageMs, 150.0) << "ChunkManager chunk creation is too slow";
}

// ============================================================================
// Voxel Operation Benchmarks
// ============================================================================

TEST_F(ChunkBenchmarks, AddCubePerformance) {
    auto testManager = std::make_unique<ChunkManager>();
    testManager->setPhysicsWorld(physicsWorld.get());
    testManager->createChunk(glm::ivec3(0, 0, 0));
    testManager->initializeAllChunkVoxelMaps();
    
    int cubeIndex = 0;
    auto result = runBenchmark("Add 100 cubes", 10, 100, [&]() {
        for (int i = 0; i < 100; ++i) {
            glm::ivec3 pos(cubeIndex % 32, (cubeIndex / 32) % 32, (cubeIndex / 1024) % 32);
            testManager->addCube(pos);
            cubeIndex++;
        }
    });
    
    EXPECT_LT(result.averageMs, 100.0) << "Adding cubes is too slow";
    EXPECT_GT(result.opsPerSecond(), 500.0) << "Should handle at least 500 cube additions per second";
}

TEST_F(ChunkBenchmarks, RemoveCubePerformance) {
    auto testManager = std::make_unique<ChunkManager>();
    testManager->setPhysicsWorld(physicsWorld.get());
    testManager->createChunk(glm::ivec3(0, 0, 0));
    testManager->initializeAllChunkVoxelMaps();
    
    auto result = runBenchmark("Remove 100 cubes", 10, 100, [&]() {
        for (int i = 0; i < 100; ++i) {
            glm::ivec3 pos(i % 32, (i / 32) % 32, (i / 1024) % 32);
            testManager->removeCube(pos);
        }
    });
    
    EXPECT_LT(result.averageMs, 100.0) << "Removing cubes is too slow";
    EXPECT_GT(result.opsPerSecond(), 500.0) << "Should handle at least 500 cube removals per second";
}

TEST_F(ChunkBenchmarks, GetCubeAtPerformance) {
    auto testManager = std::make_unique<ChunkManager>();
    testManager->setPhysicsWorld(physicsWorld.get());
    testManager->createChunk(glm::ivec3(0, 0, 0));
    testManager->initializeAllChunkVoxelMaps();
    
    auto result = runBenchmark("Query 1000 cubes", 100, 1000, [&]() {
        for (int i = 0; i < 1000; ++i) {
            glm::ivec3 pos(i % 32, (i / 32) % 32, (i / 1024) % 32);
            volatile Cube* cube = testManager->getCubeAt(pos);
            (void)cube; // Prevent optimization
        }
    });
    
    EXPECT_LT(result.averageMs, 10.0) << "Cube queries are too slow";
    EXPECT_GT(result.opsPerSecond(), 50000.0) << "Should handle at least 50k queries per second";
}

// ============================================================================
// Face Culling Benchmarks
// ============================================================================

TEST_F(ChunkBenchmarks, SingleChunkFaceCulling) {
    auto chunk = std::make_unique<Chunk>(glm::ivec3(0, 0, 0));
    chunk->setPhysicsWorld(physicsWorld.get());
    chunk->populateWithCubes();
    
    auto result = runBenchmark("Rebuild faces (single chunk)", 50, 1, [&chunk]() {
        chunk->rebuildFaces();
    });
    
    EXPECT_LT(result.averageMs, 50.0) << "Face rebuilding is too slow";
}

TEST_F(ChunkBenchmarks, MultiChunkFaceCulling) {
    auto testManager = std::make_unique<ChunkManager>();
    testManager->setPhysicsWorld(physicsWorld.get());
    
    std::vector<glm::ivec3> origins = {
        {0, 0, 0}, {32, 0, 0}, {0, 0, 32}, {32, 0, 32}
    };
    testManager->createChunks(origins);
    
    auto result = runBenchmark("Rebuild all chunk faces (4 chunks)", 20, 4, [&]() {
        testManager->rebuildAllChunkFaces();
    });
    
    EXPECT_LT(result.averageMs, 250.0) << "Multi-chunk face rebuilding is too slow";
}

// ============================================================================
// Memory Benchmarks
// ============================================================================

TEST_F(ChunkBenchmarks, ChunkMemoryFootprint) {
    std::cout << "\n[MEMORY] Chunk memory footprint test\n";
    
    size_t chunksToCreate = 100;
    std::vector<std::unique_ptr<Chunk>> chunks;
    chunks.reserve(chunksToCreate);
    
    // Create chunks and measure
    for (size_t i = 0; i < chunksToCreate; ++i) {
        auto chunk = std::make_unique<Chunk>(glm::ivec3(i * 32, 0, 0));
        chunk->setPhysicsWorld(physicsWorld.get());
        chunk->populateWithCubes();
        chunks.push_back(std::move(chunk));
    }
    
    // Estimate memory usage (rough calculation)
    size_t cubesPerChunk = 32 * 32 * 32; // Fully populated
    size_t estimatedBytesPerCube = sizeof(Cube*) + sizeof(Cube);
    size_t estimatedBytesPerChunk = estimatedBytesPerCube * cubesPerChunk + sizeof(Chunk);
    size_t totalEstimatedMB = (estimatedBytesPerChunk * chunksToCreate) / (1024 * 1024);
    
    std::cout << "  Chunks:           " << chunksToCreate << "\n";
    std::cout << "  Cubes/chunk:      ~" << cubesPerChunk << "\n";
    std::cout << "  Est. memory:      ~" << totalEstimatedMB << " MB\n";
    std::cout << "  Est. per chunk:   ~" << (estimatedBytesPerChunk / 1024) << " KB\n";
    
    // Just verify chunks were created
    EXPECT_EQ(chunks.size(), chunksToCreate);
}

// ============================================================================
// Voxel Map Initialization Benchmark
// ============================================================================

TEST_F(ChunkBenchmarks, VoxelMapInitialization) {
    auto testManager = std::make_unique<ChunkManager>();
    testManager->setPhysicsWorld(physicsWorld.get());
    
    std::vector<glm::ivec3> origins = {
        {0, 0, 0}, {32, 0, 0}, {0, 0, 32}, {32, 0, 32}
    };
    testManager->createChunks(origins);
    
    auto result = runBenchmark("Initialize voxel maps (4 chunks)", 20, 4, [&]() {
        testManager->initializeAllChunkVoxelMaps();
    });
    
    EXPECT_LT(result.averageMs, 600.0) << "Voxel map initialization is too slow";
}

} // namespace Testing
} // namespace VulkanCube
