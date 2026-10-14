// ImpulseLawTest - DebrisInteractionPlan Phase 4: one impulse law for both debris worlds.
// phxImpulseWeight (shaders/solver_shared.h) is the falloff the GPU sync_in pass and the CPU
// VoxelDynamicsWorld::applyImpulse both use: dv = J * w(d) / m along the radial direction from the
// centre (blended toward +Y by the up bias), |dv| <= IMPULSE_MAX_DV, every body reached is woken.

#include <gtest/gtest.h>

#include <cmath>

#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"
#include "solver_shared.h"

using namespace Phyxel;
using Phyxel::Physics::VoxelDynamicsWorld;
namespace DS = Phyxel::DebrisShared;

TEST(ImpulseLaw, LinearFalloffFromCentreToRadius) {
    EXPECT_FLOAT_EQ(DS::phxImpulseWeight(0.0f, 4.0f), 1.0f);
    EXPECT_FLOAT_EQ(DS::phxImpulseWeight(2.0f, 4.0f), 0.5f);
    EXPECT_FLOAT_EQ(DS::phxImpulseWeight(4.0f, 4.0f), 0.0f);
    EXPECT_FLOAT_EQ(DS::phxImpulseWeight(9.0f, 4.0f), 0.0f);
    EXPECT_FLOAT_EQ(DS::phxImpulseWeight(1.0f, 0.0f), 0.0f) << "a zero radius pushes nothing";
}

TEST(ImpulseLaw, CpuBodiesGetJTimesWeightOverMassAlongTheRadial) {
    VoxelDynamicsWorld w;
    auto* near = w.createVoxelBody(glm::vec3(2, 0, 0), glm::vec3(0.5f), 4.0f);   // d = 2 of r = 4
    auto* far  = w.createVoxelBody(glm::vec3(0, 0, 6), glm::vec3(0.5f), 4.0f);   // outside
    near->isAsleep = true;
    const int n = w.applyImpulse(glm::vec3(0), 4.0f, 20.0f, 0.0f, glm::vec3(0), DS::IMPULSE_RADIAL);
    EXPECT_EQ(n, 1);
    // 20 N*s * 0.5 / 4 kg = 2.5 m/s along +x
    EXPECT_NEAR(near->linearVelocity.x, 2.5f, 1e-4f);
    EXPECT_NEAR(near->linearVelocity.y, 0.0f, 1e-4f);
    EXPECT_FALSE(near->isAsleep) << "an impulse wakes the body it reaches";
    EXPECT_EQ(far->linearVelocity, glm::vec3(0.0f)) << "beyond the radius: untouched";
}

TEST(ImpulseLaw, UpBiasTiltsThePushAndTheClampHoldsAtTheCentre) {
    VoxelDynamicsWorld w;
    auto* b = w.createVoxelBody(glm::vec3(1, 0, 0), glm::vec3(0.5f), 1.0f);
    w.applyImpulse(glm::vec3(0), 4.0f, 2.0f, 1.0f, glm::vec3(0), DS::IMPULSE_RADIAL);
    EXPECT_NEAR(b->linearVelocity.x, 0.0f, 1e-4f) << "up bias 1 = straight up";
    EXPECT_NEAR(b->linearVelocity.y, 2.0f * 0.75f, 1e-4f);

    auto* light = w.createVoxelBody(glm::vec3(0, 0, 0.5f), glm::vec3(0.1f), 0.01f);
    w.applyImpulse(glm::vec3(0), 4.0f, 1000.0f, 0.0f, glm::vec3(0), DS::IMPULSE_RADIAL);
    EXPECT_LE(glm::length(light->linearVelocity), DS::IMPULSE_MAX_DV + 1e-3f) << "no tunnelling speeds";
}

TEST(ImpulseLaw, ACuttingConeSkipsBodiesOutsideItsHalfAngle) {
    VoxelDynamicsWorld w;
    auto* ahead  = w.createVoxelBody(glm::vec3(0, 0, -2), glm::vec3(0.5f), 1.0f);
    auto* behind = w.createVoxelBody(glm::vec3(0, 0,  2), glm::vec3(0.5f), 1.0f);
    const int n = w.applyImpulse(glm::vec3(0), 4.0f, 2.0f, 0.0f, glm::vec3(0, 0, -1),
                                 std::cos(glm::radians(30.0f)));
    EXPECT_EQ(n, 1);
    EXPECT_LT(ahead->linearVelocity.z, -0.1f);
    EXPECT_EQ(behind->linearVelocity, glm::vec3(0.0f));
}
