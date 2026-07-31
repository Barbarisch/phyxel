#include <gtest/gtest.h>

#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelOccupancyGrid.h"

#include <glm/glm.hpp>

// L2 red-before-green suite for docs/PhysicsRestOverhaul.md — the "voxels come to a FULL
// stop" contract. "Fully at rest" means: every body asleep with exactly-zero velocity within
// a time budget, no tunneling through sleeping bodies, no visible pre-sleep wobble, and a
// slept pile stays bit-identical forever. Written RED against the cold-started Baumgarte PGS
// solver; goes green with the Box3D-style soft-step port (warm starting + soft bias + relax
// pass + island sleep).

namespace Phyxel {
namespace Physics {
namespace {

constexpr float kDt = 1.0f / 60.0f;

// A 32x32 solid floor layer at cube y=0 (top surface at world y=1).
// Unit-cube bodies rest with COM at y = 1.5.
struct RestWorld {
    VoxelDynamicsWorld world;
    VoxelOccupancyGrid floor;

    RestWorld() {
        world.setThreadCount(1);            // deterministic
        world.setFallThreshold(-1000.0f);
        floor.setChunkOrigin(glm::ivec3(0));
        for (int x = 0; x < 32; ++x)
            for (int z = 0; z < 32; ++z)
                floor.setCube(glm::ivec3(x, 0, z), true);
        world.registerGrid(&floor);
    }

    void step(int frames) {
        for (int i = 0; i < frames; ++i)
            world.stepSimulation(kDt, 1, kDt);
    }

