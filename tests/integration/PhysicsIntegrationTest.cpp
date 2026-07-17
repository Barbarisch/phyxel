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

TEST_F(PhysicsIntegrationTest, OverlapsAnyBody_TestsBoxes_NotTheWholeBodyAABB) {
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // Live user case (2026-07-16): the character movement solve blocks on
    // overlapsAnyBody. Testing the WHOLE-BODY AABB is fine for a chair, but a
    // fallen tree's AABB is a multi-meter invisible envelope around trunk +
    // branches — the player was stopped ~2m from the visible wood in open air.
    // The query must test the body's actual collision BOXES.
    // Dumbbell: two 0.5-half-extent boxes 8m apart -> the whole-body AABB spans
    // x in [-4.5, 4.5], but x in (-3.5, 3.5) is pure air.
    std::vector<Physics::LocalBox> boxes;
    boxes.push_back({glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(0.5f), 1.0f});
    boxes.push_back({glm::vec3( 4.0f, 0.0f, 0.0f), glm::vec3(0.5f), 1.0f});
    auto* body = voxelWorld->createBody(boxes, glm::vec3(0.0f, 50.0f, 0.0f));
    ASSERT_NE(body, nullptr);

    const glm::vec3 charHE(0.25f, 0.9f, 0.25f);   // kinematic character capsule box
    // A character standing in the empty middle of the dumbbell must NOT collide.
    EXPECT_FALSE(voxelWorld->overlapsAnyBody(glm::vec3(0.0f, 50.0f, 0.0f), charHE))
        << "blocked by the whole-body AABB in open air between the boxes";
    // Positive controls: at each actual box it MUST collide...
    EXPECT_TRUE(voxelWorld->overlapsAnyBody(glm::vec3(-4.0f, 50.0f, 0.0f), charHE));
    EXPECT_TRUE(voxelWorld->overlapsAnyBody(glm::vec3( 4.0f, 50.0f, 0.0f), charHE));
    // ...and fully outside it must not.
    EXPECT_FALSE(voxelWorld->overlapsAnyBody(glm::vec3(0.0f, 60.0f, 0.0f), charHE));

    // groundHeight, same class: feet above the dumbbell's empty middle must NOT
    // find the whole-body AABB roof as ground; above a real box they must.
    EXPECT_LT(voxelWorld->groundHeight(glm::vec3(0.0f, 52.0f, 0.0f), 0.25f, 5.0f), -1e8f)
        << "standing on the invisible AABB roof between the boxes";
    EXPECT_NEAR(voxelWorld->groundHeight(glm::vec3(-4.0f, 52.0f, 0.0f), 0.25f, 5.0f),
                50.5f, 1e-3f) << "cannot stand on a real box top";
}

} // namespace Testing
} // namespace Phyxel
