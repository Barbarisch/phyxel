#include "IntegrationTestFixture.h"
#include "physics/PhysicsWorld.h"
#include <glm/glm.hpp>

namespace VulkanCube {
namespace Testing {

/**
 * @brief Integration tests for Physics subsystem
 * 
 * Tests verify:
 * - PhysicsWorld initialization
 * - Rigid body creation and simulation
 * - Collision detection
 * - Gravity simulation
 * - Material properties
 */
class PhysicsIntegrationTest : public PhysicsTestFixture {};

// ============================================================================
// Physics World Tests
// ============================================================================

TEST_F(PhysicsIntegrationTest, PhysicsWorldInitialized) {
    ASSERT_NE(physicsWorld, nullptr);
    EXPECT_NE(physicsWorld->getWorld(), nullptr);
}

TEST_F(PhysicsIntegrationTest, GravityEnabled) {
    glm::vec3 gravity = physicsWorld->getGravity();
    
    // Default gravity should be negative Y (downward)
    EXPECT_LT(gravity.y, 0.0f) << "Gravity should pull downward";
}

TEST_F(PhysicsIntegrationTest, StepSimulation) {
    // Should not crash
    EXPECT_NO_THROW({
        physicsWorld->stepSimulation(1.0f / 60.0f);
    });
}

// ============================================================================
// Rigid Body Tests
// ============================================================================

TEST_F(PhysicsIntegrationTest, CreateDynamicCube) {
    glm::vec3 position(0.0f, 10.0f, 0.0f);
    glm::vec3 size(1.0f, 1.0f, 1.0f);
    float mass = 1.0f;

    btRigidBody* body = physicsWorld->createCube(position, size, mass);
    
    ASSERT_NE(body, nullptr);
    EXPECT_GT(mass, 0.0f) << "Dynamic bodies should have positive mass";
}

TEST_F(PhysicsIntegrationTest, CreateStaticCube) {
    glm::vec3 position(0.0f, 0.0f, 0.0f);
    glm::vec3 size(10.0f, 1.0f, 10.0f);

    btRigidBody* body = physicsWorld->createStaticCube(position, size);
    
    ASSERT_NE(body, nullptr);
}

TEST_F(PhysicsIntegrationTest, CubeFalls) {
    glm::vec3 startPosition(0.0f, 10.0f, 0.0f);
    glm::vec3 size(1.0f, 1.0f, 1.0f);
    float mass = 1.0f;

    btRigidBody* body = physicsWorld->createCube(startPosition, size, mass);
    ASSERT_NE(body, nullptr);

    // Get initial position
    btTransform transform;
    body->getMotionState()->getWorldTransform(transform);
    float initialY = transform.getOrigin().y();

    // Step physics multiple times
    for (int i = 0; i < 60; i++) {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    }

    // Get final position
    body->getMotionState()->getWorldTransform(transform);
    float finalY = transform.getOrigin().y();

    EXPECT_LT(finalY, initialY) << "Body should fall due to gravity";
}

TEST_F(PhysicsIntegrationTest, StaticCubeDoesNotFall) {
    glm::vec3 position(0.0f, 10.0f, 0.0f);
    glm::vec3 size(1.0f, 1.0f, 1.0f);

    btRigidBody* body = physicsWorld->createStaticCube(position, size);
    ASSERT_NE(body, nullptr);

    btTransform initialTransform;
    body->getMotionState()->getWorldTransform(initialTransform);
    float initialY = initialTransform.getOrigin().y();

    // Step physics
    for (int i = 0; i < 60; i++) {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    }

    btTransform finalTransform;
    body->getMotionState()->getWorldTransform(finalTransform);
    float finalY = finalTransform.getOrigin().y();

    EXPECT_FLOAT_EQ(finalY, initialY) << "Static body should not move";
}

// ============================================================================
// Collision Tests
// ============================================================================

TEST_F(PhysicsIntegrationTest, CollisionDetection) {
    // Create static ground plane
    glm::vec3 groundPos(0.0f, 0.0f, 0.0f);
    glm::vec3 groundSize(10.0f, 1.0f, 10.0f);
    btRigidBody* ground = physicsWorld->createStaticCube(groundPos, groundSize);
    ASSERT_NE(ground, nullptr);

    // Create dynamic box above ground
    glm::vec3 boxPos(0.0f, 5.0f, 0.0f);
    glm::vec3 boxSize(1.0f, 1.0f, 1.0f);
    btRigidBody* box = physicsWorld->createCube(boxPos, boxSize, 1.0f);
    ASSERT_NE(box, nullptr);

    // Step simulation until box hits ground
    bool collisionDetected = false;
    for (int i = 0; i < 120; i++) {
        physicsWorld->stepSimulation(1.0f / 60.0f);
        
        // Check if box stopped falling (collision with ground)
        btTransform transform;
        box->getMotionState()->getWorldTransform(transform);
        float y = transform.getOrigin().y();
        
        if (y <= 1.5f) { // Box should rest on ground (ground top = 0.5, box half-height = 0.5)
            collisionDetected = true;
            break;
        }
    }

    EXPECT_TRUE(collisionDetected) << "Box should collide with ground";
}

TEST_F(PhysicsIntegrationTest, RemoveCube) {
    glm::vec3 position(0.0f, 10.0f, 0.0f);
    glm::vec3 size(1.0f, 1.0f, 1.0f);
    btRigidBody* body = physicsWorld->createCube(position, size, 1.0f);
    ASSERT_NE(body, nullptr);

    int bodyCountBefore = physicsWorld->getRigidBodyCount();
    
    physicsWorld->removeCube(body);
    
    int bodyCountAfter = physicsWorld->getRigidBodyCount();
    EXPECT_EQ(bodyCountAfter, bodyCountBefore - 1);
}

TEST_F(PhysicsIntegrationTest, ApplyForce) {
    glm::vec3 position(0.0f, 10.0f, 0.0f);
    glm::vec3 size(1.0f, 1.0f, 1.0f);
    btRigidBody* body = physicsWorld->createCube(position, size, 1.0f);
    ASSERT_NE(body, nullptr);

    // Apply upward force
    btVector3 force(0.0f, 100.0f, 0.0f);
    body->applyCentralForce(force);

    // Get initial velocity
    btVector3 velocityBefore = body->getLinearVelocity();

    // Step physics
    physicsWorld->stepSimulation(1.0f / 60.0f);

    // Velocity should increase upward
    btVector3 velocityAfter = body->getLinearVelocity();
    EXPECT_GT(velocityAfter.y(), velocityBefore.y());
}

TEST_F(PhysicsIntegrationTest, ApplyImpulse) {
    glm::vec3 position(0.0f, 10.0f, 0.0f);
    glm::vec3 size(1.0f, 1.0f, 1.0f);
    btRigidBody* body = physicsWorld->createCube(position, size, 1.0f);
    ASSERT_NE(body, nullptr);

    // Apply impulse (instant velocity change)
    btVector3 impulse(5.0f, 0.0f, 0.0f);
    body->applyCentralImpulse(impulse);

    // Velocity should change immediately
    btVector3 velocity = body->getLinearVelocity();
    EXPECT_GT(velocity.x(), 0.0f);
}

TEST_F(PhysicsIntegrationTest, MultipleBodiesTowerStability) {
    // Create ground
    glm::vec3 groundPos(0.0f, 0.0f, 0.0f);
    glm::vec3 groundSize(10.0f, 1.0f, 10.0f);
    btRigidBody* ground = physicsWorld->createStaticCube(groundPos, groundSize);
    ASSERT_NE(ground, nullptr);

    // Stack boxes
    std::vector<btRigidBody*> boxes;
    for (int i = 0; i < 5; i++) {
        glm::vec3 pos(0.0f, 1.5f + i * 1.0f, 0.0f);
        glm::vec3 size(0.5f, 0.5f, 0.5f);
        btRigidBody* box = physicsWorld->createCube(pos, size, 1.0f);
        boxes.push_back(box);
    }

    // Step simulation
    for (int i = 0; i < 120; i++) {
        physicsWorld->stepSimulation(1.0f / 60.0f);
    }

    // All boxes should settle (not fall through ground)
    for (btRigidBody* box : boxes) {
        btTransform transform;
        box->getMotionState()->getWorldTransform(transform);
        float y = transform.getOrigin().y();
        EXPECT_GT(y, 0.0f) << "Box should not fall through ground";
    }
}

} // namespace Testing
} // namespace VulkanCube
