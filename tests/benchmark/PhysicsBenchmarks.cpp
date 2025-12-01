#include "BenchmarkFixture.h"
#include "physics/PhysicsWorld.h"
#include <glm/glm.hpp>

namespace VulkanCube {
namespace Testing {

/**
 * @brief Benchmark tests for Physics system performance
 */
class PhysicsBenchmarks : public BenchmarkFixture {
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
// Rigid Body Creation Benchmarks
// ============================================================================

TEST_F(PhysicsBenchmarks, CreateDynamicCubes) {
    auto result = runBenchmark("Create 100 dynamic cubes", 10, 100, [this]() {
        for (int i = 0; i < 100; ++i) {
            glm::vec3 pos(i * 2.0f, 10.0f, 0.0f);
            glm::vec3 size(1.0f, 1.0f, 1.0f);
            physicsWorld->createCube(pos, size, 1.0f);
        }
    });
    
    EXPECT_LT(result.averageMs, 100.0) << "Creating dynamic cubes is too slow";
    EXPECT_GT(result.opsPerSecond(), 500.0) << "Should create at least 500 cubes/sec";
}

TEST_F(PhysicsBenchmarks, CreateStaticCubes) {
    auto result = runBenchmark("Create 100 static cubes", 10, 100, [this]() {
        for (int i = 0; i < 100; ++i) {
            glm::vec3 pos(i * 2.0f, 0.0f, 0.0f);
            glm::vec3 size(1.0f, 1.0f, 1.0f);
            physicsWorld->createStaticCube(pos, size);
        }
    });
    
    EXPECT_LT(result.averageMs, 100.0) << "Creating static cubes is too slow";
    EXPECT_GT(result.opsPerSecond(), 500.0) << "Should create at least 500 static cubes/sec";
}

// ============================================================================
// Physics Simulation Benchmarks
// ============================================================================

TEST_F(PhysicsBenchmarks, SimulationStepEmpty) {
    auto result = runBenchmark("Physics step (empty world)", 1000, 1, [this]() {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });
    
    EXPECT_LT(result.averageMs, 1.0) << "Empty physics step is too slow";
}

TEST_F(PhysicsBenchmarks, SimulationStep10Bodies) {
    // Create 10 falling cubes
    for (int i = 0; i < 10; ++i) {
        glm::vec3 pos(i * 2.0f, 10.0f + i, 0.0f);
        physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
    }
    
    auto result = runBenchmark("Physics step (10 bodies)", 1000, 1, [this]() {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });
    
    EXPECT_LT(result.averageMs, 5.0) << "Physics step with 10 bodies is too slow";
}

TEST_F(PhysicsBenchmarks, SimulationStep100Bodies) {
    // Create 100 cubes in a grid
    for (int i = 0; i < 100; ++i) {
        glm::vec3 pos((i % 10) * 2.0f, 10.0f + (i / 10) * 2.0f, (i / 50) * 2.0f);
        physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
    }
    
    auto result = runBenchmark("Physics step (100 bodies)", 100, 1, [this]() {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });
    
    EXPECT_LT(result.averageMs, 16.67) << "Physics step should maintain 60 FPS with 100 bodies";
    std::cout << "  Target FPS:  60 (16.67 ms)\n";
    std::cout << "  Actual FPS:  " << (1000.0 / result.averageMs) << "\n";
}

// ============================================================================
// Collision Detection Benchmarks
// ============================================================================

TEST_F(PhysicsBenchmarks, CollisionDetectionStressTest) {
    // Create a pile of cubes (lots of potential collisions)
    for (int y = 0; y < 10; ++y) {
        for (int x = 0; x < 5; ++x) {
            for (int z = 0; z < 5; ++z) {
                glm::vec3 pos(x * 1.1f, 10.0f + y * 1.1f, z * 1.1f);
                physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
            }
        }
    }
    
    // Create ground
    physicsWorld->createStaticCube(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(20.0f, 1.0f, 20.0f));
    
    auto result = runBenchmark("Physics with collision pile (250 bodies)", 50, 1, [this]() {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });
    
    std::cout << "  Bodies:      250 (pile configuration)\n";
    std::cout << "  Target FPS:  60 (16.67 ms)\n";
    std::cout << "  Actual FPS:  " << (1000.0 / result.averageMs) << "\n";
    
    // This is expected to be slower than isolated bodies
    EXPECT_LT(result.averageMs, 50.0) << "Collision-heavy simulation is extremely slow";
}

// ============================================================================
// Memory Benchmarks
// ============================================================================

TEST_F(PhysicsBenchmarks, RigidBodyCount) {
    std::cout << "\n[MEMORY] Physics world capacity test\n";
    
    // Create many bodies to test scaling
    size_t bodyCount = 1000;
    for (size_t i = 0; i < bodyCount; ++i) {
        glm::vec3 pos((i % 10) * 3.0f, 50.0f + (i / 100) * 3.0f, (i / 10 % 10) * 3.0f);
        physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
    }
    
    int actualCount = physicsWorld->getRigidBodyCount();
    std::cout << "  Created:     " << bodyCount << " rigid bodies\n";
    std::cout << "  In world:    " << actualCount << " rigid bodies\n";
    
    EXPECT_EQ(actualCount, bodyCount) << "Not all bodies were added to physics world";
}

} // namespace Testing
} // namespace VulkanCube
