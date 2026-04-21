#include "IntegrationTestFixture.h"
#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include <glm/glm.hpp>

namespace Phyxel {
namespace Testing {

class PhysicsIntegrationTest : public PhysicsTestFixture {};

TEST_F(PhysicsIntegrationTest, PhysicsWorldInitialized) {
    ASSERT_NE(physicsWorld, nullptr);
}

TEST_F(PhysicsIntegrationTest, VoxelWorldAvailable) {
    ASSERT_NE(physicsWorld->getVoxelWorld(), nullptr);
}

TEST_F(PhysicsIntegrationTest, GravityEnabled) {
    glm::vec3 gravity = physicsWorld->getGravity();
    EXPECT_LT(gravity.y, 0.0f) << "Gravity should pull downward";
}

TEST_F(PhysicsIntegrationTest, StepSimulation) {
    EXPECT_NO_THROW({
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });
}

TEST_F(PhysicsIntegrationTest, CreateVoxelBody) {
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    glm::vec3 position(0.0f, 10.0f, 0.0f);
    glm::vec3 halfExtents(0.5f, 0.5f, 0.5f);

    auto* body = voxelWorld->createVoxelBody(position, halfExtents, 1.0f);
    ASSERT_NE(body, nullptr);
}

TEST_F(PhysicsIntegrationTest, BodyFallsUnderGravity) {
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    glm::vec3 startPos(0.0f, 10.0f, 0.0f);
    auto* body = voxelWorld->createVoxelBody(startPos, glm::vec3(0.5f), 1.0f);
    ASSERT_NE(body, nullptr);

    float initialY = body->position.y;

    for (int i = 0; i < 60; ++i) {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    }

    EXPECT_LT(body->position.y, initialY) << "Body should fall due to gravity";
}

TEST_F(PhysicsIntegrationTest, ApplyImpulse) {
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    auto* body = voxelWorld->createVoxelBody(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.5f), 1.0f);
    ASSERT_NE(body, nullptr);

    body->applyCentralImpulse(glm::vec3(5.0f, 0.0f, 0.0f));
    physicsWorld->stepSimulation(1.0f / 60.0f);

    EXPECT_GT(body->linearVelocity.x, 0.0f) << "Impulse should set positive X velocity";
}

TEST_F(PhysicsIntegrationTest, BodyCountTracked) {
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    int before = voxelWorld->getBodyCount();

    voxelWorld->createVoxelBody(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.5f), 1.0f);
    voxelWorld->createVoxelBody(glm::vec3(2.0f, 5.0f, 0.0f), glm::vec3(0.5f), 1.0f);

    EXPECT_EQ(voxelWorld->getBodyCount(), before + 2);
}

} // namespace Testing
} // namespace Phyxel