    VoxelRigidBody* unitBox(const glm::vec3& pos) {
        return world.createVoxelBody(pos, glm::vec3(0.5f), 1.0f);
    }
};

float speed(const VoxelRigidBody* b) {
    return glm::length(b->linearVelocity) + glm::length(b->angularVelocity);
}

} // namespace

// A single dropped box must reach a true zero-velocity sleep quickly, at the correct
// height. (Closest-to-green today — pins the baseline contract.)
TEST(RestingContact, SingleBoxSettlesToFullRest) {
    RestWorld rw;
    VoxelRigidBody* b = rw.unitBox(glm::vec3(16.0f, 3.0f, 16.0f));

    rw.step(static_cast<int>(4.0f / kDt));   // 4 s: fall + bounce + settle + sleep budget

    EXPECT_TRUE(b->isAsleep) << "box never reached sleep after 4 s";
    EXPECT_EQ(b->linearVelocity,  glm::vec3(0.0f)) << "asleep body must have exactly zero velocity";
    EXPECT_EQ(b->angularVelocity, glm::vec3(0.0f));
    EXPECT_NEAR(b->position.y, 1.5f, 0.02f) << "rest height must be floor top + half extent";
}

// While settling (post-touchdown, pre-sleep) the box must not visibly wobble.
// RED today: Baumgarte bias converts penetration into real velocity each substep
// ("breathing"), producing mm..cm scale oscillation until the position-fallback freezes it.
TEST(RestingContact, NoVisibleWobbleWhileSettling) {
    RestWorld rw;
    VoxelRigidBody* b = rw.unitBox(glm::vec3(16.0f, 2.0f, 16.0f));   // low drop: gentle touchdown

    rw.step(static_cast<int>(1.5f / kDt));   // touchdown + restitution fully done

    // Measure per-step COM motion over the next second (or until it sleeps — a sleeping
    // body trivially satisfies the bound, which is exactly the desired end state).
    float maxStepMove = 0.0f;
    for (int i = 0; i < 60; ++i) {
        glm::vec3 before = b->position;
        rw.step(1);
        maxStepMove = std::max(maxStepMove, glm::length(b->position - before));
    }
    EXPECT_LT(maxStepMove, 0.001f)
        << "settling box moved " << maxStepMove * 1000.0f << " mm in one step — visible jitter";
}

// A 5-box stack must fully rest: every body asleep (zero velocity), stack coherent.
// RED today: per-body sleep + cold-started solver — stacked bodies keep spiking each
// other's velocities; interfaces breathe.
TEST(RestingContact, StackFullyRests) {
    RestWorld rw;
    std::vector<VoxelRigidBody*> boxes;
    for (int i = 0; i < 5; ++i)
        boxes.push_back(rw.unitBox(glm::vec3(16.0f, 1.5f + 1.01f * i, 16.0f)));

    rw.step(static_cast<int>(5.0f / kDt));

    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(boxes[i]->isAsleep) << "stack box " << i << " never slept";
        EXPECT_EQ(speed(boxes[i]), 0.0f) << "stack box " << i << " has residual velocity";
        EXPECT_NEAR(boxes[i]->position.y, 1.5f + 1.0f * i, 0.06f)
            << "stack box " << i << " not at its layer height";
    }
    // No interpenetration beyond slop between consecutive boxes.
    for (int i = 1; i < 5; ++i) {
        float gap = boxes[i]->position.y - boxes[i - 1]->position.y;
        EXPECT_GT(gap, 0.97f) << "boxes " << i - 1 << "/" << i << " interpenetrate";
    }
}

// Sleeping bodies must still be collidable. RED today (real bug): sleeping bodies are
// excluded from contact generation entirely, so the dropped box falls CLEAN THROUGH the
// sleeper and ends up overlapping it on the floor.
TEST(RestingContact, FallingBodyLandsOnSleeperAndWakesIt) {
    RestWorld rw;
    VoxelRigidBody* sleeper = rw.unitBox(glm::vec3(16.0f, 1.5f, 16.0f));
    rw.step(static_cast<int>(3.0f / kDt));
    ASSERT_TRUE(sleeper->isAsleep) << "precondition: base box must be asleep before the drop";

    VoxelRigidBody* dropped = rw.unitBox(glm::vec3(16.0f, 5.0f, 16.0f));
    rw.step(static_cast<int>(5.0f / kDt));

    // Assert the end state, which tunneling makes unreachable: the dropped box must rest
    // ON TOP of the sleeper, one box height up.
    EXPECT_NEAR(dropped->position.y, 2.5f, 0.08f)
        << "dropped box did not come to rest on top of the sleeping box (tunneled?)";
    EXPECT_NEAR(sleeper->position.y, 1.5f, 0.05f) << "base box was displaced from its rest";
    EXPECT_GT(dropped->position.y - sleeper->position.y, 0.9f)
        << "boxes overlap — the dropped box fell through the sleeper";

    // And the pair must reach full rest again after the disturbance.
    EXPECT_TRUE(dropped->isAsleep);
    EXPECT_TRUE(sleeper->isAsleep);
}

// Stress phase (CLAUDE.md): a 21-box pyramid — count axis pushed up, invariant asserted on
// EVERY body. Must fully sleep, not drift apart, and the world must go idle (active 0).
TEST(RestingContact, PyramidFullyRestsAndWorldGoesIdle) {
    RestWorld rw;
    std::vector<VoxelRigidBody*> boxes;
    std::vector<glm::vec3> spawn;
    const float sp = 1.02f;   // slight spacing so rows drop into contact
    for (int row = 0; row < 6; ++row) {          // row 0 = bottom (6 boxes) .. row 5 = top (1)
        int count = 6 - row;
        float xStart = 16.0f - 0.5f * sp * (count - 1);
        for (int i = 0; i < count; ++i) {
            glm::vec3 p(xStart + sp * i, 1.5f + 1.0f * row + 0.02f * row, 16.0f);
            spawn.push_back(p);
            boxes.push_back(rw.unitBox(p));
        }
    }

    rw.step(static_cast<int>(8.0f / kDt));

    for (size_t i = 0; i < boxes.size(); ++i) {
        EXPECT_TRUE(boxes[i]->isAsleep) << "pyramid box " << i << " never slept";
        EXPECT_EQ(speed(boxes[i]), 0.0f) << "pyramid box " << i << " has residual velocity";
        glm::vec2 driftXZ(boxes[i]->position.x - spawn[i].x, boxes[i]->position.z - spawn[i].z);
        EXPECT_LT(glm::length(driftXZ), 0.3f) << "pyramid box " << i << " drifted";
        EXPECT_GT(boxes[i]->position.y, 1.4f) << "pyramid box " << i << " sank into the floor";
    }
    EXPECT_EQ(rw.world.getActiveCount(), 0u) << "world still burning solver work on a resting pile";
}

// Once asleep, a pile must stay bit-identical forever (sleep = frozen, not "slow").
TEST(RestingContact, SleepIsForever) {
    RestWorld rw;
    VoxelRigidBody* a = rw.unitBox(glm::vec3(16.0f, 1.5f, 16.0f));
    VoxelRigidBody* b = rw.unitBox(glm::vec3(16.0f, 2.52f, 16.0f));

    rw.step(static_cast<int>(6.0f / kDt));
    ASSERT_TRUE(a->isAsleep);
    ASSERT_TRUE(b->isAsleep);

    const glm::vec3 pa = a->position, pb = b->position;
    const glm::quat qa = a->orientation, qb = b->orientation;
    rw.step(static_cast<int>(10.0f / kDt));

    EXPECT_EQ(a->position, pa);
    EXPECT_EQ(b->position, pb);
    EXPECT_EQ(a->orientation, qa);
    EXPECT_EQ(b->orientation, qb);
}

} // namespace Physics
} // namespace Phyxel
