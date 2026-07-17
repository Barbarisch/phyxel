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

TEST_F(PhysicsIntegrationTest, RotatedBody_CollidesAsItsOrientedBoxes_NotConservativeAABBs) {
    auto* voxelWorld = physicsWorld->getVoxelWorld();
    ASSERT_NE(voxelWorld, nullptr);

    // Live user case #2 (2026-07-16, measured via /api/debug/body_boxes): a
    // fallen tree's greedy-merged slabs, once the body ROTATES, inflate their
    // conservative per-box AABBs into multi-meter phantom platforms (a 6x3m
    // invisible floor ~4m up; characters levitated on it). Character queries
    // must respect the box ORIENTATION.
    // One long slab (8m x 1m x 1m) rotated 45 deg about Z: its conservative
    // AABB spans ~+-3.2m vertically, but the REAL box at x=+2.5 sits BELOW
    // y=100 (the box tilts down-right through that region... probe points
    // chosen on the empty side of the diagonal).
    std::vector<Physics::LocalBox> boxes;
    boxes.push_back({glm::vec3(0.0f), glm::vec3(4.0f, 0.5f, 0.5f), 10.0f});
    const glm::quat tilt = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 0, 1));
    auto* body = voxelWorld->createBody(boxes, glm::vec3(0.0f, 100.0f, 0.0f), tilt);
    ASSERT_NE(body, nullptr);

    const glm::vec3 charHE(0.25f, 0.9f, 0.25f);
    // The slab's long axis runs along (1,1,0)/sqrt2. Point (2.5, 100 - 2.5, 0)
    // lies ON the slab (diagonal, descending to +x? no: +45deg about Z lifts +x
    // toward +y) — ON-slab point is (2.5, 102.5, 0); the OPPOSITE corner region
    // (2.5, 97.5, 0) is inside the conservative AABB but far OUTSIDE the box.
    EXPECT_TRUE(voxelWorld->overlapsAnyBody(glm::vec3(2.5f, 102.5f, 0.0f), charHE))
        << "on-slab probe missed the real oriented box";
    EXPECT_FALSE(voxelWorld->overlapsAnyBody(glm::vec3(2.5f, 97.5f, 0.0f), charHE))
        << "blocked by the conservative AABB of a rotated slab (phantom platform)";

    // Grounding: at x=-2.5 the tilted slab's surface is at y≈98.2, far below a
    // [102,105) search band — but the rotated slab's conservative AABB roof
    // (y≈103.2) IS in that band. Phantom ground there = the levitation bug.
    EXPECT_LT(voxelWorld->groundHeight(glm::vec3(-2.5f, 105.0f, 0.0f), 0.25f, 3.0f), -1e8f)
        << "standing on the conservative-AABB roof of a rotated slab (levitation)";
    // Control: directly above the slab surface the real ground IS found (~103.2
    // at the slab's high end x≈+2.8).
    EXPECT_GT(voxelWorld->groundHeight(glm::vec3(2.5f, 105.0f, 0.0f), 0.25f, 4.0f), 102.0f)
        << "real oriented slab top not found by the down-ray grounding";
}

} // namespace Testing
} // namespace Phyxel
