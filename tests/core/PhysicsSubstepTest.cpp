// Substep-cap contract — the fix for the FPS→substeps positive feedback loop
// (2026-08-07 item-physics perf plan).
//
// PhysicsWorld::stepSimulation historically IGNORED its maxSubsteps/fixedStep
// arguments and forwarded only deltaTime, so VoxelDynamicsWorld's own default
// cap (3) applied per CALL — and the Application fixed-step loop makes one
// call per accumulated 1/60 s, so a slow frame ran an unbounded number of
// substeps (60fps→1, 15fps→4, 4fps→15/frame): physics cost grew as the frame
// slowed. Contract: the wrapper forwards the cap, and the world reports how
// many substeps a step actually ran (frame-accumulated observability).

#include <gtest/gtest.h>

#include "physics/PhysicsWorld.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"

#include <cfloat>
#include <glm/glm.hpp>

using namespace Phyxel;

TEST(PhysicsSubstep, PhysicsWorldForwardsSubstepCap) {
    Physics::PhysicsWorld pw;
    ASSERT_TRUE(pw.initialize());
    auto* body = pw.getVoxelWorld()->createVoxelBody(
        glm::vec3(0.0f, 50.0f, 0.0f), glm::vec3(0.5f), 1.0f, 0.2f, 0.6f);
    ASSERT_NE(body, nullptr);
    body->lifetime = FLT_MAX;

    // One second of accumulated time, but a cap of ONE substep: gravity may
    // integrate at most one 1/60 tick -> |v| ~ 9.81/60 ~ 0.16. If the cap is
    // ignored (the historical bug), the world default (3) applies -> ~0.49.
    pw.stepSimulation(1.0f, /*maxSubSteps=*/1, /*fixedTimeStep=*/1.0f / 60.0f);
    EXPECT_LT(glm::length(body->linearVelocity), 0.25f)
        << "maxSubsteps argument was not forwarded/honored";
}

TEST(PhysicsSubstep, WorldReportsSubstepsAndFrameTime) {
    Physics::VoxelDynamicsWorld world;
    auto* body = world.createVoxelBody(glm::vec3(0.0f, 50.0f, 0.0f),
                                       glm::vec3(0.5f), 1.0f, 0.2f, 0.6f);
    ASSERT_NE(body, nullptr);
    body->lifetime = FLT_MAX;

    // 6 substeps' worth of time, cap 3: exactly 3 run (float residue makes an
    // exact-multiple deltaTime land one short, so overshoot the cap instead).
    world.stepSimulation(6.0f / 60.0f, 3, 1.0f / 60.0f);
    const auto& stats = world.lastBroadphaseStats();
    EXPECT_EQ(stats.substeps, 3) << "substep count must be observable and capped";
    EXPECT_GT(stats.stepTotalMs, 0.0) << "frame-accumulated step time missing";
}
