#include "BenchmarkFixture.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include <glm/glm.hpp>

namespace Phyxel {
namespace Testing {

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

    Physics::VoxelRigidBody* makeBody(const glm::vec3& pos, float mass = 1.0f) {
        return physicsWorld->getVoxelWorld()->createVoxelBody(pos, glm::vec3(0.5f), mass);
    }
};

TEST_F(PhysicsBenchmarks, CreateDynamicBodies) {
    auto result = runBenchmark("Create 100 dynamic voxel bodies", 10, 100, [this]() {
        for (int i = 0; i < 100; ++i) {
            makeBody(glm::vec3(i * 2.0f, 10.0f, 0.0f));
        }
    });

    EXPECT_LT(result.averageMs, 100.0) << "Creating dynamic bodies is too slow";
    EXPECT_GT(result.opsPerSecond(), 500.0) << "Should create at least 500 bodies/sec";
}

TEST_F(PhysicsBenchmarks, SimulationStepEmpty) {
    auto result = runBenchmark("Physics step (empty world)", 1000, 1, [this]() {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });

    EXPECT_LT(result.averageMs, 1.0) << "Empty physics step is too slow";
}

TEST_F(PhysicsBenchmarks, SimulationStep10Bodies) {
    for (int i = 0; i < 10; ++i) {
        makeBody(glm::vec3(i * 2.0f, 10.0f + i, 0.0f));
    }

    auto result = runBenchmark("Physics step (10 bodies)", 1000, 1, [this]() {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });

    EXPECT_LT(result.averageMs, 5.0) << "Physics step with 10 bodies is too slow";
}

TEST_F(PhysicsBenchmarks, SimulationStep100Bodies) {
    for (int i = 0; i < 100; ++i) {
        makeBody(glm::vec3((i % 10) * 2.0f, 10.0f + (i / 10) * 2.0f, (i / 50) * 2.0f));
    }

    auto result = runBenchmark("Physics step (100 bodies)", 100, 1, [this]() {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });

    EXPECT_LT(result.averageMs, 16.67) << "Physics step should maintain 60 FPS with 100 bodies";
    std::cout << "  Target FPS:  60 (16.67 ms)\n";
    std::cout << "  Actual FPS:  " << (1000.0 / result.averageMs) << "\n";
}

TEST_F(PhysicsBenchmarks, BodyCountTracked) {
    std::cout << "\n[MEMORY] VoxelDynamicsWorld capacity test\n";

    size_t bodyCount = 1000;
    for (size_t i = 0; i < bodyCount; ++i) {
        glm::vec3 pos((i % 10) * 3.0f, 50.0f + (i / 100) * 3.0f, (i / 10 % 10) * 3.0f);
        makeBody(pos);
    }

    size_t actual = physicsWorld->getVoxelWorld()->getBodyCount();
    std::cout << "  Created:     " << bodyCount << " bodies\n";
    std::cout << "  In world:    " << actual << " bodies\n";

    EXPECT_EQ(actual, bodyCount) << "Not all bodies were added to physics world";
}

} // namespace Testing
} // namespace Phyxel
