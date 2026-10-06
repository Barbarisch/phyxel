// DebrisBodyMoverFeedTest - DebrisInteractionPlan Phase 3c: CPU rigid bodies feed GPU debris as
// movers. Contract (DebrisMoverFeed::appendRigidBodies):
//   * every compound box, at its WORLD centre, with the body's orientation and local half extents;
//   * velocity is the point velocity at the box centre (v + w x r), not the body's linear velocity;
//   * sleeping bodies are fed (debris rests on them) with zero velocity;
//   * nearest the eye first; a body that does not fit the box budget is skipped WHOLE and counted.

#include <gtest/gtest.h>

#include <glm/gtc/quaternion.hpp>

#include "core/DebrisMoverFeed.h"
#include "physics/VoxelDynamicsWorld.h"
#include "physics/VoxelRigidBody.h"

using namespace Phyxel;
using Phyxel::Physics::LocalBox;
using Phyxel::Physics::VoxelDynamicsWorld;

namespace {
std::vector<LocalBox> twoBoxes() {
    LocalBox a{}; a.offset = glm::vec3(-0.5f, 0, 0); a.halfExtents = glm::vec3(0.5f, 0.25f, 0.25f); a.mass = 1.0f;
    LocalBox b{}; b.offset = glm::vec3( 0.5f, 0, 0); b.halfExtents = glm::vec3(0.5f, 0.25f, 0.25f); b.mass = 1.0f;
    return {a, b};
}
}  // namespace

TEST(DebrisBodyMoverFeed, CompoundBoxesAreFedAtTheirWorldPoseWithPointVelocity) {
    VoxelDynamicsWorld w;
    const glm::quat yaw90 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
    auto* body = w.createBody(twoBoxes(), glm::vec3(10, 5, 10), yaw90);
    ASSERT_NE(body, nullptr);
    body->linearVelocity  = glm::vec3(3, 0, 0);
    body->angularVelocity = glm::vec3(0, 2, 0);   // spinning about +Y

    std::vector<GpuParticlePhysics::MoverBox> out;
    const auto r = DebrisMoverFeed::appendRigidBodies(w, glm::vec3(0), 512, out);
    ASSERT_EQ(r.bodiesFed, 1u);
    ASSERT_EQ(out.size(), 2u);
    for (const auto& m : out) {
        const glm::vec3 arm = m.center - body->position;
        EXPECT_NEAR(glm::length(arm), 0.5f, 1e-4f) << "box centre = position + R * offset";
        EXPECT_NEAR(std::abs(arm.z), 0.5f, 1e-4f) << "yaw 90 maps local +-X onto world -+Z";
        EXPECT_NEAR(m.halfExtents.x, 0.5f, 1e-6f) << "LOCAL half extents; the rotation carries the pose";
        EXPECT_NEAR(std::abs(glm::dot(m.rotation, yaw90)), 1.0f, 1e-4f);
        const glm::vec3 expect = body->linearVelocity + glm::cross(body->angularVelocity, arm);
        EXPECT_LT(glm::length(m.velocity - expect), 1e-4f) << "point velocity v + w x r";
    }
    EXPECT_GT(glm::length(out[0].velocity - out[1].velocity), 1.0f)
        << "the two ends of a spinning body move differently";
}

TEST(DebrisBodyMoverFeed, SleepingBodiesAreSupportsWithZeroVelocity) {
    VoxelDynamicsWorld w;
    auto* body = w.createVoxelBody(glm::vec3(0, 1, 0), glm::vec3(0.5f), 10.0f);
    body->linearVelocity = glm::vec3(0.01f, 0, 0);   // residual drift under the sleep threshold
    body->isAsleep = true;
    std::vector<GpuParticlePhysics::MoverBox> out;
    const auto r = DebrisMoverFeed::appendRigidBodies(w, glm::vec3(0), 512, out);
    ASSERT_EQ(r.bodiesFed, 1u);
    EXPECT_EQ(out[0].velocity, glm::vec3(0.0f)) << "a sleeper is a support, it does not shove";
}

TEST(DebrisBodyMoverFeed, NearestFirstAndABodyPastTheBudgetIsSkippedWhole) {
    VoxelDynamicsWorld w;
    w.createBody(twoBoxes(), glm::vec3(50, 0, 0));          // far, 2 boxes
    w.createVoxelBody(glm::vec3(5, 0, 0), glm::vec3(0.5f), 1.0f);   // near, 1 box
    w.createBody(twoBoxes(), glm::vec3(20, 0, 0));          // middle, 2 boxes

    std::vector<GpuParticlePhysics::MoverBox> out;
    const auto r = DebrisMoverFeed::appendRigidBodies(w, glm::vec3(0), 4, out);
    // near (1) + middle (2) = 3 boxes; far (2) does not fit the remaining 1 -> skipped whole.
    EXPECT_EQ(r.bodiesFed, 2u);
    EXPECT_EQ(r.boxesFed, 3u);
    EXPECT_EQ(r.bodiesSkipped, 1u);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_NEAR(out[0].center.x, 5.0f, 1e-4f) << "nearest body first";
    for (const auto& m : out) EXPECT_LT(m.center.x, 30.0f) << "no box of the skipped body leaks in";
}

TEST(DebrisBodyMoverFeed, DeadBodiesAreNotFed) {
    VoxelDynamicsWorld w;
    auto* body = w.createVoxelBody(glm::vec3(0, 1, 0), glm::vec3(0.5f), 1.0f);
    body->isDead = true;
    std::vector<GpuParticlePhysics::MoverBox> out;
    const auto r = DebrisMoverFeed::appendRigidBodies(w, glm::vec3(0), 512, out);
    EXPECT_EQ(r.bodiesFed, 0u);
    EXPECT_TRUE(out.empty());
}
