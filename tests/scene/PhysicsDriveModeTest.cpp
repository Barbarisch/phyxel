#include <gtest/gtest.h>
#include "scene/PhysicsDriveMode.h"
#include "scene/RagdollCharacter.h"
#include "scene/CharacterSkeleton.h"
#include "physics/PhysicsWorld.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

using namespace Phyxel;
using namespace Phyxel::Scene;

// ============================================================================
// Helper: build a minimal 3-bone skeleton (root → spine → head)
// ============================================================================

static CharacterSkeleton makeMinimalSkeleton() {
    CharacterSkeleton cs;
    // Root bone (Hips)
    cs.skeleton.addBone("Hips", -1,
                        glm::vec3(0, 0, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));
    // Spine
    cs.skeleton.addBone("Spine", 0,
                        glm::vec3(0, 0.3f, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));
    // Head
    cs.skeleton.addBone("Head", 1,
                        glm::vec3(0, 0.3f, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));

    cs.computeBoneSizes();
    cs.generateJointDefs();
    return cs;
}

// 5-bone skeleton: Hips → Spine, LeftLeg, RightLeg → Head
static CharacterSkeleton makeHumanoidSkeleton() {
    CharacterSkeleton cs;
    cs.skeleton.addBone("Hips", -1,
                        glm::vec3(0, 0, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));
    cs.skeleton.addBone("Spine", 0,
                        glm::vec3(0, 0.4f, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));
    cs.skeleton.addBone("Head", 1,
                        glm::vec3(0, 0.3f, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));
    cs.skeleton.addBone("LeftLeg", 0,
                        glm::vec3(-0.1f, -0.4f, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));
    cs.skeleton.addBone("RightLeg", 0,
                        glm::vec3(0.1f, -0.4f, 0),
                        glm::quat(1, 0, 0, 0),
                        glm::vec3(1));

    cs.computeBoneSizes();
    cs.generateJointDefs();
    return cs;
}

// ============================================================================
// PhysicsDriveConfig tests
// ============================================================================

TEST(PhysicsDriveConfigTest, DefaultValues) {
    PhysicsDriveConfig config;
    EXPECT_FLOAT_EQ(config.motorStrengthScale, 1.0f);
    EXPECT_FLOAT_EQ(config.balanceKp, 150.0f);
    EXPECT_FLOAT_EQ(config.balanceKi, 5.0f);
    EXPECT_FLOAT_EQ(config.balanceKd, 20.0f);
    EXPECT_FLOAT_EQ(config.moveForce, 50.0f);
    EXPECT_FLOAT_EQ(config.jumpImpulse, 100.0f);
    EXPECT_GT(config.fallAngleThreshold, 0.0f);
}

TEST(JointPIDStateTest, ResetClearsState) {
    JointPIDState state;
    state.integralError = 5.0f;
    state.integralError3D = glm::vec3(1, 2, 3);
    state.previousError = 3.0f;
    state.reset();
    EXPECT_FLOAT_EQ(state.integralError, 0.0f);
    EXPECT_FLOAT_EQ(state.previousError, 0.0f);
    EXPECT_FLOAT_EQ(state.integralError3D.x, 0.0f);
    EXPECT_FLOAT_EQ(state.integralError3D.y, 0.0f);
    EXPECT_FLOAT_EQ(state.integralError3D.z, 0.0f);
}

TEST(JointPIDGainsTest, DefaultValues) {
    JointPIDGains gains;
    EXPECT_GT(gains.kp, 0.0f);
    EXPECT_GT(gains.ki, 0.0f);
    EXPECT_GT(gains.kd, 0.0f);
    EXPECT_GT(gains.maxIntegral, 0.0f);
}

// ============================================================================
// PhysicsDriveMode construction tests (no PhysicsWorld needed)
// ============================================================================

TEST(PhysicsDriveModeTest, ConstructWithNullPhysicsWorld) {
    PhysicsDriveMode drive(nullptr);
    EXPECT_FALSE(drive.isBuilt());
    EXPECT_EQ(drive.getRootIndex(), -1);
}

TEST(PhysicsDriveModeTest, BuildFailsWithNullPhysicsWorld) {
    PhysicsDriveMode drive(nullptr);
    auto skel = makeMinimalSkeleton();
    EXPECT_FALSE(drive.buildFromSkeleton(skel, glm::vec3(0)));
}

