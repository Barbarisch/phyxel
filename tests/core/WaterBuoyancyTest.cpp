#include <gtest/gtest.h>

#include "physics/VoxelDynamicsWorld.h"

#include <glm/glm.hpp>

// L1/L2 validation for buoyancy + water drag on the CPU rigid-body world (small-scale water
// plan Phase 4.2). The water arrives through the injected submerged-fraction query, so these
// tests drive the physics in isolation with a synthetic flat water level — no WaterManager.

namespace Phyxel {
namespace Physics {
namespace {

// Flat-water AABB submersion: fraction of [mn.y, mx.y] below `level`.
std::function<float(const glm::vec3&, const glm::vec3&)> flatWater(float level) {
    return [level](const glm::vec3& mn, const glm::vec3& mx) {
        const float h = std::max(mx.y - mn.y, 1e-4f);
        return glm::clamp((level - mn.y) / h, 0.0f, 1.0f);
    };
}

VoxelRigidBody* unitBody(VoxelDynamicsWorld& w, const glm::vec3& pos, float buoyancy) {
    VoxelRigidBody* b = w.createVoxelBody(pos, glm::vec3(0.5f), 1.0f);
    b->buoyancy = buoyancy;
    return b;
}

TEST(WaterBuoyancy, DenseBodySinksThroughWater) {
    VoxelDynamicsWorld w;
    w.setThreadCount(1);
    w.setFallThreshold(-1000.0f);
    w.setWaterQuery(flatWater(50.0f));
    VoxelRigidBody* b = unitBody(w, glm::vec3(0.0f, 40.0f, 0.0f), 0.4f); // denser than water
    for (int i = 0; i < 120; ++i) w.stepSimulation(1.0f / 60.0f, 1);
    EXPECT_LT(b->position.y, 39.0f) << "a dense body must keep sinking";
    EXPECT_LT(b->linearVelocity.y, 0.0f);
}

TEST(WaterBuoyancy, BuoyantBodyRisesBreaksSurfaceAndComesToRest) {
    VoxelDynamicsWorld w;
    w.setThreadCount(1);
    w.setFallThreshold(-1000.0f);
    const float LEVEL = 50.0f;
    w.setWaterQuery(flatWater(LEVEL));
    VoxelRigidBody* b = unitBody(w, glm::vec3(0.0f, 44.0f, 0.0f), 2.0f); // floats, eq. ~50% under
    // Rise phase: it must climb toward the surface.
    for (int i = 0; i < 180; ++i) w.stepSimulation(1.0f / 60.0f, 1);
    EXPECT_GT(b->position.y, 47.0f) << "floater failed to rise from depth";
    // Settle phase: drag must kill the bob — the body ends still at its equilibrium line
    // (submerged fraction ≈ 1/buoyancy = 0.5 → centre at the water level for a unit cube).
    for (int i = 0; i < 600; ++i) w.stepSimulation(1.0f / 60.0f, 1);
    EXPECT_NEAR(b->position.y, LEVEL, 0.25f)
        << "floater should ride with ~half its height submerged";
    EXPECT_LT(std::abs(b->linearVelocity.y), 0.05f) << "bobbing never decayed to rest";
}

// ─── Current forces (tangible-water Phase E) ─────────────────────────────────────────────────────
// Written RED: setWaterFlowQuery doesn't exist yet. Moving water carries what floats in it — a
// submerged body must drift with the current and converge to a bounded terminal speed ≈ the
// current's own, and a null flow query must change nothing.

TEST(WaterBuoyancy, FloaterDriftsWithTheCurrentToBoundedTerminalSpeed) {
    VoxelDynamicsWorld w;
    w.setThreadCount(1);
    w.setFallThreshold(-1000.0f);
    w.setWaterQuery(flatWater(50.0f));
    w.setWaterFlowQuery([](const glm::vec3&) { return glm::vec3(1.6f, 0.0f, 0.0f); });
    VoxelRigidBody* b = unitBody(w, glm::vec3(0.0f, 49.5f, 0.0f), 2.0f);  // floating at the line
    const float x0 = b->position.x;
    for (int i = 0; i < 240; ++i) w.stepSimulation(1.0f / 60.0f, 1);
    EXPECT_GT(b->position.x, x0 + 2.0f) << "floater did not drift with the current";
    EXPECT_GT(b->linearVelocity.x, 0.8f) << "drift never approached the current's speed";
    EXPECT_LT(b->linearVelocity.x, 2.0f) << "drift overshot the current — coupling is a thruster";
    EXPECT_NEAR(b->linearVelocity.z, 0.0f, 0.05f) << "current is +x only";
}

TEST(WaterBuoyancy, NullFlowQueryChangesNothing) {
    VoxelDynamicsWorld a, b;
    for (VoxelDynamicsWorld* w : {&a, &b}) {
        w->setThreadCount(1);
        w->setFallThreshold(-1000.0f);
        w->setWaterQuery(flatWater(50.0f));
    }
    b.setWaterFlowQuery([](const glm::vec3&) { return glm::vec3(0.0f); });  // bound but still
    VoxelRigidBody* ba = unitBody(a, glm::vec3(0.0f, 44.0f, 0.0f), 2.0f);
    VoxelRigidBody* bb = unitBody(b, glm::vec3(0.0f, 44.0f, 0.0f), 2.0f);
    for (int i = 0; i < 240; ++i) {
        a.stepSimulation(1.0f / 60.0f, 1);
        b.stepSimulation(1.0f / 60.0f, 1);
    }
    EXPECT_FLOAT_EQ(ba->position.x, bb->position.x);
    EXPECT_FLOAT_EQ(ba->position.y, bb->position.y);
    EXPECT_FLOAT_EQ(ba->position.z, bb->position.z);
}

TEST(WaterBuoyancy, DryBehaviorIsBitIdenticalWithAndWithoutQuery) {
    // The null-query world and a query-that-says-dry world must produce IDENTICAL motion —
    // the coupling may cost nothing and change nothing on dry land.
    VoxelDynamicsWorld a, b;
    a.setThreadCount(1); b.setThreadCount(1);
    a.setFallThreshold(-1000.0f); b.setFallThreshold(-1000.0f);
    b.setWaterQuery([](const glm::vec3&, const glm::vec3&) { return 0.0f; });
    VoxelRigidBody* ba = unitBody(a, glm::vec3(0.0f, 40.0f, 0.0f), 1.6f);
    VoxelRigidBody* bb = unitBody(b, glm::vec3(0.0f, 40.0f, 0.0f), 1.6f);
    ba->linearVelocity = bb->linearVelocity = glm::vec3(1.0f, 2.0f, -0.5f);
    for (int i = 0; i < 240; ++i) {
        a.stepSimulation(1.0f / 60.0f, 1);
        b.stepSimulation(1.0f / 60.0f, 1);
        ASSERT_EQ(ba->position.x, bb->position.x) << "dry trajectories diverged at step " << i;
        ASSERT_EQ(ba->position.y, bb->position.y) << "dry trajectories diverged at step " << i;
        ASSERT_EQ(ba->position.z, bb->position.z) << "dry trajectories diverged at step " << i;
    }
}

TEST(WaterBuoyancy, WetBodyNeverSleepsAndSleptBodyWakesInRisingWater) {
    // Zero gravity isolates the sleep machinery from free-fall (nothing else keeps a test body
    // "slow" long enough to sleep in open space).
    VoxelDynamicsWorld w;
    w.setThreadCount(1);
    w.setGravity(glm::vec3(0.0f));
    w.setFallThreshold(-1000.0f);
    float level = -100.0f;   // dry world to start
    w.setWaterQuery([&level](const glm::vec3& mn, const glm::vec3& mx) {
        const float h = std::max(mx.y - mn.y, 1e-4f);
        return glm::clamp((level - mn.y) / h, 0.0f, 1.0f);
    });
    VoxelRigidBody* b = unitBody(w, glm::vec3(0.0f, 40.0f, 0.0f), 2.0f);

    // Dry + still → sleeps within the sleep window.
    for (int i = 0; i < 120; ++i) w.stepSimulation(1.0f / 60.0f, 1);
    ASSERT_TRUE(b->isAsleep) << "precondition: a still dry body must sleep";

    // Water rises past the body: the staggered ~1 Hz re-check must wake it.
    level = 45.0f;
    int steps = 0;
    while (b->isAsleep && steps++ < 180) w.stepSimulation(1.0f / 60.0f, 1);
    EXPECT_FALSE(b->isAsleep) << "rising water never woke the slept body";

    // And while wet it must NOT go back to sleep, however still it is.
    for (int i = 0; i < 300; ++i) w.stepSimulation(1.0f / 60.0f, 1);
    EXPECT_FALSE(b->isAsleep) << "a wet body slept — it would hover if the water drained";
}

}  // namespace
}  // namespace Physics
}  // namespace Phyxel
