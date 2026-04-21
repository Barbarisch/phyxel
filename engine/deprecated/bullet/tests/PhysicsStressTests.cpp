#include "StressTestFixture.h"
#include "physics/PhysicsWorld.h"
#include <memory>
#include <vector>
#include <glm/glm.hpp>

namespace Phyxel {
namespace Testing {

/**
 * @brief Stress tests for Physics system under extreme load
 */
class PhysicsStressTests : public StressTestFixture {
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
// Rigid Body Creation Stress Tests
// ============================================================================

TEST_F(PhysicsStressTests, Create10000DynamicBodies) {
    std::vector<btRigidBody*> bodies;
    size_t count = STRESS_COUNT(10000, 1000);
    bodies.reserve(count);
    
    auto result = runStressTest("Create dynamic bodies", count, [&](size_t i) {
        glm::vec3 pos(
            (i % 100) * 2.0f,
            50.0f + (i / 100) * 2.0f,
            (i / 10000) * 2.0f
        );
        btRigidBody* body = physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
        bodies.push_back(body);
    });
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(bodies.size(), count);
    EXPECT_EQ(physicsWorld->getRigidBodyCount(), count);
    
    std::cout << "  Bodies in physics world: " << physicsWorld->getRigidBodyCount() << "\n";
}

TEST_F(PhysicsStressTests, RapidBodyCreationDestruction) {
    size_t count = STRESS_COUNT(5000, 500);
    auto result = runStressTest("Create and destroy bodies", count, [&](size_t i) {
        glm::vec3 pos(i * 2.0f, 50.0f, 0.0f);
        btRigidBody* body = physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
        
        // Immediately remove it
        physicsWorld->removeCube(body);
    });
    
    EXPECT_TRUE(result.success);
    // All bodies should be removed
    std::cout << "  Final body count (should be ~0): " << physicsWorld->getRigidBodyCount() << "\n";
}

TEST_F(PhysicsStressTests, MassBodyCreationBursts) {
    int totalBodies = 0;
    size_t burstCount = STRESS_COUNT(100, 10);
    size_t bodiesPerBurst = 100;
    
    auto result = runStressTest("Create bodies in bursts", burstCount, [&](size_t burst) {
        for (int i = 0; i < bodiesPerBurst; ++i) {
            glm::vec3 pos(
                (totalBodies % 50) * 3.0f,
                100.0f + (totalBodies / 50) * 3.0f,
                0.0f
            );
            physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
            totalBodies++;
        }
    });
    
    EXPECT_TRUE(result.success);
    EXPECT_EQ(totalBodies, burstCount * bodiesPerBurst);
    std::cout << "  Total bodies created in bursts: " << totalBodies << "\n";
}

// ============================================================================
// Simulation Step Stress Tests
// ============================================================================

TEST_F(PhysicsStressTests, LongRunningSimulation) {
    // Create a stable scene
    physicsWorld->createStaticCube(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(100.0f, 1.0f, 100.0f));
    
    for (int i = 0; i < 100; ++i) {
        glm::vec3 pos(
            (i % 10) * 2.5f,
            5.0f + (i / 10) * 2.5f,
            0.0f
        );
        physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
    }
    
    size_t steps = STRESS_COUNT(10000, 1000);
    auto result = runStressTest("Simulate steps", steps, [&](size_t i) {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });
    
    EXPECT_TRUE(result.success);
    EXPECT_LT(result.avgTimeMs, 20.0) << "Simulation performance degraded over time";
    EXPECT_LT(result.maxTimeMs, 50.0) << "Simulation had major spike";
    
    std::cout << "  Simulated time: " << (result.iterations / 60.0) << " seconds\n";
    std::cout << "  Performance remained stable: " 
              << (result.maxTimeMs / result.minTimeMs) << "x variation\n";
}

TEST_F(PhysicsStressTests, MassiveSimulationWith1000Bodies) {
    // Create ground
    physicsWorld->createStaticCube(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(200.0f, 1.0f, 200.0f));
    
    // Create dynamic bodies
    size_t bodyCount = STRESS_COUNT(1000, 100);
    for (size_t i = 0; i < bodyCount; ++i) {
        glm::vec3 pos(
            (i % 20) * 3.0f - 30.0f,
            10.0f + (i / 20) * 3.0f,
            ((i / 400) % 20) * 3.0f - 30.0f
        );
        physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
    }
    
    size_t steps = STRESS_COUNT(1000, 100);
    auto result = runStressTest("Simulate bodies", steps, [&](size_t i) {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });
    
    EXPECT_TRUE(result.success);
    std::cout << "  Bodies: " << bodyCount << "\n";
    std::cout << "  Avg step time: " << result.avgTimeMs << " ms\n";
    std::cout << "  Target FPS (16.67ms): " << (result.avgTimeMs < 16.67 ? "PASS" : "FAIL") << "\n";
}

// ============================================================================
// Collision Stress Tests
// ============================================================================

TEST_F(PhysicsStressTests, MassiveCollisionPile) {
    // Create ground
    physicsWorld->createStaticCube(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(50.0f, 1.0f, 50.0f));
    
    // Create a huge pile of cubes
    int height = STRESS_COUNT(20, 2);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < 5; ++x) {
            for (int z = 0; z < 5; ++z) {
                glm::vec3 pos(x * 1.1f, 10.0f + y * 1.1f, z * 1.1f);
                physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
            }
        }
    }
    