TEST(PhysicsDriveModeTest, DefaultStateNotBuilt) {
    PhysicsDriveMode drive(nullptr);
    EXPECT_FALSE(drive.isBuilt());
    EXPECT_FALSE(drive.isGrounded());
    EXPECT_FALSE(drive.hasFallen());
    EXPECT_TRUE(drive.getParts().empty());
    EXPECT_TRUE(drive.getConstraints().empty());
}

// ============================================================================
// Tests requiring PhysicsWorld
// ============================================================================

class PhysicsDriveModeWithPhysicsTest : public ::testing::Test {
protected:
    void SetUp() override {
        physicsWorld_ = std::make_unique<Physics::PhysicsWorld>();
        physicsWorld_->initialize();
    }

    void TearDown() override {
        drive_.reset();
        physicsWorld_.reset();
    }

    std::unique_ptr<Physics::PhysicsWorld> physicsWorld_;
    std::unique_ptr<PhysicsDriveMode> drive_;
};

TEST_F(PhysicsDriveModeWithPhysicsTest, BuildFromMinimalSkeleton) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    auto skel = makeMinimalSkeleton();

    EXPECT_TRUE(drive_->buildFromSkeleton(skel, glm::vec3(10, 20, 10)));
    EXPECT_TRUE(drive_->isBuilt());
    EXPECT_EQ(drive_->getRootIndex(), 0); // Hips is root
    EXPECT_EQ(drive_->getParts().size(), 3u); // Hips, Spine, Head
    EXPECT_GE(drive_->getConstraints().size(), 2u); // Spine→Hips, Head→Spine
}

TEST_F(PhysicsDriveModeWithPhysicsTest, BuildFromHumanoidSkeleton) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    auto skel = makeHumanoidSkeleton();

    EXPECT_TRUE(drive_->buildFromSkeleton(skel, glm::vec3(0, 10, 0)));
    EXPECT_TRUE(drive_->isBuilt());
    EXPECT_EQ(drive_->getParts().size(), 5u); // Hips, Spine, Head, LeftLeg, RightLeg
    EXPECT_GE(drive_->getConstraints().size(), 4u);
}

TEST_F(PhysicsDriveModeWithPhysicsTest, PositionMatchesSpawnPoint) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    auto skel = makeMinimalSkeleton();
    glm::vec3 spawnPos(10.0f, 20.0f, 30.0f);

    drive_->buildFromSkeleton(skel, spawnPos);
    glm::vec3 pos = drive_->getPosition();

    // Root bone localPosition is (0,0,0) + offset, so should be near spawnPos
    EXPECT_NEAR(pos.x, spawnPos.x, 1.0f);
    EXPECT_NEAR(pos.y, spawnPos.y, 1.0f);
    EXPECT_NEAR(pos.z, spawnPos.z, 1.0f);
}

TEST_F(PhysicsDriveModeWithPhysicsTest, DestroyRemovesEverything) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    auto skel = makeMinimalSkeleton();
    drive_->buildFromSkeleton(skel, glm::vec3(0, 10, 0));

    EXPECT_TRUE(drive_->isBuilt());
    drive_->destroy();
    EXPECT_FALSE(drive_->isBuilt());
    EXPECT_TRUE(drive_->getParts().empty());
    EXPECT_TRUE(drive_->getConstraints().empty());
    EXPECT_EQ(drive_->getRootIndex(), -1);
}

TEST_F(PhysicsDriveModeWithPhysicsTest, RebuildAfterDestroy) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    auto skel = makeMinimalSkeleton();

    drive_->buildFromSkeleton(skel, glm::vec3(0, 10, 0));
    drive_->destroy();
    EXPECT_TRUE(drive_->buildFromSkeleton(skel, glm::vec3(5, 15, 5)));
    EXPECT_TRUE(drive_->isBuilt());
    EXPECT_EQ(drive_->getParts().size(), 3u);
}

TEST_F(PhysicsDriveModeWithPhysicsTest, GoLimpAndRestore) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    auto skel = makeMinimalSkeleton();
    drive_->buildFromSkeleton(skel, glm::vec3(0, 10, 0));

    drive_->goLimp();
    // After goLimp, prePhysicsStep should not apply motor forces
    // (testing the flag, not physics behavior)
    EXPECT_TRUE(drive_->isBuilt());

    drive_->restoreMotors();
    EXPECT_TRUE(drive_->isBuilt());
}

TEST_F(PhysicsDriveModeWithPhysicsTest, SetAndGetPosition) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    auto skel = makeMinimalSkeleton();
    drive_->buildFromSkeleton(skel, glm::vec3(0, 10, 0));

    glm::vec3 newPos(50.0f, 25.0f, 50.0f);
    drive_->setPosition(newPos);
    glm::vec3 pos = drive_->getPosition();
    EXPECT_NEAR(pos.x, newPos.x, 0.1f);
    EXPECT_NEAR(pos.y, newPos.y, 0.1f);
    EXPECT_NEAR(pos.z, newPos.z, 0.1f);
}

TEST_F(PhysicsDriveModeWithPhysicsTest, ConfigMutable) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    drive_->config().motorStrengthScale = 2.0f;
    drive_->config().balanceKp = 200.0f;
    EXPECT_FLOAT_EQ(drive_->config().motorStrengthScale, 2.0f);
    EXPECT_FLOAT_EQ(drive_->config().balanceKp, 200.0f);
}

TEST_F(PhysicsDriveModeWithPhysicsTest, SetJointGainsOverride) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    auto skel = makeMinimalSkeleton();
    drive_->buildFromSkeleton(skel, glm::vec3(0, 10, 0));

    JointPIDGains customGains;
    customGains.kp = 100.0f;
    customGains.kd = 50.0f;
    // Spine is bone 1
    drive_->setJointGains(1, customGains);
    // No crash = success (internal override map updated)
    EXPECT_TRUE(drive_->isBuilt());
}

TEST_F(PhysicsDriveModeWithPhysicsTest, PartsHaveBoneNames) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    auto skel = makeMinimalSkeleton();
    drive_->buildFromSkeleton(skel, glm::vec3(0, 10, 0));

    const auto& parts = drive_->getParts();
    ASSERT_EQ(parts.size(), 3u);

    // Check that parts have correct names
    bool hasHips = false, hasSpine = false, hasHead = false;
    for (const auto& part : parts) {
        if (part.name == "Hips") hasHips = true;
        if (part.name == "Spine") hasSpine = true;
        if (part.name == "Head") hasHead = true;
    }
    EXPECT_TRUE(hasHips);
    EXPECT_TRUE(hasSpine);
    EXPECT_TRUE(hasHead);
}

TEST_F(PhysicsDriveModeWithPhysicsTest, SetTargetPoseFromEmptyClip) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    auto skel = makeMinimalSkeleton();
    drive_->buildFromSkeleton(skel, glm::vec3(0, 10, 0));

    AnimationClip emptyClip;
    emptyClip.duration = 1.0f;
    emptyClip.ticksPerSecond = 30.0f;
    emptyClip.speed = 1.0f;
    // No crash with empty clip
    drive_->setTargetPoseFromClip(emptyClip, 0.5f);
    EXPECT_TRUE(drive_->isBuilt());
}

TEST_F(PhysicsDriveModeWithPhysicsTest, MoveAndStop) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    auto skel = makeMinimalSkeleton();
    drive_->buildFromSkeleton(skel, glm::vec3(0, 10, 0));

    drive_->move(glm::vec3(1, 0, 0));
    // No crash — internal state updated
    drive_->stopMovement();
    EXPECT_TRUE(drive_->isBuilt());
}

TEST_F(PhysicsDriveModeWithPhysicsTest, BuildFromEmptySkeletonFails) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    CharacterSkeleton emptySkel;
    EXPECT_FALSE(drive_->buildFromSkeleton(emptySkel, glm::vec3(0)));
}

TEST_F(PhysicsDriveModeWithPhysicsTest, BoneFilterSkipsFilteredBones) {
    drive_ = std::make_unique<PhysicsDriveMode>(physicsWorld_.get());
    CharacterSkeleton skel;
    skel.skeleton.addBone("Hips", -1, glm::vec3(0), glm::quat(1, 0, 0, 0), glm::vec3(1));
    skel.skeleton.addBone("LeftThumb1", 0, glm::vec3(0.1f, 0, 0), glm::quat(1, 0, 0, 0), glm::vec3(1));
    skel.skeleton.addBone("RightIndex2", 0, glm::vec3(-0.1f, 0, 0), glm::quat(1, 0, 0, 0), glm::vec3(1));
    skel.computeBoneSizes();
    skel.generateJointDefs();

    EXPECT_TRUE(drive_->buildFromSkeleton(skel, glm::vec3(0, 10, 0)));
    // Only Hips should be created (fingers are filtered)
    EXPECT_EQ(drive_->getParts().size(), 1u);
    EXPECT_EQ(drive_->getConstraints().size(), 0u);
}