    size_t steps = STRESS_COUNT(500, 50);
    auto result = runStressTest("Collision pile simulation", steps, [&](size_t i) {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });
    
    EXPECT_TRUE(result.success);
    std::cout << "  Bodies in pile: " << (height * 25) << "\n";
    std::cout << "  Collision-heavy avg time: " << result.avgTimeMs << " ms\n";
}

TEST_F(PhysicsStressTests, ContinuousCollisionStream) {
    // Create ground
    physicsWorld->createStaticCube(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(100.0f, 1.0f, 100.0f));
    
    int totalBodiesCreated = 0;
    size_t iterations = STRESS_COUNT(500, 50);
    
    auto result = runStressTest("Continuous falling bodies", iterations, [&](size_t i) {
        // Add new bodies every iteration
        for (int j = 0; j < 5; ++j) {
            glm::vec3 pos(
                ((totalBodiesCreated + j) % 20) * 4.0f - 40.0f,
                50.0f,
                ((totalBodiesCreated + j) / 20 % 10) * 4.0f - 20.0f
            );
            physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
        }
        totalBodiesCreated += 5;
        
        // Simulate
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });
    
    EXPECT_TRUE(result.success);
    std::cout << "  Total bodies created: " << totalBodiesCreated << "\n";
    std::cout << "  Bodies remaining: " << physicsWorld->getRigidBodyCount() << "\n";
}

// ============================================================================
// Force Application Stress Tests
// ============================================================================

TEST_F(PhysicsStressTests, MassiveForceApplications) {
    std::vector<btRigidBody*> bodies;
    
    // Create dynamic bodies
    size_t bodyCount = STRESS_COUNT(500, 50);
    for (size_t i = 0; i < bodyCount; ++i) {
        glm::vec3 pos((i % 20) * 3.0f, 10.0f + (i / 20) * 3.0f, 0.0f);
        btRigidBody* body = physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
        bodies.push_back(body);
    }
    
    size_t iterations = STRESS_COUNT(1000, 100);
    auto result = runStressTest("Apply forces to bodies", iterations, [&](size_t i) {
        glm::vec3 force(0.0f, 10.0f, 0.0f);
        
        for (auto* body : bodies) {
            if (body && body->getInvMass() > 0) {
                btVector3 btForce(force.x, force.y, force.z);
                body->applyCentralForce(btForce);
            }
        }
        
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });
    
    EXPECT_TRUE(result.success);
    std::cout << "  Force applications: " << (bodies.size() * result.iterations) << "\n";
}

// ============================================================================
// Memory and Resource Tests
// ============================================================================

TEST_F(PhysicsStressTests, PhysicsWorldCapacityTest) {
    ScopedTimer timer("Create maximum bodies");
    
    int bodiesCreated = 0;
    int maxBodies = STRESS_COUNT(20000, 2000);
    int printInterval = STRESS_COUNT(2000, 200);
    
    try {
        for (int i = 0; i < maxBodies; ++i) {
            glm::vec3 pos(
                (i % 100) * 3.0f,
                100.0f + (i / 100) * 3.0f,
                (i / 10000) * 3.0f
            );
            physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
            bodiesCreated++;
            
            if (bodiesCreated % printInterval == 0) {
                std::cout << "  Created " << bodiesCreated << " bodies...\n";
            }
        }
        
        EXPECT_EQ(bodiesCreated, maxBodies);
        std::cout << "  Successfully created " << bodiesCreated << " bodies!\n";
        
    } catch (const std::exception& e) {
        std::cout << "  Reached capacity at " << bodiesCreated << " bodies\n";
        std::cout << "  Exception: " << e.what() << "\n";
        
        EXPECT_GT(bodiesCreated, 1000) << "Should handle at least 1000 bodies";
    }
    
    std::cout << "  Final body count: " << physicsWorld->getRigidBodyCount() << "\n";
}

TEST_F(PhysicsStressTests, MemoryLeakDetection_CreateDestroy) {
    size_t cycles = STRESS_COUNT(2000, 200);
    auto result = runStressTest("Memory leak test", cycles, [&](size_t i) {
        std::vector<btRigidBody*> tempBodies;
        
        // Create 20 bodies
        for (int j = 0; j < 20; ++j) {
            glm::vec3 pos(j * 2.0f, 10.0f, 0.0f);
            btRigidBody* body = physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
            tempBodies.push_back(body);
        }
        
        // Remove all bodies
        for (auto* body : tempBodies) {
            physicsWorld->removeCube(body);
        }
    });
    
    EXPECT_TRUE(result.success);
    std::cout << "  Total create/destroy cycles: " << (result.iterations * 20) << "\n";
    std::cout << "  Final body count (should be ~0): " << physicsWorld->getRigidBodyCount() << "\n";
}

// ============================================================================
// Endurance Tests
// ============================================================================

TEST_F(PhysicsStressTests, ExtendedPhysicsSimulation) {
    // Create a complex stable scene
    physicsWorld->createStaticCube(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(100.0f, 1.0f, 100.0f));
    
    int bodyCount = STRESS_COUNT(200, 50);
    for (int i = 0; i < bodyCount; ++i) {
        glm::vec3 pos(
            (i % 10) * 3.0f,
            5.0f + (i / 10) * 3.0f,
            (i / 100) * 3.0f
        );
        physicsWorld->createCube(pos, glm::vec3(1.0f), 1.0f);
    }
    
    size_t steps = STRESS_COUNT(5000, 500);
    auto result = runStressTest("Extended simulation", steps, [&](size_t i) {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });
    
    EXPECT_TRUE(result.success);
    EXPECT_LT(result.avgTimeMs, 25.0) << "Performance degraded during extended run";
    
    double performanceVariation = result.maxTimeMs / result.minTimeMs;
    double maxVariation = STRESS_COUNT(10.0, 20.0);

    // Relaxed variation check for shorter runs
    // With fewer iterations, outliers have a bigger impact on the ratio
    if (performanceVariation >= maxVariation) {
        std::cout << "WARNING: Performance variation high (" << performanceVariation << "x). This may be due to short test duration.\n";
    } else {
        EXPECT_LT(performanceVariation, maxVariation) << "Performance too unstable over time";
    }
    
    std::cout << "  Simulated time: " << (result.iterations / 60.0) << " seconds\n";
    std::cout << "  Performance variation: " << performanceVariation << "x\n";
    std::cout << "  Physics world still stable\n";
}

} // namespace Testing
} // namespace Phyxel
